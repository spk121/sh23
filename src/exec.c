#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alias_store.h"
#include "ast.h"
#include "builtins.h"
#include "exec.h"
#include "exec_command.h"
#include "exec_compound.h"
#include "exec_control.h"
#include "exec_expander.h"
#include "exec_internal.h"
#include "exec_redirect.h"
#include "expander.h"
#include "fd_table.h"
#include "func_store.h"
#include "gnode.h"
#include "job_store.h"
#include "lexer.h"
#include "lib.h"
#include "logging.h"
#include "lower.h"
#include "parser.h"
#include "positional_params.h"
#include "sig_act.h"
#include "string_t.h"
#include "token.h"
#include "tokenizer.h"
#include "trap_store.h"
#include "variable_store.h"
#include "xalloc.h"
#ifdef POSIX_API
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wordexp.h>
#endif
#ifdef UCRT_API
#include <io.h>
#include <errno.h>
#include <process.h>
#include <direct.h>
#endif

/* ============================================================================
 * Constants and types
 * ============================================================================ */

#define EXECUTOR_ERROR_BUFFER_SIZE 512

/* ============================================================================
 * Helper functions
 * ============================================================================ */

/* ============================================================================
 * Executor Lifecycle Functions
 * ============================================================================ */

exec_t *exec_create_from_cfg(exec_cfg_t *cfg)
{
    exec_t *e = xcalloc(1, sizeof(exec_t));

    // ========================================================================
    // Subshell Tracking
    // ========================================================================
    e->parent = NULL;
    e->is_subshell = false;

    // Check if interactive: stdin is a tty
#ifdef POSIX_API
    e->is_interactive = isatty(STDIN_FILENO);
#elifdef UCRT_API
    e->is_interactive = _isatty(_fileno(stdin));
#else
    e->is_interactive = false; // Conservative default for ISO C
#endif

#ifdef POSIX_API
    // Check if login shell: argv[0] starts with '-'
    e->is_login_shell = (cfg->argc > 0 && cfg->argv[0] && cfg->argv[0][0] == '-');
#else
    e->is_login_shell = false; // No standard way in UCRT/ISO C
#endif

    // ========================================================================
    // Working Directory
    // ========================================================================
#ifdef POSIX_API
    char cwd_buffer[PATH_MAX];
    char *cwd = getcwd(cwd_buffer, sizeof(cwd_buffer));
    e->working_directory = cwd ? string_create_from_cstr(cwd) : string_create_from_cstr("/");
#elifdef UCRT_API
    char cwd_buffer[_MAX_PATH];
    char *cwd = _getcwd(cwd_buffer, sizeof(cwd_buffer));
    e->working_directory = cwd ? string_create_from_cstr(cwd) : string_create_from_cstr("C:\\");
#else
    // ISO C has no standard way to get cwd
    e->working_directory = string_create_from_cstr(".");
#endif

    // ========================================================================
    // File Permissions
    // ========================================================================
#ifdef POSIX_API
    // Read current umask by setting and restoring
    mode_t current_mask = umask(0);
    umask(current_mask);
    e->umask = current_mask;

    // Get file size limit
    struct rlimit rlim;
    if (getrlimit(RLIMIT_FSIZE, &rlim) == 0)
    {
        e->file_size_limit = rlim.rlim_cur;
    }
    else
    {
        e->file_size_limit = RLIM_INFINITY;
    }
#elifdef UCRT_API
    int current_mask = _umask(0);
    _umask(current_mask);
    e->umask = current_mask;
    // No file_size_limit on UCRT
#else
    // No umask or file size limits in ISO C
#endif

    // ========================================================================
    // Signal Handling
    // ========================================================================
    e->traps = trap_store_create();
    e->original_signals = sig_act_store_create();

    // ========================================================================
    // Variables & Parameters
    // ========================================================================
    e->variables = variable_store_create();

    // Import environment variables
    if (cfg->envp)
    {
        for (int i = 0; cfg->envp[i] != NULL; i++)
        {
            variable_store_add_env(e->variables, cfg->envp[i]);
        }
    }

    // Set standard shell variables
    variable_store_add_cstr(e->variables, "PWD", string_cstr(e->working_directory), /*exported*/ true, /*read_only*/ false);
    if (cfg->argc && cfg->argv[0])
        variable_store_add_cstr(e->variables, "SHELL", cfg->argv[0], /*exported*/ true, /*read_only*/ false);
    else
        variable_store_add_cstr(e->variables, "SHELL", "/bin/mgsh", /*exported*/ true, /*read_only*/ false);

    // Initialize positional parameters from command line
    if (cfg->argc > 1)
    {
        e->positional_params = positional_params_create_from_argv(cfg->argv[0], cfg->argc - 1, (const char **)&(cfg->argv[1]));
    }
    else
    {
        e->positional_params = positional_params_create();
    }

    // ========================================================================
    // Special Parameters
    // ========================================================================
    e->last_exit_status_set = true;
    e->last_exit_status = 0;

    e->last_background_pid_set = false;
    e->last_background_pid = 0;

#ifdef POSIX_API
    e->shell_pid_set = true;
    e->shell_pid = getpid();
#elifdef UCRT_API
    e->shell_pid_set = true;
    e->shell_pid = _getpid();
#else
    e->shell_pid_set = false;
    e->shell_pid = 0; // ISO C has no getpid
#endif

    e->last_argument_set = false;
    e->last_argument = NULL;

    if (cfg->argc > 0 && cfg->argv[0])
        e->shell_name = string_create_from_cstr(cfg->argv[0]);
    else
        e->shell_name = string_create_from_cstr("mgsh");

    // ========================================================================
    // Functions
    // ========================================================================
    e->functions = func_store_create();

    // ========================================================================
    // Shell Options
    // ========================================================================
    e->opt_flags_set = true;
    e->opt = cfg->opt;  // structure value copy

    // ========================================================================
    // Job Control
    // ========================================================================
    e->jobs = job_store_create();
    e->job_control_enabled = e->is_interactive;

#ifdef POSIX_API
    e->pgid = getpgrp();

    // If interactive, set up job control
    if (e->is_interactive)
    {
        // Make sure we're in our own process group
        e->pgid = getpid();
        if (setpgid(0, e->pgid) < 0)
        {
            // Ignore errors - might already be in correct group
        }

        // Take control of the terminal
        tcsetpgrp(STDIN_FILENO, e->pgid);

        // Save terminal settings
        // tcgetattr(STDIN_FILENO, &e->terminal_settings);
    }
#endif

    // ========================================================================
    // File Descriptors
    // ========================================================================
#if defined(POSIX_API) || defined(UCRT_API)
    e->open_fds = fd_table_create();

    // Track standard file descriptors
    fd_table_add(e->open_fds, STDIN_FILENO, FD_NONE, NULL);
    fd_table_add(e->open_fds, STDOUT_FILENO, FD_NONE, NULL);
    fd_table_add(e->open_fds, STDERR_FILENO, FD_NONE, NULL);

    e->next_fd = 3; // First available FD after standard FDs
#endif

    // ========================================================================
    // Aliases
    // ========================================================================
    e->aliases = alias_store_create();

    // TODO: Load aliases from init files (.bashrc, .profile, etc.)

    // ========================================================================
    // Error Reporting
    // ========================================================================
    e->error_msg = string_create_from_cstr("");

    return e;
}

// ============================================================================
// EXECUTOR FORK SUBSHELL (Create Subshell Environment)
// ============================================================================

exec_t *exec_create_subshell(exec_t *parent)
{
    Expects_not_null(parent);

    exec_t *e = xcalloc(1, sizeof(exec_t));

    // ========================================================================
    // Subshell Tracking
    // ========================================================================
    e->parent = parent;
    e->is_subshell = true;
    e->is_interactive = parent->is_interactive;
    e->is_login_shell = false; // Subshells are never login shells

    // ========================================================================
    // Working Directory
    // ========================================================================
    e->working_directory = string_create_from(parent->working_directory);

    // ========================================================================
    // File Permissions
    // ========================================================================
#ifdef POSIX_API
    e->umask = parent->umask;
    e->file_size_limit = parent->file_size_limit;
#elifdef UCRT_API
    e->umask = parent->umask;
#endif

    // ========================================================================
    // Signal Handling
    // ========================================================================
    e->traps = trap_store_copy(parent->traps);

    // Subshells create their own signal disposition tracking
    // (they inherit parent's handlers but track their own changes)
    e->original_signals = sig_act_store_create();

    // ========================================================================
    // Variables & Parameters
    // ========================================================================
    // TODO: Implement variable_store_copy or use alternative approach
    e->variables = variable_store_create();
    // For now, subshells start with empty variable store
    // In future, this should copy or reference parent variables
    e->positional_params = positional_params_copy(parent->positional_params);

    // ========================================================================
    // Special Parameters
    // ========================================================================
    e->last_exit_status_set = parent->last_exit_status_set;
    e->last_exit_status = parent->last_exit_status;

    e->last_background_pid_set = parent->last_background_pid_set;
    e->last_background_pid = parent->last_background_pid;

    // CRITICAL: Shell PID must be queried fresh in subshell!
    e->shell_pid_set = true;
#ifdef POSIX_API
    e->shell_pid = getpid();
#elifdef UCRT_API
    e->shell_pid = _getpid();
#else
    e->shell_pid = parent->shell_pid; // Fallback for ISO C
#endif

    e->last_argument_set = parent->last_argument_set;
    e->last_argument = parent->last_argument ? string_create_from(parent->last_argument) : NULL;

    e->shell_name = string_create_from(parent->shell_name);

    // ========================================================================
    // Functions
    // ========================================================================
    e->functions = func_store_copy(parent->functions);

    // ========================================================================
    // Shell Options
    // ========================================================================
    e->opt_flags_set = true;
    e->opt = parent->opt;  // structure value copy

    // ========================================================================
    // Job Control
    // ========================================================================
    // POSIX: Subshells start with empty job table and job control disabled
    e->jobs = job_store_create();
    e->job_control_enabled = false;

#ifdef POSIX_API
    // Subshell gets its own process group
    e->pgid = getpid();

    // Don't call setpgid here - it will be done after fork in parent
    // if job control is needed
#endif

    // ========================================================================
    // File Descriptors
    // ========================================================================
#if defined(POSIX_API) || defined (UCRT_API)
    // Inherit parent's FDs but create new tracker
    // The actual FDs are inherited at the OS level through fork()
    // TODO: Implement proper FD table copying if needed
    e->open_fds = fd_table_create();

    // Copy parent's next_fd
    e->next_fd = parent->next_fd;
#endif

    // ========================================================================
    // Aliases
    // ========================================================================

    e->aliases = alias_store_copy(parent->aliases);

    // ========================================================================
    // Error Reporting
    // ========================================================================
    e->error_msg = string_create_from_cstr(""); // Fresh error state

    return e;
}

// ============================================================================
// EXECUTOR CLEANUP
// ============================================================================

void exec_destroy(exec_t **executor_ptr)
{
    if (!executor_ptr || !*executor_ptr)
        return;

    exec_t *executor = *executor_ptr;

    string_destroy(&executor->working_directory);

    trap_store_destroy(&executor->traps);
    sig_act_store_destroy(&executor->original_signals);

    variable_store_destroy(&executor->variables);
    positional_params_destroy(&executor->positional_params);

    if (executor->last_argument)
        string_destroy(&executor->last_argument);
    string_destroy(&executor->shell_name);

    func_store_destroy(&executor->functions);

    job_store_destroy(&executor->jobs);

#if defined(POSIX_API) || defined(UCRT_API)
    fd_table_destroy(&executor->open_fds);
#endif

    alias_store_destroy(&executor->aliases);

    if (executor->error_msg)
        string_destroy(&executor->error_msg);

    xfree(executor);
    *executor_ptr = NULL;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

int exec_get_exit_status(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->last_exit_status;
}

void exec_set_exit_status(exec_t *executor, int status)
{
    Expects_not_null(executor);
    executor->last_exit_status = status;
}

const char *exec_get_error(const exec_t *executor)
{
    Expects_not_null(executor);

    if (string_length(executor->error_msg) == 0)
    {
        return NULL;
    }
    return string_data(executor->error_msg);
}

void exec_set_error(exec_t *executor, const char *format, ...)
{
    Expects_not_null(executor);
    Expects_not_null(format);

    va_list args;
    va_start(args, format);

    char buffer[EXECUTOR_ERROR_BUFFER_SIZE];
    vsnprintf(buffer, sizeof(buffer), format, args);

    va_end(args);

    string_clear(executor->error_msg);
    string_append_cstr(executor->error_msg, buffer);
}

void exec_clear_error(exec_t *executor)
{
    Expects_not_null(executor);
    string_clear(executor->error_msg);
}

const char *exec_get_ps1(const exec_t *executor)
{
    Expects_not_null(executor);
    Expects_not_null(executor->variables);

    const char *ps1 = variable_store_get_value_cstr(executor->variables, "PS1");
    if (ps1 && *ps1)
        return ps1;

    // Return default prompt
    return "$ ";
}

const char *exec_get_ps2(const exec_t *executor)
{
    Expects_not_null(executor);
    Expects_not_null(executor->variables);

    const char *ps2 = variable_store_get_value_cstr(executor->variables, "PS2");
    if (ps2 && *ps2)
        return ps2;

    // Return default prompt
    return "> ";
}

/* ============================================================================
 * Execution Functions
 * ============================================================================ */


exec_status_t exec_execute(exec_t *executor, const ast_node_t *root)
{
    Expects_not_null(executor);

    if (root == NULL)
    {
        return EXEC_OK;
    }

    exec_clear_error(executor);

    switch (root->type)
    {
    case AST_SIMPLE_COMMAND:
        return exec_execute_simple_command(executor, root);
    case AST_PIPELINE:
        return exec_execute_pipeline(executor, root);
    case AST_AND_OR_LIST:
        return exec_execute_andor_list(executor, root);
    case AST_COMMAND_LIST:
        return exec_execute_command_list(executor, root);
    case AST_IF_CLAUSE:
        return exec_execute_if_clause(executor, root);
    case AST_WHILE_CLAUSE:
        return exec_execute_while_clause(executor, root);
    case AST_UNTIL_CLAUSE:
        return exec_execute_until_clause(executor, root);
    case AST_FOR_CLAUSE:
        return exec_execute_for_clause(executor, root);
    case AST_CASE_CLAUSE:
        return exec_execute_case_clause(executor, root);
    case AST_SUBSHELL:
        return exec_execute_subshell(executor, root);
    case AST_BRACE_GROUP:
        return exec_execute_brace_group(executor, root);
    case AST_FUNCTION_DEF:
        return exec_execute_function_def(executor, root);
    case AST_REDIRECTED_COMMAND:
        return exec_execute_redirected_command(executor, root);
    case AST_REDIRECTION:
    case AST_CASE_ITEM:
    case AST_FUNCTION_STORED:
    case AST_NODE_TYPE_COUNT:
    default:
        exec_set_error(executor, "Unsupported AST node type: %d", root->type);
        return EXEC_NOT_IMPL;
    }
}

exec_status_t exec_execute_stream(exec_t *executor, FILE *fp)
{
    Expects_not_null(executor);
    Expects_not_null(fp);

    lexer_t *lx = lexer_create();
    if (!lx)
    {
        exec_set_error(executor, "Failed to create lexer");
        return EXEC_ERROR;
    }

    tokenizer_t *tokenizer = tokenizer_create(executor->aliases);
    if (!tokenizer)
    {
        lexer_destroy(&lx);
        exec_set_error(executor, "Failed to create tokenizer");
        return EXEC_ERROR;
    }

    exec_status_t final_status = EXEC_OK;

    // Buffer for reading lines - use a reasonably large static buffer
    // If lines are longer, fgets will read in chunks which is fine
    #define LINE_BUFFER_SIZE 4096
    char line_buffer[LINE_BUFFER_SIZE];

    // Read lines from the stream
    while (fgets(line_buffer, sizeof(line_buffer), fp) != NULL)
    {
        // Append the line to the lexer
        lexer_append_input_cstr(lx, line_buffer);

        // Tokenize the input
        token_list_t *raw_tokens = token_list_create();
        lex_status_t lex_status = lexer_tokenize(lx, raw_tokens, NULL);

        if (lex_status == LEX_ERROR)
        {
            exec_set_error(executor, "Lexer error: %s",
                          lx->error_msg ? string_cstr(lx->error_msg) : "unknown");
            token_list_destroy(&raw_tokens);
            final_status = EXEC_ERROR;
            break;
        }

        if (lex_status == LEX_INCOMPLETE)
        {
            // Need more input - continue reading (e.g., unclosed quotes, multi-line constructs)
            token_list_destroy(&raw_tokens);
            continue;
        }

        if (lex_status == LEX_NEED_HEREDOC)
        {
            // Lexer has parsed heredoc operator, needs body on next lines
            // Continue reading - lexer will process heredoc body
            token_list_destroy(&raw_tokens);
            continue;
        }

        // Process tokens through tokenizer (for alias expansion)
        token_list_t *processed_tokens = token_list_create();
        tok_status_t tok_status = tokenizer_process(tokenizer, raw_tokens, processed_tokens);
        token_list_destroy(&raw_tokens);

        if (tok_status != TOK_OK)
        {
            exec_set_error(executor, "Tokenizer error");
            token_list_destroy(&processed_tokens);
            final_status = EXEC_ERROR;
            break;
        }

        // If we have no tokens, continue
        if (processed_tokens->size == 0)
        {
            token_list_destroy(&processed_tokens);
            continue;
        }

        // Parse the tokens into a grammar tree
        parser_t *parser = parser_create_with_tokens_move(&processed_tokens);
        gnode_t *gnode = NULL;
        parse_status_t parse_status = parser_parse_program(parser, &gnode);

        if (parse_status == PARSE_ERROR)
        {
            const char *err = parser_get_error(parser);
            exec_set_error(executor, "Parse error: %s", err ? err : "unknown");
            parser_destroy(&parser);
            final_status = EXEC_ERROR;
            break;
        }

        if (parse_status == PARSE_INCOMPLETE)
        {
            // Need more input - continue reading (e.g., incomplete if/while/for)
            // Note: This should not happen often since lexer handles most multi-line cases
            if (gnode)
                g_node_destroy(&gnode);
            parser_destroy(&parser);
            continue;
        }

        if (parse_status == PARSE_EMPTY || !gnode)
        {
            // Empty input (comments only, blank lines), continue
            parser_destroy(&parser);
            continue;
        }

        // Debug: Print the gnode AST before lowering (disabled)
        #if 0
        log_debug("=== GNODE AST ===");
        gprint(gnode);
        log_debug("=================");
        #endif

        // Lower the grammar tree to AST
        ast_node_t *ast = ast_lower(gnode);
        g_node_destroy(&gnode);

        // Clean up parser and tokens
        // Note: parser doesn't own tokens, gnode took ownership of individual tokens
        parser_destroy(&parser);

        if (!ast)
        {
            // Empty program after lowering - this is valid (e.g., blank lines, 
            // comments only, or G_PROGRAM with no commands). Continue reading.
            continue;
        }

        // Execute the AST
        exec_status_t exec_status = exec_execute(executor, ast);
        ast_node_destroy(&ast);

        if (exec_status != EXEC_OK)
        {
            final_status = exec_status;
            // Stop on first error as per spec
            if (exec_status == EXEC_ERROR)
                break;
        }

        // Reset lexer for next command
        lexer_reset(lx);
    }

    // Clean up
    tokenizer_destroy(&tokenizer);
    lexer_destroy(&lx);

    return final_status;
}


/* ============================================================================
 * Visitor Pattern Support
 * ============================================================================ */

static bool ast_traverse_helper(const ast_node_t *node, ast_visitor_fn visitor, void *user_data)
{
    if (node == NULL)
    {
        return true;
    }

    // Call visitor for this node
    if (!visitor(node, user_data))
    {
        return false;
    }

    // Recursively traverse children
    switch (node->type)
    {
    case AST_SIMPLE_COMMAND:
        // No child nodes to traverse (tokens are leaves)
        break;

    case AST_PIPELINE:
        if (node->data.pipeline.commands != NULL)
        {
            for (int i = 0; i < node->data.pipeline.commands->size; i++)
            {
                if (!ast_traverse_helper(node->data.pipeline.commands->nodes[i], visitor, user_data))
                {
                    return false;
                }
            }
        }
        break;

    case AST_AND_OR_LIST:
        if (!ast_traverse_helper(node->data.andor_list.left, visitor, user_data))
        {
            return false;
        }
        if (!ast_traverse_helper(node->data.andor_list.right, visitor, user_data))
        {
            return false;
        }
        break;

    case AST_COMMAND_LIST:
        if (node->data.command_list.items != NULL)
        {
            for (int i = 0; i < node->data.command_list.items->size; i++)
            {
                if (!ast_traverse_helper(node->data.command_list.items->nodes[i], visitor, user_data))
                {
                    return false;
                }
            }
        }
        break;

    case AST_SUBSHELL:
    case AST_BRACE_GROUP:
        if (!ast_traverse_helper(node->data.compound.body, visitor, user_data))
        {
            return false;
        }
        break;

    case AST_IF_CLAUSE:
        if (!ast_traverse_helper(node->data.if_clause.condition, visitor, user_data))
        {
            return false;
        }
        if (!ast_traverse_helper(node->data.if_clause.then_body, visitor, user_data))
        {
            return false;
        }
        if (node->data.if_clause.elif_list != NULL)
        {
            for (int i = 0; i < node->data.if_clause.elif_list->size; i++)
            {
                if (!ast_traverse_helper(node->data.if_clause.elif_list->nodes[i], visitor, user_data))
                {
                    return false;
                }
            }
        }
        if (!ast_traverse_helper(node->data.if_clause.else_body, visitor, user_data))
        {
            return false;
        }
        break;

    case AST_WHILE_CLAUSE:
    case AST_UNTIL_CLAUSE:
        if (!ast_traverse_helper(node->data.loop_clause.condition, visitor, user_data))
        {
            return false;
        }
        if (!ast_traverse_helper(node->data.loop_clause.body, visitor, user_data))
        {
            return false;
        }
        break;

    case AST_FOR_CLAUSE:
        if (!ast_traverse_helper(node->data.for_clause.body, visitor, user_data))
        {
            return false;
        }
        break;

    case AST_CASE_CLAUSE:
        if (node->data.case_clause.case_items != NULL)
        {
            for (int i = 0; i < node->data.case_clause.case_items->size; i++)
            {
                if (!ast_traverse_helper(node->data.case_clause.case_items->nodes[i], visitor, user_data))
                {
                    return false;
                }
            }
        }
        break;

    case AST_CASE_ITEM:
        if (!ast_traverse_helper(node->data.case_item.body, visitor, user_data))
        {
            return false;
        }
        break;

    case AST_FUNCTION_DEF:
        if (!ast_traverse_helper(node->data.function_def.body, visitor, user_data))
        {
            return false;
        }
        if (node->data.function_def.redirections != NULL)
        {
            for (int i = 0; i < node->data.function_def.redirections->size; i++)
            {
                if (!ast_traverse_helper(node->data.function_def.redirections->nodes[i], visitor, user_data))
                {
                    return false;
                }
            }
        }
        break;

    case AST_REDIRECTED_COMMAND:
        if (!ast_traverse_helper(node->data.redirected_command.command, visitor, user_data))
        {
            return false;
        }
        if (node->data.redirected_command.redirections != NULL)
        {
            for (int i = 0; i < node->data.redirected_command.redirections->size; i++)
            {
                if (!ast_traverse_helper(node->data.redirected_command.redirections->nodes[i], visitor, user_data))
                {
                    return false;
                }
            }
        }
        break;

    default:
        break;
    }

    return true;
}

bool ast_traverse(const ast_node_t *root, ast_visitor_fn visitor, void *user_data)
{
    Expects_not_null(visitor);

    return ast_traverse_helper(root, visitor, user_data);
}

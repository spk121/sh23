/**
 * exec.c - Shell executor implementation
 *
 * This file implements the public executor API and high-level execution.
 * Frame management and policy-driven execution is in exec_frame.c.
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "exec.h"

#include "alias_store.h"
#include "ast.h"
#include "exec_frame.h"
#include "exec_redirect.h"
#include "fd_table.h"
#include "func_store.h"
#include "gnode.h"
#include "job_store.h"
#include "lexer.h"
#include "logging.h"
#include "lower.h"
#include "parser.h"
#include "positional_params.h"
#include "sig_act.h"
#include "string_list.h"
#include "string_t.h"
#include "token.h"
#include "tokenizer.h"
#include "trap_store.h"
#include "variable_store.h"
#include "xalloc.h"

#ifdef POSIX_API
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef UCRT_API
#if defined(_WIN64)
#define _AMD64_
#elif defined(_WIN32)
#define _X86_
#endif
#include <io.h>
#include <process.h>
#include <processthreadsapi.h>   // GetProcessId, OpenProcess, GetExitCodeProcess, etc.
#include <synchapi.h>            // WaitForSingleObject
#include <handleapi.h>           // CloseHandle
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define EXECUTOR_ERROR_BUFFER_SIZE 512

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

exec_result_t exec_execute_dispatch(exec_frame_t *frame, const ast_node_t *node);
exec_result_t exec_while_clause(exec_frame_t *frame, ast_node_t *node);
exec_result_t exec_for_clause(exec_frame_t *frame, ast_node_t *node);
exec_result_t exec_function_def_clause(exec_frame_t *frame, ast_node_t *node);
exec_result_t exec_redirected_command(exec_frame_t *frame, ast_node_t *node);
exec_result_t exec_case_clause(exec_frame_t *frame, ast_node_t *node);

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static void source_rc_files(struct exec_t *e)
{
    // FIXME
#if 0
#ifdef EXEC_SYSTEM_RC_PATH
    exec_load_config_file(e, EXEC_SYSTEM_RC_PATH, func_store, alias_store);
#endif
#ifdef EXEC_USER_RC_PATH
    if (e->is_interactive)
    {
        /* Check $ENV for a filename. It is subject to pathname expansion.*/
        const char *env_raw_rc_path = variable_store_get_value_cstr(e->variables, "ENV");
        if (env_raw_rc_path && *env_raw_rc_path)
        {
            char *env_rc_path = exec_expand_pathname(e, env_raw_rc_path);
#ifdef POSIX_API
            if (getuid() == geteuid() && getgid() == getegid() && getuid())
            {
                exec_load_config_file(e, env_rc_path, func_store, alias_store);
            }
#endif
            exec_load_config_file(e, env_rc_path, func_store, alias_store);
        }
        else
        {
            exec_load_config_file_from_config_directory(e, EXEC_USER_RC_NAME, func_store,
                                                        alias_store);
        }
    }
#endif

#endif
}


void exec_cfg_set_from_shell_options(exec_cfg_t *cfg, int argc, char *const *argv, char *const *envp,
                                       const char *shell_name, string_list_t *shell_args,
                                       string_list_t *env_vars, const exec_opt_flags_t *opt_flags,
                                       bool is_interactive, bool is_login_shell,
                                       bool job_control_enabled)
{
    Expects_not_null(cfg);

    if (argc >= 0 && argv)
    {
        cfg->argv_set = true;
        cfg->argc = argc;
        cfg->argv = argv;
    }
    else
    {
        cfg->argv_set = false;
    }
    if (envp)
    {
        cfg->envp_set = true;
        cfg->envp = envp;
    }
    else
    {
        cfg->envp_set = false;
    }
    if (shell_name)
    {
        cfg->shell_name_set = true;
        cfg->shell_name = shell_name;
    }
    else
    {
        cfg->shell_name_set = false;
    }
    if (shell_args)
    {
        cfg->shell_args_set = true;
        cfg->shell_args = string_list_create_from(shell_args);
    }
    else
    {
        cfg->shell_args_set = false;
    }
    if (env_vars)
    {
        cfg->env_vars_set = true;
        cfg->env_vars = string_list_create_from(env_vars);
    }
    else
    {
        cfg->env_vars_set = false;
    }
    if (opt_flags)
    {
        cfg->opt_flags_set = true;
        cfg->opt = *opt_flags;
    }
    else
    {
        cfg->opt_flags_set = false;
    }
    cfg->is_interactive_set = true;
    cfg->is_interactive = is_interactive;
    cfg->is_login_shell_set = true;
    cfg->is_login_shell = is_login_shell;
    cfg->job_control_enabled_set = true;
    cfg->job_control_enabled = job_control_enabled;
}


/* ============================================================================
 * Executor Lifecycle
 * ============================================================================ */

struct exec_t *exec_create(const struct exec_cfg_t *cfg)
{
    struct exec_t *e = xcalloc(1, sizeof(struct exec_t));

    /* The singleton exec stores
     * 1. Singleton values
     * 2. Info required to initialize the top-level frame. The top-level
     *    frame is lazily initialized on the first call to the execution.
     */

    /* -------------------------------------------------------------------------
     * Shell Identity
     * -------------------------------------------------------------------------
     */
#ifdef POSIX_API
    pid_t default_pid = getpid();
    pid_t default_ppid = getppid();
    bool default_pid_valid = true;
    bool default_ppid_valid = true;
#elifdef UCRT_API
    int default_pid = _getpid();
    int default_ppid = 0; /* No getppid in UCRT */
    bool default_pid_valid = true;
    bool default_ppid_valid = false;
#else
    int default_pid = 0;
    int default_ppid = 0;
    bool default_pid_valid = false;
    bool default_ppid_valid = false;
#endif

    e->shell_pid = cfg->shell_pid_set ? cfg->shell_pid : default_pid;
    e->shell_ppid = cfg->shell_ppid_set ? cfg->shell_ppid : default_ppid;
    e->shell_pid_valid = cfg->shell_pid_valid_set ? cfg->shell_pid_valid
                                                   : (cfg->shell_pid_set ? true : default_pid_valid);
    e->shell_ppid_valid = cfg->shell_ppid_valid_set ? cfg->shell_ppid_valid
                                                     : (cfg->shell_ppid_set ? true : default_ppid_valid);

    int argc = cfg->argv_set ? cfg->argc : 0;
    char *const *argv = cfg->argv_set ? cfg->argv : NULL;
    e->argc = argc;
    e->argv = (char **)argv;
    e->envp = cfg->envp_set ? (char **)cfg->envp : NULL;

    if (cfg->shell_name_set && cfg->shell_name)
    {
        e->shell_name = string_create_from_cstr(cfg->shell_name);
    }
    else if (argc > 0 && argv && argv[0])
    {
        e->shell_name = string_create_from_cstr(argv[0]);
    }
    else
    {
        e->shell_name = string_create_from_cstr(EXEC_CFG_FALLBACK_ARGV0);
    }

    if (cfg->shell_args_set && cfg->shell_args)
    {
        e->shell_args = cfg->shell_args;
    }
    else if (argc > 1 && argv)
    {
        e->shell_args = string_list_create_from_cstr_array((const char **)argv + 1, argc - 1);
    }
    else
    {
        e->shell_args = string_list_create();
    }

    if (cfg->env_vars_set && cfg->env_vars)
    {
        e->env_vars = cfg->env_vars;
    }
    else if (cfg->envp_set && cfg->envp)
    {
        e->env_vars = string_list_create_from_cstr_array((const char **)cfg->envp, -1);
    }
    else
    {
        e->env_vars = string_list_create_from_system_env();
    }

    if (cfg->opt_flags_set)
    {
        e->opt = cfg->opt;
    }
    else
    {
        exec_opt_flags_t opt_fallback = EXEC_CFG_FALLBACK_OPT_FLAGS_INIT;
        e->opt = opt_fallback;
    }


#ifdef POSIX_API
    bool default_is_interactive = isatty(STDIN_FILENO);
#elifdef UCRT_API
    bool default_is_interactive = _isatty(_fileno(stdin));
#else
    bool default_is_interactive = false;
#endif

    bool default_is_login_shell = (e->argc > 0 && e->argv && e->argv[0] && e->argv[0][0] == '-');

    e->is_interactive = cfg->is_interactive_set ? cfg->is_interactive : default_is_interactive;
    e->is_login_shell = cfg->is_login_shell_set ? cfg->is_login_shell : default_is_login_shell;

    /* RC File handling happens when creating the top frame, aliases is empty for now*/
    e->aliases = alias_store_create();

    e->jobs = job_store_create();
    e->job_control_enabled = cfg->job_control_enabled_set ? cfg->job_control_enabled
                                                          : e->is_interactive;

#ifdef POSIX_API
    if (cfg->pgid_set)
    {
        e->pgid = cfg->pgid;
        e->pgid_valid = cfg->pgid_valid_set ? cfg->pgid_valid : true;
    }
    else
    {
        e->pgid = getpgrp();

        if (e->is_interactive)
        {
            e->pgid = getpid();
            setpgid(0, e->pgid); /* Ignore errors */
            tcsetpgrp(STDIN_FILENO, e->pgid);
        }
        e->pgid_valid = cfg->pgid_valid_set ? cfg->pgid_valid : true;
    }
#else
    e->pgid = cfg->pgid_set ? cfg->pgid : 0;
    e->pgid_valid = cfg->pgid_valid_set ? cfg->pgid_valid : false;
#endif

    e->top_frame_initialized = false;
    e->current_frame = NULL;
    e->top_frame = NULL;
#if defined(EXEC_SYSTEM_RC_PATH) || defined(EXEC_USER_RC_PATH)
    e->rc_loaded = cfg->rc_loaded_set ? cfg->rc_loaded : false;
#else
    e->rc_loaded = cfg->rc_loaded_set ? cfg->rc_loaded : true;
#endif

    e->signals_installed = false;
    e->sigint_received = 0;
    e->sigchld_received = 0;
    for (int i = 0; i < NSIG; i++)
        e->trap_pending[i] = 0;

    /* -------------------------------------------------------------------------
     * Command Line
     * -------------------------------------------------------------------------
     */
    e->argc = cfg->argc;
    e->argv = (char **)cfg->argv;
    e->envp = (char **)cfg->envp;

    /* -------------------------------------------------------------------------
     * Top-Frame Initialization Data (lazy frame creation)
     * -------------------------------------------------------------------------
     */
    e->variables = NULL;
    e->local_variables = NULL;
    e->positional_params = NULL;
    e->functions = NULL;
    e->traps = NULL;
#if defined(POSIX_API) || defined(UCRT_API)
    e->open_fds = NULL;
    e->next_fd = 0;
#endif

    if (cfg->working_directory_set && cfg->working_directory)
    {
        e->working_directory = string_create_from_cstr(cfg->working_directory);
    }
    else
    {
        e->working_directory = NULL;
    }
#ifdef POSIX_API
    e->umask = cfg->umask_set ? cfg->umask : 0;
    e->file_size_limit = cfg->file_size_limit_set ? cfg->file_size_limit : 0;
#else
    e->umask = cfg->umask_set ? cfg->umask : 0;
#endif

    if (cfg->last_exit_status_set)
    {
        e->last_exit_status = cfg->last_exit_status;
        e->last_exit_status_set = true;
    }
    else
    {
        e->last_exit_status = 0;
        e->last_exit_status_set = true;
    }

    if (cfg->last_background_pid_set)
    {
        e->last_background_pid = cfg->last_background_pid;
        e->last_background_pid_set = true;
    }
    else
    {
        e->last_background_pid = 0;
        e->last_background_pid_set = false;
    }

    if (cfg->last_argument_set && cfg->last_argument)
    {
        e->last_argument = string_create_from_cstr(cfg->last_argument);
        e->last_argument_set = true;
    }
    else
    {
        e->last_argument = NULL;
        e->last_argument_set = false;
    }

    /* Traps and signal state (singleton) */
    e->original_signals = sig_act_store_create();

    /* Pipeline status */
    e->pipe_statuses = NULL;
    e->pipe_status_count = 0;
    e->pipe_status_capacity = 0;

    /* Frame pointers are lazily initialized */
    e->top_frame_initialized = false;
    e->top_frame = NULL;
    e->current_frame = NULL;

#if defined(EXEC_SYSTEM_RC_PATH) || defined(EXEC_USER_RC_PATH)
    e->rc_files_sourced = cfg->rc_files_sourced_set ? cfg->rc_files_sourced : false;
#else
    e->rc_files_sourced = cfg->rc_files_sourced_set ? cfg->rc_files_sourced : true;
#endif


    /* -------------------------------------------------------------------------
     * Error State
     * -------------------------------------------------------------------------
     */
    e->error_msg = string_create_from_cstr("");

    return e;
}

void exec_destroy(exec_t **executor_ptr)
{
    if (!executor_ptr || !*executor_ptr)
        return;

    exec_t *e = *executor_ptr;

    /* Pop all frames */
    while (e->current_frame)
    {
        exec_frame_pop(&e->current_frame);
    }

    /* Clean up executor-owned resources */
    string_destroy(&e->working_directory);
    string_destroy(&e->shell_name);
    string_destroy(&e->error_msg);

    if (e->last_argument)
        string_destroy(&e->last_argument);

    /* If no frames were created, these may still be owned by the executor. */
    if (e->variables)
        variable_store_destroy(&e->variables);
    if (e->local_variables)
        variable_store_destroy(&e->local_variables);
    if (e->positional_params)
        positional_params_destroy(&e->positional_params);
    if (e->functions)
        func_store_destroy(&e->functions);
    if (e->aliases)
        alias_store_destroy(&e->aliases);
    if (e->traps)
        trap_store_destroy(&e->traps);
    if (e->original_signals)
        sig_act_store_destroy(&e->original_signals);

    job_store_destroy(&e->jobs);

#if defined(POSIX_API) || defined(UCRT_API)
    if (e->open_fds)
        fd_table_destroy(&e->open_fds);
#endif

    if (e->pipe_statuses)
        xfree(e->pipe_statuses);

    xfree(e);
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
    executor->last_exit_status_set = true;
}

const char *exec_get_error(const exec_t *executor)
{
    Expects_not_null(executor);
    if (string_length(executor->error_msg) == 0)
        return NULL;
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
    /* We expect to use the current frame's variable store, but
     * if called too early, there may only be a variable store
     * at top-level, or perhaps not at all */
    if (executor->current_frame && executor->current_frame->variables)
        {
        const char *ps1 =
            variable_store_get_value_cstr(executor->current_frame->variables, "PS1");
        if (ps1 && *ps1)
            return ps1;
    }
    else if (executor->variables)
    {
        const char *ps1 =
            variable_store_get_value_cstr(executor->variables, "PS1");
        if (ps1 && *ps1)
            return ps1;
    }

    return "$ ";
}

const char *exec_get_ps2(const exec_t *executor)
{
    Expects_not_null(executor);
    const char *ps2 = variable_store_get_value_cstr(executor->variables, "PS2");
    return (ps2 && *ps2) ? ps2 : "> ";
}

positional_params_t *exec_get_positional_params(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->positional_params;
}

variable_store_t *exec_get_variables(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->variables;
}

alias_store_t *exec_get_aliases(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->aliases;
}

bool exec_is_interactive(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->is_interactive;
}

bool exec_is_login_shell(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->is_login_shell;
}

/* ============================================================================
 * Execution Functions
 * ============================================================================ */

void exec_setup_interactive_execute(exec_t *executor)
{
    Expects_not_null(executor);

    /* Ensure we have a top-level frame */
    if (!executor->current_frame)
    {
        /* Create top-level frame from exec_t init data */
        executor->top_frame = exec_frame_create_top_level(executor);
        executor->current_frame = executor->top_frame;
    }

    /* FIXME handle rcfile here */
}

exec_result_t exec_execute_dispatch(exec_frame_t *frame, const ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    /* Dispatch based on AST node type */
    /* Execute based on AST node type */
    exec_result_t result;

    switch (node->type)
    {
    case AST_COMMAND_LIST:
        result = exec_compound_list(frame, (ast_node_t *)node);
        break;

    case AST_AND_OR_LIST:
        result = exec_and_or_list(frame, (ast_node_t *)node);
        break;

    case AST_PIPELINE:
        result = exec_pipeline(frame, (ast_node_t *)node);
        break;

    case AST_SIMPLE_COMMAND:
        result = exec_simple_command(frame, (ast_node_t *)node);
        break;

    case AST_SUBSHELL:
        result = exec_subshell(frame, node->data.compound.body);
        break;

    case AST_BRACE_GROUP:
        result = exec_brace_group(frame, node->data.compound.body, NULL);
        break;

    case AST_IF_CLAUSE:
        result = exec_if_clause(frame, (ast_node_t *)node);
        break;

    case AST_WHILE_CLAUSE:
    case AST_UNTIL_CLAUSE:
        result = exec_while_clause(frame, (ast_node_t *)node);
        break;

    case AST_FOR_CLAUSE:
        result = exec_for_clause(frame, (ast_node_t *)node);
        break;

    case AST_REDIRECTED_COMMAND:
        result = exec_redirected_command(frame, (ast_node_t *)node);
        break;

    case AST_CASE_CLAUSE:
        result = exec_case_clause(frame, (ast_node_t *)node);
        break;

    case AST_FUNCTION_DEF:
        result = exec_function_def_clause(frame, (ast_node_t *)node);
        break;

    default:
        exec_set_error(frame->executor, "Unsupported AST node type: %d %s", node->type, ast_node_type_to_string(node->type));
        result.status = EXEC_NOT_IMPL;
        return result;
    }

    /* Update executor's exit status */
    if (result.has_exit_status)
    {
        frame->executor->last_exit_status = result.exit_status;
        frame->executor->last_exit_status_set = true;
        frame->last_exit_status = result.exit_status;
    }

    return result;
}

exec_status_t exec_execute(exec_t *executor, const ast_node_t *root)
{
    Expects_not_null(executor);

    if (root == NULL)
        return EXEC_OK;

    exec_clear_error(executor);

    /* Ensure we have a top-level frame */
    if (!executor->current_frame)
    {
        /* Create top-level frame from exec_t init data */
        executor->top_frame = exec_frame_create_top_level(executor);
        executor->current_frame = executor->top_frame;
    }

    /* Execute based on AST node type */
    exec_result_t result;

    switch (root->type)
    {
    case AST_COMMAND_LIST:
        result = exec_compound_list(executor->current_frame, (ast_node_t *)root);
        break;

    case AST_AND_OR_LIST:
        result = exec_and_or_list(executor->current_frame, (ast_node_t *)root);
        break;

    case AST_PIPELINE:
        result = exec_pipeline(executor->current_frame, (ast_node_t *)root);
        break;

    case AST_SIMPLE_COMMAND:
        result = exec_simple_command(executor->current_frame, (ast_node_t *)root);
        break;

    case AST_SUBSHELL:
        result = exec_subshell(executor->current_frame, root->data.compound.body);
        break;

    case AST_BRACE_GROUP:
        result =
            exec_brace_group(executor->current_frame, root->data.compound.body, NULL);
        break;

    case AST_IF_CLAUSE:
        result = exec_if_clause(executor->current_frame, (ast_node_t *)root);
        break;

    case AST_WHILE_CLAUSE:
    case AST_UNTIL_CLAUSE:
        result = exec_while_clause(executor->current_frame, (ast_node_t *)root);
        break;

    case AST_FOR_CLAUSE:
        result = exec_for_clause(executor->current_frame, (ast_node_t *)root);
        break;

    case AST_REDIRECTED_COMMAND:
        result = exec_redirected_command(executor->current_frame, (ast_node_t *)root);
        break;

    case AST_CASE_CLAUSE:
        result = exec_case_clause(executor->current_frame, (ast_node_t *)root);
        break;

    case AST_FUNCTION_DEF:
        result = exec_function_def_clause(executor->current_frame, (ast_node_t *)root);
        break;

    default:
        exec_set_error(executor, "Unsupported AST node type: %d", root->type);
        return EXEC_NOT_IMPL;
    }

    /* Update executor's exit status */
    if (result.has_exit_status)
    {
        executor->last_exit_status = result.exit_status;
        executor->last_exit_status_set = true;
        if (executor->current_frame)
        {
            executor->current_frame->last_exit_status = result.exit_status;
        }
    }

    return result.status;
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

#define LINE_BUFFER_SIZE 4096
    char line_buffer[LINE_BUFFER_SIZE];
    int line_num = 0;
    
    /* Token accumulation for incomplete parses */
    token_list_t *accumulated_tokens = NULL;

    while (fgets(line_buffer, sizeof(line_buffer), fp) != NULL)
    {
        line_num++;
        log_debug("exec_execute_stream: Reading line %d: %.*s", line_num, 
                  (int)strcspn(line_buffer, "\r\n"), line_buffer);
        
        lexer_append_input_cstr(lx, line_buffer);

        token_list_t *raw_tokens = token_list_create();
        lex_status_t lex_status = lexer_tokenize(lx, raw_tokens, NULL);

        if (lex_status == LEX_ERROR)
        {
            log_debug("exec_execute_stream: Lexer error at line %d", line_num);
            exec_set_error(executor, "Lexer error: %s",
                           lx->error_msg ? string_cstr(lx->error_msg) : "unknown");
            token_list_destroy(&raw_tokens);
            if (accumulated_tokens)
                token_list_destroy(&accumulated_tokens);
            final_status = EXEC_ERROR;
            break;
        }

        if (lex_status == LEX_INCOMPLETE || lex_status == LEX_NEED_HEREDOC)
        {
            log_debug("exec_execute_stream: Lexer incomplete/heredoc at line %d", line_num);
            token_list_destroy(&raw_tokens);
            continue;
        }

        log_debug("exec_execute_stream: Lexer produced %d raw tokens at line %d", 
                  token_list_size(raw_tokens), line_num);

        token_list_t *processed_tokens = token_list_create();
        tok_status_t tok_status = tokenizer_process(tokenizer, raw_tokens, processed_tokens);
        token_list_destroy(&raw_tokens);

        if (tok_status != TOK_OK)
        {
            log_debug("exec_execute_stream: Tokenizer error at line %d", line_num);
            exec_set_error(executor, "Tokenizer error");
            token_list_destroy(&processed_tokens);
            if (accumulated_tokens)
                token_list_destroy(&accumulated_tokens);
            final_status = EXEC_ERROR;
            break;
        }

        if (token_list_size(processed_tokens) == 0)
        {
            log_debug("exec_execute_stream: No tokens after processing at line %d", line_num);
            token_list_destroy(&processed_tokens);
            continue;
        }

        /* Accumulate tokens if we had an incomplete parse previously */
        if (accumulated_tokens)
        {
            log_debug("exec_execute_stream: Appending %d new tokens to %d accumulated tokens",
                      token_list_size(processed_tokens), token_list_size(accumulated_tokens));
            
            /* Move all tokens from processed_tokens to accumulated_tokens */
            if (token_list_append_list_move(accumulated_tokens, &processed_tokens) != 0)
            {
                log_debug("exec_execute_stream: Failed to append tokens");
                token_list_destroy(&accumulated_tokens);
                final_status = EXEC_ERROR;
                break;
            }
            
            /* Use the accumulated list for parsing */
            processed_tokens = accumulated_tokens;
            accumulated_tokens = NULL;
        }

        log_debug("exec_execute_stream: Tokenizer produced %d processed tokens at line %d",
                  token_list_size(processed_tokens), line_num);
        
        /* Debug: print first few tokens */
        for (int i = 0; i < token_list_size(processed_tokens) && i < 10; i++)
        {
            const token_t *t = token_list_get(processed_tokens, i);
            log_debug("  Token %d: type=%d, text='%s'", i, token_get_type(t),
                      string_cstr(token_to_string(t)));
        }

        parser_t *parser = parser_create_with_tokens_move(&processed_tokens);
        gnode_t *gnode = NULL;
        
        log_debug("exec_execute_stream: Starting parse at line %d", line_num);
        parse_status_t parse_status = parser_parse_program(parser, &gnode);

        if (parse_status == PARSE_ERROR)
        {
            log_debug("exec_execute_stream: Parse error at line %d", line_num);
            const char *err = parser_get_error(parser);
            if (err && err[0])
            {
                exec_set_error(executor, "Parse error at line %d: %s", line_num, err);
            }
            else
            {
                /* Parser returned error but didn't set error message */
                token_t *curr_tok = token_clone(parser_current_token(parser));
                if (curr_tok)
                {
                    string_t *tok_str = token_to_string(curr_tok);
                    log_debug("exec_execute_stream: Current token: type=%d, line=%d, col=%d, text='%s'",
                              token_get_type(curr_tok),
                              token_get_first_line(curr_tok),
                              token_get_first_column(curr_tok),
                              string_cstr(tok_str));
                    exec_set_error(executor, "Parse error at line %d, column %d near '%s'",
                                   token_get_first_line(curr_tok),
                                   token_get_first_column(curr_tok),
                                   string_cstr(tok_str));
                    string_destroy(&tok_str);
                    token_destroy(&curr_tok);
                }
                else
                {
                    log_debug("exec_execute_stream: No current token available");
                    exec_set_error(executor, "Parse error at line %d: no error details available", line_num);
                }
            }
            parser_destroy(&parser);
            final_status = EXEC_ERROR;
            break;
        }

        if (parse_status == PARSE_INCOMPLETE)
        {
            log_debug("exec_execute_stream: Parse incomplete at line %d, accumulating tokens", line_num);
            if (gnode)
                g_node_destroy(&gnode);
            /* Clone token list from parser - respect silo boundary */
            accumulated_tokens = token_list_clone(parser->tokens);
            parser_destroy(&parser);
            continue;
        }

        if (parse_status == PARSE_EMPTY || !gnode)
        {
            log_debug("exec_execute_stream: Parse empty at line %d", line_num);
            parser_destroy(&parser);
            continue;
        }

        ast_node_t *ast = ast_lower(gnode);
        g_node_destroy(&gnode);
        parser_destroy(&parser);

        if (!ast)
            continue;

        exec_status_t exec_status = exec_execute(executor, ast);
        ast_node_destroy(&ast);

        if (exec_status != EXEC_OK)
        {
            final_status = exec_status;
            if (exec_status == EXEC_ERROR)
                break;
        }

        /* Only reset lexer after successful execution */
        lexer_reset(lx);
        
        /* Clear accumulated tokens after successful execution */
        if (accumulated_tokens)
        {
            token_list_destroy(&accumulated_tokens);
            accumulated_tokens = NULL;
        }
    }

    /* Clean up any remaining accumulated tokens */
    if (accumulated_tokens)
    {
        token_list_destroy(&accumulated_tokens);
    }

    tokenizer_destroy(&tokenizer);
    lexer_destroy(&lx);

    return final_status;
}

/* ============================================================================
 * Job Control
 * ============================================================================ */

void exec_reap_background_jobs(exec_t *executor, bool notify)
{
    Expects_not_null(executor);

#ifdef POSIX_API
    int status;
    pid_t pid;
    bool any_reaped = false;

    /* -1 and WNOHANG mean we return the PID of any completed child process or -1 
     * if no child process has recently completed */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        // Determine if the job is done, or if it was terminated by a signal
        bool terminated = WIFSIGNALED(status);
        int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        if (terminated)
            job_store_set_state(executor->jobs, pid, JOB_STATE_TERMINATED);
        else
            job_store_set_state(executor->jobs, pid, JOB_STATE_DONE);
        job_store_set_exit_status(executor->jobs, pid, exit_status);
        any_reaped = true;
    }
    if (any_reaped && notify)
    {
        job_store_print_completed_jobs(executor->jobs, stdout);
    }
    if (any_reaped)
        job_store_remove_completed(executor->jobs);
#elifdef UCRT_API
    bool any_completed = false;
    job_store_t *store = executor->jobs;
    job_process_iterator_t iter = job_store_active_processes_begin(store);

    while (job_store_active_processes_next(&iter))
    {
        uintptr_t h = job_store_iter_get_handle(&iter);
        if (!h)
        {
            // Unreachable?
            job_store_iter_set_state(&iter, JOB_DONE, 0); // Return 0 as exit code?

            // Check if job is now complete
            job_state_t state = job_store_iter_get_job_state(&iter);
            if (state == JOB_DONE || state == JOB_TERMINATED)
                any_completed = true;
        }
        else if (!WaitForSingleObject(h, 0))
        {
            DWORD exit_code;
            GetExitCodeProcess(h, &exit_code);
            job_store_iter_set_state(&iter, JOB_DONE, (int)exit_code);

            // Check if job is now complete
            job_state_t state = job_store_iter_get_job_state(&iter);
            if (state == JOB_DONE || state == JOB_TERMINATED)
                any_completed = true;
        }
    }
    if (any_completed && notify)
        job_store_print_completed_jobs(executor->jobs, stdout);
    if (any_completed)
        job_store_remove_completed(executor->jobs);
#endif
}

/* ============================================================================
 * AST Visitor Pattern Support
 * ============================================================================ */

static bool ast_traverse_helper(const ast_node_t *node, ast_visitor_fn visitor, void *user_data)
{
    if (node == NULL)
        return true;

    if (!visitor(node, user_data))
        return false;

    switch (node->type)
    {
    case AST_SIMPLE_COMMAND:
        break;

    case AST_PIPELINE:
        if (node->data.pipeline.commands != NULL)
        {
            for (int i = 0; i < node->data.pipeline.commands->size; i++)
            {
                if (!ast_traverse_helper(node->data.pipeline.commands->nodes[i], visitor,
                                         user_data))
                    return false;
            }
        }
        break;

    case AST_AND_OR_LIST:
        if (!ast_traverse_helper(node->data.andor_list.left, visitor, user_data))
            return false;
        if (!ast_traverse_helper(node->data.andor_list.right, visitor, user_data))
            return false;
        break;

    case AST_COMMAND_LIST:
        if (node->data.command_list.items != NULL)
        {
            for (int i = 0; i < node->data.command_list.items->size; i++)
            {
                if (!ast_traverse_helper(node->data.command_list.items->nodes[i], visitor,
                                         user_data))
                    return false;
            }
        }
        break;

    case AST_SUBSHELL:
    case AST_BRACE_GROUP:
        if (!ast_traverse_helper(node->data.compound.body, visitor, user_data))
            return false;
        break;

    case AST_IF_CLAUSE:
        if (!ast_traverse_helper(node->data.if_clause.condition, visitor, user_data))
            return false;
        if (!ast_traverse_helper(node->data.if_clause.then_body, visitor, user_data))
            return false;
        if (node->data.if_clause.elif_list != NULL)
        {
            for (int i = 0; i < node->data.if_clause.elif_list->size; i++)
            {
                if (!ast_traverse_helper(node->data.if_clause.elif_list->nodes[i], visitor,
                                         user_data))
                    return false;
            }
        }
        if (!ast_traverse_helper(node->data.if_clause.else_body, visitor, user_data))
            return false;
        break;

    case AST_WHILE_CLAUSE:
    case AST_UNTIL_CLAUSE:
        if (!ast_traverse_helper(node->data.loop_clause.condition, visitor, user_data))
            return false;
        if (!ast_traverse_helper(node->data.loop_clause.body, visitor, user_data))
            return false;
        break;

    case AST_FOR_CLAUSE:
        if (!ast_traverse_helper(node->data.for_clause.body, visitor, user_data))
            return false;
        break;

    case AST_CASE_CLAUSE:
        if (node->data.case_clause.case_items != NULL)
        {
            for (int i = 0; i < node->data.case_clause.case_items->size; i++)
            {
                if (!ast_traverse_helper(node->data.case_clause.case_items->nodes[i], visitor,
                                         user_data))
                    return false;
            }
        }
        break;

    case AST_CASE_ITEM:
        if (!ast_traverse_helper(node->data.case_item.body, visitor, user_data))
            return false;
        break;

    case AST_FUNCTION_DEF:
        if (!ast_traverse_helper(node->data.function_def.body, visitor, user_data))
            return false;
        if (node->data.function_def.redirections != NULL)
        {
            for (int i = 0; i < node->data.function_def.redirections->size; i++)
            {
                if (!ast_traverse_helper(node->data.function_def.redirections->nodes[i], visitor,
                                         user_data))
                    return false;
            }
        }
        break;

    case AST_REDIRECTED_COMMAND:
        if (!ast_traverse_helper(node->data.redirected_command.command, visitor, user_data))
            return false;
        if (node->data.redirected_command.redirections != NULL)
        {
            for (int i = 0; i < node->data.redirected_command.redirections->size; i++)
            {
                if (!ast_traverse_helper(node->data.redirected_command.redirections->nodes[i],
                                         visitor, user_data))
                    return false;
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

exec_result_t exec_compound_list(exec_frame_t* frame, ast_node_t* list)
{
    Expects_not_null(frame);
    Expects_not_null(list);
    Expects(list->type == AST_COMMAND_LIST);

    exec_result_t result = { .status = EXEC_OK, .has_exit_status = false, .exit_status = 0 };

    // Access the command list data
    ast_node_list_t *items = list->data.command_list.items;
    cmd_separator_list_t *separators = list->data.command_list.separators;

    if (!items || items->size == 0) {
        // Empty list: no commands to execute, exit status 0
        result.has_exit_status = true;
        result.exit_status = 0;
        return result;
    }

    // Iterate through each command in the list
    for (int i = 0; i < items->size; i++) {
        ast_node_t *cmd = items->nodes[i];
        if (!cmd)
            continue;  // Skip null commands (shouldn't happen, but defensive)

        cmd_separator_t sep = cmd_separator_list_get(separators, i);
        if (sep == CMD_EXEC_BACKGROUND)
        {
            /* For POSIX, UCRT, and ISO_C, we'll just call exec_background_job,
             * but that doesn't mean this will actually run in the background.
             * We'll handle platform-specific limitations inside exec_background_job.
             *
             * In POSIX, though, exec_background_job will return quickly.
             * For the others, it depends.
             */
            string_t *command_line = ast_node_to_command_line_full(cmd);
            // Since we're not sending this to spawn, we can keep this as a single string_t
            string_list_t *argv_list = string_list_create();
            string_list_move_push_back(argv_list, &command_line);
            exec_background_job(frame, cmd, argv_list);
            continue;
        }

        // Execute the command based on its type
        exec_result_t cmd_result;
        switch (cmd->type) {
            case AST_PIPELINE:
                cmd_result = exec_pipeline(frame, cmd);
                break;
            case AST_AND_OR_LIST:
                cmd_result = exec_and_or_list(frame, cmd);
                break;
            case AST_COMMAND_LIST:
                cmd_result = exec_compound_list(frame, cmd);
                break;
            case AST_SIMPLE_COMMAND:
                cmd_result = exec_simple_command(frame, cmd);
                break;
            case AST_BRACE_GROUP:
                cmd_result = exec_brace_group(frame, cmd->data.compound.body, NULL);
                break;
            case AST_SUBSHELL:
                cmd_result = exec_subshell(frame, cmd->data.compound.body);
                break;
            case AST_IF_CLAUSE:
                cmd_result = exec_if_clause(frame, cmd);
                break;
            case AST_WHILE_CLAUSE:
            case AST_UNTIL_CLAUSE:
                cmd_result = exec_while_clause(frame, cmd);
                break;
            case AST_FOR_CLAUSE:
                cmd_result = exec_for_clause(frame, cmd);
                break;
            case AST_CASE_CLAUSE:
                cmd_result = exec_case_clause(frame, cmd);
                break;
            case AST_REDIRECTED_COMMAND:
                cmd_result = exec_redirected_command(frame, cmd);
                break;
            case AST_FUNCTION_DEF:
                cmd_result = exec_function_def_clause(frame, cmd);
                break;
            default:
                exec_set_error(frame->executor, "Unsupported command type in compound list: %d (%s)", cmd->type, ast_node_type_to_string(cmd->type));
                result.status = EXEC_NOT_IMPL;
                return result;
        }

        // Propagate errors from command execution
        if (cmd_result.status != EXEC_OK) {
            result.status = cmd_result.status;
            if (cmd_result.has_exit_status) {
                result.has_exit_status = true;
                result.exit_status = cmd_result.exit_status;
            }
            return result;  // Stop on error (unless background)
        }

        // Update the frame's exit status from the command
        if (cmd_result.has_exit_status) {
            frame->last_exit_status = cmd_result.exit_status;
            result.has_exit_status = true;
            result.exit_status = cmd_result.exit_status;
        }

        // Handle the separator after this command
        // cmd_separator_t sep = ast_node_command_list_get_separator(list, i);
        if (sep == CMD_EXEC_BACKGROUND) {
            // Run in background: Add to job store and continue without waiting
            // Note: This assumes the command spawned processes; integrate with job control
            // For simplicity, assume exec_pipeline/exec_and_or_list handle process creation
            // In a full impl, extract PIDs from cmd_result and add to job_store
            // Example: job_store_add(frame->executor->jobs, command_line, true);
            // For now, just log or skip detailed job management
            // TODO: Implement full background job tracking
        } else if (sep == CMD_EXEC_SEQUENTIAL) {
            // Wait for completion (already done above), then proceed to next
        }
        else if (sep == CMD_EXEC_END)
        {
            // End of list: No more commands
            break;
        }
        // Invalid separators are handled by the parser, so no need to check
    }

    return result;
}

exec_result_t exec_and_or_list(exec_frame_t *frame, ast_node_t *list)
{
    Expects_not_null(frame);
    Expects_not_null(list);
    Expects(list->type == AST_AND_OR_LIST);

    exec_result_t result = { .status = EXEC_OK, .has_exit_status = false, .exit_status = 0 };

    ast_node_t *left = list->data.andor_list.left;
    ast_node_t *right = list->data.andor_list.right;
    andor_operator_t op = list->data.andor_list.op;

    // Execute left command
    exec_result_t left_result;
    switch (left->type) {
        case AST_COMMAND_LIST:
            left_result = exec_compound_list(frame, left);
            break;
        case AST_AND_OR_LIST:
            left_result = exec_and_or_list(frame, left);
            break;
        case AST_PIPELINE:
            left_result = exec_pipeline(frame, left);
            break;
        case AST_SIMPLE_COMMAND:
            left_result = exec_simple_command(frame, left);
            break;
        default:
            exec_set_error(frame->executor, "Unsupported AST node type in and_or_list: %d", left->type);
            result.status = EXEC_NOT_IMPL;
            return result;
    }

    if (left_result.status != EXEC_OK) {
        result = left_result;
        return result;
    }

    // Determine if right command should be executed
    bool execute_right = false;
    if (op == ANDOR_OP_AND) {
        if (left_result.exit_status == 0) {
            execute_right = true;
        }
    } else if (op == ANDOR_OP_OR) {
        if (left_result.exit_status != 0) {
            execute_right = true;
        }
    }

    if (execute_right) {
        // Execute right command
        exec_result_t right_result;
        switch (right->type) {
            case AST_COMMAND_LIST:
                right_result = exec_compound_list(frame, right);
                break;
            case AST_AND_OR_LIST:
                right_result = exec_and_or_list(frame, right);
                break;
            case AST_PIPELINE:
                right_result = exec_pipeline(frame, right);
                break;
            case AST_SIMPLE_COMMAND:
                right_result = exec_simple_command(frame, right);
                break;
            default:
                exec_set_error(frame->executor, "Unsupported AST node type in and_or_list: %d", right->type);
                result.status = EXEC_NOT_IMPL;
                return result;
        }

        if (right_result.status != EXEC_OK) {
            result = right_result;
            return result;
        }

        result = right_result;
    } else {
        result = left_result;
    }

    // Update frame's last exit status
    frame->last_exit_status = result.exit_status;

    return result;
}

exec_result_t exec_pipeline(exec_frame_t *frame, ast_node_t *list)
{
    Expects_not_null(frame);
    Expects_not_null(list);
    Expects(list->type == AST_PIPELINE);

    ast_node_list_t *commands = list->data.pipeline.commands;
    bool is_negated = list->data.pipeline.is_negated;

    // Execute the pipeline using the frame system
    exec_result_t result = exec_pipeline_group(frame, commands, is_negated);

    return result;
}

exec_result_t exec_simple_command(exec_frame_t *frame, ast_node_t *node)
{
    /* Executing a simple command is so complicated, it gets
     * its own module, lol. Simple, my butt. */
    exec_status_t status = exec_execute_simple_command(frame, node);
    exec_result_t result;
    if (status == EXEC_OK)
    {
        result.status = EXEC_OK;
        result.has_exit_status = true;
        result.exit_status = frame->last_exit_status;
    }
    else
    {
        result.status = status;
        result.has_exit_status = false;
        result.exit_status = 0;
    }

    /* Check if a builtin set pending control flow (return, break, continue) */
    result.flow = frame->pending_control_flow;
    result.flow_depth = frame->pending_flow_depth;

    /* Reset pending flow for next command */
    frame->pending_control_flow = EXEC_FLOW_NORMAL;
    frame->pending_flow_depth = 0;

    return result;
}

exec_result_t exec_if_clause(exec_frame_t *frame, ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects(node->type == AST_IF_CLAUSE);

    exec_result_t result = { .status = EXEC_OK, .has_exit_status = false, .exit_status = 0 };

    // Execute the main condition
    exec_result_t cond_result = exec_execute_dispatch(frame, node->data.if_clause.condition);
    if (cond_result.status != EXEC_OK) {
        result = cond_result;
        return result;
    }

    // If condition succeeds (exit status 0), execute then_body
    if (cond_result.has_exit_status && cond_result.exit_status == 0) {
        exec_result_t then_result = exec_execute_dispatch(frame, node->data.if_clause.then_body);
        if (then_result.status != EXEC_OK) {
            result = then_result;
            return result;
        }
        result = then_result;
        return result;
    }

    // Check elif clauses
    if (node->data.if_clause.elif_list) {
        for (int i = 0; i < node->data.if_clause.elif_list->size; i++) {
            ast_node_t *elif_node = node->data.if_clause.elif_list->nodes[i];
            if (!elif_node || elif_node->type != AST_IF_CLAUSE) {
                // Assuming elif is also AST_IF_CLAUSE with condition and then_body
                continue;
            }
            exec_result_t elif_cond_result = exec_execute_dispatch(frame, elif_node->data.if_clause.condition);
            if (elif_cond_result.status != EXEC_OK) {
                result = elif_cond_result;
                return result;
            }
            if (elif_cond_result.has_exit_status && elif_cond_result.exit_status == 0) {
                exec_result_t elif_then_result = exec_execute_dispatch(frame, elif_node->data.if_clause.then_body);
                if (elif_then_result.status != EXEC_OK) {
                    result = elif_then_result;
                    return result;
                }
                result = elif_then_result;
                return result;
            }
        }
    }

    // Execute else_body if present
    if (node->data.if_clause.else_body) {
        exec_result_t else_result = exec_execute_dispatch(frame, node->data.if_clause.else_body);
        if (else_result.status != EXEC_OK) {
            result = else_result;
            return result;
        }
        result = else_result;
    } else {
        // No else, exit status 0
        result.has_exit_status = true;
        result.exit_status = 0;
    }

    return result;
}

exec_result_t exec_while_clause(exec_frame_t *frame, ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects(node->type == AST_WHILE_CLAUSE || node->type == AST_UNTIL_CLAUSE);

    // Extract the loop components
    ast_node_t *condition = node->data.loop_clause.condition;
    ast_node_t *body = node->data.loop_clause.body;
    bool is_until = (node->type == AST_UNTIL_CLAUSE);

    // Execute the while/until loop using the frame system
    exec_result_t result = exec_while_loop(frame, condition, body, is_until);

    return result;
}

exec_result_t exec_for_clause(exec_frame_t *frame, ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects(node->type == AST_FOR_CLAUSE);

    // Extract the for loop components
    string_t *var_name = node->data.for_clause.variable;
    token_list_t *word_tokens = node->data.for_clause.words;
    ast_node_t *body = node->data.for_clause.body;

    // Convert tokens to string list for iteration
    string_list_t *words;
    if (word_tokens && token_list_size(word_tokens) > 0)
    {
        // Expand word tokens to strings
        // For now, just extract literal strings (full expansion would use word expansion)
        words = string_list_create();
        for (int i = 0; i < token_list_size(word_tokens); i++)
        {
            const token_t *tok = token_list_get(word_tokens, i);
            string_t *word_str = token_to_string(tok);
            string_list_move_push_back(words, &word_str);
        }
    }
    else
    {
        // No words provided: iterate over positional parameters
        positional_params_t *params = frame->positional_params;
        words = positional_params_get_all(params);
    }

    // Execute the for loop using the frame system
    exec_result_t result = exec_for_loop(frame, var_name, words, body);

    // Clean up
    string_list_destroy(&words);

    return result;
}

exec_result_t exec_function_def_clause(exec_frame_t *frame, ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects_eq(node->type, AST_FUNCTION_DEF);

    ast_node_t *body = node->data.function_def.body;
    ast_node_list_t *ast_redirections = node->data.function_def.redirections;
    string_t *name = node->data.function_def.name;
    exec_result_t result = {.status = EXEC_OK,
                            .has_exit_status = true,
                            .exit_status = 0,
                            .flow = EXEC_FLOW_NORMAL,
                            .flow_depth = 0};

    // Convert AST redirections to exec_redirections_t format
    exec_redirections_t *redirections = NULL;
    if (ast_redirections && ast_node_list_size(ast_redirections) > 0)
    {
        redirections = exec_redirections_from_ast(frame, ast_redirections);
        if (!redirections)
        {
            exec_set_error(frame->executor, "Failed to convert redirections for function '%s'",
                           string_cstr(name));
            result.status = EXEC_ERROR;
            result.exit_status = 1;
            return result;
        }
    }

    func_store_t *func_store = frame->functions;
    func_store_insert_result_t ret = func_store_add_ex(func_store, name, body, redirections);

        if (ret.error != FUNC_STORE_ERROR_NONE)
    {
        exec_set_error(frame->executor, "Failed to define function '%s'", string_cstr(name));
        // Clean up redirections on failure
        if (redirections)
            exec_redirections_destroy(&redirections);
        result.status = EXEC_ERROR;
        result.exit_status = 1;
        return result;
    }

    return result;
}

exec_result_t exec_redirected_command(exec_frame_t *frame, ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects(node->type == AST_REDIRECTED_COMMAND);

    ast_node_t *command = node->data.redirected_command.command;
    ast_node_list_t *ast_redirections = node->data.redirected_command.redirections;

    // Convert AST redirections to exec_redirections_t format
    exec_redirections_t *redirections = exec_redirections_from_ast(frame, ast_redirections);
    if (!redirections && ast_redirections && ast_node_list_size(ast_redirections) > 0)
    {
        // Conversion failed
        exec_result_t error_result = {.status = EXEC_ERROR,
                                      .has_exit_status = true,
                                      .exit_status = 1,
                                      .flow = EXEC_FLOW_NORMAL,
                                      .flow_depth = 0};
        return error_result;
    }

    // Execute the command with redirections using the brace group frame
    // (which shares state but applies/restores redirections)
    exec_result_t result = exec_brace_group(frame, command, redirections);

    // Clean up
    if (redirections)
        exec_redirections_destroy(&redirections);

    return result;
}

/**
 * Execute a while/until loop from within an EXEC_FRAME_LOOP frame.
 *
 * This function performs the actual loop iteration logic for while and until
 * constructs. It must be called from within an EXEC_FRAME_LOOP frame (created
 * by exec_while_loop() in exec_frame.c) so that break/continue semantics work
 * correctly.
 *
 * The EXEC_FRAME_LOOP frame provides:
 * - loop_depth tracking for nested loops
 * - EXEC_LOOP_TARGET policy for handling break/continue with proper depth
 * - EXEC_RETURN_TRANSPARENT policy so return statements propagate up to functions
 *
 * This function evaluates the condition on each iteration and executes the body
 * when appropriate (condition succeeds for while, fails for until). Control flow
 * statements (break, continue, return) in the body are handled by checking the
 * flow field in the result and responding accordingly.
 *
 * @param frame The current execution frame (must be EXEC_FRAME_LOOP)
 * @param params Parameters including condition, body, and until_mode flag
 * @return exec_result_t with final loop status and any pending control flow
 */
exec_result_t exec_condition_loop(exec_frame_t *frame, exec_params_t *params)
{
    Expects_not_null(frame);
    Expects_not_null(params);
    Expects_not_null(params->condition);
    Expects_not_null(params->body);

    exec_result_t result = {.status = EXEC_OK,
                            .has_exit_status = true,
                            .exit_status = 0,
                            .flow = EXEC_FLOW_NORMAL,
                            .flow_depth = 0};

    bool is_until = params->until_mode;

    while (true)
    {
        // Execute condition
        exec_result_t cond_result = exec_execute_dispatch(frame, params->condition);
        if (cond_result.status != EXEC_OK)
        {
            return cond_result;
        }

        // Check if we should exit the loop
        bool condition_met = (cond_result.exit_status == 0);
        if (is_until ? condition_met : !condition_met)
        {
            // Exit loop
            break;
        }

        // Execute body
        exec_result_t body_result;
        if (params->body->type == AST_COMMAND_LIST)
            body_result = exec_compound_list(frame, params->body);
        else
            body_result = exec_execute_dispatch(frame, params->body);

        // Handle control flow
        if (body_result.flow == EXEC_FLOW_BREAK)
        {
            if (body_result.flow_depth > 1)
            {
                // Breaking to outer loop
                body_result.flow_depth--;
                return body_result;
            }
            else
            {
                // Breaking this loop
                result.exit_status = body_result.exit_status;
                result.has_exit_status = body_result.has_exit_status;
                break;
            }
        }
        else if (body_result.flow == EXEC_FLOW_CONTINUE)
        {
            if (body_result.flow_depth > 1)
            {
                // Continuing to outer loop
                body_result.flow_depth--;
                return body_result;
            }
            // Continue this loop - just continue to next iteration
            result.exit_status = body_result.exit_status;
            result.has_exit_status = body_result.has_exit_status;
            continue;
        }
        else if (body_result.flow == EXEC_FLOW_RETURN)
        {
            // Return propagates up
            return body_result;
        }
        else if (body_result.status != EXEC_OK)
        {
            return body_result;
        }

        // Update exit status from body
        result.exit_status = body_result.exit_status;
        result.has_exit_status = body_result.has_exit_status;
    }

    return result;
}


/**
 * Execute a for loop from within an EXEC_FRAME_LOOP frame.
 *
 * This function performs the actual loop iteration logic for for constructs.
 * It must be called from within an EXEC_FRAME_LOOP frame (created by
 * exec_for_loop() in exec_frame.c) so that break/continue semantics work
 * correctly.
 *
 * The EXEC_FRAME_LOOP frame provides:
 * - loop_depth tracking for nested loops
 * - EXEC_LOOP_TARGET policy for handling break/continue with proper depth
 * - EXEC_RETURN_TRANSPARENT policy so return statements propagate up to functions
 *
 * This function iterates over the word list, setting the loop variable to each
 * word in turn and executing the body. Control flow statements (break, continue,
 * return) in the body are handled by checking the flow field in the result and
 * responding accordingly.
 *
 * @param frame The current execution frame (must be EXEC_FRAME_LOOP)
 * @param params Parameters including loop_var_name, iteration_words, and body
 * @return exec_result_t with final loop status and any pending control flow
 */
exec_result_t exec_iteration_loop(exec_frame_t *frame, exec_params_t *params)
{
    Expects_not_null(frame);
    Expects_not_null(params);
    Expects_not_null(params->loop_var_name);
    Expects_not_null(params->iteration_words);
    Expects_not_null(params->body);

    exec_result_t result = {.status = EXEC_OK,
                            .has_exit_status = true,
                            .exit_status = 0,
                            .flow = EXEC_FLOW_NORMAL,
                            .flow_depth = 0};

    // Iterate over each word
    for (size_t i = 0; i < string_list_size(params->iteration_words); i++)
    {
        const string_t *word = string_list_at(params->iteration_words, i);

        // Set loop variable
        variable_store_add(frame->variables, params->loop_var_name, word, false, false);

        // Execute body
        exec_result_t body_result;
        if (params->body->type == AST_COMMAND_LIST)
            body_result = exec_compound_list(frame, params->body);
        else
            body_result = exec_execute_dispatch(frame, params->body);

        // Handle control flow
        if (body_result.flow == EXEC_FLOW_BREAK)
        {
            if (body_result.flow_depth > 1)
            {
                // Breaking to outer loop
                body_result.flow_depth--;
                return body_result;
            }
            else
            {
                // Breaking this loop
                result.exit_status = body_result.exit_status;
                result.has_exit_status = body_result.has_exit_status;
                break;
            }
        }
        else if (body_result.flow == EXEC_FLOW_CONTINUE)
        {
            if (body_result.flow_depth > 1)
            {
                // Continuing to outer loop
                body_result.flow_depth--;
                return body_result;
            }
            // Continue this loop - just continue to next iteration
            result.exit_status = body_result.exit_status;
            result.has_exit_status = body_result.has_exit_status;
            continue;
        }
        else if (body_result.flow == EXEC_FLOW_RETURN)
        {
            // Return propagates up
            return body_result;
        }
        else if (body_result.status != EXEC_OK)
        {
            return body_result;
        }

        // Update exit status from body
        result.exit_status = body_result.exit_status;
        result.has_exit_status = body_result.has_exit_status;
    }

    return result;
}

/**
 * Execute a pipeline from within an EXEC_FRAME_PIPELINE frame.
 *
 * This function orchestrates the execution of multiple commands connected by pipes.
 * It must be called from within an EXEC_FRAME_PIPELINE frame. It creates pipes,
 * forks child processes for each command, sets up pipe plumbing, and waits for all
 * children to complete.
 *
 * Each pipeline command runs in its own EXEC_FRAME_PIPELINE_CMD frame (created in
 * the child process after fork) with proper subshell semantics: copied variables,
 * reset traps, and proper process group handling.
 *
 * @param frame The current execution frame (must be EXEC_FRAME_PIPELINE)
 * @param params Parameters including pipeline_commands and pipeline_negated
 * @return exec_result_t with final pipeline status
 */
exec_result_t exec_pipeline_orchestrate(exec_frame_t *frame, exec_params_t *params)
{
    Expects_not_null(frame);
    Expects_not_null(params);
    Expects_not_null(params->pipeline_commands);

    ast_node_list_t *commands = params->pipeline_commands;
    bool is_negated = params->pipeline_negated;
    int ncmds = commands->size;

    exec_result_t result = {.status = EXEC_OK,
                            .has_exit_status = true,
                            .exit_status = 0,
                            .flow = EXEC_FLOW_NORMAL,
                            .flow_depth = 0};

    if (ncmds == 0)
    {
        result.exit_status = is_negated ? 1 : 0;
        return result;
    }

    if (ncmds == 1)
    {
        // Single command - just execute it
        ast_node_t *cmd = commands->nodes[0];
        exec_result_t cmd_result = exec_execute_dispatch(frame, cmd);
        if (is_negated && cmd_result.has_exit_status)
        {
            cmd_result.exit_status = (cmd_result.exit_status == 0) ? 1 : 0;
        }
        return cmd_result;
    }

#ifdef POSIX_API
    // Create pipes: we need (ncmds - 1) pipes
    int pipes[2 * (ncmds - 1)];
    for (int i = 0; i < ncmds - 1; i++)
    {
        if (pipe(pipes + 2 * i) == -1)
        {
            exec_set_error(frame->executor, "pipe() failed");
            result.status = EXEC_ERROR;
            result.exit_status = 1;
            return result;
        }
    }

    pid_t pids[ncmds];
    pid_t pipeline_pgid = 0;

    // Fork and execute each command
    for (int i = 0; i < ncmds; i++)
    {
        ast_node_t *cmd = commands->nodes[i];

        pid_t pid = fork();
        if (pid == -1)
        {
            exec_set_error(frame->executor, "fork() failed in pipeline");
            // Close all pipes
            for (int j = 0; j < 2 * (ncmds - 1); j++)
            {
                close(pipes[j]);
            }
            result.status = EXEC_ERROR;
            result.exit_status = 1;
            return result;
        }
        else if (pid == 0)
        {
            /* Child process */

            // Set up stdin from previous pipe
            if (i > 0)
            {
                dup2(pipes[2 * (i - 1)], STDIN_FILENO);
            }

            // Set up stdout to next pipe
            if (i < ncmds - 1)
            {
                dup2(pipes[2 * i + 1], STDOUT_FILENO);
            }

            // Close all pipe FDs
            for (int j = 0; j < 2 * (ncmds - 1); j++)
            {
                close(pipes[j]);
            }

            // Set up process group
            if (i == 0)
            {
                // First command creates the group
                setpgid(0, 0);
            }
            else
            {
                // Other commands join the group
                setpgid(0, pipeline_pgid);
            }

            // Execute the command (in the context of the current frame, which is already
            // an EXEC_FRAME_PIPELINE that shares parent state)
            exec_result_t cmd_result = exec_execute_dispatch(frame, cmd);

            // Exit the child with the command's exit status
            _exit(cmd_result.has_exit_status ? cmd_result.exit_status : 0);
        }

        // Parent process
        pids[i] = pid;
        if (i == 0)
        {
            pipeline_pgid = pid; // First child is the group leader
        }
    }

    // Parent: close all pipes
    for (int i = 0; i < 2 * (ncmds - 1); i++)
    {
        close(pipes[i]);
    }

    // Wait for all children
    int last_status = 0;
    for (int i = 0; i < ncmds; i++)
    {
        int status;
        if (waitpid(pids[i], &status, 0) < 0)
        {
            exec_set_error(frame->executor, "waitpid() failed in pipeline");
            result.status = EXEC_ERROR;
            result.exit_status = 1;
            return result;
        }

        // The pipeline's exit status is the exit status of the last command
        if (i == ncmds - 1)
        {
            if (WIFEXITED(status))
            {
                last_status = WEXITSTATUS(status);
            }
            else if (WIFSIGNALED(status))
            {
                last_status = 128 + WTERMSIG(status);
            }
            else
            {
                last_status = 127;
            }
        }
    }

    result.exit_status = is_negated ? (last_status == 0 ? 1 : 0) : last_status;
    frame->last_exit_status = result.exit_status;

    return result;

#else
    // For non-POSIX systems, pipelines are not supported
    exec_set_error(frame->executor, "Pipelines not supported on this platform");
    result.status = EXEC_NOT_IMPL;
    result.exit_status = 1;
    return result;
#endif
}

exec_result_t exec_case_clause(exec_frame_t *frame, ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects(node->type == AST_CASE_CLAUSE);

    exec_result_t result = {
        .status = EXEC_OK,
        .has_exit_status = true,
        .exit_status = 0,
        .flow = EXEC_FLOW_NORMAL,
        .flow_depth = 0
    };

    // Get the word to match against
    token_t *word_token = node->data.case_clause.word;
    if (!word_token) {
        return result; // No word, exit status 0
    }

    // Expand the word (for now, just convert to string - full expansion would be needed)
    string_t *word = token_to_string(word_token);
    if (!word) {
        exec_set_error(frame->executor, "Failed to expand case word");
        result.status = EXEC_ERROR;
        result.exit_status = 1;
        return result;
    }

    // Iterate through case items
    ast_node_list_t *case_items = node->data.case_clause.case_items;
    if (!case_items) {
        string_destroy(&word);
        return result; // No items, exit status 0
    }

    bool matched = false;
    for (int i = 0; i < case_items->size && !matched; i++) {
        ast_node_t *item = case_items->nodes[i];
        if (!item || item->type != AST_CASE_ITEM) {
            continue;
        }

        // Check if any pattern in this item matches
        token_list_t *patterns = item->data.case_item.patterns;
        if (!patterns) {
            continue;
        }

        for (int j = 0; j < token_list_size(patterns); j++) {
            const token_t *pattern_token = token_list_get(patterns, j);
            string_t *pattern = token_to_string(pattern_token);
            
            // Pattern matching (for now, simple string equality - full impl would use fnmatch)
            bool pattern_matches = false;
#ifdef POSIX_API
            // Use fnmatch for proper pattern matching
            pattern_matches = (fnmatch(string_cstr(pattern), string_cstr(word), 0) == 0);
#else
            // Fallback to simple string comparison
            pattern_matches = string_eq(pattern, word);
#endif
            
            string_destroy(&pattern);
            
            if (pattern_matches) {
                matched = true;
                
                // Execute the body of this case item
                ast_node_t *body = item->data.case_item.body;
                if (body) {
                    result = exec_execute_dispatch(frame, body);
                }
                
                break; // Stop checking patterns
            }
        }
    }

    string_destroy(&word);
    
    // If no match found, exit status remains 0
    return result;
}


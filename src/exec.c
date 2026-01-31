/**
 * exec.c - Shell executor implementation
 *
 * This file implements the public executor API and high-level execution.
 * Frame management and policy-driven execution is in exec_frame.c.
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "exec.h"
#include "exec_frame.h"
#include "exec_internal.h"

#include "alias_store.h"
#include "ast.h"
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
#include "string_list.h"
#include "token.h"
#include "tokenizer.h"
#include "trap_store.h"
#include "variable_store.h"
#include "xalloc.h"

#ifdef POSIX_API
#include <fcntl.h>
#include <limits.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef UCRT_API
#include <direct.h>
#include <io.h>
#include <process.h>
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define EXECUTOR_ERROR_BUFFER_SIZE 512

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

struct exec_t *exec_create(struct exec_cfg_t *cfg)
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
        e->shell_args = string_list_create_from_cstr_array((const char *const *)argv + 1, argc - 1);
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
        e->env_vars = string_list_create_from_cstr_array((const char *const *)cfg->envp, -1);
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

exec_status_t exec_setup_interactive_execute(exec_t *executor)
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
    case AST_WHILE_CLAUSE:
    case AST_UNTIL_CLAUSE:
    case AST_FOR_CLAUSE:
    case AST_CASE_CLAUSE:
    case AST_FUNCTION_DEF:
    case AST_REDIRECTED_COMMAND:
        /* These need their own handlers - for now return not implemented */
        exec_set_error(executor, "AST node type %d not yet implemented in new executor",
                       root->type);
        return EXEC_NOT_IMPL;

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

    while (fgets(line_buffer, sizeof(line_buffer), fp) != NULL)
    {
        lexer_append_input_cstr(lx, line_buffer);

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

        if (lex_status == LEX_INCOMPLETE || lex_status == LEX_NEED_HEREDOC)
        {
            token_list_destroy(&raw_tokens);
            continue;
        }

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

        if (processed_tokens->size == 0)
        {
            token_list_destroy(&processed_tokens);
            continue;
        }

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
            if (gnode)
                g_node_destroy(&gnode);
            parser_destroy(&parser);
            continue;
        }

        if (parse_status == PARSE_EMPTY || !gnode)
        {
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

        lexer_reset(lx);
    }

    tokenizer_destroy(&tokenizer);
    lexer_destroy(&lx);

    return final_status;
}

/* ============================================================================
 * Job Control
 * ============================================================================ */

void exec_reap_background_jobs(exec_t *executor)
{
    Expects_not_null(executor);

#ifdef POSIX_API
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        job_store_mark_done(executor->jobs, pid, exit_status);
    }
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
        if (!cmd) continue;  // Skip null commands (shouldn't happen, but defensive)

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
            // Add other command types as implemented (e.g., AST_SIMPLE_COMMAND)
            default:
                // For now, treat as unsupported; in full impl, add more cases
                exec_set_error(frame->executor, "Unsupported command type in compound list: %d", cmd->type);
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
        cmd_separator_t sep = ast_node_command_list_get_separator(list, i);
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
    abort();
}

exec_result_t exec_pipeline(exec_frame_t *frame, ast_node_t *list)
{
    abort();
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

    return result;
}

/* Stub implementations for unimplemented functions */

exec_t *exec_create_subshell(exec_t *executor)
{
    /* For now, return the same executor.
     * In a full implementation, this would create a new child executor
     * with appropriate state copying for a true subshell.
     */
    return executor;
}

exec_result_t exec_condition_loop(exec_frame_t *frame, exec_params_t *params)
{
    (void)frame;
    (void)params;
    return (exec_result_t){.status = EXEC_ERROR};
}

exec_result_t exec_iteration_loop(exec_frame_t *frame, exec_params_t *params)
{
    (void)frame;
    (void)params;
    return (exec_result_t){.status = EXEC_ERROR};
}

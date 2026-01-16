#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "builtins.h"
#include "exec.h"
#include "exec_command.h"
#include "exec_expander.h"
#include "exec_internal.h"
#include "exec_redirect.h"
#include "func_store.h"
#include "logging.h"
#include "positional_params.h"
#include "string_t.h"
#include "token.h"
#include "variable_store.h"
#include "xalloc.h"

#ifdef POSIX_API
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef UCRT_API
#include <process.h>
#endif


/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Populate special shell variables into a variable store.
 * Populates $?, $!, $$, $_, $- from executor state.
 */
static void exec_populate_special_variables(variable_store_t *store, const exec_t *ex)
{
    Expects_not_null(store);
    Expects_not_null(ex);
    char buf[32];

    if (ex->last_exit_status_set)
    {
        snprintf(buf, sizeof(buf), "%d", ex->last_exit_status);
        variable_store_add_cstr(store, "?", buf, false, false);
    }

    if (ex->last_background_pid_set)
    {
        snprintf(buf, sizeof(buf), "%d", ex->last_background_pid);
        variable_store_add_cstr(store, "!", buf, false, false);
    }

    if (ex->shell_pid_set)
    {
        snprintf(buf, sizeof(buf), "%d", ex->shell_pid);
        variable_store_add_cstr(store, "$", buf, false, false);
    }

    if (ex->last_argument_set)
    {
        variable_store_add_cstr(store, "_", string_cstr(ex->last_argument), false, false);
    }

    if (ex->opt_flags_set)
    {
        char flags[16] = {0};
        int idx = 0;

        if (ex->opt.allexport) flags[idx++] = 'a';
        if (ex->opt.errexit) flags[idx++] = 'e';
        if (ex->opt.noclobber) flags[idx++] = 'C';
        if (ex->opt.noglob) flags[idx++] = 'f';
        if (ex->opt.noexec) flags[idx++] = 'n';
        if (ex->opt.nounset) flags[idx++] = 'u';
        if (ex->opt.verbose) flags[idx++] = 'v';
        if (ex->opt.xtrace) flags[idx++] = 'x';
        if (ex->is_interactive) flags[idx++] = 'i';

        flags[idx] = '\0';
        variable_store_add_cstr(store, "-", flags, false, false);
    }
}

/**
 * Build a temporary variable store for a simple command:
 *   - copies all variables from executor->variables
 *   - populates special vars ($?, $!, $$, $_, $-)
 *   - overlays assignment words from the command with expanded RHS
 */
static variable_store_t *exec_build_temp_store_for_simple_command(exec_t *ex,
                                                                   const ast_node_t *node)
{
    Expects_not_null(ex);
    Expects_not_null(node);
    Expects_eq(node->type, AST_SIMPLE_COMMAND);

    variable_store_t *temp = variable_store_create();
    if (!temp)
        return NULL;

    variable_store_copy_all(temp, ex->variables);
    exec_populate_special_variables(temp, ex);

    token_list_t *assignments = node->data.simple_command.assignments;
    if (assignments)
    {
        for (int i = 0; i < token_list_size(assignments); i++)
        {
            token_t *tok = token_list_get(assignments, i);
            string_t *value = exec_expand_assignment_value(ex, tok);  // Direct call
            if (!value)
            {
                variable_store_destroy(&temp);
                return NULL;
            }

            var_store_error_t err =
                variable_store_add(temp, tok->assignment_name, value, true, false);
            string_destroy(&value);

            if (err != VAR_STORE_ERROR_NONE)
            {
                variable_store_destroy(&temp);
                return NULL;
            }
        }
    }

    return temp;
}

/**
 * Apply prefix assignments to the shell's variable store.
 * Used for special builtins where POSIX requires assignments to persist.
 */
static exec_status_t exec_apply_prefix_assignments(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_SIMPLE_COMMAND);

    const token_list_t *assignments = node->data.simple_command.assignments;
    if (!assignments)
        return EXEC_OK;

    for (int i = 0; i < token_list_size(assignments); i++)
    {
        token_t *tok = token_list_get(assignments, i);
        string_t *value = exec_expand_assignment_value(executor, tok);
        if (!value)
        {
            exec_set_error(executor, "Failed to expand assignment value");
            return EXEC_ERROR;
        }

        var_store_error_t err = variable_store_add(executor->variables, tok->assignment_name, value,
                                                    false, false);
        string_destroy(&value);

        if (err != VAR_STORE_ERROR_NONE)
        {
            exec_set_error(executor, "Cannot assign variable (error %d)", err);
            return EXEC_ERROR;
        }
    }

    return EXEC_OK;
}

/**
 * Execute a shell function.
 */
static exec_status_t exec_invoke_function(exec_t *executor, const ast_node_t *func_def,
                                           const string_list_t *args)
{
    Expects_not_null(executor);
    Expects_not_null(func_def);
    Expects_eq(func_def->type, AST_FUNCTION_DEF);
    Expects_not_null(args);

    exec_status_t status = EXEC_OK;

    positional_params_t *saved_params = executor->positional_params;

    int argc = string_list_size(args);
    if (argc > 1)
    {
        const char **argv = xcalloc((size_t)(argc - 1), sizeof(char *));
        for (int i = 1; i < argc; i++)
        {
            argv[i - 1] = string_cstr(string_list_at(args, i));
        }

        const char *shell_name = executor->shell_name ? string_cstr(executor->shell_name) : "sh";
        executor->positional_params =
            positional_params_create_from_argv(shell_name, argc - 1, argv);
        xfree(argv);
    }
    else
    {
        executor->positional_params = positional_params_create();
    }

    if (!executor->positional_params)
    {
        exec_set_error(executor, "Failed to create positional parameters for function");
        executor->positional_params = saved_params;
        return EXEC_ERROR;
    }

    const ast_node_list_t *func_redirs = func_def->data.function_def.redirections;
#ifdef POSIX_API
    saved_fd_t *saved_fds = NULL;
    int saved_count = 0;

    if (func_redirs && ast_node_list_size(func_redirs) > 0)
    {
        status =
            exec_apply_redirections_posix(executor, func_redirs, &saved_fds, &saved_count);
        if (status != EXEC_OK)
        {
            positional_params_destroy(&executor->positional_params);
            executor->positional_params = saved_params;
            return status;
        }
    }
#elifdef UCRT_API
    saved_fd_t *saved_fds = NULL;
    int saved_count = 0;

    if (func_redirs && ast_node_list_size(func_redirs) > 0)
    {
        fflush(NULL);
        status =
            exec_apply_redirections_ucrt_c(executor, func_redirs, &saved_fds, &saved_count);
        if (status != EXEC_OK)
        {
            positional_params_destroy(&executor->positional_params);
            executor->positional_params = saved_params;
            return status;
        }
    }
#else
    if (func_redirs && ast_node_list_size(func_redirs) > 0)
    {
        exec_set_error(executor, "Function redirections not supported in ISO_C mode");
        positional_params_destroy(&executor->positional_params);
        executor->positional_params = saved_params;
        return EXEC_ERROR;
    }
#endif

    const ast_node_t *body = func_def->data.function_def.body;
    if (body)
    {
        status = exec_execute(executor, body);
    }
    else
    {
        exec_set_exit_status(executor, 0);
    }

#ifdef POSIX_API
    if (func_redirs && saved_fds)
    {
        exec_restore_redirections_posix(saved_fds, saved_count);
        xfree(saved_fds);
    }
#elifdef UCRT_API
    if (func_redirs && saved_fds)
    {
        fflush(NULL);
        exec_restore_redirections_ucrt_c(saved_fds, saved_count);
        xfree(saved_fds);
    }
#endif

    positional_params_destroy(&executor->positional_params);
    executor->positional_params = saved_params;

    return status;
}

/* ============================================================================
 * Simple Command Execution
 * ============================================================================ */

exec_status_t exec_execute_simple_command(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_SIMPLE_COMMAND);

    exec_status_t status = EXEC_OK;

    const token_list_t *word_tokens = node->data.simple_command.words;
    const token_list_t *assign_tokens = node->data.simple_command.assignments;
    const ast_node_list_t *redirs = node->data.simple_command.redirections;

    bool has_words = (word_tokens && token_list_size(word_tokens) > 0);

    /* Assignment-only command */
    if (!has_words)
    {
        if (assign_tokens)
        {
            for (int i = 0; i < token_list_size(assign_tokens); i++)
            {
                token_t *tok = token_list_get(assign_tokens, i);
                string_t *value = exec_expand_assignment_value(executor, tok);
                if (!value)
                {
                    exec_set_error(executor, "assignment expansion failed");
                    status = EXEC_ERROR;
                    goto out_base_exp;
                }

                var_store_error_t err =
                    variable_store_add(executor->variables, tok->assignment_name, value,
                                       false, false);
                string_destroy(&value);

                if (err != VAR_STORE_ERROR_NONE)
                {
                    exec_set_error(executor, "cannot assign variable (error %d)", err);
                    status = EXEC_ERROR;
                    goto out_base_exp;
                }
            }
        }

        exec_set_exit_status(executor, 0);
        status = EXEC_OK;
        goto out_base_exp;
    }

    /* Build temporary variable store */
    variable_store_t *temp_vars =
        exec_build_temp_store_for_simple_command(executor, node);
    if (!temp_vars)
    {
        exec_set_error(executor, "failed to build temporary variable store");
        status = EXEC_ERROR;
        goto out_base_exp;
    }

    /* Expand command words */
    string_list_t *expanded_words = exec_expand_words(executor, word_tokens);
    if (!expanded_words || string_list_size(expanded_words) == 0)
    {
        exec_set_exit_status(executor, 0);
        if (expanded_words)
            string_list_destroy(&expanded_words);
        status = EXEC_OK;
        goto out_exp_temp;
    }

    const string_t *cmd_name_str = string_list_at(expanded_words, 0);
    const char *cmd_name = string_cstr(cmd_name_str);

    /* Apply redirections */
#ifdef POSIX_API
    saved_fd_t *saved_fds = NULL;
    int saved_count = 0;

    status = exec_apply_redirections_posix(executor, redirs, &saved_fds, &saved_count);
    if (status != EXEC_OK)
    {
        string_list_destroy(&expanded_words);
        goto out_exp_temp;
    }
#elif defined(UCRT_API)
    saved_fd_t *saved_fds = NULL;
    int saved_count = 0;

    fflush(NULL);

    status = exec_apply_redirections_ucrt_c(executor, redirs, &saved_fds, &saved_count);
    if (status != EXEC_OK)
    {
        string_list_destroy(&expanded_words);
        goto out_exp_temp;
    }
#else
    if (redirs && ast_node_list_size(redirs) > 0)
    {
        exec_set_error(executor, "redirections not supported in ISO_C_API mode");
        string_list_destroy(&expanded_words);
        status = EXEC_ERROR;
        goto out_exp_temp;
    }
#endif

    /* Classify and execute command */
    int cmd_exit_status = 0;
    builtin_class_t builtin_class = builtin_classify_cstr(cmd_name);

    /* Special builtins: apply prefix assignments permanently */
    if (builtin_class == BUILTIN_SPECIAL && assign_tokens && token_list_size(assign_tokens) > 0)
    {
        exec_status_t assign_status = exec_apply_prefix_assignments(executor, node);
        if (assign_status != EXEC_OK)
        {
            string_list_destroy(&expanded_words);
            status = assign_status;
            goto out_restore_redirs;
        }
    }

    /* Execute: special builtins */
    if (builtin_class == BUILTIN_SPECIAL)
    {
        builtin_func_t builtin_fn = builtin_get_function_cstr(cmd_name);
        if (builtin_fn != NULL)
        {
            cmd_exit_status = (*builtin_fn)(executor, expanded_words);
            goto done_execution;
        }
        exec_set_error(executor, "%s: special builtin not implemented", cmd_name);
        cmd_exit_status = 1;
        goto done_execution;
    }

    /* Execute: shell functions */
    const ast_node_t *func_def = func_store_get_def_cstr(executor->functions, cmd_name);
    if (func_def != NULL)
    {
        exec_status_t func_status = exec_invoke_function(executor, func_def, expanded_words);
        cmd_exit_status = executor->last_exit_status;
        if (func_status != EXEC_OK)
            status = func_status;
        goto done_execution;
    }

    /* Execute: regular builtins */
    if (builtin_class == BUILTIN_REGULAR)
    {
        builtin_func_t builtin_fn = builtin_get_function_cstr(cmd_name);
        if (builtin_fn != NULL)
        {
            cmd_exit_status = (*builtin_fn)(executor, expanded_words);
            goto done_execution;
        }
    }

    /* Execute: external command */
    {
#ifdef POSIX_API
        int argc = string_list_size(expanded_words);
        char **argv = xcalloc((size_t)argc + 1, sizeof(char *));
        for (int i = 0; i < argc; i++)
        {
            argv[i] = xstrdup(string_cstr(string_list_at(expanded_words, i)));
        }
        argv[argc] = NULL;

        char *const *envp = variable_store_with_parent_get_envp(temp_vars, executor->variables);

        pid_t pid = fork();
        if (pid == -1)
        {
            exec_set_error(executor, "fork failed");
            cmd_exit_status = 127;
        }
        else if (pid == 0)
        {
            execve(cmd_name, argv, envp);
            execvp(cmd_name, argv);
            perror(cmd_name);
            _exit(127);
        }
        else
        {
            int wstatus = 0;
            if (waitpid(pid, &wstatus, 0) < 0)
            {
                cmd_exit_status = 127;
            }
            else if (WIFEXITED(wstatus))
            {
                cmd_exit_status = WEXITSTATUS(wstatus);
            }
            else if (WIFSIGNALED(wstatus))
            {
                cmd_exit_status = 128 + WTERMSIG(wstatus);
            }
            else
            {
                cmd_exit_status = 127;
            }
        }

        for (int i = 0; argv[i]; i++)
            xfree(argv[i]);
        xfree(argv);

#elif defined(UCRT_API)
        int argc = string_list_size(expanded_words);
        char **argv = xcalloc((size_t)argc + 1, sizeof(char *));
        for (int i = 0; i < argc; i++)
        {
            argv[i] = xstrdup(string_cstr(string_list_at(expanded_words, i)));
        }
        argv[argc] = NULL;

        if (!cmd_name || strlen(cmd_name) == 0)
        {
            exec_set_error(executor, "empty command name");
            cmd_exit_status = 127;
            for (int i = 0; argv[i]; i++)
                xfree(argv[i]);
            xfree(argv);
            goto done_execution;
        }

        char *const *envp = variable_store_with_parent_get_envp(temp_vars, executor->variables);

        intptr_t spawn_result =
            _spawnvpe(_P_WAIT, cmd_name, (const char *const *)argv, (const char *const *)envp);

        if (spawn_result == -1)
        {
            int err = errno;
            if (err == ENOENT)
                exec_set_error(executor, "%s: command not found", cmd_name);
            else if (err == ENOEXEC)
                exec_set_error(executor, "%s: not executable", cmd_name);
            else
                exec_set_error(executor, "%s: execution failed (errno=%d)", cmd_name, err);

            cmd_exit_status = 127;
            status = EXEC_ERROR;
        }
        else
        {
            cmd_exit_status = (int)spawn_result;
        }

        for (int i = 0; argv[i]; i++)
            xfree(argv[i]);
        xfree(argv);

#else
        string_t *cmdline = string_create();
        for (int i = 0; i < string_list_size(expanded_words); i++)
        {
            if (i > 0)
                string_append_cstr(cmdline, " ");
            string_append(cmdline, string_list_at(expanded_words, i));
        }

        string_t *env_fname = variable_store_write_env_file(temp_vars, executor->variables);
        int rc = system(string_cstr(cmdline));
        if (env_fname)
        {
            remove(string_cstr(env_fname));
            string_destroy(&env_fname);
        }

        string_destroy(&cmdline);
        if (rc == -1)
        {
            exec_set_error(executor, "system() failed");
            cmd_exit_status = 127;
        }
        else
        {
            cmd_exit_status = rc;
        }
#endif
    }

done_execution:
    exec_set_exit_status(executor, cmd_exit_status);

    /* Update $_ with last argument */
    if (string_list_size(expanded_words) > 1)
    {
        const string_t *last_arg =
            string_list_at(expanded_words, string_list_size(expanded_words) - 1);
        if (!executor->last_argument)
            executor->last_argument = string_create();
        string_clear(executor->last_argument);
        string_append(executor->last_argument, last_arg);
        executor->last_argument_set = true;
    }

    string_list_destroy(&expanded_words);

out_restore_redirs:
#if defined(POSIX_API)
    if (redirs && saved_fds)
    {
        exec_restore_redirections_posix(saved_fds, saved_count);
        xfree(saved_fds);
    }
#elif defined(UCRT_API)
    if (redirs && saved_fds)
    {
        fflush(NULL);
        exec_restore_redirections_ucrt_c(saved_fds, saved_count);
        xfree(saved_fds);
    }
#endif

    if (status != EXEC_ERROR)
        status = EXEC_OK;

out_exp_temp:
    variable_store_destroy(&temp_vars);

out_base_exp:
    return status;
}

/* ============================================================================
 * Function Definition
 * ============================================================================ */

exec_status_t exec_execute_function_def(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_FUNCTION_DEF);

    const string_t *name = node->data.function_def.name;

    if (!executor->functions)
    {
        exec_set_error(executor, "function store is not initialized");
        exec_set_exit_status(executor, 1);
        return EXEC_ERROR;
    }

    func_store_error_t err = func_store_add(executor->functions, name, node);

    if (err != FUNC_STORE_ERROR_NONE)
    {
        switch (err)
        {
        case FUNC_STORE_ERROR_EMPTY_NAME:
            exec_set_error(executor, "empty function name");
            exec_set_exit_status(executor, 1);
            break;

        case FUNC_STORE_ERROR_NAME_TOO_LONG:
            exec_set_error(executor, "function name too long");
            exec_set_exit_status(executor, 1);
            break;

        case FUNC_STORE_ERROR_NAME_INVALID_CHARACTER:
        case FUNC_STORE_ERROR_NAME_STARTS_WITH_DIGIT:
            exec_set_error(executor, "invalid function name");
            exec_set_exit_status(executor, 1);
            break;

        case FUNC_STORE_ERROR_STORAGE_FAILURE:
            exec_set_error(executor, "failed to store function definition");
            exec_set_exit_status(executor, 1);
            break;

        case FUNC_STORE_ERROR_NOT_FOUND:
        default:
            exec_set_error(executor, "internal function store error");
            exec_set_exit_status(executor, 1);
            break;
        }

        return EXEC_ERROR;
    }

    exec_clear_error(executor);
    exec_set_exit_status(executor, 0);
    return EXEC_OK_INTERNAL_FUNCTION_STORED;
}

/* ============================================================================
 * Redirected Command Wrapper
 * ============================================================================ */

exec_status_t exec_execute_redirected_command(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_REDIRECTED_COMMAND);

    const ast_node_t *inner = node->data.redirected_command.command;
    const ast_node_list_t *redirs = node->data.redirected_command.redirections;

#ifdef POSIX_API
    saved_fd_t *saved_fds = NULL;
    int saved_count = 0;

    exec_status_t st =
        exec_apply_redirections_posix(executor, redirs, &saved_fds, &saved_count);
    if (st != EXEC_OK)
    {
        return st;
    }
#elifdef UCRT_API
    saved_fd_t *saved_fds = NULL;
    int saved_count = 0;

    exec_status_t st = exec_apply_redirections_ucrt_c(executor, redirs, &saved_fds, &saved_count);
    if (st != EXEC_OK)
    {
        return st;
    }
#else
    exec_status_t st = exec_apply_redirections_iso_c(executor, redirs);
    if (st != EXEC_OK)
    {
        return st;
    }
#endif

    st = exec_execute(executor, inner);

#if defined(POSIX_API)
    if (saved_fds)
    {
        exec_restore_redirections_posix(saved_fds, saved_count);
        xfree(saved_fds);
    }
#elif defined(UCRT_API)
    if (saved_fds)
    {
        exec_restore_redirections_ucrt_c(saved_fds, saved_count);
        xfree(saved_fds);
    }
#endif

    return st;
}

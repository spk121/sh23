#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exec_command.h"

#include "ast.h"
#include "builtins.h"
#include "exec.h"
#include "exec_expander.h"
#include "exec_frame_policy.h"
#include "exec_internal.h"
#include "exec_redirect.h"
#include "func_store.h"
#include "job_store.h"
#include "logging.h"
#include "string_list.h"
#include "string_t.h"
#include "token.h"
#include "trap_store.h"
#include "variable_store.h"
#include "xalloc.h"

#ifdef POSIX_API
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef UCRT_API
#if defined(_WIN64)
#define _AMD64_
#elif defined(_WIN32)
#define _X86_
#endif
#include <handleapi.h>
#include <process.h>
#include <processthreadsapi.h>
#endif

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Populate special shell variables into a variable store.
 * Populates $?, $!, $$, $_, $- from frame state.
 */
static void exec_populate_special_variables(variable_store_t *store, const exec_frame_t *frame)
{
    Expects_not_null(store);
    Expects_not_null(frame);
    Expects_not_null(frame->executor);

    const exec_t *ex = frame->executor;
    char buf[32];

    /* $? - last exit status from frame */
    snprintf(buf, sizeof(buf), "%d", frame->last_exit_status);
    variable_store_add_cstr(store, "?", buf, false, false);

    /* $! - last background PID from frame */
    if (frame->last_bg_pid > 0)
    {
        snprintf(buf, sizeof(buf), "%d", frame->last_bg_pid);
        variable_store_add_cstr(store, "!", buf, false, false);
    }

    /* $$ - shell PID from executor (singleton) */
    if (ex->shell_pid_valid)
    {
        snprintf(buf, sizeof(buf), "%d", ex->shell_pid);
        variable_store_add_cstr(store, "$", buf, false, false);
    }

    /* $_ - last argument from executor */
    if (ex->last_argument_set)
    {
        variable_store_add_cstr(store, "_", string_cstr(ex->last_argument), false, false);
    }

    /* $- - option flags from frame */
    if (frame->opt_flags)
    {
        char flags[16] = {0};
        int idx = 0;

        if (frame->opt_flags->allexport)
            flags[idx++] = 'a';
        if (frame->opt_flags->errexit)
            flags[idx++] = 'e';
        if (frame->opt_flags->noclobber)
            flags[idx++] = 'C';
        if (frame->opt_flags->noglob)
            flags[idx++] = 'f';
        if (frame->opt_flags->noexec)
            flags[idx++] = 'n';
        if (frame->opt_flags->nounset)
            flags[idx++] = 'u';
        if (frame->opt_flags->verbose)
            flags[idx++] = 'v';
        if (frame->opt_flags->xtrace)
            flags[idx++] = 'x';
        if (ex->is_interactive)
            flags[idx++] = 'i';

        flags[idx] = '\0';
        variable_store_add_cstr(store, "-", flags, false, false);
    }
}

/**
 * Build a temporary variable store for a simple command:
 *   - copies all variables from frame->variables
 *   - populates special vars ($?, $!, $$, $_, $-)
 *   - overlays assignment words from the command with expanded RHS
 */
static variable_store_t *exec_build_temp_store_for_simple_command(exec_frame_t *frame,
                                                                  const ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects_eq(node->type, AST_SIMPLE_COMMAND);

    variable_store_t *temp = variable_store_create();
    variable_store_copy_all(temp, frame->variables);
    exec_populate_special_variables(temp, frame);

    token_list_t *assignments = node->data.simple_command.assignments;
    if (assignments)
    {
        for (int i = 0; i < token_list_size(assignments); i++)
        {
            const token_t *tok = token_list_get(assignments, i);
            string_t *value = expand_assignment_value(frame, tok);

            var_store_error_t err =
                variable_store_add(temp, tok->assignment_name, value, true, false);
            string_destroy(&value);

            if (err != VAR_STORE_ERROR_NONE)
            {
                log_warn("Failed to add variable '%s' to temporary variable store",
                         string_cstr(tok->assignment_name));
            }
        }
    }

    return temp;
}

/**
 * Apply prefix assignments to the shell's variable store.
 * Used for special builtins where POSIX requires assignments to persist.
 *
 * This gets called when the executor is executing a special builtin command,
 * and when it's variable store is the shell's temp store for that command.
 * So we apply the assignments to both the temp store and the shell's main store.
 *
 */
static exec_status_t exec_apply_prefix_assignments(exec_frame_t *frame,
                                                   variable_store_t *main_store,
                                                   const ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects_eq(node->type, AST_SIMPLE_COMMAND);

    exec_t *executor = frame->executor;
    const token_list_t *assignments = node->data.simple_command.assignments;
    if (!assignments)
        return EXEC_OK;

    for (int i = 0; i < token_list_size(assignments); i++)
    {
        const token_t *tok = token_list_get(assignments, i);
        string_t *value = expand_assignment_value(frame, tok);
        var_store_error_t err =
            variable_store_add(frame->variables, tok->assignment_name, value, false, false);

        if (err != VAR_STORE_ERROR_NONE)
        {
            exec_set_error(executor, "Cannot assign variable (error %d)", err);
            string_destroy(&value);
            return EXEC_ERROR;
        }
        /* If we get here, the variable name and value must have been valid, so no need
         * for error checking */
        variable_store_add(main_store, tok->assignment_name, value, false, false);
        string_destroy(&value);
    }

    return EXEC_OK;
}

/* ============================================================================
 * Simple Command Execution
 * ============================================================================ */
exec_status_t exec_execute_simple_command(exec_frame_t *frame, const ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects_eq(node->type, AST_SIMPLE_COMMAND);

    exec_t *executor = frame->executor;
    exec_status_t status = EXEC_OK;

    const token_list_t *word_tokens = node->data.simple_command.words;
    const token_list_t *assign_tokens = node->data.simple_command.assignments;
    const ast_node_list_t *redirs = node->data.simple_command.redirections;

    bool has_words = (word_tokens && token_list_size(word_tokens) > 0);

    /* Assignment-only command (no words, no redirs) */
    if (!has_words && !redirs)
    {
        if (assign_tokens)
        {
            for (int i = 0; i < token_list_size(assign_tokens); i++)
            {
                const token_t *tok = token_list_get(assign_tokens, i);
                string_t *value = expand_assignment_value(frame, tok);
                if (!value)
                {
                    exec_set_error(executor, "assignment expansion failed");
                    return EXEC_ERROR;
                }

                var_store_error_t err =
                    variable_store_add(frame->variables, tok->assignment_name, value, false, false);
                string_destroy(&value);

                if (err != VAR_STORE_ERROR_NONE)
                {
                    exec_set_error(executor, "cannot assign variable (error %d)", err);
                    return EXEC_ERROR;
                }
            }
        }

        frame->last_exit_status = 0;
        return EXEC_OK;
    }

    /* Swap in temporary variable store */
    frame->saved_variables = frame->variables;
    variable_store_t *temp_vars = exec_build_temp_store_for_simple_command(frame, node);
    frame->variables = temp_vars;
    temp_vars = NULL;

    /* Convert AST redirections early */
    exec_redirections_t *runtime_redirs = NULL;
    if (redirs && ast_node_list_size(redirs) > 0)
    {
        runtime_redirs = exec_redirections_from_ast(frame, redirs);
        if (!runtime_redirs)
        {
            status = EXEC_ERROR;
            goto out_restore_vars;
        }
    }
    if (!runtime_redirs)
        runtime_redirs = exec_redirections_create();

    /* Expand command words */
    string_list_t *expanded_words =
        has_words ? expand_words(frame, word_tokens) : string_list_create();
    if (has_words && !expanded_words)
    {
        status = EXEC_ERROR;
        goto out_destroy_redirs;
    }

    int cmd_exit_status = 0;

    if (has_words)
    {
        const char *cmd_name = string_cstr(string_list_at(expanded_words, 0));

        builtin_class_t builtin_class = builtin_classify_cstr(cmd_name);

        if (token_is_reserved_word(cmd_name))
        {
            exec_set_error(executor, "%s: syntax error - reserved word in command position",
                           cmd_name);
            cmd_exit_status = 2;
            goto done_execution;
        }

        /* Special builtins: persist assignments */
        if (builtin_class == BUILTIN_SPECIAL && assign_tokens && token_list_size(assign_tokens) > 0)
        {
            exec_status_t assign_st =
                exec_apply_prefix_assignments(frame, frame->saved_variables, node);
            if (assign_st != EXEC_OK)
            {
                status = assign_st;
                goto done_execution;
            }
        }

        /* ────────────────────────────────────────────────
           Internal commands (builtins + functions)
           ──────────────────────────────────────────────── */
        bool is_internal = false;

        /* Shell function */
        const ast_node_t *func_body = func_store_get_def_cstr(frame->functions, cmd_name);
        if (func_body != NULL)
        {
            is_internal = true;

            string_t *func_name_str = string_create_from_cstr(cmd_name);
            const exec_redirections_t *func_redirs =
                func_store_get_redirections(frame->functions, func_name_str);
            string_destroy(&func_name_str);

            string_list_t *func_args = string_list_create_slice(expanded_words, 1, -1);

#if defined(POSIX_API)
            exec_status_t redir_st = exec_apply_redirections_posix(frame, runtime_redirs);
#elif defined(UCRT_API)
            exec_status_t redir_st = exec_apply_redirections_ucrt_c(frame, runtime_redirs);
#else
            exec_status_t redir_st = EXEC_OK; /* ISO C: no redirections */
#endif
            if (redir_st != EXEC_OK)
            {
                status = redir_st;
                string_list_destroy(&func_args);
                goto done_execution;
            }

            exec_result_t func_result = exec_function(frame, func_body, func_args, func_redirs);
            cmd_exit_status = func_result.exit_status;

            string_list_destroy(&func_args);

#if defined(POSIX_API)
            exec_restore_redirections_posix(frame);
#elif defined(UCRT_API)
            exec_restore_redirections_ucrt_c(frame);
#endif
            goto done_execution;
        }

        /* Regular builtin */
        if (builtin_class == BUILTIN_REGULAR)
        {
            is_internal = true;

            builtin_func_t builtin_fn = builtin_get_function_cstr(cmd_name);
            if (builtin_fn != NULL)
            {
#if defined(POSIX_API)
                exec_status_t redir_st = exec_apply_redirections_posix(frame, runtime_redirs);
#elif defined(UCRT_API)
                exec_status_t redir_st = exec_apply_redirections_ucrt_c(frame, runtime_redirs);
#else
                exec_status_t redir_st = EXEC_OK; /* ISO C: no redirections */
#endif
                if (redir_st != EXEC_OK)
                {
                    status = redir_st;
                    goto done_execution;
                }

                cmd_exit_status = (*builtin_fn)(frame, expanded_words);

#if defined(POSIX_API)
                exec_restore_redirections_posix(frame);
#elif defined(UCRT_API)
                exec_restore_redirections_ucrt_c(frame);
#endif

                goto done_execution;
            }
        }

        /* Special builtin (non-assignment case) */
        if (builtin_class == BUILTIN_SPECIAL)
        {
            is_internal = true;

            builtin_func_t builtin_fn = builtin_get_function_cstr(cmd_name);
            if (builtin_fn != NULL)
            {
#if defined(POSIX_API)
                exec_status_t redir_st = exec_apply_redirections_posix(frame, runtime_redirs);
#elif defined(UCRT_API)
                exec_status_t redir_st = exec_apply_redirections_ucrt_c(frame, runtime_redirs);
#else
                exec_status_t redir_st = EXEC_OK; /* ISO C: no redirections */
#endif
                if (redir_st != EXEC_OK)
                {
                    status = redir_st;
                    goto done_execution;
                }

                cmd_exit_status = (*builtin_fn)(frame, expanded_words);

#if defined(POSIX_API)
                exec_restore_redirections_posix(frame);
#elif defined(UCRT_API)
                exec_restore_redirections_ucrt_c(frame);
#endif

                goto done_execution;
            }
            else
            {
                exec_set_error(executor, "%s: special builtin not implemented", cmd_name);
                cmd_exit_status = 1;
                goto done_execution;
            }
        }
    }

    /* External command execution */
    if (has_words)
    {
        const char *cmd_name = string_cstr(string_list_at(expanded_words, 0));

#ifdef POSIX_API
        if (!cmd_name || *cmd_name == '\0')
        {
            exec_set_error(executor, "empty command name");
            cmd_exit_status = 127;
            goto done_execution;
        }

        int argc = string_list_size(expanded_words);
        char **argv = xcalloc((size_t)argc + 1, sizeof(char *));
        for (int i = 0; i < argc; i++)
        {
            argv[i] = xstrdup(string_cstr(string_list_at(expanded_words, i)));
        }
        argv[argc] = NULL;

        char *const *envp = variable_store_get_envp(frame->variables);

        pid_t pid = fork();
        if (pid == -1)
        {
            exec_set_error(executor, "fork failed: %s", strerror(errno));
            cmd_exit_status = 127;
        }
        else if (pid == 0) /* child */
        {
            /* Apply redirections in child */
            exec_status_t redir_st = exec_apply_redirections_posix(frame, runtime_redirs);
            if (redir_st != EXEC_OK)
            {
                _exit(127);
            }

            /* Close CLOEXEC FDs */
            fd_table_t *fds = exec_frame_get_fds(frame);
            size_t cloexec_count = 0;
            int *cloexec_fds = fd_table_get_fds_with_flag(fds, FD_CLOEXEC, &cloexec_count);
            for (size_t j = 0; j < cloexec_count; j++)
            {
                close(cloexec_fds[j]);
            }
            xfree(cloexec_fds);

            /* Exec */
            execve(cmd_name, argv, envp);
#if defined(HAVE_EXECVPE)
            execvpe(cmd_name, argv, envp); /* fallback: PATH lookup with custom env */
#else
            /* Manual PATH search fallback if execvpe is not available */
            const char *path_env = NULL;
            for (char *const *ep = envp; ep && *ep; ++ep) {
                if (strncmp(*ep, "PATH=", 5) == 0) {
                    path_env = *ep + 5;
                    break;
                }
            }
            if (path_env && strchr(cmd_name, '/') == NULL) {
                char *path = xstrdup(path_env);
                char *saveptr = NULL;
                char *dir = strtok_r(path, ":", &saveptr);
                while (dir) {
                    char fullpath[PATH_MAX];
                    snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, cmd_name);
                    execve(fullpath, argv, envp);
                    dir = strtok_r(NULL, ":", &saveptr);
                }
                xfree(path);
            } else {
                execve(cmd_name, argv, envp);
            }
            perror(cmd_name);
            _exit(127);
#endif
        }
        else /* parent */
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

        char *const *envp = variable_store_get_envp(executor->variables);

        intptr_t spawn_result = 0;
        if (frame->policy && frame->policy->classification.is_background)
        {
            if (runtime_redirs && runtime_redirs->count > 0)
            {
                log_warn("Redirections for background commands are not supported on UCRT; ignoring "
                         "redirections.");
            }
            log_debug("Preparing to execute external background command: %s", cmd_name);
            for (int i = 0; i < string_list_size(expanded_words); i++)
            {
                log_debug("\targv%d: %s", i, argv[i]);
            }
            spawn_result = _spawnvpe(_P_NOWAIT, cmd_name, (const char *const *)argv,
                                     (const char *const *)envp);
        }
        else
        {
            if (runtime_redirs && runtime_redirs->count > 0)
            {
                exec_status_t redir_st = exec_apply_redirections_ucrt_c(frame, runtime_redirs);
                if (redir_st != EXEC_OK)
                {
                    for (int i = 0; argv[i]; i++)
                        xfree(argv[i]);
                    xfree(argv);
                    status = redir_st;
                    goto done_execution;
                }
            }
            log_debug("Preparing to execute external command: %s", cmd_name);
            for (int i = 0; i < string_list_size(expanded_words); i++)
            {
                log_debug("\targv%d: %s", i, argv[i]);
            }
            spawn_result =
                _spawnvpe(_P_WAIT, cmd_name, (const char *const *)argv, (const char *const *)envp);
            if (runtime_redirs && runtime_redirs->count > 0)
            {
                exec_restore_redirections_ucrt_c(frame);
            }
        }
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
        }
        else
        {
            if (frame->policy && frame->policy->classification.is_background)
            {
                int pid = GetProcessId((HANDLE)spawn_result);
                frame->last_bg_pid = pid;
                if (frame->executor->jobs)
                {
                    string_t *cmdline = string_create_from_cstr_list(argv, " ");
                    int job_id = job_store_add(frame->executor->jobs, cmdline, true);
                    if (job_id >= 0 && pid > 0)
                    {
                        job_store_add_process(frame->executor->jobs, job_id, pid,
                                              (unsigned long)spawn_result, cmdline);
                    }
                    string_destroy(&cmdline);
                }
            }
            cmd_exit_status = (int)spawn_result;
        }

        for (int i = 0; argv[i]; i++)
            xfree(argv[i]);
        xfree(argv);
        if (cmd_exit_status == 127 && !exec_get_error(executor))
        {
            exec_set_error(executor, "%s: command not found", cmd_name);
        }
    }
#else
        /* ISO C fallback using system() */
        if (!cmd_name || *cmd_name == '\0')
        {
            exec_set_error(executor, "empty command name");
            cmd_exit_status = 127;
            goto done_execution;
        }

        string_t *cmdline = string_create();
        for (int i = 0; i < string_list_size(expanded_words); i++)
        {
            if (i > 0)
                string_append_cstr(cmdline, " ");
            string_append(cmdline, string_list_at(expanded_words, i));
        }

        string_t *env_fname = variable_store_write_env_file(executor->variables);

        int rc = system(string_cstr(cmdline));

        if (env_fname)
        {
            remove(string_cstr(env_fname));
            string_destroy(&env_fname);
        }

        string_destroy(&cmdline);

        if (rc == -1)
        {
            exec_set_error(executor, "system() failed: %s", strerror(errno));
            cmd_exit_status = 127;
        }
        else
        {
            cmd_exit_status = rc;
        }

        if (cmd_exit_status == 127 && !exec_get_error(executor))
        {
            exec_set_error(executor, "%s: command not found", cmd_name);
        }
#endif

    done_execution:
        frame->last_exit_status = cmd_exit_status;

        /* Update $_ with last argument */
        if (string_list_size(expanded_words) > 1)
        {
            const string_t *last_arg =
                string_list_at(expanded_words, string_list_size(expanded_words) - 1);
            if (!executor->last_argument)
                executor->last_argument = string_create();
            string_set(executor->last_argument, last_arg);
            executor->last_argument_set = true;
        }

    out_cleanup_words:
        string_list_destroy(&expanded_words);

    out_destroy_redirs:
        exec_redirections_destroy(&runtime_redirs);

    out_restore_vars:
        /* Restore original variable store */
        variable_store_destroy(&frame->variables);
        frame->variables = frame->saved_variables;
        frame->saved_variables = NULL;

        return status;
    }

    /* ============================================================================
     * Function Definition
     * ============================================================================ */

    exec_status_t exec_execute_function_def(exec_t * executor, const ast_node_t *node)
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

    exec_status_t exec_execute_redirected_command(exec_frame_t * frame, const ast_node_t *node)
    {
        Expects_not_null(frame);
        Expects_not_null(node);
        Expects_eq(node->type, AST_REDIRECTED_COMMAND);

        exec_t *executor = frame->executor;

        const ast_node_t *inner = node->data.redirected_command.command;
        const ast_node_list_t *ast_redirs = node->data.redirected_command.redirections;

        /* Convert AST redirections to runtime structure */
    exec_redirections_t *runtime_redirs = exec_redirections_from_ast(frame, ast_redirs);
    if (!runtime_redirs) {
        return EXEC_ERROR;
    }

    int st = exec_frame_apply_redirections(frame, runtime_redirs);
    if (st != 0)
    {
        exec_redirections_destroy(&runtime_redirs);
        return EXEC_ERROR;
    }

        st = exec_execute(executor, inner);
        exec_restore_redirections(frame, runtime_redirs);
        exec_redirections_destroy(&runtime_redirs);
        return st;
    }

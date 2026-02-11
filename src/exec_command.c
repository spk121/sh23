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
#include <processthreadsapi.h>
#include <process.h>
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

        if (frame->opt_flags->allexport) flags[idx++] = 'a';
        if (frame->opt_flags->errexit) flags[idx++] = 'e';
        if (frame->opt_flags->noclobber) flags[idx++] = 'C';
        if (frame->opt_flags->noglob) flags[idx++] = 'f';
        if (frame->opt_flags->noexec) flags[idx++] = 'n';
        if (frame->opt_flags->nounset) flags[idx++] = 'u';
        if (frame->opt_flags->verbose) flags[idx++] = 'v';
        if (frame->opt_flags->xtrace) flags[idx++] = 'x';
        if (ex->is_interactive) flags[idx++] = 'i';

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
 *
 * This gets called when the executor is executing a special builtin command,
 * and when it's variable store is the shell's temp store for that command.
 * So we apply the assignments to both the temp store and the shell's main store.
 *
 */
static exec_status_t exec_apply_prefix_assignments(exec_frame_t *frame, variable_store_t *main_store, const ast_node_t *node)
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
        var_store_error_t err = variable_store_add(frame->variables, tok->assignment_name, value,
                                                    false, false);

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

    /* Assignment-only command */
    if (!has_words)
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
                    status = EXEC_ERROR;
                    goto out_base_exp;
                }

                var_store_error_t err =
                    variable_store_add(frame->variables, tok->assignment_name, value,
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

        frame->last_exit_status = 0;
        status = EXEC_OK;
        goto out_base_exp;
    }

    /* Build temporary variable store. The assignments at the beginning of a simple command
    * and the positional parameters. */
    variable_store_t *old_vars = frame->variables;
    variable_store_t *temp_vars =
        exec_build_temp_store_for_simple_command(frame, node);
    if (!temp_vars)
    {
        exec_set_error(executor, "failed to build temporary variable store");
        status = EXEC_ERROR;
        goto out_base_exp;
    }

#if 0
    /* Check to make sure that old_vars and temp_vars have the same contents, but, don't
     * share any pointers */
    if( variable_store_debug_verify_independent(old_vars, temp_vars) )
    {
        log_debug("After creation: Temporary variable store is independent from old store");
    }
#endif

    /* Since these varible changes shouldn't survive after the command execution, we'll swap in
     * the temporary variable store to use with the expansion step */
    frame->variables = temp_vars;
    temp_vars = NULL;

    /* Expand command words. */
    string_list_t *expanded_words = expand_words(frame, word_tokens);
    if (!expanded_words || string_list_size(expanded_words) == 0)
    {
        frame->last_exit_status = 0;
        if (expanded_words)
            string_list_destroy(&expanded_words);
        status = EXEC_OK;
        goto out_exp_temp;
    }

    const string_t *cmd_name_str = string_list_at(expanded_words, 0);
    int cmd_exit_status = 0;

    if (!cmd_name_str) {
        exec_set_error(executor, "empty command name");
        cmd_exit_status = 127;
        goto done_execution;
    }
    const char *cmd_name = string_cstr(cmd_name_str);

    /* Convert AST redirections to runtime structure */
    exec_redirections_t *runtime_redirs = NULL;
    if (redirs && ast_node_list_size(redirs) > 0)
    {
        runtime_redirs = exec_redirections_from_ast(frame, redirs);
        if (!runtime_redirs)
        {
            string_list_destroy(&expanded_words);
            status = EXEC_ERROR;
            goto out_exp_temp;
        }
    }

    /* Apply redirections */
    saved_fd_t *saved_fds = NULL;
    int saved_count = 0;

#ifdef POSIX_API
    status = exec_apply_redirections_posix(frame, runtime_redirs, &saved_fds, &saved_count);
    if (status != EXEC_OK)
    {
        string_list_destroy(&expanded_words);
        exec_redirections_destroy(&runtime_redirs);
        goto out_exp_temp;
    }
#elif defined(UCRT_API)
    if (runtime_redirs)
    {
        fflush(NULL);

        status = exec_apply_redirections_ucrt_c(frame, runtime_redirs, &saved_fds, &saved_count);
        if (status != EXEC_OK)
        {
            string_list_destroy(&expanded_words);
            exec_redirections_destroy(&runtime_redirs);
            goto out_exp_temp;
        }
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
    builtin_class_t builtin_class = builtin_classify_cstr(cmd_name);

    /* Check if this is a reserved word that shouldn't be executed as a command */
    if (token_is_reserved_word(cmd_name))
    {
        exec_set_error(executor, "%s: syntax error - reserved word in command position", cmd_name);
        string_list_destroy(&expanded_words);
        status = EXEC_ERROR;
        cmd_exit_status = 2;
        goto out_restore_redirs;
    }

    /* Special builtins: When calling special built-in, variable assignments survive the current shell. */
    if (builtin_class == BUILTIN_SPECIAL && assign_tokens && token_list_size(assign_tokens) > 0)
    {
        /* Right now, the frame is still using the temp variable store, but we need to
         * apply them to the permanent variable store when they happen in the context of
         * special built-ins. */
        exec_status_t assign_status = exec_apply_prefix_assignments(frame, old_vars, node);
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
            cmd_exit_status = (*builtin_fn)(frame, expanded_words);
            goto done_execution;
        }
        exec_set_error(executor, "%s: special builtin not implemented", cmd_name);
        cmd_exit_status = 1;
        goto done_execution;
    }

    /* Execute: shell functions */
    const ast_node_t *func_body = func_store_get_def_cstr(frame->functions, cmd_name);
    if (func_body != NULL)
    {
        /* func_store returns the function body, not the full AST_FUNCTION_DEF node */

        /* Get redirections associated with the function definition */
        string_t *func_name_str = string_create_from_cstr(cmd_name);
        const exec_redirections_t *func_redirs = func_store_get_redirections(frame->functions, func_name_str);
        string_destroy(&func_name_str);

        /* Execute function in a new function frame with argument scope isolation */
        exec_result_t func_result = exec_function(frame,
                                                   func_body,
                                                   expanded_words,
                                                   func_redirs);

        cmd_exit_status = func_result.exit_status;
        goto done_execution;
    }

    /* Execute: regular builtins */
    if (builtin_class == BUILTIN_REGULAR)
    {
        builtin_func_t builtin_fn = builtin_get_function_cstr(cmd_name);
        if (builtin_fn != NULL)
        {
            cmd_exit_status = (*builtin_fn)(frame, expanded_words);
            goto done_execution;
        }
    }

    /* Execute: external command */
    {
#ifdef POSIX_API
        if (!cmd_name || strlen(cmd_name) == 0)
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

        // We already copied current shell variables into the temp store, and
        // the executor is using that store for expansion, so we can use it here.
        char *const *envp = variable_store_get_envp(executor->variables);

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

        char *const *envp =
            variable_store_get_envp(executor->variables);

        /* Unlike POSIX, where the fork that created a background process happens way back at
         * the exec_in_frame() call, UCRT background process creation happens from _spawnvpe. In
         * UCRT we can only run simple commands in the background. */
        intptr_t spawn_result = 0;
        if (frame->policy->classification.is_background)
        {
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
            log_debug("Preparing to execute external command: %s", cmd_name);
            for (int i = 0; i < string_list_size(expanded_words); i++)
            {
                log_debug("\targv%d: %s", i, argv[i]);
            }
            spawn_result =
                _spawnvpe(_P_WAIT, cmd_name, (const char *const *)argv, (const char *const *)envp);
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
            if (frame->policy->classification.is_background)
            {
                /* In background execution, spawn_result is the Win32 process handle of the started process */
                int pid = GetProcessId((HANDLE)spawn_result);

                /* Store the PID. A PID of zero is an error, but, we'll still record it. */
                frame->last_bg_pid = pid;
                if (frame->executor->jobs)
                {
                    string_t *cmdline = string_create_from_cstr_list(argv, " ");
                    int job_id = job_store_add(frame->executor->jobs, cmdline, true);
                    if (job_id >= 0 && pid > 0)
                    {
                        job_store_add_process(frame->executor->jobs, job_id, pid, (unsigned long)spawn_result, cmdline);
                    }
                    string_destroy(&cmdline);
                }
            }
            cmd_exit_status = (int)spawn_result;
        }

        for (int i = 0; argv[i]; i++)
            xfree(argv[i]);
        xfree(argv);

#else
        if (!cmd_name || strlen(cmd_name) == 0)
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
            exec_set_error(executor, "system() failed");
            cmd_exit_status = 127;
        }
        else
        {
            cmd_exit_status = rc;
        }
#endif
    }
    if (cmd_exit_status == 127 && !exec_get_error(executor))
    {
        exec_set_error(executor, "%s: command not found", cmd_name);
    }

done_execution:
    frame->last_exit_status = cmd_exit_status;

#if 0
    /* Check to make sure that old_vars and temp_vars have the same contents, but, don't
     * share any pointers */
    if (variable_store_debug_verify_independent(old_vars, frame->variables))
    {
        log_debug("After execution: Temporary variable store is independent from old store");
    }
#endif


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
    /* Fallthrough */

out_restore_redirs:
#if defined(POSIX_API)
    if (runtime_redirs && saved_fds)
    {
        exec_restore_redirections_posix(saved_fds, saved_count);
        xfree(saved_fds);
    }
#elif defined(UCRT_API)
    if (runtime_redirs && saved_fds)
    {
        fflush(NULL);
        exec_restore_redirections_ucrt_c(saved_fds, saved_count);
        xfree(saved_fds);
    }
#endif

    exec_redirections_destroy(&runtime_redirs);

    if (status != EXEC_ERROR)
        status = EXEC_OK;

out_exp_temp:
    /* Restore the permanent variable store */
    variable_store_destroy(&frame->variables);
    frame->variables = old_vars;

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

exec_status_t exec_execute_redirected_command(exec_frame_t *frame, const ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects_eq(node->type, AST_REDIRECTED_COMMAND);

    exec_t *executor = frame->executor;

    const ast_node_t *inner = node->data.redirected_command.command;
    const ast_node_list_t *ast_redirs = node->data.redirected_command.redirections;

    /* Convert AST redirections to runtime structure */
    exec_redirections_t *runtime_redirs = exec_redirections_from_ast(frame, ast_redirs);

    saved_fd_t *saved_fds = NULL;
    int saved_count = 0;

    /* Apply redirections based on platform */
#ifdef POSIX_API
    exec_status_t st = exec_apply_redirections_posix(frame, runtime_redirs, &saved_fds, &saved_count);
    if (st != EXEC_OK)
    {
        exec_redirections_destroy(&runtime_redirs);
        return st;
    }
#elifdef UCRT_API
    exec_status_t st = exec_apply_redirections_ucrt_c(frame, runtime_redirs, &saved_fds, &saved_count);
    if (st != EXEC_OK)
    {
        exec_redirections_destroy(&runtime_redirs);
        return st;
    }
#else
    /* ISO_C: No redirections supported */
    if (runtime_redirs && runtime_redirs->count > 0)
    {
        exec_set_error(executor, "Redirections are not supported in ISO_C_API mode");
        exec_redirections_destroy(&runtime_redirs);
        return EXEC_ERROR;
    }
    exec_status_t st = EXEC_OK;
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

    exec_redirections_destroy(&runtime_redirs);
    return st;
}

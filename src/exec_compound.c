#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "ast.h"
#include "exec.h"
#include "exec_compound.h"
#include "exec_internal.h"
#include "job_store.h"
#include "logging.h"
#include "string_t.h"

#include <stdio.h>

#ifdef POSIX_API
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef UCRT_API
#include <errno.h>
#include <process.h>
#endif

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

#ifdef POSIX_API
static exec_status_t exec_execute_pipeline_posix(exec_t *executor, const ast_node_t *node);
static void exec_run_subshell_child(exec_t *executor, const ast_node_t *node);
static void exec_run_brace_group_child(exec_t *executor, const ast_node_t *node);
static void exec_run_function_def_child(exec_t *executor, const ast_node_t *node);
static void exec_run_simple_command_child(exec_t *executor, const ast_node_t *node);
static void exec_run_redirected_command_child(exec_t *executor, const ast_node_t *node);
#endif

static exec_status_t exec_run_background(exec_t *executor, ast_node_t *node);

/* ============================================================================
 * Background Job Execution
 * ============================================================================ */

#ifdef POSIX_API
/**
 * Execute an AST node in the background (POSIX implementation).
 */
static exec_status_t exec_run_background(exec_t *executor, ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);

    string_t *cmd_str = ast_node_to_string(node);
    if (!cmd_str)
        cmd_str = string_create_from_cstr("(unknown)");

    pid_t pid = fork();

    if (pid < 0)
    {
        exec_set_error(executor, "fork failed for background job");
        string_destroy(&cmd_str);
        return EXEC_ERROR;
    }

    if (pid == 0)
    {
        /* Child process - execute the command */
        exec_status_t status = exec_execute(executor, node);
        int exit_code = executor->last_exit_status;
        if (status == EXEC_ERROR)
            exit_code = 1;
        _exit(exit_code);
    }

    /* Parent process - create job entry */
    string_t *job_cmd = string_create_from(cmd_str);
    int job_id = job_store_add(executor->jobs, job_cmd, true);

    if (job_id < 0)
    {
        exec_set_error(executor, "failed to create job entry");
        string_destroy(&cmd_str);
        return EXEC_ERROR;
    }

    string_t *proc_cmd = cmd_str;
    job_store_add_process(executor->jobs, job_id, pid, proc_cmd);

    executor->last_background_pid = pid;
    executor->last_background_pid_set = true;

    if (executor->is_interactive)
    {
        fprintf(stderr, "[%d] %d\n", job_id, (int)pid);
    }

    exec_set_exit_status(executor, 0);
    return EXEC_OK;
}

#elifdef UCRT_API
/* UCRT background execution - extract the relevant code from exec.c */
static exec_status_t exec_run_background(exec_t *executor, ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);

    /* Try to background simple commands via _spawnvpe
     * For complex commands, fall back to synchronous execution with warning */
    
    if (node->type != AST_SIMPLE_COMMAND)
    {
        if (executor->is_interactive)
        {
            fprintf(stderr, "mgsh: warning: backgrounding complex commands not supported, "
                            "running synchronously\n");
        }
        return exec_execute(executor, node);
    }

    /* For simple commands, try _spawnvpe with _P_NOWAIT 
     * (Copy the exec_run_background_simple_command_ucrt logic from exec.c) */
    
    // TODO: Extract and implement UCRT simple command backgrounding
    // For now, fallback to synchronous
    if (executor->is_interactive)
    {
        fprintf(stderr, "mgsh: warning: background execution not fully implemented for UCRT\n");
    }
    return exec_execute(executor, node);
}

#else
/* ISO C - no background support */
static exec_status_t exec_run_background(exec_t *executor, ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);

    if (executor->is_interactive)
    {
        fprintf(stderr, "mgsh: background execution (&) not supported\n");
    }

    return exec_execute(executor, node);
}
#endif

/* ============================================================================
 * Background Job Reaping
 * ============================================================================ */

#ifdef POSIX_API
void exec_reap_background_jobs(exec_t *executor)
{
    if (!executor || !executor->jobs)
        return;

    int wstatus;
    pid_t pid;

    while ((pid = waitpid(-1, &wstatus, WNOHANG)) > 0)
    {
        job_state_t new_state;
        int exit_status = 0;

        if (WIFEXITED(wstatus))
        {
            new_state = JOB_DONE;
            exit_status = WEXITSTATUS(wstatus);
        }
        else if (WIFSIGNALED(wstatus))
        {
            new_state = JOB_TERMINATED;
            exit_status = WTERMSIG(wstatus);
        }
        else if (WIFSTOPPED(wstatus))
        {
            new_state = JOB_STOPPED;
            exit_status = WSTOPSIG(wstatus);
        }
        else
        {
            continue;
        }

        job_store_set_process_state(executor->jobs, pid, new_state, exit_status);

        for (job_t *job = executor->jobs->jobs; job; job = job->next)
        {
            for (process_t *proc = job->processes; proc; proc = proc->next)
            {
                if (proc->pid == pid)
                {
                    if (executor->is_interactive && !job->is_notified)
                    {
                        const char *state_str = "";
                        switch (job->state)
                        {
                        case JOB_DONE:
                            state_str = "Done";
                            break;
                        case JOB_STOPPED:
                            state_str = "Stopped";
                            break;
                        case JOB_TERMINATED:
                            state_str = "Terminated";
                            break;
                        default:
                            break;
                        }

                        if (job->state != JOB_RUNNING)
                        {
                            fprintf(stderr, "[%d]%c  %s\t\t%s\n", job->job_id,
                                    (job == executor->jobs->current_job)    ? '+'
                                    : (job == executor->jobs->previous_job) ? '-'
                                                                            : ' ',
                                    state_str,
                                    job->command_line ? string_cstr(job->command_line) : "");
                            job->is_notified = true;
                        }
                    }
                    goto next_pid;
                }
            }
        }
    next_pid:;
    }

    job_store_remove_completed(executor->jobs);
}

#elifdef UCRT_API
void exec_reap_background_jobs(exec_t *executor)
{
    if (!executor || !executor->jobs)
        return;

    /* UCRT implementation using _cwait - copy from exec.c */
    for (job_t *job = executor->jobs->jobs; job; job = job->next)
    {
        if (job->state == JOB_RUNNING)
        {
            for (process_t *proc = job->processes; proc; proc = proc->next)
            {
                if (proc->state == JOB_RUNNING)
                {
                    int term_status = 0;
                    intptr_t result = _cwait(&term_status, (intptr_t)proc->pid, _WAIT_CHILD);

                    if (result != -1)
                    {
                        proc->state = JOB_DONE;
                        proc->exit_status = term_status;

                        bool all_done = true;
                        for (process_t *p = job->processes; p; p = p->next)
                        {
                            if (p->state == JOB_RUNNING)
                            {
                                all_done = false;
                                break;
                            }
                        }

                        if (all_done)
                        {
                            job->state = JOB_DONE;

                            if (executor->is_interactive && !job->is_notified)
                            {
                                fprintf(stderr, "[%d]%c  Done\t\t%s\n", job->job_id,
                                        (job == executor->jobs->current_job) ? '+' : ' ',
                                        job->command_line ? string_cstr(job->command_line) : "");
                                job->is_notified = true;
                            }
                        }
                    }
                    else if (errno == ECHILD)
                    {
                        proc->state = JOB_DONE;
                        proc->exit_status = 0;
                    }
                }
            }
        }
    }

    job_store_remove_completed(executor->jobs);
}

#else
void exec_reap_background_jobs(exec_t *executor)
{
    (void)executor;
}
#endif

/* ============================================================================
 * Command List Execution
 * ============================================================================ */

exec_status_t exec_execute_command_list(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_COMMAND_LIST);

    exec_status_t status = EXEC_OK;

    if (node->data.command_list.items == NULL)
    {
        return EXEC_OK;
    }

    int item_count = node->data.command_list.items->size;

    for (int i = 0; i < item_count; i++)
    {
        ast_node_t *item = node->data.command_list.items->nodes[i];

        cmd_separator_t sep = CMD_EXEC_END;
        if (i < ast_node_command_list_separator_count(node))
        {
            sep = ast_node_command_list_get_separator(node, i);
        }

        if (sep == CMD_EXEC_BACKGROUND)
        {
            status = exec_run_background(executor, item);

            if (status == EXEC_OK_INTERNAL_FUNCTION_STORED)
            {
                ast_node_t *placeholder = ast_node_create_function_stored();
                node->data.command_list.items->nodes[i] = placeholder;
                status = EXEC_OK;
            }

            continue;
        }

        status = exec_execute(executor, item);

        if (status == EXEC_OK_INTERNAL_FUNCTION_STORED)
        {
            ast_node_t *placeholder = ast_node_create_function_stored();
            node->data.command_list.items->nodes[i] = placeholder;
            status = EXEC_OK;
        }

        if (status == EXEC_RETURN || status == EXEC_BREAK || status == EXEC_CONTINUE ||
            status == EXEC_EXIT)
        {
            return status;
        }
    }

    return status;
}

/* ============================================================================
 * AND/OR List Execution
 * ============================================================================ */

exec_status_t exec_execute_andor_list(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_AND_OR_LIST);

    exec_status_t status = exec_execute(executor, node->data.andor_list.left);
    if (status != EXEC_OK)
    {
        return status;
    }

    int left_exit = executor->last_exit_status;

    if (node->data.andor_list.op == ANDOR_OP_AND)
    {
        if (left_exit == 0)
        {
            status = exec_execute(executor, node->data.andor_list.right);
        }
    }
    else // ANDOR_OP_OR
    {
        if (left_exit != 0)
        {
            status = exec_execute(executor, node->data.andor_list.right);
        }
    }

    return status;
}

/* ============================================================================
 * Pipeline Execution
 * ============================================================================ */

exec_status_t exec_execute_pipeline(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_PIPELINE);

    const ast_node_list_t *cmds = node->data.pipeline.commands;
    bool is_negated = node->data.pipeline.is_negated;

    if (cmds == NULL || ast_node_list_size(cmds) == 0)
    {
        return EXEC_OK;
    }

    int n = ast_node_list_size(cmds);

    if (n == 1)
    {
        const ast_node_t *only = ast_node_list_get(cmds, 0);
        exec_status_t st = exec_execute(executor, only);

        if (st == EXEC_OK && is_negated)
        {
            int s = exec_get_exit_status(executor);
            exec_set_exit_status(executor, s == 0 ? 1 : 0);
        }

        return st;
    }

#ifdef POSIX_API
    return exec_execute_pipeline_posix(executor, node);
#elifdef UCRT_API
    exec_set_error(executor, "Pipelines are not yet supported in UCRT_API mode");
    return EXEC_NOT_IMPL;
#else
    exec_set_error(executor, "Pipelines are not supported in ISO_C_API mode");
    return EXEC_ERROR;
#endif
}

#ifdef POSIX_API
static exec_status_t exec_execute_pipeline_posix(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_PIPELINE);

    const ast_node_list_t *cmds = node->data.pipeline.commands;
    bool is_negated = node->data.pipeline.is_negated;
    int n = ast_node_list_size(cmds);

    if (n <= 0)
        return EXEC_OK;

    int num_pipes = n - 1;
    int(*pipes)[2] = NULL;

    if (num_pipes > 0)
    {
        pipes = xcalloc((size_t)num_pipes, sizeof(int[2]));
        for (int i = 0; i < num_pipes; i++)
        {
            if (pipe(pipes[i]) < 0)
            {
                exec_set_error(executor, "pipe() failed");
                for (int j = 0; j < i; j++)
                {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
                xfree(pipes);
                return EXEC_ERROR;
            }
        }
    }

    pid_t *pids = xcalloc((size_t)n, sizeof(pid_t));

    for (int i = 0; i < n; i++)
    {
        const ast_node_t *cmd = ast_node_list_get(cmds, i);

        pid_t pid = fork();
        if (pid < 0)
        {
            exec_set_error(executor, "fork() failed");

            for (int k = 0; k < num_pipes; k++)
            {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }
            xfree(pipes);
            xfree(pids);
            return EXEC_ERROR;
        }

        if (pid == 0)
        {
            /* Child process */

            if (i > 0)
            {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0)
                    _exit(127);
            }

            if (i < n - 1)
            {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0)
                    _exit(127);
            }

            for (int k = 0; k < num_pipes; k++)
            {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }

            switch (cmd->type)
            {
            case AST_SIMPLE_COMMAND:
                exec_run_simple_command_child(executor, cmd);
                break;
            case AST_REDIRECTED_COMMAND:
                exec_run_redirected_command_child(executor, cmd);
                break;
            case AST_SUBSHELL:
                exec_run_subshell_child(executor, cmd);
                break;
            case AST_BRACE_GROUP:
                exec_run_brace_group_child(executor, cmd);
                break;
            case AST_FUNCTION_DEF:
                exec_run_function_def_child(executor, cmd);
                break;
            default:
                _exit(127);
            }

            _exit(127);
        }

        pids[i] = pid;
    }

    for (int k = 0; k < num_pipes; k++)
    {
        close(pipes[k][0]);
        close(pipes[k][1]);
    }
    xfree(pipes);

    int last_status = 0;
    for (int i = 0; i < n; i++)
    {
        int status = 0;
        if (waitpid(pids[i], &status, 0) < 0)
        {
            continue;
        }

        if (i == n - 1)
        {
            int code = 0;
            if (WIFEXITED(status))
                code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                code = 128 + WTERMSIG(status);
            else
                code = 127;

            last_status = code;
        }
    }

    xfree(pids);

    if (is_negated)
        last_status = (last_status == 0) ? 1 : 0;

    exec_set_exit_status(executor, last_status);
    return EXEC_OK;
}

/* Pipeline child helper stubs - these need to be extracted from exec.c */
static void exec_run_simple_command_child(exec_t *executor, const ast_node_t *node)
{
    /* TODO: Extract from exec.c */
    (void)executor;
    (void)node;
    _exit(127);
}

static void exec_run_redirected_command_child(exec_t *executor, const ast_node_t *node)
{
    /* TODO: Extract from exec.c */
    (void)executor;
    (void)node;
    _exit(127);
}

static void exec_run_subshell_child(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_SUBSHELL);

    const ast_node_t *body = node->data.compound.body;

    exec_t *child = exec_create_subshell(executor);
    if (!child)
    {
        _exit(127);
    }

    exec_status_t st = exec_execute(child, body);

    int exit_code = 0;
    if (st == EXEC_OK)
        exit_code = child->last_exit_status;
    else if (st == EXEC_EXIT)
        exit_code = child->last_exit_status;
    else if (st == EXEC_RETURN)
        exit_code = child->last_exit_status;
    else if (st == EXEC_BREAK || st == EXEC_CONTINUE)
        exit_code = 1;
    else
        exit_code = 127;

    exec_destroy(&child);
    _exit(exit_code);
}

static void exec_run_brace_group_child(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_BRACE_GROUP);

    const ast_node_t *body = node->data.compound.body;

    exec_t *child = exec_create_subshell(executor);
    if (!child)
    {
        _exit(127);
    }

    exec_status_t st = exec_execute(child, body);

    int exit_code = 0;
    if (st == EXEC_OK)
        exit_code = child->last_exit_status;
    else
        exit_code = 127;

    exec_destroy(&child);
    _exit(exit_code);
}

static void exec_run_function_def_child(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_FUNCTION_DEF);

    var_store_error_t err = exec_execute_function_def(executor, node);
    if (err != VAR_STORE_ERROR_NONE)
    {
        _exit(127);
    }
    _exit(0);
}
#endif /* POSIX_API */

/* ============================================================================
 * Subshell Execution
 * ============================================================================ */

exec_status_t exec_execute_subshell(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_SUBSHELL);

    const ast_node_t *body = node->data.compound.body;

#ifdef POSIX_API
    pid_t pid = fork();
    if (pid < 0)
    {
        exec_set_error(executor, "fork() failed for subshell");
        return EXEC_ERROR;
    }

    if (pid == 0)
    {
        /* Child process */
        exec_t *child = exec_create_subshell(executor);
        if (!child)
        {
            _exit(127);
        }

        exec_status_t st = exec_execute(child, body);

        int exit_code = 0;
        if (st == EXEC_OK)
            exit_code = child->last_exit_status;
        else if (st == EXEC_EXIT)
            exit_code = child->last_exit_status;
        else if (st == EXEC_RETURN)
            exit_code = child->last_exit_status;
        else if (st == EXEC_BREAK || st == EXEC_CONTINUE)
            exit_code = 1;
        else
            exit_code = 127;

        exec_destroy(&child);
        _exit(exit_code);
    }

    /* Parent process */
    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
    {
        exec_set_error(executor, "waitpid() failed for subshell");
        return EXEC_ERROR;
    }

    int exit_code = 0;
    if (WIFEXITED(status))
        exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        exit_code = 128 + WTERMSIG(status);
    else
        exit_code = 127;

    exec_set_exit_status(executor, exit_code);
    return EXEC_OK;

#else
    /* UCRT/ISO C - emulated subshell */
    exec_t *child = exec_create_subshell(executor);
    if (!child)
    {
        exec_set_error(executor, "failed to create subshell executor");
        return EXEC_ERROR;
    }

    exec_status_t st = exec_execute(child, body);

    int exit_code = 0;
    if (st == EXEC_OK)
    {
        exit_code = child->last_exit_status;
    }
    else if (st == EXEC_EXIT)
    {
        exit_code = child->last_exit_status;
    }
    else if (st == EXEC_RETURN)
    {
        exit_code = child->last_exit_status;
    }
    else if (st == EXEC_BREAK || st == EXEC_CONTINUE)
    {
        fprintf(stderr, "mgsh: break/continue outside loop in subshell\n");
        exit_code = 1;
    }
    else
    {
        exit_code = 1;
    }

    exec_destroy(&child);
    exec_set_exit_status(executor, exit_code);
    return EXEC_OK;
#endif
}

/* ============================================================================
 * Brace Group Execution
 * ============================================================================ */

exec_status_t exec_execute_brace_group(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_BRACE_GROUP);

    const ast_node_t *body = node->data.compound.body;

    if (!body)
    {
        exec_set_exit_status(executor, 0);
        return EXEC_OK;
    }

    exec_status_t status = exec_execute(executor, body);
    return status;
}
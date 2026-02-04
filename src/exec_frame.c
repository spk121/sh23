/**
 * exec_frame.c - Execution frame management for mgsh
 *
 * This file implements frame creation (push), destruction (pop), and execution.
 * The behavior is driven by the policy table defined in exec_frame_policy.h.
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "exec_frame.h"

#include "alias_store.h"
#include "ast.h"
#include "exec.h"
#include "exec_frame_policy.h"
#include "exec_internal.h"
#include "fd_table.h"
#include "func_store.h"
#include "job_store.h"
#include "logging.h"
#include "positional_params.h"
#include "string_list.h"
#include "string_t.h"
#include "trap_store.h"
#include "variable_store.h"
#include "xalloc.h"
#ifdef UCRT_API
#include <direct.h>
#include <io.h>
#include <process.h>
#endif

/* ============================================================================
 * Helper Functions - System Queries
 * ============================================================================ */

static string_t *get_working_directory_from_system(void)
{
#ifdef POSIX_API
    char cwd_buffer[PATH_MAX];
    char *cwd = getcwd(cwd_buffer, sizeof(cwd_buffer));
    return cwd ? string_create_from_cstr(cwd) : string_create_from_cstr("/");
#elifdef UCRT_API
    char cwd_buffer[_MAX_PATH];
    char *cwd = _getcwd(cwd_buffer, sizeof(cwd_buffer));
    return cwd ? string_create_from_cstr(cwd) : string_create_from_cstr("C:\\");
#else
    // ISO C has no standard way to get cwd
    return string_create_from_cstr(".");
#endif
}

#ifdef POSIX_API
static mode_t get_umask_from_system(void)
#else
static int get_umask_from_system(void)
#endif
{
#ifdef POSIX_API
    mode_t current_mask = umask(0);
    umask(current_mask);
    return current_mask;
#elifdef UCRT_API
    int current_mask = _umask(0);
    _umask(current_mask);
    return current_mask;
#else
    return 0; // ISO C has no umask
#endif
}

/* ============================================================================
 * Frame Initialization - Policy-Driven Init Functions
 * ============================================================================ */
static exec_opt_flags_t *exec_opt_flags_create(void)
{
    exec_opt_flags_t *opts = xcalloc(1, sizeof(exec_opt_flags_t));
    return opts;
}

static exec_opt_flags_t *exec_opt_flags_clone(const exec_opt_flags_t *src)
{
    exec_opt_flags_t *opts = xcalloc(1, sizeof(exec_opt_flags_t));

    memmove(opts, src, sizeof(exec_opt_flags_t));
    return opts;
}

static void exec_opt_flags_destroy(exec_opt_flags_t **opts_ptr)
{
    if (!opts_ptr || !*opts_ptr)
        return;
    xfree(*opts_ptr);
    *opts_ptr = NULL;
}

/**
 * Generic scope-based initialization.
 * This macro generates the boilerplate for OWN/COPY/SHARE handling.
 */
#define INIT_BY_SCOPE(frame, field, scope, create_fn, clone_fn)                                    \
    do                                                                                             \
    {                                                                                              \
        switch (scope)                                                                             \
        {                                                                                          \
        case EXEC_SCOPE_OWN:                                                                       \
            (frame)->field = create_fn();                                                          \
            break;                                                                                 \
        case EXEC_SCOPE_COPY:                                                                      \
            Expects_not_null((frame)->parent);                                                     \
            (frame)->field = clone_fn((frame)->parent->field);                                     \
            break;                                                                                 \
        case EXEC_SCOPE_SHARE:                                                                     \
            Expects_not_null((frame)->parent);                                                     \
            (frame)->field = (frame)->parent->field;                                               \
            break;                                                                                 \
        default:                                                                                   \
            Expects(false && "Invalid scope");                                                     \
            break;                                                                                 \
        }                                                                                          \
    } while (0)

static void init_variables(exec_frame_t *frame, exec_t *exec)
{
    const exec_frame_policy_t *policy = frame->policy;

    switch (policy->variables.scope)
    {
    case EXEC_SCOPE_OWN:
        if (policy->variables.init_from_envp)
        {
            frame->variables = variable_store_create_from_envp(exec->envp);
        }
        else
        {
            frame->variables = variable_store_create();
        }
        break;
    case EXEC_SCOPE_COPY:
        Expects_not_null(frame->parent);
        if (policy->variables.copy_exports_only)
        {
            frame->variables = variable_store_clone_exported(frame->parent->variables);
        }
        else
        {
            frame->variables = variable_store_clone(frame->parent->variables);
        }
        break;
    case EXEC_SCOPE_SHARE:
        Expects_not_null(frame->parent);
        frame->variables = frame->parent->variables;
        break;
    default:
        Expects(false && "Invalid variable scope");
    }

    /* Local variable store for functions */
    if (policy->variables.has_locals)
    {
        frame->local_variables = variable_store_create();
    }
    else
    {
        frame->local_variables = NULL;
    }
}

static void init_positional_params(exec_frame_t *frame, exec_t *exec, exec_params_t *params)
{
    const exec_frame_policy_t *policy = frame->policy;

    switch (policy->positional.scope)
    {
    case EXEC_SCOPE_OWN:
        /* Initialize based on argn policy */
        switch (policy->positional.argn)
        {
        case EXEC_POSITIONAL_INIT_ARGV:
            frame->positional_params = positional_params_create_from_argv(
                string_cstr(exec->shell_name), exec->argc, (const char **)exec->argv);
            break;
        case EXEC_POSITIONAL_INIT_CALL_ARGS:
            if (params && params->arguments)
            {
                frame->positional_params = positional_params_create_from_string_list(
                    /* $0 inherited from parent */
                    positional_params_get_arg0(frame->parent->positional_params),
                    params->arguments);
            }
            else
            {
                frame->positional_params = positional_params_create();
            }
            break;
        default:
            frame->positional_params = positional_params_create();
            break;
        }
        break;

    case EXEC_SCOPE_COPY:
        Expects_not_null(frame->parent);
        frame->positional_params = positional_params_clone(frame->parent->positional_params);
        break;

    case EXEC_SCOPE_SHARE:
        Expects_not_null(frame->parent);
        frame->positional_params = frame->parent->positional_params;

        /* Handle temporary override for dot scripts */
        if (policy->positional.can_override && params && params->arguments)
        {
            frame->saved_positional_params = frame->positional_params;
            frame->positional_params = positional_params_create_from_string_list(
                positional_params_get_arg0(frame->parent->positional_params), params->arguments);
        }
        break;

    default:
        Expects(false && "Invalid positional params scope");
    }

    /* Handle $0 based on arg0 policy */
    switch (policy->positional.arg0)
    {
    case EXEC_ARG0_INIT_SHELL_OR_SCRIPT:
        /* Already handled in ARGV case above */
        break;
    case EXEC_ARG0_INHERIT:
        /* Already inherited via clone or share */
        break;
    case EXEC_ARG0_SET_TO_SOURCED_SCRIPT:
        if (params && params->script_path)
        {
            positional_params_set_arg0(frame->positional_params, params->script_path);
        }
        break;
    }
}

static void init_fds(exec_frame_t *frame)
{
    INIT_BY_SCOPE(frame, open_fds, frame->policy->fds.scope, fd_table_create, fd_table_clone);
}

static void init_traps(exec_frame_t *frame)
{
    const exec_frame_policy_t *policy = frame->policy;

    INIT_BY_SCOPE(frame, traps, policy->traps.scope, trap_store_create, trap_store_clone);

    /* Reset non-ignored traps for subshells */
    if (policy->traps.resets_non_ignored && policy->traps.scope == EXEC_SCOPE_COPY)
    {
        trap_store_reset_non_ignored(frame->traps);
    }
}

static void init_options(exec_frame_t *frame)
{
    INIT_BY_SCOPE(frame, opt_flags, frame->policy->options.scope, exec_opt_flags_create,
                  exec_opt_flags_clone);
}

static void init_cwd(exec_frame_t *frame)
{
    const exec_frame_policy_t *policy = frame->policy;

    switch (policy->cwd.scope)
    {
    case EXEC_SCOPE_OWN:
        if (policy->cwd.init_from_system)
        {
            frame->working_directory = get_working_directory_from_system();
        }
        else
        {
            frame->working_directory = string_create();
        }
        break;
    case EXEC_SCOPE_COPY:
        Expects_not_null(frame->parent);
        frame->working_directory = string_create_from(frame->parent->working_directory);
        break;
    case EXEC_SCOPE_SHARE:
        Expects_not_null(frame->parent);
        frame->working_directory = frame->parent->working_directory;
        break;
    default:
        Expects(false && "Invalid cwd scope");
    }
}

static void init_umask(exec_frame_t *frame)
{
    const exec_frame_policy_t *policy = frame->policy;

    switch (policy->umask.scope)
    {
    case EXEC_SCOPE_OWN:
#ifdef POSIX_API
        frame->umask = xmalloc(sizeof(mode_t));
#else
        frame->umask = xmalloc(sizeof(int));
#endif
        if (policy->umask.init_from_system)
        {
            *frame->umask = get_umask_from_system();
        }
        else if (policy->umask.init_to_0022)
        {
            *frame->umask = 0022;
        }
        else
        {
            *frame->umask = 0;
        }
        break;
    case EXEC_SCOPE_COPY:
        Expects_not_null(frame->parent);
#ifdef POSIX_API
        frame->umask = xmalloc(sizeof(mode_t));
#else
        frame->umask = xmalloc(sizeof(int));
#endif
        *frame->umask = *frame->parent->umask;
        break;
    case EXEC_SCOPE_SHARE:
        Expects_not_null(frame->parent);
        frame->umask = frame->parent->umask;
        break;
    default:
        Expects(false && "Invalid umask scope");
    }
}

static void init_functions(exec_frame_t *frame)
{
    INIT_BY_SCOPE(frame, functions, frame->policy->functions.scope, func_store_create,
                  func_store_clone);
}

static void init_aliases(exec_frame_t *frame)
{
    INIT_BY_SCOPE(frame, aliases, frame->policy->aliases.scope, alias_store_create,
                  alias_store_clone);
}

/* ============================================================================
 * Frame Push - Create and Initialize a New Frame
 * ============================================================================ */

exec_frame_t *exec_frame_push(exec_frame_t *parent, exec_frame_type_t type, exec_t *exec,
                              exec_params_t *params)
{
    exec_frame_t *frame = xcalloc(1, sizeof(exec_frame_t));

    frame->type = type;
    frame->policy = &EXEC_FRAME_POLICIES[type];
    frame->parent = parent;
    frame->executor = exec;

    /* Initialize all scope-dependent storage */
    init_variables(frame, exec);
    init_positional_params(frame, exec, params);
    init_fds(frame);
    init_traps(frame);
    init_options(frame);
    init_cwd(frame);
    init_umask(frame);
    init_functions(frame);
    init_aliases(frame);

    /* Initialize frame-local state */
    frame->loop_depth = parent ? parent->loop_depth : 0;
    if (frame->policy->flow.is_loop)
    {
        frame->loop_depth++;
    }

    frame->last_exit_status = parent ? parent->last_exit_status : 0;
    frame->last_bg_pid = parent ? parent->last_bg_pid : 0;

    /* Control flow state */
    frame->pending_control_flow = EXEC_FLOW_NORMAL;
    frame->pending_flow_depth = 0;

    /* Source tracking */
    if (frame->policy->source.tracks_location)
    {
        if (params && params->script_path)
        {
            frame->source_name = string_create_from(params->script_path);
        }
        else if (parent && parent->source_name)
        {
            frame->source_name = string_create_from(parent->source_name);
        }
        else
        {
            frame->source_name = string_create();
        }
        frame->source_line = params ? params->source_line : 0;
    }
    else
    {
        frame->source_name = NULL;
        frame->source_line = 0;
    }

    frame->in_trap_handler = (type == EXEC_FRAME_TRAP);

    frame->executor->current_frame = frame;
    if (!parent)
    {
        frame->executor->top_frame = frame;
        frame->executor->top_frame_initialized = true;
    }

    return frame;
}

exec_frame_t *exec_frame_create_top_level(exec_t *exec)
{
    Expects_not_null(exec);

    exec_frame_t *frame = exec_frame_push(NULL, EXEC_FRAME_TOP_LEVEL, exec, NULL);

    /* Transfer any pre-initialized top-frame state from exec_t */
    if (exec->variables)
    {
        variable_store_destroy(&frame->variables);
        frame->variables = exec->variables;
        exec->variables = NULL;
    }

    if (exec->positional_params)
    {
        positional_params_destroy(&frame->positional_params);
        frame->positional_params = exec->positional_params;
        exec->positional_params = NULL;
    }

    if (exec->functions)
    {
        func_store_destroy(&frame->functions);
        frame->functions = exec->functions;
        exec->functions = NULL;
    }

    if (exec->aliases)
    {
        alias_store_destroy(&frame->aliases);
        frame->aliases = exec->aliases;
        exec->aliases = NULL;
    }

    if (exec->traps)
    {
        trap_store_destroy(&frame->traps);
        frame->traps = exec->traps;
        exec->traps = NULL;
    }

#if defined(POSIX_API) || defined(UCRT_API)
    if (exec->open_fds)
    {
        fd_table_destroy(&frame->open_fds);
        frame->open_fds = exec->open_fds;
        exec->open_fds = NULL;
    }
#endif

    if (exec->working_directory)
    {
        if (frame->working_directory)
        {
            string_destroy(&frame->working_directory);
        }
        frame->working_directory = exec->working_directory;
        exec->working_directory = NULL;
    }

    *frame->opt_flags = exec->opt;

    if (frame->umask)
    {
        *frame->umask = exec->umask;
    }

    frame->last_exit_status = exec->last_exit_status;
    frame->last_bg_pid = exec->last_background_pid;

    frame->executor->top_frame = frame;
    frame->executor->current_frame = frame;
    frame->executor->top_frame_initialized = true;

    return frame;
}

/* ============================================================================
 * Frame Pop - Cleanup and Destroy a Frame
 * ============================================================================ */

/**
 * Cleanup helper - only frees resources this frame owns.
 */
static void cleanup_frame_resources(exec_frame_t *frame)
{
    const exec_frame_policy_t *policy = frame->policy;

    /* Variables */
    if (policy->variables.scope != EXEC_SCOPE_SHARE && frame->variables)
    {
        variable_store_destroy(&frame->variables);
    }
    if (frame->local_variables)
    {
        variable_store_destroy(&frame->local_variables);
    }

    /* Positional params */
    if (policy->positional.scope != EXEC_SCOPE_SHARE && frame->positional_params)
    {
        positional_params_destroy(&frame->positional_params);
    }
    /* Restore saved positional params (dot script override case) */
    if (frame->saved_positional_params)
    {
        /* The overridden params were owned by this frame, destroy them */
        if (frame->positional_params != frame->saved_positional_params)
        {
            positional_params_destroy(&frame->positional_params);
        }
        /* Note: saved_positional_params points to parent's, don't destroy */
        frame->saved_positional_params = NULL;
    }

    /* File descriptors */
    if (policy->fds.scope != EXEC_SCOPE_SHARE && frame->open_fds)
    {
        fd_table_destroy(&frame->open_fds);
    }

    /* Traps */
    if (policy->traps.scope != EXEC_SCOPE_SHARE && frame->traps)
    {
        trap_store_destroy(&frame->traps);
    }

    /* Options */
    if (policy->options.scope != EXEC_SCOPE_SHARE && frame->opt_flags)
    {
        exec_opt_flags_destroy(&frame->opt_flags);
    }

    /* Working directory */
    if (policy->cwd.scope != EXEC_SCOPE_SHARE && frame->working_directory)
    {
        string_destroy(&frame->working_directory);
    }

    /* umask */
    if (policy->umask.scope != EXEC_SCOPE_SHARE && frame->umask)
    {
        xfree(frame->umask);
        frame->umask = NULL;
    }

    /* Functions */
    if (policy->functions.scope != EXEC_SCOPE_SHARE && frame->functions)
    {
        func_store_destroy(&frame->functions);
    }

    /* Aliases */
    if (policy->aliases.scope != EXEC_SCOPE_SHARE && frame->aliases)
    {
        alias_store_destroy(&frame->aliases);
    }

    /* Source name */
    if (frame->source_name)
    {
        string_destroy(&frame->source_name);
    }
}

exec_frame_t *exec_frame_pop(exec_frame_t **frame_ptr)
{
    Expects_not_null(frame_ptr);
    exec_frame_t *frame = *frame_ptr;
    Expects_not_null(frame);

    exec_frame_t *parent = frame->parent;
    if (parent)
    {
        parent->executor->current_frame = parent;
    }
    else
    {
        /* No parent: we're popping the top-level frame */
        frame->executor->current_frame = NULL;
        frame->executor->top_frame = NULL;
        frame->executor->top_frame_initialized = false;
    }

    const exec_frame_policy_t *policy = frame->policy;

    /* Run EXIT trap if applicable */
    if (policy->traps.exit_trap_runs)
    {
        if (frame->traps->exit_trap_set)
            trap_store_run_exit_trap(frame->traps, frame);
    }

    /* Propagate exit status to parent if applicable */
    if (policy->exit.affects_parent_status && parent)
    {
        parent->last_exit_status = frame->last_exit_status;
    }

    /* Cleanup resources */
    cleanup_frame_resources(frame);

    /* Free the frame itself */
    xfree(frame);
    *frame_ptr = NULL;

    return parent;
}

/* ============================================================================
 * Frame Execution - The Main Entry Point
 * ============================================================================ */

/**
 * Handle process group setup based on policy.
 */
static void setup_process_group(exec_frame_t *frame, exec_params_t *params)
{
#ifdef POSIX_API
    switch (frame->policy->process.pgroup)
    {
    case EXEC_PGROUP_NONE:
        /* No process group manipulation */
        break;
    case EXEC_PGROUP_START:
        /* Create new process group with this process as leader */
        setpgid(0, 0);
        break;
    case EXEC_PGROUP_PIPELINE:
        /* Pipeline semantics: first starts, others join */
        if (params && params->pipeline_pgid > 0)
        {
            /* Join existing pipeline group */
            setpgid(0, params->pipeline_pgid);
        }
        else
        {
            /* First in pipeline: create new group */
            setpgid(0, 0);
        }
        break;
    }
#endif
}

/**
 * Handle control flow based on policy.
 */
static exec_result_t handle_control_flow(exec_frame_t *frame, exec_result_t result)
{
    const exec_frame_policy_t *policy = frame->policy;

    switch (result.flow)
    {
    case EXEC_FLOW_NORMAL:
        /* Nothing special to do */
        return result;

    case EXEC_FLOW_RETURN:
        switch (policy->flow.return_behavior)
        {
        case EXEC_RETURN_TARGET:
            /* We're the target; return completes here */
            return (exec_result_t){.exit_status = result.exit_status,
                                   .has_exit_status = true,
                                   .flow = EXEC_FLOW_NORMAL,
                                   .flow_depth = 0};
        case EXEC_RETURN_TRANSPARENT:
            /* Pass it up to parent */
            return result;
        case EXEC_RETURN_DISALLOWED:
            /* Error: shouldn't happen if AST validation is correct */
            log_error("return: not valid in this context");
            return (exec_result_t){.exit_status = 1,
                                   .has_exit_status = true,
                                   .flow = EXEC_FLOW_NORMAL,
                                   .flow_depth = 0};
        }
        break;

    case EXEC_FLOW_BREAK:
    case EXEC_FLOW_CONTINUE:
        switch (policy->flow.loop_control)
        {
        case EXEC_LOOP_TARGET:
            /* We're a loop; handle break/continue */
            if (result.flow_depth <= 1)
            {
                /* This loop is the target */
                if (result.flow == EXEC_FLOW_BREAK)
                {
                    /* Break: exit loop with current status */
                    return (exec_result_t){.exit_status = result.exit_status,
                                           .has_exit_status = result.has_exit_status,
                                           .flow = EXEC_FLOW_NORMAL,
                                           .flow_depth = 0};
                }
                else
                {
                    /* Continue: signal to continue loop iteration */
                    return (exec_result_t){
                        .exit_status = 0,
                        .has_exit_status = true,
                        .flow = EXEC_FLOW_CONTINUE,
                        .flow_depth = 0 /* Consumed by this loop */
                    };
                }
            }
            else
            {
                /* Decrement depth and propagate */
                return (exec_result_t){.exit_status = result.exit_status,
                                       .has_exit_status = result.has_exit_status,
                                       .flow = result.flow,
                                       .flow_depth = result.flow_depth - 1};
            }
        case EXEC_LOOP_TRANSPARENT:
            /* Pass through unchanged */
            return result;
        case EXEC_LOOP_DISALLOWED:
            /* Error: can't break/continue from here */
            log_error("break/continue: not valid in this context");
            return (exec_result_t){.exit_status = 1,
                                   .has_exit_status = true,
                                   .flow = EXEC_FLOW_NORMAL,
                                   .flow_depth = 0};
        }
        break;
    }

    return result;
}

/**
 * Execute the frame's body based on params.
 */
static exec_result_t execute_frame_body(exec_frame_t *frame, exec_params_t *params)
{
    exec_result_t result = {
        .exit_status = 0, .has_exit_status = true, .flow = EXEC_FLOW_NORMAL, .flow_depth = 0};

    if (!params)
    {
        return result;
    }

    /* Apply redirections */
    if (params->redirections)
    {
        int redir_result = exec_frame_apply_redirections(frame, params->redirections);
        if (redir_result != 0)
        {
            result.exit_status = 1;
            return result;
        }
    }

    /* Handle pipe FD setup for EXEC_FRAME_PIPELINE_CMD */
#ifdef POSIX_API
    if (params->stdin_pipe_fd >= 0)
    {
        dup2(params->stdin_pipe_fd, STDIN_FILENO);
        close(params->stdin_pipe_fd);
    }
    if (params->stdout_pipe_fd >= 0)
    {
        dup2(params->stdout_pipe_fd, STDOUT_FILENO);
        close(params->stdout_pipe_fd);
    }
    /* Close all other pipe FDs in this child process */
    if (params->pipe_fds_to_close)
    {
        for (int i = 0; i < params->pipe_fds_count; i++)
        {
            close(params->pipe_fds_to_close[i]);
        }
    }
#endif

    /* Execute body based on what's provided */
    if (params->body)
    {
        switch (params->body->type)
        {
        case AST_COMMAND_LIST:
            result = exec_compound_list(frame, params->body);
            break;
        case AST_SIMPLE_COMMAND:
            result = exec_simple_command(frame, params->body);
            break;
        case AST_BRACE_GROUP:
            result = exec_brace_group(frame, params->body->data.compound.body, NULL);
            break;
        case AST_SUBSHELL:
            result = exec_subshell(frame, params->body->data.compound.body);
            break;
        case AST_IF_CLAUSE:
            result = exec_if_clause(frame, params->body);
            break;
        case AST_WHILE_CLAUSE:
        case AST_UNTIL_CLAUSE:
            result = exec_while_clause(frame, params->body);
            break;
        case AST_FOR_CLAUSE:
            result = exec_for_clause(frame, params->body);
            break;
        case AST_CASE_CLAUSE:
            result = exec_case_clause(frame, params->body);
            break;
        case AST_PIPELINE:
            result = exec_pipeline(frame, params->body);
            break;
        case AST_AND_OR_LIST:
            result = exec_and_or_list(frame, params->body);
            break;
        default:
            log_error("execute_frame_body: unsupported body type %d (%s)", 
                     params->body->type, ast_node_type_to_string(params->body->type));
            result.exit_status = 1;
            result.has_exit_status = true;
            result.flow = EXEC_FLOW_NORMAL;
            result.flow_depth = 0;
            break;
        }
    }
    else if (params->condition)
    {
        /* While/until loop */
        result = exec_condition_loop(frame, params);
    }
    else if (params->iteration_words)
    {
        /* For loop */
        result = exec_iteration_loop(frame, params);
    }
    else if (params->pipeline_commands)
    {
        /* Pipeline orchestration */
        result = exec_pipeline_orchestrate(frame, params);
    }

    /* Handle control flow */
    result = handle_control_flow(frame, result);

    /* Restore redirections */
    if (params->redirections)
    {
        exec_restore_redirections(frame, params->redirections);
    }

    return result;
}

#ifdef UCRT_API

// Returns number of arguments parsed, or -1 on error
// Allocates *argvp with malloc(); caller must free(*argvp) and each (*argvp)[i]
int split_command_line(const char *cmdline, char ***argvp)
{
    if (!cmdline || !argvp)
        return -1;

    int argc = 0;
    char **argv = NULL;
    const char *p = cmdline;

    while (*p)
    {
        while (isspace((unsigned char)*p))
            p++; // skip leading whitespace

        if (!*p)
            break;

        char *arg = xmalloc(strlen(p) + 1); // worst-case
        if (!arg)
            goto fail;
        char *q = arg;

        int in_quote = 0;
        int backslash_count = 0;

        while (*p)
        {
            if (*p == '\\')
            {
                backslash_count++;
                p++;
                continue;
            }

            if (*p == '"')
            {
                // Even backslashes → literal ", odd → escape "
                if (backslash_count % 2 == 1)
                {
                    backslash_count--; // consume the escaping backslash
                    *q++ = '"';
                }
                else
                {
                    in_quote = !in_quote;
                }
                backslash_count = 0;
                p++;
                continue;
            }

            if (!in_quote && isspace((unsigned char)*p))
            {
                break; // end of arg
            }

            // Dump accumulated backslashes
            while (backslash_count--)
                *q++ = '\\';
            *q++ = *p++;
            backslash_count = 0;
        }

        // Trailing backslashes outside quotes are literal
        while (backslash_count--)
            *q++ = '\\';

        *q = '\0';

        // realloc logic omitted for brevity — use a dynamic array in real code
        char **new_argv = xrealloc(argv, (argc + 2) * sizeof(char *));
        if (!new_argv)
        {
            xfree(arg);
            goto fail;
        }
        argv = new_argv;
        argv[argc++] = arg;
        argv[argc] = NULL;
    }

    *argvp = argv;
    return argc;

fail:
    // cleanup omitted for brevity
    return -1;
}

#endif


/**
 * Main entry point for frame execution.
 */
exec_result_t exec_in_frame(exec_frame_t *parent, exec_frame_type_t type, exec_params_t *params)
{
    Expects_not_null(parent);
    exec_t *exec = parent->executor;
    const exec_frame_policy_t *policy = &EXEC_FRAME_POLICIES[type];
    exec_result_t result = {0};

    /* Handle forking if required */
    if (policy->process.forks)
    {
#ifdef POSIX_API
        pid_t pid = fork();
        if (pid < 0)
        {
            /* Fork failed */
            return (exec_result_t){.exit_status = 1,
                                   .has_exit_status = true,
                                   .flow = EXEC_FLOW_NORMAL,
                                   .flow_depth = 0};
        }
        else if (pid > 0)
        {
            /* Parent process */
            if (policy->classification.is_background)
            {
                /* Background job: record and return immediately */
                parent->last_bg_pid = pid;
                string_t *cmdline =
                    params ? string_list_join(params->command_args) : string_create_from_cstr("");
                job_store_add(exec->jobs, pid, cmdline);
                string_destroy(&cmdline);
                return (exec_result_t){.exit_status = 0,
                                       .has_exit_status = true,
                                       .flow = EXEC_FLOW_NORMAL,
                                       .flow_depth = 0};
            }
            else
            {
                /* Foreground: wait for child */
                int status;
                waitpid(pid, &status, 0);
                int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
                return (exec_result_t){.exit_status = exit_status,
                                       .has_exit_status = true,
                                       .flow = EXEC_FLOW_NORMAL,
                                       .flow_depth = 0};
            }
        }
        /* Child process continues below */
#elifdef UCRT_API
        /* While POSIX can fork and get a PID before executing a command,
         * in UCRT, we can't get a handle until when the command is spawned.
         * In POSIX, because of fork, you handle both background and foreground
         * here based on PID, but, in UCRT, this block is all forground execution, and the
         * background task is handled in the spawn call in exec_simple_command.
         * 
         * So, basically, we do nothing here. But when we create and initialize the frame,
         * below, it will have the background policy flag set, which will be used later.
         */

        /* Parent process continues. */
#else
        /* ISO C: no fork support, run in foreground with warning */
        if (policy->classification.is_background)
        {
            fprintf(stderr, "Warning: background execution not supported, running in foreground\n");
        }
        /* Continue without forking */
#endif
    }

    /* Create and initialize the frame */
    exec_frame_t *frame = exec_frame_push(parent, type, exec, params);

    /* Setup process group */
    setup_process_group(frame, params);

    /* Execute the frame body */
    result = execute_frame_body(frame, params);

    /* Store exit status in frame before cleanup */
    frame->last_exit_status = result.exit_status;

    /* Pop the frame (runs EXIT trap, cleans up resources) */
    exec_frame_pop(&frame);

    /* Handle process termination for forked processes */
    if (policy->exit.terminates_process)
    {
#ifdef POSIX_API
        _exit(result.exit_status);
#else
        /* For ISO C mode, just return */
#endif
    }

    return result;
}

/* ============================================================================
 * Convenience Wrappers
 * ============================================================================ */

exec_result_t exec_subshell(exec_frame_t *parent, ast_node_t *body)
{
    exec_params_t params = {
        .body = body,
    };
    return exec_in_frame(parent, EXEC_FRAME_SUBSHELL, &params);
}

exec_result_t exec_brace_group(exec_frame_t *parent, ast_node_t *body,
                               exec_redirections_t *redirections)
{
    exec_params_t params = {
        .body = body,
        .redirections = redirections,
    };
    return exec_in_frame(parent, EXEC_FRAME_BRACE_GROUP, &params);
}

exec_result_t exec_function(exec_frame_t *parent, ast_node_t *body, string_list_t *arguments,
                            exec_redirections_t *redirections)
{
    exec_params_t params = {
        .body = body,
        .arguments = arguments,
        .redirections = redirections,
    };
    return exec_in_frame(parent, EXEC_FRAME_FUNCTION, &params);
}

exec_result_t exec_for_loop(exec_frame_t *parent, string_t *var_name,
                            string_list_t *words, ast_node_t *body)
{
    exec_params_t params = {
        .body = body,
        .loop_var_name = var_name,
        .iteration_words = words,
    };
    return exec_in_frame(parent, EXEC_FRAME_LOOP, &params);
}

exec_result_t exec_while_loop(exec_frame_t *parent, ast_node_t *condition,
                              ast_node_t *body, bool until_mode)
{
    exec_params_t params = {
        .body = body,
        .condition = condition,
        .until_mode = until_mode,
    };
    return exec_in_frame(parent, EXEC_FRAME_LOOP, &params);
}

exec_result_t exec_dot_script(exec_frame_t *parent, string_t *script_path,
                              ast_node_t *body, string_list_t *arguments)
{
    exec_params_t params = {
        .body = body,
        .script_path = script_path,
        .arguments = arguments,
    };
    return exec_in_frame(parent, EXEC_FRAME_DOT_SCRIPT, &params);
}

exec_result_t exec_trap_handler(exec_frame_t *parent, ast_node_t *body)
{
    exec_params_t params = {
        .body = body,
    };
    return exec_in_frame(parent, EXEC_FRAME_TRAP, &params);
}

exec_result_t exec_background_job(exec_frame_t *parent, ast_node_t *body,
                                  string_list_t *command_args)
{
    exec_params_t params = {
        .body = body,
        .command_args = command_args,
    };
    return exec_in_frame(parent, EXEC_FRAME_BACKGROUND_JOB, &params);
}

exec_result_t exec_pipeline_group(exec_frame_t *parent, ast_node_list_t *commands, bool negated)
{
    exec_params_t params = {
        .pipeline_commands = commands,
        .pipeline_negated = negated,
    };
    return exec_in_frame(parent, EXEC_FRAME_PIPELINE, &params);
}

#ifdef POSIX_API
exec_result_t exec_pipeline_cmd(exec_frame_t *parent, ast_node_t *body,
                                pid_t pipeline_pgid)
#else
exec_result_t exec_pipeline_cmd(exec_frame_t *parent, ast_node_t *body,
                                int pipeline_pgid)
#endif
{
    exec_params_t params = {
        .body = body,
        .pipeline_pgid = pipeline_pgid,
    };
    return exec_in_frame(parent, EXEC_FRAME_PIPELINE_CMD, &params);
}

exec_result_t exec_eval(exec_frame_t *parent, ast_node_t *body)
{
    exec_params_t params = {
        .body = body,
    };
    return exec_in_frame(parent, EXEC_FRAME_EVAL, &params);
}

/* ============================================================================
 * Variable and File Descriptor Access
 * ============================================================================ */

exec_frame_t *exec_frame_find_return_target(exec_frame_t *frame)
{
    Expects_not_null(frame);

    exec_frame_t *current = frame;
    while (current)
    {
        if (current->policy->flow.return_behavior == EXEC_RETURN_TARGET)
        {
            return current;
        }
        current = current->parent;
    }
    return NULL;
}

exec_frame_t* exec_frame_find_loop(exec_frame_t* frame)
{
    Expects_not_null(frame);
    exec_frame_t *current = frame;
    while (current)
    {
        if (current->policy->flow.is_loop)
        {
            return current;
        }
        current = current->parent;
    }
    return NULL;
}

variable_store_t* exec_frame_get_variables(exec_frame_t* frame)
{
    if (!frame)
        return NULL;

    return frame->variables;
}

fd_table_t* exec_frame_get_fds(exec_frame_t* frame)
{
    if (!frame)
        return NULL;

    return frame->open_fds;
}

trap_store_t *exec_frame_get_traps(exec_frame_t *frame)
{
    Expects_not_null(frame);
    return frame->traps;
}

string_t* exec_frame_get_variable(exec_frame_t* frame, const string_t* name)
{
    if (!frame || !name)
        return NULL;

    /* Check local store first if present */
    if (frame->local_variables)
    {
        const string_t *local_value = variable_store_get_value(frame->local_variables, name);
        if (local_value)
            return (string_t *)local_value;
    }

    /* Fall back to global store */
    if (frame->variables)
    {
        const string_t *global_value = variable_store_get_value(frame->variables, name);
        return (string_t *)global_value;
    }

    return NULL;
}

void exec_frame_set_variable(exec_frame_t* frame, const string_t* name,
    const string_t* value)
{
    if (!frame || !name || !value)
        return;

    /* Set in local store if present, otherwise in global store */
    if (frame->local_variables)
    {
        variable_store_add(frame->local_variables, name, value, false, false);
    }
    else if (frame->variables)
    {
        variable_store_add(frame->variables, name, value, false, false);
    }
}

int exec_frame_declare_local(exec_frame_t* frame, const string_t* name,
    const string_t* value)
{
    if (!frame || !name || !value)
        return -1;

    if (!frame->local_variables)
        return -1;

    var_store_error_t result = variable_store_add(frame->local_variables, name, value, false, false);
    return (result == VAR_STORE_ERROR_NONE) ? 0 : -1;
}

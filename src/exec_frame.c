/**
 * exec_frame.c - Execution frame management for miga
 *
 * This file implements frame creation (push), destruction (pop), and execution.
 * The behavior is driven by the policy table defined in exec_frame_policy.h.
 *
 * The exec_frame.c module provides functions that may be used by any other part
 * of the executor.  The frame.c module, however, is intended as a thin wrapper
 * around frame-level operations that are exposed to builtins and library consumers.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef MIGA_POSIX_API
#define _GNU_SOURCE
#endif 
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifdef MIGA_POSIX_API
#include <errno.h>
#include <fnmatch.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef MIGA_UCRT_API
#include <direct.h>
#include <io.h>
#include <stdlib.h>
#endif

#include "exec_frame.h"

#include "alias_store.h"
#include "ast.h"
#include "miga/exec.h"
#include "miga/frame.h"
#include "exec_command.h"
#include "exec_frame_expander.h"
#include "exec_frame_policy.h"
#include "exec_redirect.h"
#include "exec_types_internal.h"
#include "miga/type_pub.h"
#include "fd_table.h"
#include "func_store.h"
#include "glob_util.h"
#include "job_store.h"
#include "lexer.h"
#include "lib.h"
#include "logging.h"
#include "lower.h"
#include "gnode.h"
#include "parse_session.h"
#include "parser.h"
#include "token.h"
#include "tokenizer.h"
#include "positional_params.h"
#include "miga/strlist.h"
#include "miga/string_t.h"
#include "trap_store.h"
#include "variable_store.h"
#include "miga/xalloc.h"

exec_frame_execute_result_t exec_frame_execute_pipeline_orchestrate(miga_frame_t *frame,
                                                                    exec_params_t *params);
static exec_frame_execute_result_t exec_frame_execute_condition_loop(miga_frame_t *frame,
                                                                     exec_params_t *params);
exec_frame_execute_result_t exec_frame_execute_iteration_loop(miga_frame_t *frame,
                                                              exec_params_t *params);
exec_frame_execute_result_t exec_frame_execute_compound_list(miga_frame_t *frame,
                                                             const ast_node_t *list);
exec_frame_execute_result_t exec_frame_execute_and_or_list(miga_frame_t *frame, ast_node_t *list);
exec_frame_execute_result_t exec_frame_execute_pipeline(miga_frame_t *frame, ast_node_t *list);
exec_frame_execute_result_t exec_frame_execute_simple_command(miga_frame_t *frame,
                                                              ast_node_t *node);

exec_frame_execute_result_t exec_frame_execute_subshell(miga_frame_t *frame, ast_node_t *node);
exec_frame_execute_result_t exec_frame_execute_brace_group(miga_frame_t *frame, ast_node_t *node,
                                                           exec_redirections_t *redirs);
exec_frame_execute_result_t exec_frame_execute_if_clause(miga_frame_t *frame, ast_node_t *node);
exec_frame_execute_result_t exec_frame_execute_while_clause(miga_frame_t *frame, ast_node_t *node);
exec_frame_execute_result_t exec_frame_execute_for_clause(miga_frame_t *frame, ast_node_t *node);
exec_frame_execute_result_t exec_frame_execute_redirected_command(miga_frame_t *frame,
                                                                  ast_node_t *node);
exec_frame_execute_result_t exec_frame_execute_case_clause(miga_frame_t *frame, ast_node_t *node);
exec_frame_execute_result_t exec_frame_execute_function_def_clause(miga_frame_t *frame,
                                                                   ast_node_t *node);
exec_frame_execute_result_t exec_frame_execute_pipeline_group(miga_frame_t *frame,
                                                              ast_node_list_t *node,
                                                              bool is_negated);
exec_frame_execute_result_t exec_frame_execute_while_loop(miga_frame_t *frame,
                                                          ast_node_t *condition, ast_node_t *node,
                                                          bool is_negated);
exec_frame_execute_result_t exec_frame_execute_for_loop(miga_frame_t *frame, string_t *var_name,
                                                        strlist_t *words, ast_node_t *body);
static exec_frame_execute_result_t exec_frame_execute_background_job(miga_frame_t *parent,
                                                                     ast_node_t *body,
                                                                     strlist_t *command_args);


/* ============================================================================
 * Helper Functions - System Queries
 * ============================================================================ */

#ifdef MIGA_POSIX_API
static mode_t get_umask_from_system(void)
#else
static int get_umask_from_system(void)
#endif
{
#ifdef MIGA_POSIX_API
    mode_t current_mask = umask(0);
    umask(current_mask);
    return current_mask;
#elifdef MIGA_UCRT_API
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

static void init_variables(miga_frame_t *frame, miga_exec_t *exec)
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

static void init_positional_params(miga_frame_t *frame, miga_exec_t *exec, exec_params_t *params)
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
                frame->positional_params = positional_params_create_from_strlist(
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
            frame->positional_params = positional_params_create_from_strlist(
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

static void init_fds(miga_frame_t *frame)
{
    INIT_BY_SCOPE(frame, open_fds, frame->policy->fds.scope, fd_table_create, fd_table_clone);
}

static void init_traps(miga_frame_t *frame)
{
    const exec_frame_policy_t *policy = frame->policy;

    INIT_BY_SCOPE(frame, traps, policy->traps.scope, trap_store_create, trap_store_clone);

    /* Reset non-ignored traps for subshells */
    if (policy->traps.resets_non_ignored && policy->traps.scope == EXEC_SCOPE_COPY)
    {
        trap_store_reset_non_ignored(frame->traps);
    }
}

static void init_options(miga_frame_t *frame)
{
    INIT_BY_SCOPE(frame, opt_flags, frame->policy->options.scope, exec_opt_flags_create,
                  exec_opt_flags_clone);
}

static void init_cwd(miga_frame_t *frame)
{
    const exec_frame_policy_t *policy = frame->policy;

    switch (policy->cwd.scope)
    {
    case EXEC_SCOPE_OWN:
        if (policy->cwd.init_from_system)
        {
            frame->working_directory = lib_getcwd();
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

static void init_umask(miga_frame_t *frame)
{
    const exec_frame_policy_t *policy = frame->policy;

    switch (policy->umask.scope)
    {
    case EXEC_SCOPE_OWN:
#ifdef MIGA_POSIX_API
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
#ifdef MIGA_POSIX_API
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

static void init_functions(miga_frame_t *frame)
{
    INIT_BY_SCOPE(frame, functions, frame->policy->functions.scope, func_store_create,
                  func_store_clone);
}

static void init_aliases(miga_frame_t *frame)
{
    INIT_BY_SCOPE(frame, aliases, frame->policy->aliases.scope, alias_store_create,
                  alias_store_clone);
}

/* ============================================================================
 * Frame Push - Create and Initialize a New Frame
 * ============================================================================ */

miga_frame_t *exec_frame_push(miga_frame_t *parent, exec_frame_type_t type, miga_exec_t *exec,
                              exec_params_t *params)
{
    miga_frame_t *frame = xcalloc(1, sizeof(miga_frame_t));

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
#if !defined(MIGA_POSIX_API) && !defined(MIGA_UCRT_API)
    if (frame->policy->stdio.inherits_redirected_stdio)
    {
        frame->stdin_fp = parent ? parent->stdin_fp : NULL;
        frame->stdout_fp = parent ? parent->stdout_fp : NULL;
        frame->stderr_fp = parent ? parent->stderr_fp : NULL;
    }
#endif

    /* Initialize frame-local state */
    frame->loop_depth = parent ? parent->loop_depth : 0;
    if (frame->policy->flow.is_loop)
    {
        frame->loop_depth++;
    }

    frame->last_exit_status = parent ? parent->last_exit_status : 0;
    frame->last_bg_pid = parent ? parent->last_bg_pid : 0;

    /* Control flow state */
    frame->pending_control_flow = MIGA_FRAME_FLOW_NORMAL;
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

miga_frame_t *exec_frame_create_top_level(miga_exec_t *exec)
{
    Expects_not_null(exec);

    miga_frame_t *frame = exec_frame_push(NULL, EXEC_FRAME_TOP_LEVEL, exec, NULL);

    /* Transfer any pre-initialized top-frame state from miga_exec_t */
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

#if defined(MIGA_POSIX_API) || defined(MIGA_UCRT_API)
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

    frame->source_line = 0;

    return frame;
}

/* ============================================================================
 * Frame Pop - Cleanup and Destroy a Frame
 * ============================================================================ */

/**
 * Cleanup helper - only frees resources this frame owns.
 */
static void cleanup_frame_resources(miga_frame_t *frame)
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
#if !defined(MIGA_POSIX_API) && !defined(MIGA_UCRT_API)
    // It is a coding error if these aren't properly closed. They
    // should have been closed/destroyed when the redirections closed up.
    if (*frame->stdin_fp && (!frame->parent || *frame->parent->stdin_fp != *frame->stdin_fp))
    {
        log_error("Redirected stdin not closed properly");
        fclose(*frame->stdin_fp);
        *frame->stdin_fp = NULL;
    }
    if (*frame->stdout_fp && (!frame->parent || *frame->parent->stdout_fp != *frame->stdout_fp))
    {
        log_error("Redirected stdout not closed properly");
        fclose(*frame->stdout_fp);
        *frame->stdout_fp = NULL;
    }
    if (*frame->stderr_fp && (!frame->parent || *frame->parent->stderr_fp != *frame->stderr_fp))
    {
        log_error("Redirected stderr not closed properly");
        fclose(*frame->stderr_fp);
        *frame->stderr_fp = NULL;
    }
#endif
}

miga_frame_t *exec_frame_pop(miga_frame_t **frame_ptr)
{
    Expects_not_null(frame_ptr);
    miga_frame_t *frame = *frame_ptr;
    Expects_not_null(frame);

    miga_frame_t *parent = frame->parent;
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
        if (frame->traps && frame->traps->exit_trap_set)
            trap_store_run_exit_trap(frame->traps, (void *) frame);
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
static void setup_process_group(miga_frame_t *frame, exec_params_t *params)
{
#ifdef MIGA_POSIX_API
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
static exec_frame_execute_result_t handle_control_flow(miga_frame_t *frame,
                                                       exec_frame_execute_result_t result)
{
    const exec_frame_policy_t *policy = frame->policy;

    switch (result.flow)
    {
    case MIGA_FRAME_FLOW_NORMAL:
        /* Nothing special to do */
        return result;

    case MIGA_FRAME_FLOW_RETURN:
        switch (policy->flow.return_behavior)
        {
        case EXEC_RETURN_TARGET:
            /* We're the target; return completes here */
            return (exec_frame_execute_result_t){.exit_status = result.exit_status,
                                                 .has_exit_status = true,
                                                 .flow = MIGA_FRAME_FLOW_NORMAL,
                                                 .flow_depth = 0};
        case EXEC_RETURN_TRANSPARENT:
            /* Pass it up to parent */
            return result;
        case EXEC_RETURN_DISALLOWED:
            /* Error: shouldn't happen if AST validation is correct */
            log_error("return: not valid in this context");
            return (exec_frame_execute_result_t){.exit_status = 1,
                                                 .has_exit_status = true,
                                                 .flow = MIGA_FRAME_FLOW_NORMAL,
                                                 .flow_depth = 0};
        }
        break;

    case MIGA_FRAME_FLOW_TOP:
        /* Unwind all frames to top level (exit); always propagate */
        return result;

    case MIGA_FRAME_FLOW_BREAK:
    case MIGA_FRAME_FLOW_CONTINUE:
        switch (policy->flow.loop_control)
        {
        case EXEC_LOOP_TARGET:
            /* We're a loop; handle break/continue */
            if (result.flow_depth <= 1)
            {
                /* This loop is the target */
                if (result.flow == MIGA_FRAME_FLOW_BREAK)
                {
                    /* Break: exit loop with current status */
                    return (exec_frame_execute_result_t){.exit_status = result.exit_status,
                                                         .has_exit_status = result.has_exit_status,
                                                         .flow = MIGA_FRAME_FLOW_NORMAL,
                                                         .flow_depth = 0};
                }
                else
                {
                    /* Continue: signal to continue loop iteration */
                    return (exec_frame_execute_result_t){
                        .exit_status = 0,
                        .has_exit_status = true,
                        .flow = MIGA_FRAME_FLOW_CONTINUE,
                        .flow_depth = 0 /* Consumed by this loop */
                    };
                }
            }
            else
            {
                /* Decrement depth and propagate */
                return (exec_frame_execute_result_t){.exit_status = result.exit_status,
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
            return (exec_frame_execute_result_t){.exit_status = 1,
                                                 .has_exit_status = true,
                                                 .flow = MIGA_FRAME_FLOW_NORMAL,
                                                 .flow_depth = 0};
        }
        break;
    }

    return result;
}

/**
 * Execute the frame's body based on params.
 */
static exec_frame_execute_result_t execute_frame_body(miga_frame_t *frame, exec_params_t *params)
{
    exec_frame_execute_result_t result = {
        .exit_status = 0, .has_exit_status = true, .flow = MIGA_FRAME_FLOW_NORMAL, .flow_depth = 0};

    if (!params)
    {
        return result;
    }

    /* Apply redirections */
    if (params->redirections)
    {
        miga_exec_status_t redir_result = exec_redirect_apply_redirectons(frame, params->redirections);
        if (redir_result != MIGA_EXEC_STATUS_OK)
        {
            result.exit_status = 1;
            return result;
        }
    }

    /* Handle pipe FD setup for EXEC_FRAME_PIPELINE_CMD */
#ifdef MIGA_POSIX_API
    if (params->stdin_pipe_fd >= 0)
    {
        dup2(params->stdin_pipe_fd, STDIN_FILENO);
        close(params->stdin_pipe_fd);

        // Track: STDIN is now a pipe-redirected FD
        string_t *name =
            fd_table_generate_name_ex(STDIN_FILENO, params->stdin_pipe_fd, FD_IS_REDIRECTED);
        fd_table_add(frame->open_fds, STDIN_FILENO, FD_IS_REDIRECTED, name);
        string_destroy(&name);
    }
    if (params->stdout_pipe_fd >= 0)
    {
        dup2(params->stdout_pipe_fd, STDOUT_FILENO);
        close(params->stdout_pipe_fd);

        // Track: STDOUT is now a pipe-redirected FD
        string_t *name =
            fd_table_generate_name_ex(STDOUT_FILENO, params->stdout_pipe_fd, FD_IS_REDIRECTED);
        fd_table_add(frame->open_fds, STDOUT_FILENO, FD_IS_REDIRECTED, name);
        string_destroy(&name);
    }
    if (params->pipe_fds_to_close)
    {
        for (int i = 0; i < params->pipe_fds_count; i++)
        {
            close(params->pipe_fds_to_close[i]);
            fd_table_mark_closed(frame->open_fds, params->pipe_fds_to_close[i]);
        }
    }
#endif

    /* Execute body based on what's provided */
    if (params->body)
    {
        result = exec_frame_execute_dispatch(frame, params->body);
    }
    else if (params->condition)
    {
        /* While/until loop */
        result = exec_frame_execute_condition_loop(frame, params);
    }
    else if (params->iteration_words)
    {
        /* For loop */
        result = exec_frame_execute_iteration_loop(frame, params);
    }
    else if (params->pipeline_commands)
    {
        /* Pipeline orchestration */
        result = exec_frame_execute_pipeline_orchestrate(frame, params);
    }

    /* Handle control flow */
    result = handle_control_flow(frame, result);

    /* Restore redirections */
    if (params->redirections)
    {
        exec_redirect_restore_redirections(frame, params->redirections);
    }

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
 * @return exec_frame_execute_result_t with final loop status and any pending control flow
 */
static exec_frame_execute_result_t exec_frame_execute_condition_loop(miga_frame_t *frame,
                                                                     exec_params_t *params)
{
    Expects_not_null(frame);
    Expects_not_null(params);
    Expects_not_null(params->condition);
    Expects_not_null(params->body);

    exec_frame_execute_result_t result = {
        .exit_status = 0, .has_exit_status = true, .flow = MIGA_FRAME_FLOW_NORMAL, .flow_depth = 0};
    bool is_until = params->until_mode;

    while (true)
    {
        // Execute condition
        exec_frame_execute_result_t cond_result =
            exec_frame_execute_dispatch(frame, params->condition);
        if (cond_result.status != MIGA_EXEC_STATUS_OK)
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
        exec_frame_execute_result_t body_result;
        if (params->body->type == AST_COMMAND_LIST)
            body_result = exec_frame_execute_iteration_loop(frame, params);
        else
            body_result = exec_frame_execute_dispatch(frame, params->body);

        // Handle control flow
        if (body_result.flow == MIGA_FRAME_FLOW_BREAK)
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
        else if (body_result.flow == MIGA_FRAME_FLOW_CONTINUE)
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
        else if (body_result.flow == MIGA_FRAME_FLOW_RETURN || body_result.flow == MIGA_FRAME_FLOW_TOP)
        {
            // Return/exit propagates up
            return body_result;
        }
        else if (body_result.status != MIGA_EXEC_STATUS_OK)
        {
            return body_result;
        }

        // Update exit status from body
        result.exit_status = body_result.exit_status;
        result.has_exit_status = body_result.has_exit_status;
    }

    return result;
}

 static exec_frame_execute_result_t exec_frame_execute_background_job(miga_frame_t *parent, ast_node_t *body,
                                 strlist_t *command_args)
 {
     exec_params_t params = {
         .body = body,
         .command_args = command_args,
     };

     return exec_in_frame(parent, EXEC_FRAME_BACKGROUND_JOB, &params);
 }

 exec_frame_execute_result_t exec_frame_execute_subshell(miga_frame_t *parent, ast_node_t *body)
 {
     exec_params_t params = {
         .body = body,
     };
     return exec_in_frame(parent, EXEC_FRAME_SUBSHELL, &params);
 }

exec_frame_execute_result_t exec_frame_execute_brace_group(miga_frame_t *parent, ast_node_t *node,
                                                            exec_redirections_t *redirs)
{
    exec_params_t params = {
        .body = node,
        .redirections = redirs,
    };
    return exec_in_frame(parent, EXEC_FRAME_BRACE_GROUP, &params);
}

exec_frame_execute_result_t exec_frame_execute_pipeline_group(miga_frame_t *parent,
                                                              ast_node_list_t *pipeline_commands,
                                                              bool is_negated)
{
     exec_params_t params = {
        .pipeline_commands = pipeline_commands,
         .pipeline_negated = is_negated,
     };
     return exec_in_frame(parent, EXEC_FRAME_PIPELINE, &params);
}

exec_frame_execute_result_t exec_frame_execute_for_loop(miga_frame_t *frame, string_t *var_name,
                                                        strlist_t *words, ast_node_t *body)
{
    exec_params_t params = {
        .iteration_words = words,
        .loop_var_name = var_name,
        .body = body,
    };
    return exec_in_frame(frame, EXEC_FRAME_LOOP, &params);
}


#ifdef MIGA_UCRT_API

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
exec_frame_execute_result_t exec_in_frame(miga_frame_t *parent, exec_frame_type_t type,
                                          exec_params_t *params)
{
    Expects_not_null(parent);
    miga_exec_t *exec = parent->executor;
    const exec_frame_policy_t *policy = &EXEC_FRAME_POLICIES[type];
    exec_frame_execute_result_t result = {0};

    /* Handle forking if required */
    if (policy->process.forks)
    {
#ifdef MIGA_POSIX_API
        pid_t pid = fork();
        if (pid < 0)
        {
            /* Fork failed */
            return (exec_frame_execute_result_t){.exit_status = 1,
                                                 .has_exit_status = true,
                                                 .flow = MIGA_FRAME_FLOW_NORMAL,
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
                    params ? strlist_join(params->command_args, " ") : string_create_from_cstr("");
                int job_id = job_store_add(exec->jobs, cmdline, true /* is_background */);
                job_store_add_process(exec->jobs, job_id, pid, cmdline);
                string_destroy(&cmdline);
                return (exec_frame_execute_result_t){.exit_status = 0,
                                                     .has_exit_status = true,
                                                     .flow = MIGA_FRAME_FLOW_NORMAL,
                                                     .flow_depth = 0};
            }
            else
            {
                /* Foreground: wait for child */
                int status;
                waitpid(pid, &status, 0);
                int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
                return (exec_frame_execute_result_t){.exit_status = exit_status,
                                                     .has_exit_status = true,
                                                     .flow = MIGA_FRAME_FLOW_NORMAL,
                                                     .flow_depth = 0};
            }
        }
        /* Child process continues below */
#elifdef MIGA_UCRT_API
        /* While POSIX can fork and get a PID before executing a command,
         * in UCRT, we can't get a handle until when the command is spawned.
         * In POSIX, because of fork, you handle both background and foreground
         * here based on PID, but, in UCRT, this block is all forground execution, and the
         * background task is handled in the spawn call in exec_frame_execute_simple_command_impl.
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
    miga_frame_t *frame = exec_frame_push(parent, type, exec, params);

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
#ifdef MIGA_POSIX_API
        _exit(result.exit_status);
#else
        /* For ISO C mode, just return */
#endif
    }

    return result;
}

/* ============================================================================
 * Variable and File Descriptor Access
 * ============================================================================ */

miga_frame_t *exec_frame_find_return_target(miga_frame_t *frame)
{
    Expects_not_null(frame);

    miga_frame_t *current = frame;
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

miga_frame_t *exec_frame_find_loop(miga_frame_t *frame)
{
    Expects_not_null(frame);
    miga_frame_t *current = frame;
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

variable_store_t *exec_frame_get_variables(miga_frame_t *frame)
{
    if (!frame)
        return NULL;

    return frame->variables;
}

fd_table_t *exec_frame_get_fds(miga_frame_t *frame)
{
    if (!frame)
        return NULL;

    return frame->open_fds;
}

trap_store_t *exec_frame_get_traps(miga_frame_t *frame)
{
    Expects_not_null(frame);
    return frame->traps;
}

const string_t *exec_frame_get_variable(const miga_frame_t *frame, const string_t *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_gt(name->length, 0);

    /* Check local store first if present */
    if (frame->local_variables)
    {
        const string_t *local_value = variable_store_get_value(frame->local_variables, name);
        if (local_value)
            return local_value;
    }

    /* Fall back to global store */
    if (frame->variables)
    {
        const string_t *global_value = variable_store_get_value(frame->variables, name);
        return global_value;
    }

    return NULL;
}

void exec_frame_set_variable(miga_frame_t *frame, const string_t *name, const string_t *value)
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

int exec_frame_declare_local(miga_frame_t *frame, const string_t *name, const string_t *value)
{
    if (!frame || !name || !value)
        return -1;

    if (!frame->local_variables)
        return -1;

    var_store_error_t result =
        variable_store_add(frame->local_variables, name, value, false, false);
    return (result == VAR_STORE_ERROR_NONE) ? 0 : -1;
}

/* ============================================================================
 * Frame-level execution operations
 * ============================================================================ */

/**
 * Update the LINENO variable in the frame's variable store.
 *
 * POSIX says LINENO is set by the shell "to a decimal number representing the
 * current sequential line number (numbered starting with 1) within a script or
 * function before it executes each command."
 *
 * LINENO is a regular variable — the user can unset or reset it. If they do,
 * it will be recreated or updated again when the next command executes.
 *
 * We only update when we have a valid source line (> 0) from the AST, and
 * only when executing within a script or function context.
 */
static void exec_frame_update_lineno(miga_frame_t *frame, const ast_node_t *node)
{
    if (!frame || !node)
        return;

    /* Only update for valid source lines (parser-assigned, 1-based) */
    if (node->first_line <= 0)
        return;

    /* Update the frame's internal line tracker */
    frame->source_line = node->first_line;

    /* Write LINENO into the variable store as a regular variable */
    variable_store_t *vars = exec_frame_get_variables(frame);
    if (!vars)
        return;

    if (variable_store_has_name_cstr(vars, "LINENO") &&
        variable_store_is_read_only_cstr(vars, "LINENO"))
        /* You can't make LINENO readonly, permanently. Muhuhuwahaha! */
        variable_store_set_read_only_cstr(vars, "LINENO", false);

    char buf[24];
    snprintf(buf, sizeof(buf), "%d", node->first_line);
    variable_store_add_cstr(vars, "LINENO", buf, false, false);
}

exec_frame_execute_result_t exec_frame_execute_dispatch(miga_frame_t *frame, const ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    exec_frame_execute_result_t result = {
        .status = MIGA_EXEC_STATUS_OK, .has_exit_status = false, .exit_status = 0};

    /* Update LINENO before executing this command */
    exec_frame_update_lineno(frame, node);

    switch (node->type)
    {
    case AST_COMMAND_LIST:
        result = exec_frame_execute_compound_list(frame, (ast_node_t *)node);
        break;

    case AST_AND_OR_LIST:
        result = exec_frame_execute_and_or_list(frame, (ast_node_t *)node);
        break;

    case AST_PIPELINE:
        result = exec_frame_execute_pipeline(frame, (ast_node_t *)node);
        break;

    case AST_SIMPLE_COMMAND:
        result = exec_frame_execute_simple_command(frame, (ast_node_t *)node);
        break;

    case AST_SUBSHELL:
        result = exec_frame_execute_subshell(frame, node->data.compound.body);
        break;

    case AST_BRACE_GROUP:
        result = exec_frame_execute_brace_group(frame, node->data.compound.body, NULL);
        break;

    case AST_IF_CLAUSE:
        result = exec_frame_execute_if_clause(frame, (ast_node_t *)node);
        break;

    case AST_WHILE_CLAUSE:
    case AST_UNTIL_CLAUSE:
        result = exec_frame_execute_while_clause(frame, (ast_node_t *)node);
        break;

    case AST_FOR_CLAUSE:
        result = exec_frame_execute_for_clause(frame, (ast_node_t *)node);
        break;

    case AST_REDIRECTED_COMMAND:
        result = exec_frame_execute_redirected_command(frame, (ast_node_t *)node);
        break;

    case AST_CASE_CLAUSE:
        result = exec_frame_execute_case_clause(frame, (ast_node_t *)node);
        break;

    case AST_FUNCTION_DEF:
        result = exec_frame_execute_function_def_clause(frame, (ast_node_t *)node);
        break;

    default:
        exec_set_error_printf(frame->executor, "Unsupported AST node type: %d %s", node->type,
                              ast_node_type_to_string(node->type));
        result.status = MIGA_EXEC_STATUS_NOT_IMPL;
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

exec_frame_execute_result_t exec_frame_execute_compound_list(miga_frame_t *frame,
                                                             const ast_node_t *list)
{
    Expects_not_null(frame);
    Expects_not_null(list);
    Expects(list->type == AST_COMMAND_LIST);

    exec_frame_execute_result_t result = {
        .status = MIGA_EXEC_STATUS_OK, .has_exit_status = false, .exit_status = 0};

    // Access the command list data
    ast_node_list_t *items = list->data.command_list.items;
    cmd_separator_list_t *separators = list->data.command_list.separators;

    if (!items || items->size == 0)
    {
        // Empty list: no commands to execute, exit status 0
        result.has_exit_status = true;
        result.exit_status = 0;
        return result;
    }

    // Iterate through each command in the list
    for (int i = 0; i < items->size; i++)
    {
        ast_node_t *cmd = items->nodes[i];
        if (!cmd)
            continue; // Skip null commands (shouldn't happen, but defensive)

        /* Update LINENO before executing this command */
        exec_frame_update_lineno(frame, cmd);

        exec_frame_execute_result_t cmd_result;
        cmd_separator_t sep = cmd_separator_list_get(separators, i);
        if (sep == CMD_EXEC_BACKGROUND)
        {
            /* For POSIX, UCRT, and ISO_C, we'll just call exec_frame_execute_background_job,
             * but that doesn't mean this will actually run in the background.
             * We'll handle platform-specific limitations inside exec_in_frame() or its children.
             *
             * In POSIX, exec_job_background will return immediately because the bg process forked.
             * For UCRT, it will run in the background if the command is spawnable, otherwise
             * foreground with a warning.
             * For ISO_C, it will run in the foreground with a warning.
             */
            string_t *command_line = ast_node_to_command_line_full(cmd);
            // Since we're not sending this to spawn, we can keep this as a single string_t
            strlist_t *argv_list = strlist_create();
            strlist_move_push_back(argv_list, &command_line);
            cmd_result = exec_frame_execute_background_job(frame, cmd, argv_list);
            strlist_destroy(&argv_list);
        }
        else
        {
            // Execute the command based on its type
            switch (cmd->type)
            {
            case AST_PIPELINE:
                cmd_result = exec_frame_execute_pipeline(frame, cmd);
                break;
            case AST_AND_OR_LIST:
                cmd_result = exec_frame_execute_and_or_list(frame, cmd);
                break;
            case AST_COMMAND_LIST:
                cmd_result = exec_frame_execute_compound_list(frame, cmd);
                break;
            case AST_SIMPLE_COMMAND:
                cmd_result = exec_frame_execute_simple_command(frame, cmd);
                break;
            case AST_BRACE_GROUP:
                cmd_result = exec_frame_execute_brace_group(frame, cmd->data.compound.body, NULL);
                break;
            case AST_SUBSHELL:
                cmd_result = exec_frame_execute_subshell(frame, cmd->data.compound.body);
                break;
            case AST_IF_CLAUSE:
                cmd_result = exec_frame_execute_if_clause(frame, cmd);
                break;
            case AST_WHILE_CLAUSE:
            case AST_UNTIL_CLAUSE:
                cmd_result = exec_frame_execute_while_clause(frame, cmd);
                break;
            case AST_FOR_CLAUSE:
                cmd_result = exec_frame_execute_for_clause(frame, cmd);
                break;
            case AST_CASE_CLAUSE:
                cmd_result = exec_frame_execute_case_clause(frame, cmd);
                break;
            case AST_REDIRECTED_COMMAND:
                cmd_result = exec_frame_execute_redirected_command(frame, cmd);
                break;
            case AST_FUNCTION_DEF:
                cmd_result = exec_frame_execute_function_def_clause(frame, cmd);
                break;
            default:
                exec_set_error_printf(frame->executor,
                                      "Unsupported command type in compound list: %d (%s)",
                                      cmd->type, ast_node_type_to_string(cmd->type));
                result.status = MIGA_EXEC_STATUS_NOT_IMPL;
                return result;
            }
        }
        // Propagate errors from command execution
        if (cmd_result.status != MIGA_EXEC_STATUS_OK)
        {
            result.status = cmd_result.status;
            if (cmd_result.has_exit_status)
            {
                result.has_exit_status = true;
                result.exit_status = cmd_result.exit_status;
            }
            return result; // Stop on error (unless background)
        }

        // Update the frame's exit status from the command
        if (cmd_result.has_exit_status)
        {
            frame->last_exit_status = cmd_result.exit_status;
            result.has_exit_status = true;
            result.exit_status = cmd_result.exit_status;
        }

        // Handle the separator after this command
        // cmd_separator_t sep = ast_node_command_list_get_separator(list, i);
        if (sep == CMD_EXEC_BACKGROUND)
        {
            // Run in background: Add to job store and continue without waiting
            // Note: This assumes the command spawned processes; integrate with job control
            // For simplicity, assume exec_pipeline/exec_and_or_list handle process creation
            // In a full impl, extract PIDs from cmd_result and add to job_store
            // Example: job_store_add(frame->executor->jobs, command_line, true);
            // For now, just log or skip detailed job management
            // TODO: Implement full background job tracking
        }
        else if (sep == CMD_EXEC_SEQUENTIAL)
        {
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

exec_frame_execute_result_t exec_frame_execute_and_or_list(miga_frame_t *frame, ast_node_t *list)
{
    Expects_not_null(frame);
    Expects_not_null(list);
    Expects(list->type == AST_AND_OR_LIST);

    exec_frame_execute_result_t result = {
        .status = MIGA_EXEC_STATUS_OK, .has_exit_status = false, .exit_status = 0};

    ast_node_t *left = list->data.andor_list.left;
    ast_node_t *right = list->data.andor_list.right;
    andor_operator_t op = list->data.andor_list.op;

    /* Update LINENO for left side */
    exec_frame_update_lineno(frame, left);

    // Execute left command
    exec_frame_execute_result_t left_result;
    switch (left->type)
    {
    case AST_COMMAND_LIST:
        left_result = exec_frame_execute_compound_list(frame, left);
        break;
    case AST_AND_OR_LIST:
        left_result = exec_frame_execute_and_or_list(frame, left);
        break;
    case AST_PIPELINE:
        left_result = exec_frame_execute_pipeline(frame, left);
        break;
    case AST_SIMPLE_COMMAND:
        left_result = exec_frame_execute_simple_command(frame, left);
        break;
    default:
        exec_set_error_printf(frame->executor, "Unsupported AST node type in and_or_list: %d (%s)",
                              left->type, ast_node_type_to_string(left->type));
        result.status = MIGA_EXEC_STATUS_NOT_IMPL;
        return result;
    }

    if (left_result.status != MIGA_EXEC_STATUS_OK)
    {
        result = left_result;
        return result;
    }

    // Determine if right command should be executed
    bool execute_right = false;
    if (op == ANDOR_OP_AND)
    {
        if (left_result.exit_status == 0)
        {
            execute_right = true;
        }
    }
    else if (op == ANDOR_OP_OR)
    {
        if (left_result.exit_status != 0)
        {
            execute_right = true;
        }
    }

    if (execute_right)
    {
        /* Update LINENO for right side (only if we're going to execute it) */
        exec_frame_update_lineno(frame, right);

        // Execute right command
        exec_frame_execute_result_t right_result;
        switch (right->type)
        {
        case AST_COMMAND_LIST:
            right_result = exec_frame_execute_compound_list(frame, right);
            break;
        case AST_AND_OR_LIST:
            right_result = exec_frame_execute_and_or_list(frame, right);
            break;
        case AST_PIPELINE:
            right_result = exec_frame_execute_pipeline(frame, right);
            break;
        case AST_SIMPLE_COMMAND:
            right_result = exec_frame_execute_simple_command(frame, right);
            break;
        default:
            exec_set_error_printf(frame->executor,
                                  "Unsupported AST node type in and_or_list: %d (%s)", right->type,
                                  ast_node_type_to_string(right->type));
            result.status = MIGA_EXEC_STATUS_NOT_IMPL;
            return result;
        }

        if (right_result.status != MIGA_EXEC_STATUS_OK)
        {
            result = right_result;
            return result;
        }

        result = right_result;
    }
    else
    {
        result = left_result;
    }

    // Update frame's last exit status
    frame->last_exit_status = result.exit_status;

    return result;
}

exec_frame_execute_result_t exec_frame_execute_pipeline(miga_frame_t *frame, ast_node_t *list)
{
    Expects_not_null(frame);
    Expects_not_null(list);
    Expects(list->type == AST_PIPELINE);

    ast_node_list_t *commands = list->data.pipeline.commands;
    bool is_negated = list->data.pipeline.is_negated;

    // Execute the pipeline using the frame system
    exec_frame_execute_result_t result =
        exec_frame_execute_pipeline_group(frame, commands, is_negated);

    return result;
}

exec_frame_execute_result_t exec_frame_execute_simple_command(miga_frame_t *frame, ast_node_t *node)
{
    /* Executing a simple command is so complicated, it gets
     * its own module, lol. Simple, my butt. */
    exec_frame_execute_result_t result = exec_frame_execute_simple_command_impl(frame, node);

    /* Pick up any pending control flow that a builtin set on the frame
     * (return, break, continue).  The simple-command impl doesn't set
     * these in the result struct itself. */
    result.flow = frame->pending_control_flow;
    result.flow_depth = frame->pending_flow_depth;

    /* Reset pending flow for next command */
    frame->pending_control_flow = MIGA_FRAME_FLOW_NORMAL;
    frame->pending_flow_depth = 0;

    return result;
}

exec_frame_execute_result_t exec_frame_execute_if_clause(miga_frame_t *frame, ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects(node->type == AST_IF_CLAUSE);

    exec_frame_execute_result_t result = {
        .status = MIGA_EXEC_STATUS_OK, .has_exit_status = false, .exit_status = 0};

    // Execute the main condition
    exec_frame_execute_result_t cond_result =
        exec_frame_execute_dispatch(frame, node->data.if_clause.condition);
    if (cond_result.status != MIGA_EXEC_STATUS_OK)
    {
        result = cond_result;
        return result;
    }

    // If condition succeeds (exit status 0), execute then_body
    if (cond_result.has_exit_status && cond_result.exit_status == 0)
    {
        exec_frame_execute_result_t then_result =
            exec_frame_execute_dispatch(frame, node->data.if_clause.then_body);
        if (then_result.status != MIGA_EXEC_STATUS_OK)
        {
            result = then_result;
            return result;
        }
        result = then_result;
        return result;
    }

    // Check elif clauses
    if (node->data.if_clause.elif_list)
    {
        for (int i = 0; i < node->data.if_clause.elif_list->size; i++)
        {
            ast_node_t *elif_node = node->data.if_clause.elif_list->nodes[i];
            if (!elif_node || elif_node->type != AST_IF_CLAUSE)
            {
                // Assuming elif is also AST_IF_CLAUSE with condition and then_body
                continue;
            }
            exec_frame_execute_result_t elif_cond_result =
                exec_frame_execute_dispatch(frame, elif_node->data.if_clause.condition);
            if (elif_cond_result.status != MIGA_EXEC_STATUS_OK)
            {
                result = elif_cond_result;
                return result;
            }
            if (elif_cond_result.has_exit_status && elif_cond_result.exit_status == 0)
            {
                exec_frame_execute_result_t elif_then_result =
                    exec_frame_execute_dispatch(frame, elif_node->data.if_clause.then_body);
                if (elif_then_result.status != MIGA_EXEC_STATUS_OK)
                {
                    result = elif_then_result;
                    return result;
                }
                result = elif_then_result;
                return result;
            }
        }
    }

    // Execute else_body if present
    if (node->data.if_clause.else_body)
    {
        exec_frame_execute_result_t else_result =
            exec_frame_execute_dispatch(frame, node->data.if_clause.else_body);
        if (else_result.status != MIGA_EXEC_STATUS_OK)
        {
            result = else_result;
            return result;
        }
        result = else_result;
    }
    else
    {
        // No else, exit status 0
        result.has_exit_status = true;
        result.exit_status = 0;
    }

    return result;
}

exec_frame_execute_result_t exec_frame_execute_while_clause(miga_frame_t *frame, ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects(node->type == AST_WHILE_CLAUSE || node->type == AST_UNTIL_CLAUSE);

    // Extract the loop components
    ast_node_t *condition = node->data.loop_clause.condition;
    ast_node_t *body = node->data.loop_clause.body;
    bool is_until = (node->type == AST_UNTIL_CLAUSE);

    // Execute the while/until loop using the frame system
    exec_params_t params = {
        .condition = condition,
        .body = body,
        .until_mode = is_until,
    };
    exec_frame_execute_result_t result = exec_frame_execute_condition_loop(frame, &params);

    return result;
}

exec_frame_execute_result_t exec_frame_execute_for_clause(miga_frame_t *frame, ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects(node->type == AST_FOR_CLAUSE);

    // Extract the for loop components
    string_t *var_name = node->data.for_clause.variable;
    token_list_t *word_tokens = node->data.for_clause.words;
    ast_node_t *body = node->data.for_clause.body;

    // Build the word list via proper POSIX expansion or positional parameters
    strlist_t *words;
    if (word_tokens && token_list_size(word_tokens) > 0)
    {
        // Perform full word expansion: tilde, parameter, command substitution,
        // arithmetic, field splitting, and pathname expansion
        words = expand_words(frame, word_tokens);
        if (!words)
        {
            words = strlist_create(); // Treat expansion failure as empty list
        }
    }
    else
    {
        // No words provided: iterate over positional parameters ("$@")
        positional_params_t *params = frame->positional_params;
        words = positional_params_get_all(params);
    }

    // Execute the for loop using the frame system
    exec_frame_execute_result_t result = exec_frame_execute_for_loop(frame, var_name, words, body);

    // Clean up
    strlist_destroy(&words);

    return result;
}

exec_frame_execute_result_t exec_frame_execute_function_def_clause(miga_frame_t *frame,
                                                                   ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects_eq(node->type, AST_FUNCTION_DEF);

    ast_node_t *body = node->data.function_def.body;
    ast_node_list_t *ast_redirections = node->data.function_def.redirections;
    string_t *name = node->data.function_def.name;
    exec_frame_execute_result_t result = {.status = MIGA_EXEC_STATUS_OK,
                                          .has_exit_status = true,
                                          .exit_status = 0,
                                          .flow = MIGA_FRAME_FLOW_NORMAL,
                                          .flow_depth = 0};

    // Convert AST redirections to exec_redirections_t format
    exec_redirections_t *redirections = NULL;
    if (ast_redirections && ast_node_list_size(ast_redirections) > 0)
    {
        redirections = exec_redirections_create_from_ast_nodes(frame, ast_redirections);
        if (!redirections)
        {
            exec_set_error_printf(frame->executor,
                                  "Failed to convert redirections for function '%s'",
                                  string_cstr(name));
            result.status = MIGA_EXEC_STATUS_ERROR;
            result.exit_status = 1;
            return result;
        }
    }

    func_store_t *func_store = frame->functions;
    func_store_insert_result_t ret = func_store_add_ex(func_store, name, body, redirections);

    if (ret.error != FUNC_STORE_ERROR_NONE)
    {
        exec_set_error_printf(frame->executor, "Failed to define function '%s'", string_cstr(name));
        // Clean up redirections on failure
        if (redirections)
            exec_redirections_destroy(&redirections);
        result.status = MIGA_EXEC_STATUS_ERROR;
        result.exit_status = 1;
        return result;
    }

    return result;
}

exec_frame_execute_result_t exec_frame_execute_redirected_command(miga_frame_t *frame,
                                                                  ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects(node->type == AST_REDIRECTED_COMMAND);

    ast_node_t *command = node->data.redirected_command.command;
    ast_node_list_t *ast_redirections = node->data.redirected_command.redirections;

    // Convert AST redirections to exec_redirections_t format
    exec_redirections_t *redirections = exec_redirections_create_from_ast_nodes(frame, ast_redirections);
    if (!redirections && ast_redirections && ast_node_list_size(ast_redirections) > 0)
    {
        // Conversion failed
        exec_frame_execute_result_t error_result = {.status = MIGA_EXEC_STATUS_ERROR,
                                                    .has_exit_status = true,
                                                    .exit_status = 1,
                                                    .flow = MIGA_FRAME_FLOW_NORMAL,
                                                    .flow_depth = 0};
        return error_result;
    }

    // Execute the command with redirections using the brace group frame
    // (which shares state but applies/restores redirections)
    exec_frame_execute_result_t result =
        exec_frame_execute_brace_group(frame, command, redirections);

    // Clean up
    if (redirections)
        exec_redirections_destroy(&redirections);

    return result;
}

#if DUPLICATE_CODE
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
 * @return exec_frame_execute_result_t with final loop status and any pending control flow
 */
exec_frame_execute_result_t exec_frame_execute_condition_loop(miga_frame_t *frame,
                                                              exec_params_t *params)
{
    Expects_not_null(frame);
    Expects_not_null(params);
    Expects_not_null(params->condition);
    Expects_not_null(params->body);

    exec_frame_execute_result_t result = {.status = MIGA_EXEC_STATUS_OK,
                                          .has_exit_status = true,
                                          .exit_status = 0,
                                          .flow = MIGA_FRAME_FLOW_NORMAL,
                                          .flow_depth = 0};

    bool is_until = params->until_mode;

    while (true)
    {
        // Execute condition
        exec_frame_execute_result_t cond_result =
            exec_frame_execute_dispatch(frame, params->condition);
        if (cond_result.status != MIGA_EXEC_STATUS_OK)
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
        exec_frame_execute_result_t body_result;
        if (params->body->type == AST_COMMAND_LIST)
            body_result = exec_frame_execute_compound_list(frame, params->body);
        else
            body_result = exec_frame_execute_dispatch(frame, params->body);

        // Handle control flow
        if (body_result.flow == MIGA_FRAME_FLOW_BREAK)
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
        else if (body_result.flow == MIGA_FRAME_FLOW_CONTINUE)
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
        else if (body_result.flow == MIGA_FRAME_FLOW_RETURN || body_result.flow == MIGA_FRAME_FLOW_TOP)
        {
            // Return/exit propagates up
            return body_result;
        }
        else if (body_result.status != MIGA_EXEC_STATUS_OK)
        {
            return body_result;
        }

        // Update exit status from body
        result.exit_status = body_result.exit_status;
        result.has_exit_status = body_result.has_exit_status;
    }

    return result;
}
#endif

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
 * @return exec_frame_execute_result_t with final loop status and any pending control flow
 */
exec_frame_execute_result_t exec_frame_execute_iteration_loop(miga_frame_t *frame,
                                                              exec_params_t *params)
{
    Expects_not_null(frame);
    Expects_not_null(params);
    Expects_not_null(params->loop_var_name);
    Expects_not_null(params->iteration_words);
    Expects_not_null(params->body);

    exec_frame_execute_result_t result = {.status = MIGA_EXEC_STATUS_OK,
                                          .has_exit_status = true,
                                          .exit_status = 0,
                                          .flow = MIGA_FRAME_FLOW_NORMAL,
                                          .flow_depth = 0};

    // Iterate over each word
    for (int i = 0; i < strlist_size(params->iteration_words); i++)
    {
        const string_t *word = strlist_at(params->iteration_words, i);

        // Set loop variable
        variable_store_add(frame->variables, params->loop_var_name, word, false, false);

        // Execute body
        exec_frame_execute_result_t body_result;
        if (params->body->type == AST_COMMAND_LIST)
            body_result = exec_frame_execute_compound_list(frame, params->body);
        else
            body_result = exec_frame_execute_dispatch(frame, params->body);

        // Handle control flow
        if (body_result.flow == MIGA_FRAME_FLOW_BREAK)
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
        else if (body_result.flow == MIGA_FRAME_FLOW_CONTINUE)
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
        else if (body_result.flow == MIGA_FRAME_FLOW_RETURN || body_result.flow == MIGA_FRAME_FLOW_TOP)
        {
            // Return/exit propagates up
            return body_result;
        }
        else if (body_result.status != MIGA_EXEC_STATUS_OK)
        {
            return body_result;
        }

        // Update exit status from body
        result.exit_status = body_result.exit_status;
        result.has_exit_status = body_result.has_exit_status;
    }

    return result;
}

/* ============================================================================
 * Helper: EINTR-safe waitpid
 * ============================================================================ */

#ifdef MIGA_POSIX_API
/**
 * waitpid wrapper that retries on EINTR.
 * Returns the pid on success, -1 on real error (with errno set).
 */
static pid_t waitpid_eintr(pid_t pid, int *status, int options)
{
    pid_t ret;
    do
    {
        ret = waitpid(pid, status, options);
    } while (ret == -1 && errno == EINTR);
    return ret;
}
#endif

/* ============================================================================
 * Pipeline Orchestration
 * ============================================================================ */

/**
 * Execute a pipeline from within an EXEC_FRAME_PIPELINE frame.
 *
 * This function orchestrates the execution of multiple commands connected by
 * pipes. It creates pipes, forks child processes for each command, sets up
 * pipe plumbing, and waits for all children to complete.
 *
 * Process group handling follows the standard POSIX practice of calling
 * setpgid() in both parent and child to avoid race conditions. The first
 * child's PID becomes the pipeline's process group ID; subsequent children
 * join that group. Both parent and child call setpgid() because:
 *   - The child might run before the parent records pipeline_pgid
 *   - The parent might run before the child calls setpgid() on itself
 * One of the two calls will succeed; the other will get EACCES/ESRCH
 * harmlessly.
 *
 * @param frame  The current execution frame (must be EXEC_FRAME_PIPELINE)
 * @param params Parameters including pipeline_commands and pipeline_negated
 * @return exec_frame_execute_result_t with final pipeline status
 */
exec_frame_execute_result_t exec_frame_execute_pipeline_orchestrate(miga_frame_t *frame,
                                                                    exec_params_t *params)
{
    Expects_not_null(frame);
    Expects_not_null(params);
    Expects_not_null(params->pipeline_commands);

    ast_node_list_t *commands = params->pipeline_commands;
    bool is_negated = params->pipeline_negated;
    int ncmds = commands->size;

    exec_frame_execute_result_t result = {.status = MIGA_EXEC_STATUS_OK,
                                          .has_exit_status = true,
                                          .exit_status = 0,
                                          .flow = MIGA_FRAME_FLOW_NORMAL,
                                          .flow_depth = 0};

    if (ncmds == 0)
    {
        result.exit_status = is_negated ? 1 : 0;
        return result;
    }

    if (ncmds == 1)
    {
        /* Single command — no pipes needed, just execute directly */
        ast_node_t *cmd = commands->nodes[0];
        exec_frame_execute_result_t cmd_result = exec_frame_execute_dispatch(frame, cmd);
        if (is_negated && cmd_result.has_exit_status)
        {
            cmd_result.exit_status = (cmd_result.exit_status == 0) ? 1 : 0;
        }
        return cmd_result;
    }

#ifdef MIGA_POSIX_API
    /*
     * Allocate pipes and PID array on the heap to avoid VLA stack overflow
     * with pathological input. We need (ncmds - 1) pipes, each being 2 fds.
     */
    int num_pipes = ncmds - 1;
    int *pipes = xcalloc(2 * num_pipes, sizeof(int));
    pid_t *pids = xcalloc(ncmds, sizeof(pid_t));

    /* Initialize all pipe fds to -1 so cleanup knows which were opened */
    for (int i = 0; i < 2 * num_pipes; i++)
    {
        pipes[i] = -1;
    }

    /* Create all pipes up front */
    for (int i = 0; i < num_pipes; i++)
    {
        if (pipe(pipes + 2 * i) == -1)
        {
            exec_set_error_printf(frame->executor, "pipe() failed: %s", strerror(errno));
            result.status = MIGA_EXEC_STATUS_ERROR;
            result.exit_status = 1;
            goto cleanup;
        }
    }

    /* Initialize all PIDs to -1 so cleanup knows which children were forked */
    for (int i = 0; i < ncmds; i++)
    {
        pids[i] = -1;
    }

    pid_t pipeline_pgid = 0;

    /* Fork and execute each command */
    for (int i = 0; i < ncmds; i++)
    {
        ast_node_t *cmd = commands->nodes[i];

        pid_t pid = fork();
        if (pid == -1)
        {
            exec_set_error_printf(frame->executor, "fork() failed in pipeline: %s", strerror(errno));

            /* Kill and reap all children we already forked */
            for (int j = 0; j < i; j++)
            {
                kill(pids[j], SIGTERM);
            }
            for (int j = 0; j < i; j++)
            {
                int discard;
                waitpid_eintr(pids[j], &discard, 0);
            }

            result.status = MIGA_EXEC_STATUS_ERROR;
            result.exit_status = 1;
            goto cleanup;
        }
        else if (pid == 0)
        {
            /* ---- Child process ---- */

            /*
             * Set up process group (child side).
             * For the first child (i == 0), pipeline_pgid is 0, so
             * setpgid(0, 0) creates a new group with this child as leader.
             * For subsequent children, pipeline_pgid was set by the parent
             * before this fork, so we join the existing group.
             *
             * Errors are ignored: EACCES means the parent already called
             * setpgid for us (or the child has already exec'd), and ESRCH
             * is similarly benign in this context.
             */
            setpgid(0, pipeline_pgid);

            /* Set up stdin from previous pipe */
            if (i > 0)
            {
                dup2(pipes[2 * (i - 1)], STDIN_FILENO);
            }

            /* Set up stdout to next pipe */
            if (i < ncmds - 1)
            {
                dup2(pipes[2 * i + 1], STDOUT_FILENO);
            }

            /* Close all pipe FDs in the child */
            for (int j = 0; j < 2 * num_pipes; j++)
            {
                close(pipes[j]);
            }

            /* Execute the command */
            exec_frame_execute_result_t cmd_result = exec_frame_execute_dispatch(frame, cmd);

            /* Exit the child with the command's exit status */
            if (is_negated && cmd_result.has_exit_status)
            {
                cmd_result.exit_status = (cmd_result.exit_status == 0) ? 1 : 0;
            }
            _exit(cmd_result.has_exit_status ? cmd_result.exit_status : 0);
        }

        /* ---- Parent process ---- */
        pids[i] = pid;

        if (i == 0)
        {
            /* First child is the group leader */
            pipeline_pgid = pid;
        }

        /*
         * Set up process group (parent side).
         * We call setpgid() here as well to eliminate the race condition
         * where the parent proceeds to fork the next child (which needs
         * pipeline_pgid) before the first child has called setpgid() on
         * itself. One of the parent/child setpgid calls will succeed;
         * the other will get EACCES (child already exec'd) which is fine.
         */
        setpgid(pid, pipeline_pgid);

        /*
         * Eagerly close pipe ends that the parent no longer needs.
         * After forking child i:
         *   - The write end of pipe i (pipes[2*i+1]) has been dup2'd into
         *     child i's stdout. Parent doesn't need it.
         *   - The read end of pipe i-1 (pipes[2*(i-1)]) has been dup2'd
         *     into child i's stdin. Parent doesn't need it.
         * Closing eagerly ensures that:
         *   - Children see EOF on their stdin when the upstream writer exits
         *   - We don't accumulate open fds across the fork loop
         */
        if (i < ncmds - 1)
        {
            close(pipes[2 * i + 1]);
            pipes[2 * i + 1] = -1;
        }
        if (i > 0)
        {
            close(pipes[2 * (i - 1)]);
            pipes[2 * (i - 1)] = -1;
        }
    }

    /* Close any remaining pipe fds the parent still holds */
    for (int i = 0; i < 2 * num_pipes; i++)
    {
        if (pipes[i] >= 0)
        {
            close(pipes[i]);
            pipes[i] = -1;
        }
    }

    /* Wait for all children (collect every status for future pipefail support) */
    int last_status = 0;
    for (int i = 0; i < ncmds; i++)
    {
        int status;
        pid_t waited = waitpid_eintr(pids[i], &status, 0);
        if (waited < 0)
        {
            /*
             * ECHILD means the child was already reaped (e.g. by a signal
             * handler). Treat as exit status 127. For any other error,
             * report but continue waiting for remaining children.
             */
            if (errno != ECHILD)
            {
                exec_set_error_printf(frame->executor, "waitpid() failed for pid %d: %s", (int)pids[i],
                               strerror(errno));
            }
            if (i == ncmds - 1)
            {
                last_status = 127;
            }
            continue;
        }

        int child_status;
        if (WIFEXITED(status))
        {
            child_status = WEXITSTATUS(status);
        }
        else if (WIFSIGNALED(status))
        {
            child_status = 128 + WTERMSIG(status);
        }
        else
        {
            child_status = 127;
        }

        /*
         * TODO: Store child_status in frame->executor->pipe_statuses[i]
         * for pipefail / $PIPESTATUS support.
         */

        if (i == ncmds - 1)
        {
            last_status = child_status;
        }
    }

    result.exit_status = is_negated ? (last_status == 0 ? 1 : 0) : last_status;
    frame->last_exit_status = result.exit_status;

cleanup:
    /* Free heap-allocated arrays */
    for (int i = 0; i < 2 * num_pipes; i++)
    {
        if (pipes[i] >= 0)
        {
            close(pipes[i]);
        }
    }
    xfree(pipes);
    xfree(pids);

    return result;

#else
    /* For non-POSIX systems, multi-command pipelines are not supported */
    exec_set_error_cstr(frame->executor, "Pipelines not supported on this platform");
    result.status = MIGA_EXEC_STATUS_NOT_IMPL;
    result.exit_status = 1;
    return result;
#endif
}

exec_frame_execute_result_t exec_frame_execute_case_clause(miga_frame_t *frame, ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects(node->type == AST_CASE_CLAUSE);

    exec_frame_execute_result_t result = {.status = MIGA_EXEC_STATUS_OK,
                                          .has_exit_status = true,
                                          .exit_status = 0,
                                          .flow = MIGA_FRAME_FLOW_NORMAL,
                                          .flow_depth = 0};

    /* Get the word to match against */
    token_t *word_token = node->data.case_clause.word;
    if (!word_token)
    {
        return result;
    }

    /*
     * POSIX XCU 2.9.4.3: The case word undergoes tilde expansion, parameter
     * expansion, command substitution, and arithmetic expansion — but NOT
     * field splitting or pathname expansion.
     */
    string_t *word = expand_word_nosplit(frame, word_token);
    if (!word)
    {
        exec_set_error_cstr(frame->executor, "Failed to expand case word");
        result.status = MIGA_EXEC_STATUS_ERROR;
        result.exit_status = 1;
        return result;
    }

    /* Iterate through case items */
    ast_node_list_t *case_items = node->data.case_clause.case_items;
    if (!case_items)
    {
        string_destroy(&word);
        return result;
    }

    bool matched = false;
    for (int i = 0; i < case_items->size && !matched; i++)
    {
        ast_node_t *item = case_items->nodes[i];
        if (!item || item->type != AST_CASE_ITEM)
        {
            continue;
        }

        token_list_t *patterns = item->data.case_item.patterns;
        if (!patterns)
        {
            continue;
        }

        for (int j = 0; j < token_list_size(patterns); j++)
        {
            const token_t *pattern_token = token_list_get(patterns, j);

            /*
             * POSIX XCU 2.9.4.3: Each pattern undergoes the same expansions
             * as the case word (tilde, parameter, command subst, arithmetic)
             * but NOT field splitting or pathname expansion. The pattern
             * metacharacters (*, ?, [...]) retain their special meaning for
             * fnmatch-style matching against the case word — they are NOT
             * used for filesystem globbing.
             */
            string_t *pattern = expand_word_nosplit(frame, pattern_token);
            if (!pattern)
            {
                continue;
            }

            /* Match pattern against word */
            bool pattern_matches = false;
#ifdef MIGA_POSIX_API
            pattern_matches = (fnmatch(string_cstr(pattern), string_cstr(word), 0) == 0);
#else
            pattern_matches = glob_util_match(string_cstr(pattern), string_cstr(word), 0);
#endif

            string_destroy(&pattern);

            if (pattern_matches)
            {
                matched = true;

                /* Execute the body of this case item */
                ast_node_t *body = item->data.case_item.body;
                if (body)
                {
                    result = exec_frame_execute_dispatch(frame, body);
                }

                break;
            }
        }
    }

    string_destroy(&word);

    /* If no match found, exit status remains 0 */
    return result;
}

/* ============================================================================
 * String Core Execution
 * ============================================================================ */

/**
 * Core implementation for executing shell commands from a string.
 *
 * This function processes a single chunk of input (typically one line),
 * handling lexing, tokenization, parsing, and execution. It maintains
 * state in the provided session for handling multi-line constructs.
 *
 * @param frame   The execution frame
 * @param input   The input string to process
 * @param session The parse session (maintains lexer, tokenizer, and accumulated tokens)
 * @return Status indicating success, need for more input, or error
 */
miga_exec_status_t exec_frame_string_core(miga_frame_t *frame, const char *input,
                                     parse_session_t *session)
{
    Expects_not_null(frame);
    Expects_not_null(input);
    Expects_not_null(session);
    Expects_not_null(session->lexer);
    Expects_not_null(session->tokenizer);
    Expects_not_null(frame->executor);

    miga_exec_t *executor = frame->executor;
    lexer_t *lx = session->lexer;
    tokenizer_t *tokenizer = session->tokenizer;

    session->line_num++;
    log_debug("exec_frame_string_core: Processing line %d: %.*s", session->line_num,
              (int)strcspn(input, "\r\n"), input);

    lexer_append_input_cstr(lx, input);

    token_list_t *raw_tokens = token_list_create();
    lex_status_t lex_status = lexer_tokenize(lx, raw_tokens, NULL);

    if (lex_status == LEX_ERROR)
    {
        log_debug("exec_frame_string_core: Lexer error at line %d", session->line_num);
        const char *err = lexer_get_error(lx);
        frame_set_error_printf(frame, "Lexer error: %s", err ? err : "unknown");
        token_list_destroy(&raw_tokens);
        return MIGA_EXEC_STATUS_ERROR;
    }

    if (lex_status == LEX_INCOMPLETE || lex_status == LEX_NEED_HEREDOC)
    {
        log_debug("exec_frame_string_core: Lexer incomplete/heredoc at line %d", session->line_num);

        /* Check if any tokens were produced before the incomplete state.
         * If so, we should accumulate them for parsing when more input arrives. */
        if (token_list_size(raw_tokens) > 0)
        {
            log_debug("exec_frame_string_core: Lexer produced %d tokens before becoming incomplete",
                      token_list_size(raw_tokens));

            /* Process the tokens that were produced */
            token_list_t *processed_tokens = token_list_create();
            tok_status_t tok_status = tokenizer_process(tokenizer, raw_tokens, processed_tokens);
            token_list_destroy(&raw_tokens);

            if (tok_status == TOK_ERROR)
            {
                log_debug("exec_frame_string_core: Tokenizer error on incomplete line %d",
                          session->line_num);
                const char *err = tokenizer_get_error(tokenizer);
                frame_set_error_printf(frame, "Tokenizer error: %s", err ? err : "unknown");
                token_list_destroy(&processed_tokens);
                return MIGA_EXEC_STATUS_ERROR;
            }

            if (tok_status == TOK_INCOMPLETE)
            {
                log_debug("exec_frame_string_core: Tokenizer incomplete (compound command) during lexer "
                          "incomplete at line %d",
                          session->line_num);
                /* Tokens are buffered in tokenizer, continue to next line */
                token_list_destroy(&processed_tokens);
                return MIGA_EXEC_STATUS_INCOMPLETE;
            }

            /* Accumulate these tokens for when the lexer completes */
            if (session->accumulated_tokens == NULL)
            {
                session->accumulated_tokens = processed_tokens;
            }
            else
            {
                if (token_list_append_list_move(session->accumulated_tokens, &processed_tokens) !=
                    0)
                {
                    log_debug("exec_frame_string_core: Failed to append incomplete tokens");
                    return MIGA_EXEC_STATUS_ERROR;
                }
            }
        }
        else
        {
            token_list_destroy(&raw_tokens);
        }
        return MIGA_EXEC_STATUS_INCOMPLETE;
    }

    log_debug("exec_frame_string_core: Lexer produced %d raw tokens at line %d",
              token_list_size(raw_tokens), session->line_num);

    token_list_t *processed_tokens = token_list_create();
    tok_status_t tok_status = tokenizer_process(tokenizer, raw_tokens, processed_tokens);
    token_list_destroy(&raw_tokens);

    if (tok_status == TOK_ERROR)
    {
        log_debug("exec_frame_string_core: Tokenizer error at line %d", session->line_num);
        const char *err = tokenizer_get_error(tokenizer);
        frame_set_error_printf(frame, "Tokenizer error: %s", err ? err : "unknown");
        token_list_destroy(&processed_tokens);
        return MIGA_EXEC_STATUS_ERROR;
    }

    if (tok_status == TOK_INCOMPLETE)
    {
        log_debug(
            "exec_frame_string_core: Tokenizer incomplete (compound command) at line %d, tokens buffered",
            session->line_num);
        /* Tokenizer is buffering tokens for an incomplete compound command.
         * The processed_tokens list will be empty - tokens are held in the tokenizer's buffer.
         * Continue reading more input. */
        token_list_destroy(&processed_tokens);
        return MIGA_EXEC_STATUS_INCOMPLETE;
    }

    if (token_list_size(processed_tokens) == 0)
    {
        log_debug("exec_frame_string_core: No tokens after processing at line %d", session->line_num);
        token_list_destroy(&processed_tokens);
        return MIGA_EXEC_STATUS_EMPTY;
    }

    /* Accumulate tokens if we had an incomplete parse previously */
    if (session->accumulated_tokens)
    {
        log_debug("exec_frame_string_core: Appending %d new tokens to %d accumulated tokens",
                  token_list_size(processed_tokens), token_list_size(session->accumulated_tokens));

        /* Move all tokens from processed_tokens to accumulated_tokens */
        if (token_list_append_list_move(session->accumulated_tokens, &processed_tokens) != 0)
        {
            log_debug("exec_frame_string_core: Failed to append tokens");
            return MIGA_EXEC_STATUS_ERROR;
        }

        /* Use the accumulated list for parsing */
        processed_tokens = session->accumulated_tokens;
        session->accumulated_tokens = NULL;
    }

    log_debug("exec_frame_string_core: Tokenizer produced %d processed tokens at line %d",
              token_list_size(processed_tokens), session->line_num);

    /* Debug: print all tokens */
    for (int i = 0; i < token_list_size(processed_tokens); i++)
    {
        const token_t *t = token_list_get(processed_tokens, i);
        log_debug("  Token %d: type=%d, text='%s'", i, token_get_type(t),
                  string_cstr(token_to_string(t)));
    }

    parser_t *parser = parser_create_with_tokens_move(&processed_tokens);
    gnode_t *gnode = NULL;

    log_debug("exec_frame_string_core: Starting parse at line %d", session->line_num);
    parse_status_t parse_status = parser_parse_program(parser, &gnode);

    if (parse_status == PARSE_ERROR)
    {
        log_debug("exec_frame_string_core: Parse error at line %d", session->line_num);
        const char *err = parser_get_error(parser);
        if (err && err[0])
        {
            frame_set_error_printf(frame, "Parse error at line %d: %s", session->line_num, err);
        }
        else
        {
            /* Parser returned error but didn't set error message */
            token_t *curr_tok = token_clone(parser_current_token(parser));
            if (curr_tok)
            {
                string_t *tok_str = token_to_string(curr_tok);
                log_debug("exec_frame_string_core: Current token: type=%d, line=%d, col=%d, text='%s'",
                          token_get_type(curr_tok), token_get_first_line(curr_tok),
                          token_get_first_column(curr_tok), string_cstr(tok_str));
                frame_set_error_printf(frame, "Parse error at line %d, column %d near '%s'",
                                       token_get_first_line(curr_tok),
                                       token_get_first_column(curr_tok), string_cstr(tok_str));
                string_destroy(&tok_str);
                token_destroy(&curr_tok);
            }
            else
            {
                log_debug("exec_frame_string_core: No current token available");
                frame_set_error_printf(frame, "Parse error at line %d: no error details available",
                                       session->line_num);
            }
        }
        parser_destroy(&parser);
        return MIGA_EXEC_STATUS_ERROR;
    }

    if (parse_status == PARSE_INCOMPLETE)
    {
        log_debug("exec_frame_string_core: Parse incomplete at line %d, accumulating tokens",
                  session->line_num);
        if (gnode)
            g_node_destroy(&gnode);
        /* Clone token list from parser - respect silo boundary */
        session->accumulated_tokens = token_list_clone(parser->tokens);
        parser_destroy(&parser);
        return MIGA_EXEC_STATUS_INCOMPLETE;
    }

    if (parse_status == PARSE_EMPTY || !gnode)
    {
        log_debug("exec_frame_string_core: Parse empty at line %d", session->line_num);
        parser_destroy(&parser);
        return MIGA_EXEC_STATUS_EMPTY;
    }

    ast_node_t *ast = ast_lower(gnode);
    g_node_destroy(&gnode);
    parser_destroy(&parser);

    if (!ast)
    {
        return MIGA_EXEC_STATUS_EMPTY;
    }

    /* Execute via the dispatch function */
    exec_frame_execute_result_t result = exec_frame_execute_dispatch(frame, ast);

    /* Update frame's exit status */
    if (result.has_exit_status)
    {
        frame->last_exit_status = result.exit_status;
        executor->last_exit_status = result.exit_status;
        executor->last_exit_status_set = true;
    }

    ast_node_destroy(&ast);

    if (result.status == MIGA_EXEC_STATUS_ERROR)
    {
        return MIGA_EXEC_STATUS_ERROR;
    }

    /* Reset context after successful execution */
    parse_session_reset(session);

    return MIGA_EXEC_STATUS_OK;
}

/**
 * Core implementation for executing shell commands from a stream.
 *
 * Reads lines from fp and feeds them to exec_frame_string_core one at a time,
 * handling lines longer than the read buffer by accumulating chunks.
 */
miga_exec_status_t exec_frame_stream_core(miga_frame_t *frame, FILE *fp, parse_session_t *session)
{
    Expects_not_null(frame);
    Expects_not_null(fp);
    Expects_not_null(session);
    Expects_not_null(session->lexer);
    Expects_not_null(session->tokenizer);
    Expects_not_null(frame->executor);

    miga_exec_status_t final_status = MIGA_EXEC_STATUS_OK;

    /* Read a single logical line of any size, efficiently */
#define LINE_CHUNK_SIZE 4096
    char chunk[LINE_CHUNK_SIZE];
    char *line_buf = NULL;
    size_t line_buf_size = 0;
    size_t line_len = 0;

    while (fgets(chunk, sizeof(chunk), fp) != NULL)
    {
        size_t chunk_len = strlen(chunk);

        if (chunk_len > 0 && chunk[chunk_len - 1] == '\n')
        {
            if (line_len == 0)
            {
                /* Fast path: entire line fits in chunk */
                miga_exec_status_t status = exec_frame_string_core(frame, chunk, session);
                if (session->line_num > 0)
                    frame->source_line = session->line_num;

                switch (status)
                {
                case MIGA_EXEC_STATUS_OK:
                case MIGA_EXEC_STATUS_EMPTY:
                    final_status = MIGA_EXEC_STATUS_OK;
                    break;
                case MIGA_EXEC_STATUS_INCOMPLETE:
                    final_status = MIGA_EXEC_STATUS_INCOMPLETE;
                    break;
                case MIGA_EXEC_STATUS_ERROR:
                    final_status = MIGA_EXEC_STATUS_ERROR;
                    break;
                }
                return final_status;
            }
            else
            {
                /* Append final chunk to accumulated buffer */
                if (line_len + chunk_len + 1 > line_buf_size)
                {
                    line_buf_size = (line_len + chunk_len + 1) * 2;
                    line_buf = (char *)xrealloc(line_buf, line_buf_size);
                }
                memcpy(line_buf + line_len, chunk, chunk_len);
                line_len += chunk_len;
                line_buf[line_len] = '\0';
                break; /* Line complete — fall through to process it */
            }
        }
        else
        {
            /* No newline yet — accumulate */
            if (line_buf == NULL)
            {
                line_buf_size = (chunk_len + 1) * 2;
                line_buf = (char *)xmalloc(line_buf_size);
                memcpy(line_buf, chunk, chunk_len);
                line_len = chunk_len;
                line_buf[line_len] = '\0';
            }
            else
            {
                if (line_len + chunk_len + 1 > line_buf_size)
                {
                    line_buf_size = (line_len + chunk_len + 1) * 2;
                    line_buf = (char *)xrealloc(line_buf, line_buf_size);
                }
                memcpy(line_buf + line_len, chunk, chunk_len);
                line_len += chunk_len;
                line_buf[line_len] = '\0';
            }
        }
    }

    /* Process whatever we accumulated (may be empty on pure EOF) */
    if (line_len > 0)
    {
        miga_exec_status_t status = exec_frame_string_core(frame, line_buf, session);
        if (session->line_num > 0)
            frame->source_line = session->line_num;

        switch (status)
        {
        case MIGA_EXEC_STATUS_OK:
        case MIGA_EXEC_STATUS_EMPTY:
            final_status = MIGA_EXEC_STATUS_OK;
            break;
        case MIGA_EXEC_STATUS_INCOMPLETE:
            final_status = MIGA_EXEC_STATUS_INCOMPLETE;
            break;
        case MIGA_EXEC_STATUS_ERROR:
            final_status = MIGA_EXEC_STATUS_ERROR;
            break;
        }
    }

    if (line_buf)
        xfree(line_buf);

    return final_status;
}

#ifndef EXEC_FRAME_H
#define EXEC_FRAME_H

/**
 * exec_frame.h - Execution frame structure and management API
 *
 * This header defines:
 * - exec_frame_t: The execution frame structure
 * - exec_params_t: Parameters for frame creation/execution
 * - exec_result_t: Result of frame execution
 * - Frame management functions (push, pop, exec_in_frame)
 * - Convenience wrappers for common frame types
 */

#include <stdbool.h>
#ifdef POSIX_API
#include <sys/types.h>
#endif

#include "string_t.h"
#include "string_list.h"
#include "ast.h"
#include "variable_store.h"
#include "fd_table.h"
#include "positional_params.h"
#include "alias_store.h"
#include "func_store.h"

#include "exec_frame_policy.h"

 /* Forward declarations */
typedef struct exec_t exec_t;
typedef struct exec_opt_flags_t exec_opt_flags_t;
typedef struct exec_redirections_t exec_redirections_t;
typedef struct trap_store_t trap_store_t;
typedef enum exec_status_t exec_status_t;

/* ============================================================================
 * Control Flow State
 * ============================================================================ */

/**
 * Control flow state after executing a frame or command.
 */
typedef enum exec_control_flow_t {
    EXEC_FLOW_NORMAL,   /* Normal execution */
    EXEC_FLOW_RETURN,   /* 'return' executed */
    EXEC_FLOW_BREAK,    /* 'break' executed */
    EXEC_FLOW_CONTINUE  /* 'continue' executed */
} exec_control_flow_t;

/* ============================================================================
 * Execution Frame Structure
 * ============================================================================ */

 /**
  * An execution frame represents a single execution context in the shell.
  * Frames form a stack, with each frame potentially sharing or owning
  * various pieces of state (variables, file descriptors, traps, etc.)
  * based on its policy.
  */
typedef struct exec_frame_t {
    /* Frame identity */
    exec_frame_type_t type;
    const exec_frame_policy_t* policy;

    /* Parent frame (NULL for top-level) */
    struct exec_frame_t* parent;

    /* Back-reference to executor. Canonically, frames are owned by the executor. */
    exec_t *executor;

    /* -------------------------------------------------------------------------
     * Scope-dependent storage
     * -------------------------------------------------------------------------
     * Ownership depends on the frame's policy:
     * - EXEC_SCOPE_SHARE: Points to parent's instance, must NOT be freed
     * - EXEC_SCOPE_OWN/COPY: Owned by this frame, must be freed on pop
     */
    variable_store_t *variables;
    variable_store_t *saved_variables;
    variable_store_t *local_variables;  /* Only for frames with has_locals=true */
    positional_params_t* positional_params;
    positional_params_t* saved_positional_params;  /* For dot script override restore */
    func_store_t* functions;
    alias_store_t* aliases;
    fd_table_t* open_fds;
    trap_store_t* traps;
    exec_opt_flags_t* opt_flags;
    string_t* working_directory;
#ifdef POSIX_API
    mode_t *umask;
#else
    int *umask;
#endif

    /* -------------------------------------------------------------------------
     * Frame-local state (always owned by this frame)
     * -------------------------------------------------------------------------
     */
    int loop_depth;         /* 0 if not in loop, else depth of nested loops */
    int last_exit_status;   /* $? */
    int last_bg_pid;        /* $! */

    /* Control flow state (set by builtins like return, break, continue) */
    exec_control_flow_t pending_control_flow;
    int pending_flow_depth;  /* For 'break N' / 'continue N' */

    /* Source tracking */
    string_t* source_name;  /* $BASH_SOURCE / script name */
    int source_line;        /* $LINENO */

    /* Trap handler state */
    bool in_trap_handler;   /* Prevents recursive trap handling */
} exec_frame_t;

/* ============================================================================
 * Execution Parameters
 * ============================================================================ */

 /**
  * Parameters passed when creating/executing a frame.
  * Different fields are used depending on the frame type.
  */
typedef struct exec_params_t
{
    /* Body to execute */
    const ast_node_t *body;

    /* Redirections to apply */
    const exec_redirections_t *redirections;

    /* For functions and dot scripts: arguments to set $1, $2, ... */
    string_list_t *arguments;

    /* For dot scripts: the script path (for $0 and source tracking) */
    string_t *script_path;

    /* For loops */
    const ast_node_t *condition;    /* while/until condition */
    bool until_mode;                /* true for until, false for while */
    string_list_t *iteration_words; /* for loop word list */
    string_t *loop_var_name;        /* for loop variable */

    /* For pipelines (EXEC_FRAME_PIPELINE) */
    ast_node_list_t *pipeline_commands; /* List of commands in pipeline */
    bool pipeline_negated;              /* true for ! pipeline */

    /* For pipeline commands (EXEC_FRAME_PIPELINE_CMD) */
#ifdef POSIX_API
    pid_t pipeline_pgid; /* Process group to join (0 = create new) */
#else
    int pipeline_pgid;
#endif
    int stdin_pipe_fd;      /* -1 if not piped, else fd to dup2 to stdin */
    int stdout_pipe_fd;     /* -1 if not piped, else fd to dup2 to stdout */
    int *pipe_fds_to_close; /* Array of all pipe FDs to close */
    int pipe_fds_count;     /* Count of pipe FDs to close */

    /* For background jobs / debugging */
    string_list_t *command_args; /* Original command text for job display */

    /* Source location */
    int source_line;
} exec_params_t;

/* ============================================================================
 * Execution Result
 * ============================================================================ */

/**
 * Result of executing a frame or command.
 */
typedef struct exec_result_t {
    exec_status_t status;       /* Execution status (EXEC_OK, EXEC_ERROR, etc.) */
    int exit_status;            /* The exit status ($?) */
    bool has_exit_status;       /* Whether exit_status is valid */
    exec_control_flow_t flow;   /* Control flow state */
    int flow_depth;             /* For 'break N' / 'continue N' */
} exec_result_t;

/* ============================================================================
 * Frame Management API
 * ============================================================================ */

exec_frame_t *exec_frame_create_top_level(exec_t *exec);

/**
 * Push a new frame onto the stack.
 * Initializes all scope-dependent storage according to the frame's policy.
 *
 * @param parent  The parent frame (NULL only for top-level)
 * @param type    The type of frame to create
 * @param exec    The executor state
 * @param params  Optional parameters for frame initialization
 * @return        The newly created frame
 */
exec_frame_t* exec_frame_push(exec_frame_t* parent, exec_frame_type_t type,
    exec_t* exec, exec_params_t* params);

/**
 * Pop a frame from the stack.
 * Runs EXIT trap if applicable, cleans up owned resources, returns parent.
 *
 * @param frame_ptr  Pointer to the frame to pop (set to NULL on return)
 * @return           The parent frame
 */
exec_frame_t* exec_frame_pop(exec_frame_t** frame_ptr);

/**
 * Main entry point: create a frame, execute it, and clean up.
 * Handles forking if required by the frame's policy.
 *
 * @param parent  The parent frame
 * @param type    The type of frame to create
 * @param params  Parameters for frame creation and execution
 * @return        The result of execution
 */
exec_result_t exec_in_frame(exec_frame_t* parent, exec_frame_type_t type,
    exec_params_t* params);

/* ============================================================================
 * Convenience Wrappers
 * ============================================================================
 * These wrap exec_in_frame() with appropriate frame types and params.
 */

exec_result_t exec_subshell(exec_frame_t* parent,
const ast_node_t* body);

exec_result_t exec_brace_group(exec_frame_t* parent,
const ast_node_t* body,
const exec_redirections_t* redirections);

exec_result_t exec_function(exec_frame_t* parent,
const ast_node_t* body,
string_list_t* arguments,
const exec_redirections_t* redirections);

exec_result_t exec_for_loop(exec_frame_t* parent,
string_t* var_name, string_list_t* words,
const ast_node_t* body);

exec_result_t exec_while_loop(exec_frame_t* parent,
const ast_node_t* condition, const ast_node_t* body,
bool until_mode);

exec_result_t exec_dot_script(exec_frame_t* parent,
string_t* script_path, const ast_node_t* body,
string_list_t* arguments);

exec_result_t exec_trap_handler(exec_frame_t* parent,
const ast_node_t* body);

exec_result_t exec_background_job(exec_frame_t* parent,
const ast_node_t* body,
string_list_t* command_args);

exec_result_t exec_pipeline_group(exec_frame_t *parent, ast_node_list_t *commands, bool negated);

#ifdef POSIX_API
exec_result_t exec_pipeline_cmd(exec_frame_t* parent,
    const ast_node_t* body,
    pid_t pipeline_pgid);
#else
exec_result_t exec_pipeline_cmd(exec_frame_t* parent,
    const ast_node_t* body,
    int pipeline_pgid);
#endif

exec_result_t exec_eval(exec_frame_t* parent,
const ast_node_t* body);

/* ============================================================================
 * Frame Query Functions
 * ============================================================================ */

 /**
  * Find the nearest ancestor frame that is a return target.
  * Returns NULL if no return target exists (return is invalid).
  */
exec_frame_t* exec_frame_find_return_target(exec_frame_t* frame);

/**
 * Find the nearest ancestor frame that is a loop.
 * Returns NULL if not inside a loop (break/continue is invalid).
 */
exec_frame_t* exec_frame_find_loop(exec_frame_t* frame);

/**
 * Get the effective variable store for this frame.
 * (Walks up SHARE chain if necessary.)
 */
variable_store_t* exec_frame_get_variables(exec_frame_t* frame);

/**
 * Get the effective fd table for this frame.
 */
fd_table_t* exec_frame_get_fds(exec_frame_t* frame);

/**
 * Get the effective trap store for this frame.
 */
trap_store_t* exec_frame_get_traps(exec_frame_t* frame);

/* ============================================================================
 * Variable Access Helpers
 * ============================================================================ */

 /**
  * Get variable value, checking local store first if applicable.
  */
const string_t* exec_frame_get_variable(const exec_frame_t* frame, const string_t* name);

/**
 * Set variable, respecting local scope if applicable.
 */
void exec_frame_set_variable(exec_frame_t* frame, const string_t* name,
    const string_t* value);

/**
 * Declare a local variable (only valid in frames with has_locals=true).
 * Returns 0 on success, -1 if locals not supported in this frame.
 */
int exec_frame_declare_local(exec_frame_t* frame, const string_t* name,
    const string_t* value);

#endif /* EXEC_FRAME_H */

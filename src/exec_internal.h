#ifndef EXEC_INTERNAL_H
#define EXEC_INTERNAL_H

/**
 * exec_internal.h - Executor internals
 *
 * This header defines:
 * - exec_t: The executor singleton state (full definition)
 * - Redirection structures
 * - Internal execution functions
 *
 * For public API, see exec.h
 * For frame-specific definitions, see exec_frame.h
 * For policy definitions, see exec_frame_policy.h
 */

#include <stdbool.h>
#ifdef POSIX_API
#include <sys/resource.h>
#include <sys/types.h>
#endif

#include "alias_store.h"
#include "ast.h"
#include "fd_table.h"
#include "func_store.h"
#include "job_store.h"
#include "positional_params.h"
#include "sig_act.h"
#include "string_list.h"
#include "string_t.h"
#include "trap_store.h"
#include "variable_store.h"

/* Include public API for exec_opt_flags_t and exec_status_t */
#include "exec.h"

/* Include frame definitions */
#include "exec_frame.h"

/* ============================================================================
 * Redirection Structures
 * ============================================================================ */

/**
 * Types of redirection operations.
 */

 /* redirection_type_t is recycled from the one in ast.h */

/**
 * How the target of a redirection is specified.
 */

/* redir_target_kind_t is recycled from the one in ast.h */

/**
 * Runtime representation of a single redirection.
 */
typedef struct exec_redirection_t
{
    redirection_type_t type;
    int explicit_fd;     /* [n] prefix, or -1 for default */
    bool is_io_location; /* POSIX 2024 {varname} syntax */
    string_t *io_location_varname;
    redir_target_kind_t target_kind;

    union {
        /* REDIR_TARGET_FILE */
        struct
        {
            bool is_expanded;
            string_t *filename; /* Expanded filename */
            token_t *tok; /* Original parts for expansion (if not yet expanded) */
        } file;

        /* REDIR_TARGET_FD */
        struct
        {
            int fixed_fd;            /* Literal fd number, or -1 */
            string_t *fd_expression; /* If fd comes from expansion */
        } fd;

        /* REDIR_TARGET_HEREDOC */
        struct
        {
            string_t *content;    /* The heredoc content */
            bool needs_expansion; /* false if delimiter was quoted */
        } heredoc;

        /* REDIR_TARGET_IO_LOCATION */
        struct
        {
            string_t *raw_filename; /* For {var}>file */
            int fixed_fd;           /* For {var}>&N */
        } io_location;
    } target;

    int source_line; /* For error messages */
} exec_redirection_t;

/**
 * Dynamic array of runtime redirections.
 */
typedef struct exec_redirections_t
{
    exec_redirection_t *items;
    size_t count;
    size_t capacity;
} exec_redirections_t;

exec_redirections_t *exec_redirections_create(void);
void exec_redirections_destroy(exec_redirections_t **redirections);
void exec_redirections_append(exec_redirections_t *redirections, exec_redirection_t *redir);


/* ============================================================================
 * Redirection Handling
 * ============================================================================ */

/**
 * Apply redirections to the current frame.
 * Saves original fd state for later restoration.
 * (Implemented in exec_redirect.c)
 *
 * @return 0 on success, -1 on error
 */
int exec_frame_apply_redirections(exec_frame_t *frame, const exec_redirections_t *redirections);

/**
 * Restore file descriptors to their state before redirections were applied.
 */
void exec_restore_redirections(exec_frame_t *frame, const exec_redirections_t *redirections);

/* ============================================================================
 * Command Execution
 * ============================================================================ */

/**
 * Execute a compound list (sequence of and-or lists).
 */
exec_result_t exec_compound_list(exec_frame_t *frame, const ast_node_t *list);

/**
 * Execute an and-or list (cmd1 && cmd2 || cmd3).
 */
exec_result_t exec_and_or_list(exec_frame_t *frame, ast_node_t *list);

/**
 * Execute a pipeline (cmd1 | cmd2 | cmd3).
 */
exec_result_t exec_pipeline(exec_frame_t *frame, ast_node_t *pipeline);

/**
 * Execute a simple command (assignments, redirections, command word).
 */
exec_result_t exec_simple_command(exec_frame_t *frame, ast_node_t *cmd);

/**
 * Dispatch execution based on AST node type.
 */
exec_result_t exec_execute_dispatch(exec_frame_t *frame, const ast_node_t *node);

/**
 * Execute an if clause.
 */
exec_result_t exec_if_clause(exec_frame_t *frame, ast_node_t *node);

/**
 * Execute a while or until clause.
 */
exec_result_t exec_while_clause(exec_frame_t *frame, ast_node_t *node);

/**
 * Execute a for clause.
 */
exec_result_t exec_for_clause(exec_frame_t *frame, ast_node_t *node);

/**
 * Execute a function definition clause.
 */
exec_result_t exec_function_def_clause(exec_frame_t *frame, ast_node_t *node);

/**
 * Execute a redirected command.
 */
exec_result_t exec_redirected_command(exec_frame_t *frame, ast_node_t *node);

/**
 * Execute a case clause.
 */
exec_result_t exec_case_clause(exec_frame_t *frame, ast_node_t *node);

/**
 * Execute an external command (fork/exec).
 */
exec_result_t exec_external_command(exec_frame_t *frame, string_list_t *argv,
                                    exec_redirections_t *redirections);

/**
 * Execute a builtin command.
 */
exec_result_t exec_builtin_command(exec_frame_t *frame, const string_t *name, string_list_t *argv);

/* ============================================================================
 * Subshell Management
 * ============================================================================ */

/**
 * Create a subshell executor from a parent executor.
 * This sets up appropriate state copying for a true subshell context.
 */
exec_t *exec_create_subshell(exec_t *executor);

/* ============================================================================
 * Loop Execution
 * ============================================================================ */

/**
 * Execute a while/until loop.
 */
exec_result_t exec_condition_loop(exec_frame_t *frame, exec_params_t *params);

/**
 * Execute a for loop.
 */
exec_result_t exec_iteration_loop(exec_frame_t *frame, exec_params_t *params);

/**
 * Orchestrate pipeline execution (called from EXEC_FRAME_PIPELINE).
 */
exec_result_t exec_pipeline_orchestrate(exec_frame_t *frame, exec_params_t *params);

/* ============================================================================
 * Special Parameter Access
 * ============================================================================ */

/**
 * Get the value of a special parameter ($?, $$, $!, $#, $@, $*, $0, $1, ...).
 * Returns NULL if not a special parameter.
 */
string_t *exec_get_special_param(exec_frame_t *frame, const string_t *name);

/**
 * Check if a name is a special parameter.
 */
bool exec_is_special_param(const string_t *name);

#endif /* EXEC_INTERNAL_H */

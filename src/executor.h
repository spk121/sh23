#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "ast.h"
#include "string_t.h"
#include <stdbool.h>

/* ============================================================================
 * Executor Status (return codes)
 * ============================================================================ */

typedef enum
{
    EXEC_OK = 0,        // successful execution
    EXEC_ERROR,         // error during execution
    EXEC_NOT_IMPL,      // feature not yet implemented
} exec_status_t;

/* ============================================================================
 * Executor Context
 * ============================================================================ */

typedef struct executor_t
{
    /* Exit status from last command */
    int last_exit_status;

    /* Error reporting */
    string_t *error_msg;

    /* Execution state */
    bool dry_run; // if true, don't actually execute, just validate

} executor_t;

/* ============================================================================
 * Executor Lifecycle Functions
 * ============================================================================ */

/**
 * Create a new executor.
 */
executor_t *executor_create(void);

/**
 * Destroy an executor and free all associated memory.
 * Safe to call with NULL.
 */
void executor_destroy(executor_t **executor);

/* ============================================================================
 * Execution Functions
 * ============================================================================ */

/**
 * Execute an AST.
 * 
 * @param executor The executor context
 * @param root The root AST node to execute
 * 
 * @return EXEC_OK on success, EXEC_ERROR on error, EXEC_NOT_IMPL for unsupported node types
 */
exec_status_t executor_execute(executor_t *executor, const ast_node_t *root);

/**
 * Execute a command list.
 */
exec_status_t executor_execute_command_list(executor_t *executor, const ast_node_t *node);

/**
 * Execute an and/or list.
 */
exec_status_t executor_execute_andor_list(executor_t *executor, const ast_node_t *node);

/**
 * Execute a pipeline.
 */
exec_status_t executor_execute_pipeline(executor_t *executor, const ast_node_t *node);

/**
 * Execute a simple command.
 */
exec_status_t executor_execute_simple_command(executor_t *executor, const ast_node_t *node);

/**
 * Execute a redirected command wrapper.
 */
exec_status_t executor_execute_redirected_command(executor_t *executor, const ast_node_t *node);

/**
 * Execute an if clause.
 */
exec_status_t executor_execute_if_clause(executor_t *executor, const ast_node_t *node);

/**
 * Execute a while clause.
 */
exec_status_t executor_execute_while_clause(executor_t *executor, const ast_node_t *node);

/**
 * Execute an until clause.
 */
exec_status_t executor_execute_until_clause(executor_t *executor, const ast_node_t *node);

/**
 * Execute a for clause.
 */
exec_status_t executor_execute_for_clause(executor_t *executor, const ast_node_t *node);

/**
 * Execute a case clause.
 */
exec_status_t executor_execute_case_clause(executor_t *executor, const ast_node_t *node);

/**
 * Execute a subshell.
 */
exec_status_t executor_execute_subshell(executor_t *executor, const ast_node_t *node);

/**
 * Execute a brace group.
 */
exec_status_t executor_execute_brace_group(executor_t *executor, const ast_node_t *node);

/**
 * Execute a function definition.
 */
exec_status_t executor_execute_function_def(executor_t *executor, const ast_node_t *node);

/* ============================================================================
 * Visitor Pattern Support
 * ============================================================================ */

/**
 * Visitor callback function type.
 * Returns true to continue traversal, false to stop.
 */
typedef bool (*ast_visitor_fn)(const ast_node_t *node, void *user_data);

/**
 * Traverse an AST in pre-order, calling the visitor function for each node.
 * 
 * @param root The root node to start traversal from
 * @param visitor The visitor function to call for each node
 * @param user_data User data to pass to the visitor function
 * 
 * @return true if traversal completed, false if stopped early
 */
bool ast_traverse(const ast_node_t *root, ast_visitor_fn visitor, void *user_data);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Get the last exit status.
 */
int executor_get_exit_status(const executor_t *executor);

/**
 * Set the exit status.
 */
void executor_set_exit_status(executor_t *executor, int status);

/**
 * Get the error message from the last failed operation.
 * Returns NULL if no error.
 */
const char *executor_get_error(const executor_t *executor);

/**
 * Set an error message.
 */
void executor_set_error(executor_t *executor, const char *format, ...);

/**
 * Clear the error state.
 */
void executor_clear_error(executor_t *executor);

/**
 * Enable or disable dry-run mode.
 * In dry-run mode, commands are validated but not executed.
 */
void executor_set_dry_run(executor_t *executor, bool dry_run);

#endif /* EXECUTOR_H */

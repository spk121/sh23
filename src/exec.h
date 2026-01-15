#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <stdio.h>
#include "alias_store.h"
#include "ast.h"
#include "exec_command.h"
#include "exec_internal.h"
#include "expander.h"
#include "positional_params.h"
#include "string_t.h"
#include "variable_store.h"

#ifdef POSIX_API
#include <sys/types.h>
#include <sys/resource.h>
#endif
#include "func_store.h"
#include "sig_act.h"
#include "job_store.h"
#if defined(POSIX_API) || defined(UCRT_API)
#include "fd_table.h"
#endif
#include "trap_store.h"
#include <stdbool.h>



/* ============================================================================
 * Executor Context
 * ============================================================================ */





typedef struct
{
    // Start-up environment
    int argc;
    char *const *argv;
    char *const *envp;

    // Flags
    exec_opt_flags_t opt;
} exec_cfg_t;

/* ============================================================================
 * Executor Lifecycle Functions
 * ============================================================================ */

/**
 * Create a new executor.
 */
exec_t *exec_create_from_cfg(exec_cfg_t *cfg);
exec_t *exec_create_subshell(exec_t *parent);

/**
 * Destroy an executor and free all associated memory.
 * Safe to call with nullptr.
 */
void exec_destroy(exec_t **executor);

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
exec_status_t exec_execute(exec_t *executor, const ast_node_t *root);

/**
 * Execute commands from a stream (file or stdin).
 * Reads lines from the stream, parses them, and executes them.
 *
 * @param executor The executor context
 * @param fp The file stream to read from
 * @return EXEC_OK on success, EXEC_ERROR on error
 */
exec_status_t exec_execute_stream(exec_t *executor, FILE *fp);

/**
 * Execute a command list.
 */
exec_status_t exec_execute_command_list(exec_t *executor, const ast_node_t *node);

/**
 * Execute an and/or list.
 */
exec_status_t exec_execute_andor_list(exec_t *executor, const ast_node_t *node);

/**
 * Execute a pipeline.
 */
exec_status_t exec_execute_pipeline(exec_t *executor, const ast_node_t *node);

/**
 * Execute a simple command.
 */
exec_status_t exec_execute_simple_command(exec_t *executor, const ast_node_t *node);

/**
 * Execute a redirected command wrapper.
 */
exec_status_t exec_execute_redirected_command(exec_t *executor, const ast_node_t *node);

/**
 * Execute an if clause.
 */
exec_status_t exec_execute_if_clause(exec_t *executor, const ast_node_t *node);

/**
 * Execute a while clause.
 */
exec_status_t exec_execute_while_clause(exec_t *executor, const ast_node_t *node);

/**
 * Execute an until clause.
 */
exec_status_t exec_execute_until_clause(exec_t *executor, const ast_node_t *node);

/**
 * Execute a for clause.
 */
exec_status_t exec_execute_for_clause(exec_t *executor, const ast_node_t *node);

/**
 * Execute a case clause.
 */
exec_status_t exec_execute_case_clause(exec_t *executor, const ast_node_t *node);

/**
 * Execute a subshell.
 */
exec_status_t exec_execute_subshell(exec_t *executor, const ast_node_t *node);

/**
 * Execute a brace group.
 */
exec_status_t exec_execute_brace_group(exec_t *executor, const ast_node_t *node);

/**
 * Execute a function definition.
 */
exec_status_t exec_execute_function_def(exec_t *executor, const ast_node_t *node);

void exec_reap_background_jobs(exec_t *executor);

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
int exec_get_exit_status(const exec_t *executor);

/**
 * Set the exit status.
 */
void exec_set_exit_status(exec_t *executor, int status);

/**
 * Get the error message from the last failed operation.
 * Returns NULL if no error.
 */
const char *exec_get_error(const exec_t *executor);

/**
 * Set an error message.
 */
void exec_set_error(exec_t *executor, const char *format, ...);

/**
 * Clear the error state.
 */
void exec_clear_error(exec_t *executor);

/**
 * Get the PS1 prompt string.
 * Returns the value of the PS1 variable from the variable store,
 * or a default prompt if PS1 is not set.
 *
 * @param executor The executor context
 * @return The PS1 prompt string (never NULL)
 */
const char *exec_get_ps1(const exec_t *executor);

/**
 * Get the PS2 prompt string.
 * Returns the value of the PS2 variable from the variable store,
 * or a default prompt if PS2 is not set.
 *
 * @param executor The executor context
 * @return The PS2 prompt string (never NULL)
 */
const char *exec_get_ps2(const exec_t *executor);

/* ============================================================================
 * Expander Callbacks
 * ============================================================================ */

/**
 * Command substitution callback for the expander.
 * Executes a command and returns its output.
 *
 * @param command The command string to execute
 * @param userdata Pointer to the executor context
 * @param command The command to execute
 * @return The output of the command as a newly allocated string_t (caller must free),
 *         or NULL on error
 */
string_t *exec_command_subst_callback(void *userdata, const string_t *command);

/**
 * Pathname expansion (glob) callback for the expander.
 * Platform behavior:
 * - POSIX_API: uses POSIX glob() for pathname expansion.
 * - UCRT_API: uses _findfirst/_findnext from <io.h> for wildcard matching.
 * - ISO_C: no implementation; returns NULL so the expander preserves the literal.
 *
 * @param pattern The glob pattern to expand
 * @param user_data Pointer to the shell_t context (opaque to this function)
 * @param pattern The glob pattern to expand
 * @return On success with matches: a newly allocated list of filenames
 *         (caller must free with string_list_destroy). On no matches or error:
 *         returns NULL, signaling the expander to leave the pattern unexpanded.
 */
string_list_t *exec_pathname_expansion_callback(void *user_data, const string_t *pattern);

#endif


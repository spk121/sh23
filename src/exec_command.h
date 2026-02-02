#pragma once

#include "exec_internal.h"
#include "string_list.h"

/* Forward declarations */
struct ast_node_t;
struct expander_t;

/* ============================================================================
 * Simple Command Execution
 * ============================================================================ */

/**
 * Execute a simple command.
 *
 * Handles the complete simple command execution sequence:
 * - Assignment-only commands
 * - Variable expansion and assignment prefix handling
 * - Redirection setup and teardown
 * - Command lookup (special builtins, functions, regular builtins, external commands)
 * - External command execution (fork/exec, spawn, or system)
 */
exec_status_t exec_execute_simple_command(exec_frame_t *frame, const struct ast_node_t *node);

/**
 * Execute a function definition.
 * Stores the function in the function store.
 */
exec_status_t exec_execute_function_def(exec_t *executor, const struct ast_node_t *node);

/**
 * Execute a redirected command wrapper.
 * Applies redirections and executes the inner command.
 */
exec_status_t exec_execute_redirected_command(exec_frame_t *frame, const struct ast_node_t *node);
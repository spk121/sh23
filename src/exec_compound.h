#pragma once

#include "exec_internal.h"

/* Forward declaration */
struct ast_node_t;

/* ============================================================================
 * Compound Command Execution Functions
 * ============================================================================ */

/**
 * Execute a command list (sequential and/or background commands).
 */
exec_status_t exec_execute_command_list(exec_t *executor, const struct ast_node_t *node);

/**
 * Execute an AND/OR list (&&, ||).
 */
exec_status_t exec_execute_andor_list(exec_t *executor, const struct ast_node_t *node);

/**
 * Execute a pipeline (|).
 */
exec_status_t exec_execute_pipeline(exec_t *executor, const struct ast_node_t *node);

/**
 * Execute a subshell ( ).
 */
exec_status_t exec_execute_subshell(exec_t *executor, const struct ast_node_t *node);

/**
 * Execute a brace group { }.
 */
exec_status_t exec_execute_brace_group(exec_t *executor, const struct ast_node_t *node);

/* ============================================================================
 * Background Job Management
 * ============================================================================ */

/**
 * Check for and reap any completed background jobs.
 * Prints notifications for completed jobs in interactive mode.
 *
 * @param executor  The executor context
 */
void exec_reap_background_jobs(exec_t *executor);
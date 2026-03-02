#pragma once

#include "exec_frame.h"

/* Forward declarations */
struct ast_node_t;

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

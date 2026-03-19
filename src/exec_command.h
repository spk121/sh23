#ifndef EXEC_COMMAND_H
#define EXEC_COMMAND_H

/**
 * exec_command.h - Simple command execution for miga
 *
 * This file declares the interface for executing simple commands within an
 * execution frame. It handles assignment-only commands, variable expansion,
 * redirection setup, command lookup, and external command execution.
 *
 * Simple commands are so complicated, they get their own module, lol.
 */

#include "ast.h"
#include "exec_types_internal.h"
#include "miga/type_pub.h"

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
exec_frame_execute_result_t exec_frame_execute_simple_command_impl(miga_frame_t *frame,
                                                                   const struct ast_node_t *node);

#endif /* EXEC_COMMAND_H */

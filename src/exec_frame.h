#ifndef EXEC_FRAME_H
#define EXEC_FRAME_H

/**
 * exec_frame.h - Execution frame structure and management API
 *
 * This header defines:
 * - miga_frame_t: The execution frame structure
 * - exec_params_t: Parameters for frame creation/execution
 * - exec_frame_execute_result_t: Result of frame execution
 * - Frame management functions (push, pop, exec_in_frame)
 * - Convenience wrappers for common frame types
 */

#ifdef MIGA_POSIX_API
#define _POSIX_C_SOURCE 202405L
#endif

#ifdef MIGA_POSIX_API
#include <sys/types.h>
#endif

#include <stdio.h>

#include "ast.h"
#include "exec_types_internal.h"
#include "miga/type_pub.h"
#include "fd_table.h"
#include "miga/strlist.h"
#include "miga/string_t.h"
#include "trap_store.h"
#include "variable_store.h"
#include "parse_session.h"

#include "exec_frame_policy.h"

/* Forward declarations */
typedef struct miga_exec_t miga_exec_t;
typedef struct exec_opt_flags_t exec_opt_flags_t;
typedef struct exec_redirections_t exec_redirections_t;
typedef struct trap_store_t trap_store_t;

/* ============================================================================
 * Frame Management API
 * ============================================================================ */

miga_frame_t *exec_frame_create_top_level(miga_exec_t *exec);

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
miga_frame_t *exec_frame_push(miga_frame_t *parent, exec_frame_type_t type, miga_exec_t *exec,
                              exec_params_t *params);

/**
 * Pop a frame from the stack.
 * Runs EXIT trap if applicable, cleans up owned resources, returns parent.
 *
 * @param frame_ptr  Pointer to the frame to pop (set to NULL on return)
 * @return           The parent frame
 */
miga_frame_t *exec_frame_pop(miga_frame_t **frame_ptr);

/**
 * Main entry point: create a frame, execute it, and clean up.
 * Handles forking if required by the frame's policy.
 *
 * @param parent  The parent frame
 * @param type    The type of frame to create
 * @param params  Parameters for frame creation and execution
 * @return        The result of execution
 */
struct exec_frame_execute_result_t exec_in_frame(miga_frame_t *parent, exec_frame_type_t type,
                                                 exec_params_t *params);

struct exec_frame_execute_result_t exec_frame_execute_dispatch(miga_frame_t *frame,
                                                               const ast_node_t *node);

struct exec_frame_execute_result_t exec_frame_execute_function_body(
    miga_frame_t *frame, const ast_node_t *func_body, strlist_t *func_args,
    const exec_redirections_t *func_redirs);

struct exec_frame_execute_result_t exec_frame_execute_command_string(miga_frame_t *frame,
                                                                     const string_t *command_str);

/* ============================================================================
 * Frame Query Functions
 * ============================================================================ */

/**
 * Find the nearest ancestor frame that is a return target.
 * Returns NULL if no return target exists (return is invalid).
 */
miga_frame_t *exec_frame_find_return_target(miga_frame_t *frame);

/**
 * Find the nearest ancestor frame that is a loop.
 * Returns NULL if not inside a loop (break/continue is invalid).
 */
miga_frame_t *exec_frame_find_loop(miga_frame_t *frame);

/**
 * Get the effective variable store for this frame.
 * (Walks up SHARE chain if necessary.)
 */
variable_store_t *exec_frame_get_variables(miga_frame_t *frame);

/**
 * Get the effective fd table for this frame.
 */
fd_table_t *exec_frame_get_fds(miga_frame_t *frame);

/**
 * Get the effective trap store for this frame.
 */
trap_store_t *exec_frame_get_traps(miga_frame_t *frame);

/* ============================================================================
 * Variable Access Helpers
 * ============================================================================ */

/**
 * Get variable value, checking local store first if applicable.
 */
const string_t *exec_frame_get_variable(const miga_frame_t *frame, const string_t *name);

/**
 * Set variable, respecting local scope if applicable.
 */
void exec_frame_set_variable(miga_frame_t *frame, const string_t *name, const string_t *value);

/**
 * Declare a local variable (only valid in frames with has_locals=true).
 * Returns 0 on success, -1 if locals not supported in this frame.
 */
int exec_frame_declare_local(miga_frame_t *frame, const string_t *name, const string_t *value);

/* ============================================================================
 * String Core Execution
 * ============================================================================ */

/**
 * Core implementation for executing shell commands from a string.
 *
 * Processes a single chunk of input (typically one line), handling lexing,
 * tokenization, parsing, and execution. Maintains state in the provided
 * session for handling multi-line constructs.
 */
miga_exec_status_t exec_frame_string_core(miga_frame_t *frame, const char *input,
                                     parse_session_t *session);

/**
 * Core implementation for executing shell commands from a stream.
 *
 * Reads lines from fp and feeds them to exec_frame_string_core one at a time,
 * handling lines longer than the read buffer by accumulating chunks.
 */
miga_exec_status_t exec_frame_stream_core(miga_frame_t *frame, FILE *fp, parse_session_t *session);

#endif /* EXEC_FRAME_H */

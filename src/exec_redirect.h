#ifndef EXEC_REDIRECT_H
#define EXEC_REDIRECT_H

/**
 * @file exec_redirect.h
 * @brief Runtime redirection structures and platform-agnostic redirection API.
 *
 * This header covers:
 *   - exec_redirections_t lifecycle (create, destroy, clone, append)
 *   - AST-to-runtime conversion
 *   - Platform-agnostic apply/restore wrappers
 *   - Platform-specific apply/restore declarations
 */

#include "ast.h"
#include "exec_types_internal.h"

/* ============================================================================
 * Redirection Structure Lifecycle
 * ============================================================================ */

/**
 * Create an empty runtime redirection list.
 */
exec_redirections_t *exec_redirections_create(void);

/**
 * Destroy a runtime redirection list and free all associated memory.
 * Sets *redirections to NULL.
 */
void exec_redirections_destroy(exec_redirections_t **redirections);

/**
 * Deep-copy a redirection list.
 *
 * @param redirs  Source redirection list (may be NULL).
 * @return A new cloned list, or NULL if source was NULL or allocation failed.
 */
exec_redirections_t *exec_redirections_clone(const exec_redirections_t *redirs);

/**
 * Append a single redirection entry to the list (deep-copied).
 *
 * @return true on success, false on allocation failure.
 */
bool exec_redirections_append(exec_redirections_t *redirections, exec_redirection_t *redir);

/* ============================================================================
 * AST to Runtime Conversion
 * ============================================================================ */

/**
 * Convert AST redirection nodes to a runtime exec_redirections_t list.
 * The caller is responsible for destroying the returned structure.
 *
 * @param frame      The execution frame (for variable expansion and errors).
 * @param ast_redirs The AST redirection node list.
 * @return A new exec_redirections_t, or NULL on error or empty input.
 */
exec_redirections_t *exec_redirections_create_from_ast_nodes(miga_frame_t *frame,
                                                             const ast_node_list_t *ast_redirs);

/* ============================================================================
 * Platform-Agnostic Redirection API
 * ============================================================================ */

/**
 * Apply redirections from a runtime redirection list.
 * Dispatches to the correct platform-specific implementation.
 *
 * @return MIGA_EXEC_STATUS_OK on success, MIGA_EXEC_STATUS_ERROR on failure.
 */
miga_exec_status_t exec_redirect_apply_redirectons(miga_frame_t *frame,
                                            const exec_redirections_t *redirections);

/**
 * Restore redirections previously applied by exec_redirect_apply_redirectons().
 * Dispatches to the correct platform-specific implementation.
 */
void exec_redirect_restore_redirections(miga_frame_t *frame, const exec_redirections_t *redirections);

/* ============================================================================
 * Platform-Specific Implementations
 * ============================================================================
 *
 * All platform variants share the same two-function contract:
 *   - exec_apply_redirections_*()   applies redirections and records state
 *                                   in the frame's fd_table for later restore.
 *   - exec_restore_redirections_*() reads that state and undoes the
 *                                   redirections.
 *
 * Callers should prefer the platform-agnostic wrappers above.
 */

#ifdef MIGA_POSIX_API

miga_exec_status_t exec_apply_redirections_posix(miga_frame_t *frame, const exec_redirections_t *redirs);
void exec_restore_redirections_posix(miga_frame_t *frame);

#elifdef MIGA_UCRT_API

miga_exec_status_t exec_apply_redirections_ucrt_c(miga_frame_t *frame,
                                             const exec_redirections_t *redirs);
void exec_restore_redirections_ucrt_c(miga_frame_t *frame);

#else

/**
 * ISO C redirection support is limited.  For internal commands (builtins and
 * shell functions), redirections are applied by setting the FILE* pointers
 * on the frame.  External command redirection is not possible in ISO C mode.
 */
miga_exec_status_t exec_apply_redirections_iso_c(miga_frame_t *frame, const exec_redirections_t *redirs);

/**
 * Restore ISO C redirections by closing any FILE* pointers that were opened
 * during apply.
 */
void exec_restore_redirections_iso_c(miga_frame_t *frame);

#endif

#endif /* EXEC_REDIRECT_H */

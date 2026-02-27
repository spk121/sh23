#ifndef EXEC_REDIRECT_H
#define EXEC_REDIRECT_H

#include "ast.h"
#include "exec_expander.h"
#include "exec_internal.h"

/**
 * Platform-specific redirection application functions.
 * These work exclusively with exec_redirections_t (runtime structures).
 * AST redirections should be converted to exec_redirections_t before calling.
 *
 * All three platform variants share the same two-function contract:
 *   - exec_apply_redirections_*()   applies redirections and records state in
 *                                   the frame's fd_table for later restoration.
 *   - exec_restore_redirections_*() reads that state back and undoes the
 *                                   redirections, leaving the frame's streams
 *                                   in their pre-apply condition.
 */
#ifdef POSIX_API
exec_status_t exec_apply_redirections_posix(exec_frame_t *frame, const exec_redirections_t *redirs);
void exec_restore_redirections_posix(exec_frame_t *frame);
#elifdef UCRT_API
exec_status_t exec_apply_redirections_ucrt_c(exec_frame_t *frame,
                                             const exec_redirections_t *redirs);
void exec_restore_redirections_ucrt_c(exec_frame_t *frame);
#else
/**
 * ISO C redirection support is limited to file redirections (< > >> <>)
 * on the three standard streams (stdin/stdout/stderr) only. FD-to-FD
 * duplication, explicit closes, and heredocs are not supported and will
 * return EXEC_NOT_IMPL. Restoration uses freopen() back to the platform
 * standard device paths (/dev/stdin etc.) since ISO C has no dup().
 */
exec_status_t exec_apply_redirections_iso_c(exec_frame_t *frame, const exec_redirections_t *redirs);
void exec_restore_redirections_iso_c(exec_frame_t *frame);
#endif

/**
 * Convert AST redirection nodes to runtime exec_redirections_t structure.
 * The caller is responsible for destroying the returned structure.
 */
exec_redirections_t *exec_redirections_from_ast(exec_frame_t *frame,
                                                const ast_node_list_t *ast_redirs);

/**
 * Clone a redirection structure (deep copy).
 * @param redirs Source redirection structure (may be NULL)
 * @return New cloned structure, or NULL if source was NULL or allocation failed
 */
exec_redirections_t *exec_redirections_clone(const exec_redirections_t *redirs);

#endif

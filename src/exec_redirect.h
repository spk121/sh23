#ifndef EXEC_REDIRECT_H
#define EXEC_REDIRECT_H

#include "ast.h"
#include "exec_internal.h"
#include "exec_expander.h"

/**
 * Structure representing a saved file descriptor backup.
 * Used to restore file descriptors after redirections are applied.
 */
typedef struct
{
    int fd;         ///< The original file descriptor that was redirected
    int backup_fd;  ///< The saved copy of the original FD (for restoration)
} saved_fd_t;

/**
 * Platform-specific redirection application functions.
 * These work exclusively with exec_redirections_t (runtime structures).
 * AST redirections should be converted to exec_redirections_t before calling.
 */
#ifdef POSIX_API
exec_status_t exec_apply_redirections_posix(exec_frame_t *frame,
                                                   const exec_redirections_t *redirs,
                                                   saved_fd_t **out_saved, int *out_saved_count);
void exec_restore_redirections_posix(saved_fd_t *saved, int saved_count);
#elifdef UCRT_API
exec_status_t exec_apply_redirections_ucrt_c(exec_frame_t *frame,
                                                    const exec_redirections_t *redirs,
                                                    saved_fd_t **out_saved, int *out_saved_count);
void exec_restore_redirections_ucrt_c(saved_fd_t *saved, int saved_count);
#endif

exec_status_t exec_apply_redirections_iso_c(exec_frame_t *frame,
                                                   const exec_redirections_t *redirs,
                                                   saved_fd_t **out_saved, int *out_saved_count);
void exec_restore_redirections_iso_c(saved_fd_t *saved, int saved_count);

/**
 * Convert AST redirection nodes to runtime exec_redirections_t structure.
 * The caller is responsible for destroying the returned structure.
 */
exec_redirections_t *exec_redirections_from_ast(exec_frame_t *frame, const ast_node_list_t *ast_redirs);

#endif
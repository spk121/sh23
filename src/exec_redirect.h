#ifndef EXEC_REDIRECT_H
#define EXEC_REDIRECT_H

#include "ast.h"
#include "exec_internal.h"
#include "exec_expander.h"

#ifdef POSIX_API
exec_status_t exec_apply_redirections_posix(exec_t *executor,
                                                   const ast_node_list_t *redirs,
                                                   saved_fd_t **out_saved, int *out_saved_count);
void exec_restore_redirections_posix(saved_fd_t *saved, int saved_count);
#elifdef UCRT_API
exec_status_t exec_apply_redirections_ucrt_c(exec_t *executor,
                                                    const ast_node_list_t *redirs,
                                                    saved_fd_t **out_saved, int *out_saved_count);
void exec_restore_redirections_ucrt_c(saved_fd_t *saved, int saved_count);
#endif

exec_status_t exec_apply_redirections_iso_c(exec_t *executor,
                                                   const ast_node_list_t *redirs,
                                                   saved_fd_t **out_saved, int *out_saved_count);
void exec_restore_redirections_iso_c(saved_fd_t *saved, int saved_count);

#endif
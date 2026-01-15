#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "exec_internal.h"
#include "exec_redirect.h"
#include "expander.h"
#include "lib.h"
#include "logging.h"
#include "string_t.h"
#include "xalloc.h"

#ifdef UCRT_API
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#endif

/**
 * Result of parsing a file descriptor number.
 */
typedef struct
{
    bool success;
    int fd;               /**< -1 means "just close" case (plain "-") */
    bool close_after_use; /**< true if we saw trailing '-' after a number */
} parse_fd_result_t;

/**
 * Parse a file descriptor number from a string.
 *
 * @param str     The string to parse (e.g., "1", "2")
 * @param out_fd  Output parameter for the parsed fd
 * @return        true on success, false if str is not a valid non-negative integer
 *
 * Unlike atoi(), this function:
 * - Rejects empty strings
 * - Rejects strings with non-digit characters (except leading whitespace)
 * - Rejects negative numbers
 * - Detects overflow
 */

static parse_fd_result_t parse_fd_number(const char *str)
{
    parse_fd_result_t result = {.success = false, .fd = -1, .close_after_use = false};

    if (str == NULL || *str == '\0')
        return result;

    /* Skip leading whitespace */
    while (*str == ' ' || *str == '\t')
        str++;

    if (*str == '\0')
        return result;

    /* Special case: plain "-" → close the target fd */
    if (*str == '-' && (str[1] == '\0' || (str[1] == ' ' || str[1] == '\t')))
    {
        // Consume the '-' and any trailing whitespace
        str++;
        while (*str == ' ' || *str == '\t')
            str++;
        if (*str == '\0')
        {
            result.success = true;
            result.fd = -1; // marker for "just close"
            return result;
        }
        // else: trailing junk after plain '-', fall through to failure
    }

    /* Reject standalone negative sign or negative numbers */
    if (*str == '-')
        return result;

    /* Allow optional leading '+' (like your original) */
    if (*str == '+')
        str++;

    if (*str == '\0' || !(*str >= '0' && *str <= '9'))
        return result;

    /* Parse digits with overflow check */
    long long val = 0;
    const char *start_digits = str;
    while (*str >= '0' && *str <= '9')
    {
        val = val * 10 + (*str - '0');
        if (val > INT_MAX) // You could also check INT_MIN if you want to be pedantic
            return result;
        str++;
    }

    /* Now we expect either end-of-string or a trailing '-' (move+close) */
    bool saw_minus = false;
    if (*str == '-')
    {
        saw_minus = true;
        str++;
    }

    /* Skip trailing whitespace */
    while (*str == ' ' || *str == '\t')
        str++;

    /* Must be end of string now — no trailing garbage allowed */
    if (*str != '\0')
        return result;

    /* If we saw '-', it must have come right after digits (no space) */
    if (saw_minus && str == start_digits)
    {
        // "-" was not after digits — invalid
        return result;
    }

    result.success = true;
    result.fd = (int)val;
    result.close_after_use = saw_minus;
    return result;
}

#ifdef POSIX_API
exec_status_t exec_apply_redirections_posix(exec_t *executor, expander_t *exp,
                                            const ast_node_list_t *redirs, saved_fd_t **out_saved,
                                            int *out_saved_count)
{
    Expects_not_null(executor);
    Expects_not_null(exp);
    Expects_not_null(redirs);
    Expects_not_null(out_saved);
    Expects_not_null(out_saved_count);

    int count = ast_node_list_size(redirs);
    saved_fd_t *saved = xcalloc(count, sizeof(saved_fd_t));
    if (!saved)
    {
        exec_set_error(executor, "Out of memory");
        return EXEC_ERROR;
    }

    int saved_i = 0;

    for (int i = 0; i < count; i++)
    {
        const ast_node_t *r = ast_node_list_get(redirs, i);
        Expects_eq(r->type, AST_REDIRECTION);

        int fd = (r->data.redirection.io_number >= 0 ? r->data.redirection.io_number
                  : (r->data.redirection.redir_type == REDIR_READ ||
                     r->data.redirection.redir_type == REDIR_FROM_BUFFER ||
                     r->data.redirection.redir_type == REDIR_FROM_BUFFER_STRIP)
                      ? 0
                      : 1);

        // Save original FD
        int backup = dup(fd);
        if (backup < 0)
        {
            exec_set_error(executor, "dup() failed");
            xfree(saved);
            return EXEC_ERROR;
        }

        saved[saved_i].fd = fd;
        saved[saved_i].backup_fd = backup;
        saved_i++;

        redir_target_kind_t opk = r->data.redirection.operand;

        switch (opk)
        {

        case REDIR_TARGET_FILE: {
            string_t *fname_str =
                expander_expand_redirection_target(exp, r->data.redirection.target);
            const char *fname = string_cstr(fname_str);
            int flags = 0;
            mode_t mode = 0666;

            switch (r->data.redirection.redir_type)
            {
            case REDIR_READ:
                flags = O_RDONLY;
                break;
            case REDIR_WRITE:
                flags = O_WRONLY | O_CREAT | O_TRUNC;
                break;
            case REDIR_APPEND:
                flags = O_WRONLY | O_CREAT | O_APPEND;
                break;
            case REDIR_READWRITE:
                flags = O_RDWR | O_CREAT;
                break;
            case REDIR_WRITE_FORCE:
                flags = O_WRONLY | O_CREAT | O_TRUNC;
                break;
            default:
                exec_set_error(executor, "Invalid filename redirection");
                string_destroy(&fname_str);
                xfree(saved);
                return EXEC_ERROR;
            }

            int newfd = open(fname, flags, mode);
            if (newfd < 0)
            {
                exec_set_error(executor, "Failed to open '%s'", fname);
                string_destroy(&fname_str);
                xfree(saved);
                return EXEC_ERROR;
            }

            if (dup2(newfd, fd) < 0)
            {
                exec_set_error(executor, "dup2() failed");
                string_destroy(&fname_str);
                close(newfd);
                xfree(saved);
                return EXEC_ERROR;
            }

            close(newfd);
            string_destroy(&fname_str);
            break;
        }

        case REDIR_TARGET_FD: {
            string_t *fd_str = expander_expand_redirection_target(exp, r->data.redirection.target);
            if (!fd_str)
            {
                exec_set_error(executor, "Failed to expand file descriptor target");
                xfree(saved);
                return EXEC_ERROR;
            }

            const char *lex = string_cstr(fd_str);
            parse_fd_result_t src = parse_fd_number(lex);

            if (!src.success)
            {
                exec_set_error(executor, "Invalid file descriptor: '%s'", lex);
                string_destroy(&fd_str);
                xfree(saved);
                return EXEC_ERROR;
            }

            if (dup2(src.fd, fd) < 0)
            {
                exec_set_error(executor, "dup2(%d, %d) failed: %s", src.fd, fd, strerror(errno));
                string_destroy(&fd_str);
                xfree(saved);
                return EXEC_ERROR;
            }
            string_destroy(&fd_str);
            break;
        }

        case REDIR_TARGET_CLOSE: {
            close(fd);
            break;
        }

        case REDIR_TARGET_BUFFER: {
            /* Heredoc content: expand unless delimiter was quoted */
            string_t *content_str = NULL;
            if (r->data.redirection.buffer)
            {
                if (r->data.redirection.buffer_needs_expansion)
                {
                    /* Unquoted delimiter: perform parameter, command, and arithmetic expansion
                     */
                    content_str = expander_expand_heredoc(exp, r->data.redirection.buffer,
                                                          /*is_quoted=*/false);
                }
                else
                {
                    /* Quoted delimiter: no expansion, preserve literal content */
                    content_str = string_create_from(r->data.redirection.buffer);
                }
            }
            const char *content = content_str ? string_cstr(content_str) : "";

            int pipefd[2];
            if (pipe(pipefd) < 0)
            {
                exec_set_error(executor, "pipe() failed");
                string_destroy(&content_str);
                xfree(saved);
                return EXEC_ERROR;
            }

            write(pipefd[1], content, strlen(content));
            close(pipefd[1]);

            if (dup2(pipefd[0], fd) < 0)
            {
                exec_set_error(executor, "dup2() failed for heredoc");
                close(pipefd[0]);
                string_destroy(&content_str);
                xfree(saved);
                return EXEC_ERROR;
            }

            close(pipefd[0]);
            string_destroy(&content_str);
            break;
        }

        default:
            exec_set_error(executor, "Unknown redirection operand");
            xfree(saved);
            return EXEC_ERROR;
        }
    }

    *out_saved = saved;
    *out_saved_count = saved_i;
    return EXEC_OK;
}

void exec_restore_redirections_posix(saved_fd_t *saved, int saved_count)
{
    for (int i = 0; i < saved_count; i++)
    {
        dup2(saved[i].backup_fd, saved[i].fd);
        close(saved[i].backup_fd);
    }
}

#elifdef UCRT_API
exec_status_t exec_apply_redirections_ucrt_c(exec_t *executor, expander_t *exp,
                                                    const ast_node_list_t *redirs,
                                                    saved_fd_t **out_saved, int *out_saved_count)
{
    Expects_not_null(executor);
    Expects_not_null(exp);
    Expects_not_null(out_saved);
    Expects_not_null(out_saved_count);

    if (!redirs || ast_node_list_size(redirs) == 0)
    {
        // No redirections - success
        *out_saved = NULL;
        *out_saved_count = 0;
        return EXEC_OK;
    }

    int count = ast_node_list_size(redirs);
    saved_fd_t *saved = xcalloc(count, sizeof(saved_fd_t));

    int saved_i = 0;

    for (int i = 0; i < count; i++)
    {
        const ast_node_t *r = ast_node_list_get(redirs, i);
        Expects_eq(r->type, AST_REDIRECTION);

        // Determine which file descriptor to redirect
        // If io_number is specified, use it; otherwise default based on redir_type
        int fd = (r->data.redirection.io_number >= 0 ? r->data.redirection.io_number
                  : (r->data.redirection.redir_type == REDIR_READ ||
                     r->data.redirection.redir_type == REDIR_FROM_BUFFER ||
                     r->data.redirection.redir_type == REDIR_FROM_BUFFER_STRIP)
                      ? STDIN_FILENO
                      : STDOUT_FILENO);

        // Save original FD
        int backup = _dup(fd);
        if (backup < 0)
        {
            exec_set_error(executor, "_dup() failed: %s", strerror(errno));
            xfree(saved);
            return EXEC_ERROR;
        }

        saved[saved_i].fd = fd;
        saved[saved_i].backup_fd = backup;
        saved_i++;

        redir_target_kind_t opk = r->data.redirection.operand;

        switch (opk)
        {
        case REDIR_TARGET_FILE: {
            // Expand the filename
            string_t *fname_str =
                expander_expand_redirection_target(exp, r->data.redirection.target);
            if (!fname_str)
            {
                exec_set_error(executor, "Failed to expand redirection target");
                xfree(saved);
                return EXEC_ERROR;
            }
            const char *fname = string_cstr(fname_str);

            // Determine flags and permissions for _open
            int flags = 0;
            int pmode = _S_IREAD | _S_IWRITE;

            switch (r->data.redirection.redir_type)
            {
            case REDIR_READ:
                flags = _O_RDONLY;
                break;
            case REDIR_WRITE:
                flags = _O_WRONLY | _O_CREAT | _O_TRUNC;
                break;
            case REDIR_APPEND:
                flags = _O_WRONLY | _O_CREAT | _O_APPEND;
                break;
            case REDIR_READWRITE:
                flags = _O_RDWR | _O_CREAT;
                break;
            case REDIR_WRITE_FORCE:
                flags = _O_WRONLY | _O_CREAT | _O_TRUNC;
                break;
            default:
                exec_set_error(executor, "Invalid filename redirection type");
                string_destroy(&fname_str);
                for (int j = 0; j < saved_i; j++)
                    _close(saved[j].backup_fd);
                xfree(saved);
                return EXEC_ERROR;
            }

            // Open the file
            int newfd = _open(fname, flags, pmode);
            if (newfd < 0)
            {
                exec_set_error(executor, "Failed to open '%s': %s", fname, strerror(errno));
                string_destroy(&fname_str);
                for (int j = 0; j < saved_i; j++)
                    _close(saved[j].backup_fd);
                xfree(saved);
                return EXEC_ERROR;
            }

            // Redirect the file descriptor
            if (_dup2(newfd, fd) < 0)
            {
                exec_set_error(executor, "_dup2() failed: %s", strerror(errno));
                string_destroy(&fname_str);
                _close(newfd);
                for (int j = 0; j < saved_i; j++)
                    _close(saved[j].backup_fd);
                xfree(saved);
                return EXEC_ERROR;
            }

            // Close the temporary file descriptor
            _close(newfd);
            string_destroy(&fname_str);
            break;
        }

        case REDIR_TARGET_FD: {
            string_t *fd_str = expander_expand_redirection_target(exp, r->data.redirection.target);
            if (!fd_str)
            {
                exec_set_error(executor, "Failed to expand file descriptor target");
                xfree(saved);
                return EXEC_ERROR;
            }

            const char *lex = string_cstr(fd_str);
            parse_fd_result_t src = parse_fd_number(lex);

            if (!src.success)
            {
                exec_set_error(executor, "Invalid file descriptor: '%s'", lex);
                string_destroy(&fd_str);
                xfree(saved);
                return EXEC_ERROR;
            }

            if (src.fd == -1)
            { // Plain '-': treat as close
                _close(fd);
                break;
            }

            if (_dup2(src.fd, fd) < 0)
            {
                exec_set_error(executor, "_dup2(%d, %d) failed: %s", src.fd, fd, strerror(errno));
                string_destroy(&fd_str);
                xfree(saved);
                return EXEC_ERROR;
            }

            if (src.close_after_use)
            {
                if (src.fd == fd)
                {
                    fprintf(stderr, "Warning: Self-move redirection (%d>&%d-) ignored\n", fd,
                            src.fd);
                }
                else if (_close(src.fd) < 0)
                {
                    fprintf(stderr, "Warning: Failed to close source FD %d after move: %s\n",
                            src.fd, strerror(errno));
                }
            }

            string_destroy(&fd_str);
            break;
        }

        case REDIR_TARGET_CLOSE: {
            // Close the file descriptor
            _close(fd);
            break;
        }

        case REDIR_TARGET_BUFFER: {
            /* Heredoc content: expand unless delimiter was quoted */
            string_t *content_str = NULL;
            if (r->data.redirection.buffer)
            {
                if (r->data.redirection.buffer_needs_expansion)
                {
                    /* Unquoted delimiter: perform parameter, command, and arithmetic expansion */
                    content_str = expander_expand_heredoc(exp, r->data.redirection.buffer,
                                                          /*is_quoted=*/false);
                }
                else
                {
                    /* Quoted delimiter: no expansion, preserve literal content */
                    content_str = string_create_from(r->data.redirection.buffer);
                }
            }
            const char *content = content_str ? string_cstr(content_str) : "";
            size_t content_len = strlen(content);

            /* Use _pipe() to create a pipe for the heredoc content.
             * Note: _pipe() on Windows has a size parameter; we use a reasonable buffer size.
             * The pipe is opened in text mode by default; use _O_BINARY if needed. */
            int pipefd[2];
            if (_pipe(pipefd, (unsigned int)(content_len + 1024), _O_BINARY) < 0)
            {
                exec_set_error(executor, "_pipe() failed: %s", strerror(errno));
                string_destroy(&content_str);
                for (int j = 0; j < saved_i; j++)
                    _close(saved[j].backup_fd);
                xfree(saved);
                return EXEC_ERROR;
            }

            /* Write content to the write end of the pipe */
            if (content_len > 0)
            {
                _write(pipefd[1], content, (unsigned int)content_len);
            }
            _close(pipefd[1]); /* Close write end */
            string_destroy(&content_str);

            /* Redirect the target fd to the read end of the pipe */
            if (_dup2(pipefd[0], fd) < 0)
            {
                exec_set_error(executor, "_dup2() failed for heredoc: %s", strerror(errno));
                _close(pipefd[0]);
                for (int j = 0; j < saved_i; j++)
                    _close(saved[j].backup_fd);
                xfree(saved);
                return EXEC_ERROR;
            }

            _close(pipefd[0]); /* Close original read end, dup2'd copy remains */
            break;
        }

        case REDIR_TARGET_FD_STRING:
        case REDIR_TARGET_INVALID:
        default:
            exec_set_error(executor, "Unsupported redirection operand type in UCRT_API mode");
            for (int j = 0; j < saved_i; j++)
                _close(saved[j].backup_fd);
            xfree(saved);
            return EXEC_NOT_IMPL;
        }
    }

    *out_saved = saved;
    *out_saved_count = saved_i;
    return EXEC_OK;
}

void exec_restore_redirections_ucrt_c(saved_fd_t *saved, int saved_count)
{
    if (!saved)
        return;

    for (int i = saved_count - 1; i >= 0; i--)
    {
        if (_dup2(saved[i].backup_fd, saved[i].fd) < 0)
        {
            fprintf(stderr, "Warning: Failed to restore FD %d from %d: %s\n", saved[i].fd,
                    saved[i].backup_fd, strerror(errno));
            // Don't set exec_error here if you want to continue; just warn.
            // Do I want to continue?
        }
        _close(saved[i].backup_fd);
    }
}
#endif

exec_status_t exec_apply_redirections_iso_c(exec_t *executor, expander_t *exp,
                                            const ast_node_list_t *redirs, saved_fd_t **out_saved,
                                            int *out_saved_count)
{
    (void)redirs;

    exec_set_error(executor, "Redirections are not supported in ISO_C_API mode");

    return EXEC_ERROR;
}

void exec_restore_redirections_iso_c(saved_fd_t *saved, int saved_count)
{
    (void)saved;
    (void)saved_count;
    // No-op in ISO_C_API mode
}

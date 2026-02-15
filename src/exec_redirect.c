#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "ast.h"
#include "exec_internal.h"
#include "exec_redirect.h"
#include "exec_expander.h"
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
        val = val * 10 + (long long)(*str - '0');
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
exec_status_t exec_apply_redirections_posix(exec_frame_t *frame,
                                            const exec_redirections_t *redirs, saved_fd_t **out_saved,
                                            int *out_saved_count)
{
    Expects_not_null(frame);
    Expects_not_null(redirs);
    Expects_not_null(out_saved);
    Expects_not_null(out_saved_count);

    exec_t *executor = frame->executor;
    int count = (int)redirs->count;
    saved_fd_t *saved = xcalloc(count, sizeof(saved_fd_t));
    if (!saved)
    {
        exec_set_error(executor, "Out of memory");
        return EXEC_ERROR;
    }

    int saved_i = 0;

    for (int i = 0; i < count; i++)
    {
        const exec_redirection_t *r = &redirs->items[i];

        int fd = (r->explicit_fd >= 0 ? r->explicit_fd
                  : (r->type == REDIR_READ ||
                     r->type == REDIR_FROM_BUFFER ||
                     r->type == REDIR_FROM_BUFFER_STRIP)
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
                expand_redirection_target(frame, r->data.redirection.target);
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
            string_t *fd_str = exec_expand_redirection_target(executor, r->data.redirection.target);
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
                    content_str = exec_expand_heredoc(executor, r->data.redirection.buffer,
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
exec_status_t exec_apply_redirections_ucrt_c(exec_frame_t *frame,
                                                    const exec_redirections_t *redirs,
                                                    saved_fd_t **out_saved, int *out_saved_count)
{
    Expects_not_null(frame);
    Expects_not_null(redirs);
    Expects_not_null(out_saved);
    Expects_not_null(out_saved_count);

    exec_t *executor = frame->executor;
    int count = (int)redirs->count;
    saved_fd_t *saved = xcalloc(count, sizeof(saved_fd_t));

    fflush(stdout);
    fflush(stderr);
    int saved_i = 0;

    for (int i = 0; i < count; i++)
    {
        const exec_redirection_t *r = &redirs->items[i];

        int fd = (r->explicit_fd >= 0 ? r->explicit_fd
                  : (r->type == REDIR_READ ||
                     r->type == REDIR_FROM_BUFFER ||
                     r->type == REDIR_FROM_BUFFER_STRIP)
                      ? 0
                      : 1);

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

        redir_target_kind_t opk = r->target_kind;

        switch (opk)
        {
        case REDIR_TARGET_FILE: {
            string_t *expanded_target = NULL;
            if (!r->target.file.is_expanded)
            {
                expanded_target = expand_redirection_target(frame, r->target.file.tok);
            }
            else
            {
                expanded_target = string_create_from(r->target.file.filename);
            }
                
            const char *fname = string_cstr(expanded_target);

            /* On Windows, translate /dev/null to NULL.
             * FIXME: do I need to handle MinGW */
            if (strcmp(fname, "/dev/null") == 0 || strcmp(fname, "\\dev\\null") == 0)
            {
                fname = "NUL";
            }

            // Determine flags and permissions for _open
            int flags = 0;
            int pmode = _S_IREAD | _S_IWRITE;

            switch (r->type)
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
                for (int j = 0; j < saved_i; j++)
                    _close(saved[j].backup_fd);
                xfree(saved);
                string_destroy(&expanded_target);
                return EXEC_ERROR;
            }

            // Open the file
            int newfd = _open(fname, flags, pmode);
            if (newfd < 0)
            {
                exec_set_error(executor, "Failed to open '%s': %s", fname, strerror(errno));
                for (int j = 0; j < saved_i; j++)
                    _close(saved[j].backup_fd);
                xfree(saved);
                string_destroy(&expanded_target);
                return EXEC_ERROR;
            }

            // Redirect the file descriptor
            if (_dup2(newfd, fd) < 0)
            {
                exec_set_error(executor, "_dup2() failed: %s", strerror(errno));
                _close(newfd);
                for (int j = 0; j < saved_i; j++)
                    _close(saved[j].backup_fd);
                xfree(saved);
                string_destroy(&expanded_target);
                return EXEC_ERROR;
            }

            // Close the temporary file descriptor
            _close(newfd);
            string_destroy(&expanded_target);
            break;
        }

        case REDIR_TARGET_FD: {
            string_t *fd_str = NULL;
            if (r->target.fd.fd_expression)
            {
                /* If we have a string expression, create a temporary token for expansion */
                fd_str = string_create_from(r->target.fd.fd_expression);
            }
            else if (r->target.fd.fixed_fd >= 0)
            {
                /* If we have a fixed FD, convert it to string */
                char fd_buf[32];
                snprintf(fd_buf, sizeof(fd_buf), "%d", r->target.fd.fixed_fd);
                fd_str = string_create_from_cstr(fd_buf);
            }

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
            if (r->target.heredoc.content)
            {
                if (r->target.heredoc.needs_expansion)
                {
                    /* Unquoted delimiter: perform parameter, command, and arithmetic expansion */
                    content_str = exec_expand_heredoc(executor, r->target.heredoc.content,
                                                          /*is_quoted=*/false);
                }
                else
                {
                    /* Quoted delimiter: no expansion, preserve literal content */
                    content_str = string_create_from(r->target.heredoc.content);
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

exec_status_t exec_apply_redirections_iso_c(exec_frame_t *frame,
                                            const exec_redirections_t *redirs, saved_fd_t **out_saved,
                                            int *out_saved_count)
{
    (void)out_saved;
    (void)out_saved_count;

    Expects_not_null(frame);
    Expects_not_null(redirs);
    exec_t *executor = frame->executor;

    return EXEC_ERROR;
}

void exec_restore_redirections_iso_c(saved_fd_t *saved, int saved_count)
{
    (void)saved;
    (void)saved_count;
    // No-op in ISO_C_API mode
}

/* ============================================================================
 * Redirection Structure Management
 * ============================================================================ */

/**
 * Create a new empty redirection structure.
 */
exec_redirections_t *exec_redirections_create(void)
{
    exec_redirections_t *redirs = xcalloc(1, sizeof(exec_redirections_t));
    if (!redirs)
        return NULL;

    redirs->items = NULL;
    redirs->count = 0;
    redirs->capacity = 0;
    return redirs;
}

/**
 * Destroy a redirection structure and free all associated memory.
 */
void exec_redirections_destroy(exec_redirections_t **redirections)
{
    if (!redirections || !*redirections)
        return;

    exec_redirections_t *redirs = *redirections;

    if (redirs->items)
    {
        for (size_t i = 0; i < redirs->count; i++)
        {
            exec_redirection_t *redir = &redirs->items[i];

            /* Free target data based on type */
            switch (redir->target_kind)
            {
                case REDIR_TARGET_FILE:
                    if (redir->target.file.filename)
                        string_destroy(&redir->target.file.filename);
                    if (redir->target.file.tok)
                        token_destroy(&redir->target.file.tok);
                    break;
                case REDIR_TARGET_FD:
                    if (redir->target.fd.fd_expression)
                        string_destroy(&redir->target.fd.fd_expression);
                    break;
                case REDIR_TARGET_BUFFER:
                    if (redir->target.heredoc.content)
                        string_destroy(&redir->target.heredoc.content);
                    break;
                case REDIR_TARGET_CLOSE:
                case REDIR_TARGET_FD_STRING:
                case REDIR_TARGET_INVALID:
                default:
                    break;
            }

            /* Free location variable name if present */
            if (redir->io_location_varname)
                string_destroy(&redir->io_location_varname);
        }
        xfree(redirs->items);
    }

    xfree(*redirections);
    *redirections = NULL;
}

/**
 * Append a redirection to the structure.
 */
void exec_redirections_append(exec_redirections_t *redirections, exec_redirection_t *redir)
{
    if (!redirections || !redir)
        return;

    /* Grow capacity if needed */
    if (redirections->count >= redirections->capacity)
    {
        size_t new_capacity = redirections->capacity == 0 ? 4 : redirections->capacity * 2;
        exec_redirection_t *new_items = xrealloc(redirections->items, new_capacity * sizeof(exec_redirection_t));
        if (!new_items)
            return;

        redirections->items = new_items;
        redirections->capacity = new_capacity;
    }

    /* Copy the redirection */
    redirections->items[redirections->count] = *redir;
    redirections->count++;
}


exec_redirections_t* exec_redirections_clone(const exec_redirections_t* redirs)
{
    if (!redirs)
        return NULL;

    exec_redirections_t *clone = exec_redirections_create();
    if (!clone)
        return NULL;

    /* Pre-allocate capacity to match source */
    if (redirs->capacity > 0)
    {
        clone->items = xcalloc(redirs->capacity, sizeof(exec_redirection_t));
        if (!clone->items)
        {
            exec_redirections_destroy(&clone);
            return NULL;
        }
        clone->capacity = redirs->capacity;
    }

    /* Deep copy each redirection */
    for (size_t i = 0; i < redirs->count; i++)
    {
        const exec_redirection_t *src = &redirs->items[i];
        exec_redirection_t *dst = &clone->items[clone->count];

        /* Copy scalar fields */
        dst->type = src->type;
        dst->explicit_fd = src->explicit_fd;
        dst->target_kind = src->target_kind;
        dst->source_line = src->source_line;

        /* Deep copy io_location_varname if present */
        if (src->io_location_varname)
        {
            dst->io_location_varname = string_create_from(src->io_location_varname);
        }
        else
        {
            dst->io_location_varname = NULL;
        }

        /* Deep copy target data based on target_kind */
        switch (src->target_kind)
        {
            case REDIR_TARGET_FILE:
                if (src->target.file.filename)
                {
                    dst->target.file.filename = string_create_from(src->target.file.filename);
                }
                else
                {
                    dst->target.file.filename = NULL;
                }
                if (src->target.file.tok)
                    {
                    dst->target.file.tok = token_clone(src->target.file.tok);
                }
                else
                {
                    dst->target.file.tok = NULL;
                }
                break;

            case REDIR_TARGET_FD:
                dst->target.fd.fixed_fd = src->target.fd.fixed_fd;
                if (src->target.fd.fd_expression)
                {
                    dst->target.fd.fd_expression = string_create_from(src->target.fd.fd_expression);
                }
                else
                {
                    dst->target.fd.fd_expression = NULL;
                }
                break;

            case REDIR_TARGET_BUFFER:
                if (src->target.heredoc.content)
                {
                    dst->target.heredoc.content = string_create_from(src->target.heredoc.content);
                }
                else
                {
                    dst->target.heredoc.content = NULL;
                }
                dst->target.heredoc.needs_expansion = src->target.heredoc.needs_expansion;
                break;

            case REDIR_TARGET_CLOSE:
            case REDIR_TARGET_FD_STRING:
            case REDIR_TARGET_INVALID:
            default:
                /* No additional data to copy */
                break;
        }

        clone->count++;
    }

    return clone;
}

/* ============================================================================
 * Runtime Redirection Wrapper (exec_redirections_t)
 * ============================================================================ */

/**
 * Apply redirections from the runtime structure.
 * Selects the appropriate platform-specific implementation.
 *
 * @param frame The execution frame

 * @param redirections The runtime redirection structure
 * @return 0 on success, -1 on error
 */
int exec_frame_apply_redirections(exec_frame_t *frame, const exec_redirections_t *redirections)
{
    if (!frame || !redirections)
        return 0; // No redirections or frame is NULL - this is OK

    if (!redirections->items || redirections->count == 0)
        return 0; // No redirections to apply

    saved_fd_t *saved_fds = NULL;
    int saved_count = 0;

#ifdef POSIX_API
    exec_status_t st = exec_apply_redirections_posix(frame, redirections, &saved_fds, &saved_count);
    if (st != EXEC_OK)
        return -1;
#elifdef UCRT_API
    exec_status_t st = exec_apply_redirections_ucrt_c(frame, redirections, &saved_fds, &saved_count);
    if (st != EXEC_OK)
        return -1;
#else
    exec_status_t st = exec_apply_redirections_iso_c(frame, redirections, &saved_fds, &saved_count);
    if (st != EXEC_OK)
        return -1;
#endif

    /* Note: Caller is responsible for calling exec_restore_redirections later
     * and passing the saved_fds/saved_count that were returned from this function.
     * We return them via out-params from the platform-specific functions.
     */
    return 0;
}

/**
 * Restore redirections after they were applied.
 *
 * @param frame The execution frame
 * @param redirections The runtime redirection structure (for context, not strictly needed)
 */
void exec_restore_redirections(exec_frame_t *frame, const exec_redirections_t *redirections)
{
    (void)frame;
    (void)redirections;

    /* This is now a no-op since saved_fds are not stored in frame.
     * The caller should manage saved_fds directly from the return values of
     * exec_apply_redirections_posix/ucrt_c/iso_c.
     */
}

/* ============================================================================
 * AST to Runtime Conversion
 * ============================================================================ */

/**
 * Convert AST redirection nodes to runtime exec_redirections_t structure.
 * This happens once during command execution, converting from AST to runtime structures.
 *
 * @param frame The execution frame (for error reporting)
 * @param ast_redirs The AST redirection nodes
 * @return A new exec_redirections_t structure, or NULL on error
 */
exec_redirections_t *exec_redirections_from_ast(exec_frame_t *frame, const ast_node_list_t *ast_redirs)
{
    if (!ast_redirs || ast_node_list_size(ast_redirs) == 0)
        return NULL;

    exec_redirections_t *redirs = exec_redirections_create();
    if (!redirs)
    {
        exec_set_error(frame->executor, "Failed to create redirection structure");
        return NULL;
    }

    int count = ast_node_list_size(ast_redirs);
    for (int i = 0; i < count; i++)
    {
        const ast_node_t *ast_redir = ast_node_list_get(ast_redirs, i);
        if (ast_redir->type != AST_REDIRECTION)
        {
            exec_set_error(frame->executor, "Expected AST_REDIRECTION node");
            exec_redirections_destroy(&redirs);
            return NULL;
        }

        /* Create a runtime redirection from the AST node */
        exec_redirection_t *runtime_redir = xcalloc(1, sizeof(exec_redirection_t));
        if (!runtime_redir)
        {
            exec_set_error(frame->executor, "Failed to allocate redirection structure");
            exec_redirections_destroy(&redirs);
            return NULL;
        }

        /* Access AST redirection data */
        const redirection_type_t ast_redir_type = ast_redir->data.redirection.redir_type;
        const int ast_io_number = ast_redir->data.redirection.io_number;
        const redir_target_kind_t ast_operand = ast_redir->data.redirection.operand;
        const bool ast_buffer_needs_expansion = ast_redir->data.redirection.buffer_needs_expansion;
        const token_t *ast_target = ast_redir->data.redirection.target;
        const string_t *ast_buffer = ast_redir->data.redirection.buffer;
        const string_t *ast_fd_string = ast_redir->data.redirection.fd_string;

        runtime_redir->type = ast_redir_type;
        runtime_redir->explicit_fd = ast_io_number;
        runtime_redir->target_kind = ast_operand;

        /* Copy target information based on target kind */
        switch (ast_operand)
        {
            case REDIR_TARGET_FILE:
                /* For file target, we store the token (which contains parts).
                 * At expansion time, we'll process these parts.
                 * For now, store a simple copy of the filename if it's a simple literal.
                 */
                if (ast_target && ast_target->parts && ast_target->parts->size > 0)
                {
                    /* If all the parts are PART_LITERAL, let's construct the target.file.filename now. */
                    bool all_literal = true;
                    for (int p = 0; p < ast_target->parts->size; p++)
                    {
                        part_t *part = ast_target->parts->parts[p];
                        if (!part || part->type != PART_LITERAL || !part->text)
                        {
                            /* Not a simple literal - we'll need to expand at runtime */
                            all_literal = false;
                            break;
                        }
                    }

                    if (all_literal)
                    {
                        /* All parts are literal - construct the filename now */
                        string_t *filename = string_create();
                        for (int p = 0; p < ast_target->parts->size; p++)
                        {
                            part_t *part = ast_target->parts->parts[p];
                            if (part && part->type == PART_LITERAL && part->text)
                            {
                                string_append(filename, part->text);
                            }
                        }
                        runtime_redir->target.file.filename = filename;
                        runtime_redir->target.file.is_expanded = true;
                    }
                    else
                    {
                        runtime_redir->target.file.tok = token_clone(ast_target);
                        runtime_redir->target.file.is_expanded = false;
                    }
                }
                break;

            case REDIR_TARGET_FD:
                /* For FD target, we can have a fixed fd or an expression */
                if (ast_target && ast_target->io_number >= 0)
                {
                    runtime_redir->target.fd.fixed_fd = ast_target->io_number;
                }
                else if (ast_target && ast_target->parts && ast_target->parts->size > 0)
                {
                    /* Store the first part's text as fd_expression */
                    part_t *part = ast_target->parts->parts[0];
                    if (part && part->type == PART_LITERAL && part->text)
                    {
                        runtime_redir->target.fd.fd_expression = string_create_from(part->text);
                    }
                }
                break;

            case REDIR_TARGET_CLOSE:
                /* No target data needed */
                break;

            case REDIR_TARGET_BUFFER:
                if (ast_buffer)
                {
                    runtime_redir->target.heredoc.content = string_create_from(ast_buffer);
                }
                runtime_redir->target.heredoc.needs_expansion = ast_buffer_needs_expansion;
                break;

            case REDIR_TARGET_INVALID:
            default:
                exec_set_error(frame->executor, "Invalid redirection target kind");
                xfree(runtime_redir);
                exec_redirections_destroy(&redirs);
                return NULL;
        }

        exec_redirections_append(redirs, runtime_redir);
    }

    return redirs;
}


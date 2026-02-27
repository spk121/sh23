#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <limits.h>
#include <stdio.h>
#include <string.h>
#ifdef UCRT_API
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#ifndef FD_SETSIZE
#define FD_SETSIZE 64
#endif
#endif

#include "ast.h"
#include "exec_expander.h"
#include "exec_frame.h"
#include "exec_internal.h"
#include "exec_redirect.h"
#include "lib.h"
#include "logging.h"
#include "string_t.h"
#include "xalloc.h"

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

    /* Allow optional leading '+' */
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
exec_status_t exec_apply_redirections_posix(exec_frame_t *frame, const exec_redirections_t *redirs)
{
    Expects_not_null(frame);
    Expects_not_null(redirs);

    exec_t *executor = frame->executor;
    fd_table_t *fds = exec_frame_get_fds(frame);
    if (!fds)
    {
        exec_set_error(executor, "No FD table available");
        return EXEC_ERROR;
    }

    size_t count = redirs->count;
    if (count == 0)
        return EXEC_OK;

    // Step 1: Collect unique FDs that will be redirected (to save only once).
    // We still do a pre-pass so that all backups are created before any
    // redirection is applied. This avoids a subtle ordering hazard: if the
    // redirection list contains "3>a 4>&3", processing them sequentially
    // without pre-saving fd 4 would capture the already-redirected fd 3 as
    // fd 4's backup instead of fd 4's original target.
    bool will_redirect[FD_SETSIZE] = {false};
    for (size_t i = 0; i < count; i++)
    {
        const exec_redirection_t *r = &redirs->items[i];
        int target_fd = (r->explicit_fd >= 0 ? r->explicit_fd
                         : (r->type == REDIR_READ || r->type == REDIR_FROM_BUFFER ||
                            r->type == REDIR_FROM_BUFFER_STRIP || r->type == REDIR_READWRITE)
                             ? 0
                             : 1);
        if (target_fd >= 0 && target_fd < FD_SETSIZE)
            will_redirect[target_fd] = true;
    }

    // Step 2: Save originals for FDs that will be redirected.
    // All state is written into fds; no parallel saved_fd_t array needed.
    const int min_backup_fd = 10; // Common shell convention: backups from 10+
    for (int fd = 0; fd < FD_SETSIZE; fd++)
    {
        if (!will_redirect[fd])
            continue;

        // Skip if already redirected by an outer scope — the outermost save
        // is the one we need to restore to, so don't overwrite it.
        if (fd_table_has_flag(fds, fd, FD_REDIRECTED))
            continue;

        // F_DUPFD_CLOEXEC: kernel picks the lowest available fd >= min_backup_fd
        // and sets O_CLOEXEC atomically, keeping backup fds out of child processes.
        log_debug("apply(posix): phase1 saving fd=%d before redirect", fd);
        int backup = fcntl(fd, F_DUPFD_CLOEXEC, min_backup_fd);
        if (backup < 0)
        {
            exec_set_error(executor, "fcntl(F_DUPFD_CLOEXEC) failed for fd %d: %s", fd,
                           strerror(errno));
            goto cleanup_error;
        }
        log_debug("apply(posix): phase1 fcntl(F_DUPFD_CLOEXEC, %d) -> backup fd=%d", fd, backup);

        string_t *saved_name = fd_table_generate_name_ex(backup, fd, FD_SAVED | FD_CLOEXEC);
        if (!fd_table_add(fds, backup, FD_SAVED | FD_CLOEXEC, saved_name))
        {
            string_destroy(&saved_name);
            exec_set_error(executor, "Failed to track saved FD %d", backup);
            close(backup);
            goto cleanup_error;
        }
        fd_table_mark_saved(fds, backup, fd);
        string_destroy(&saved_name);
    }

    // Step 3: Apply redirections in order (left-to-right, allowing overwrites).
    for (size_t i = 0; i < count; i++)
    {
        const exec_redirection_t *r = &redirs->items[i];

        int target_fd = (r->explicit_fd >= 0 ? r->explicit_fd
                         : (r->type == REDIR_READ || r->type == REDIR_FROM_BUFFER ||
                            r->type == REDIR_FROM_BUFFER_STRIP || r->type == REDIR_READWRITE)
                             ? 0
                             : 1);
        if (target_fd < 0)
        {
            exec_set_error(executor, "Invalid target FD");
            goto cleanup_error;
        }

        switch (r->target_kind)
        {
        case REDIR_TARGET_FILE: {
            string_t *fname_str = expand_redirection_target(frame, r->data.redirection.target);
            if (!fname_str)
            {
                exec_set_error(executor, "Failed to expand file target");
                goto cleanup_error;
            }
            const char *fname = string_cstr(fname_str);
            int flags = 0;
            mode_t mode = 0666; // umask will be applied

            switch (r->type)
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
                exec_set_error(executor, "Unsupported redirection type %d", r->type);
                string_destroy(&fname_str);
                goto cleanup_error;
            }

            log_debug("apply(posix): open('%s') for fd=%d", fname, target_fd);
            int newfd = open(fname, flags, mode);
            if (newfd < 0)
            {
                exec_set_error(executor, "open('%s') failed: %s", fname, strerror(errno));
                string_destroy(&fname_str);
                goto cleanup_error;
            }
            log_debug("apply(posix): open -> newfd=%d", newfd);

            log_debug("apply(posix): dup2(%d -> %d) wiring fd=%d to '%s'", newfd, target_fd,
                      target_fd, fname);
            if (dup2(newfd, target_fd) < 0)
            {
                exec_set_error(executor, "dup2(%d, %d) failed: %s", newfd, target_fd,
                               strerror(errno));
                close(newfd);
                string_destroy(&fname_str);
                goto cleanup_error;
            }
            log_debug("apply(posix): close(%d) temp file fd", newfd);
            close(newfd);

            if (!fd_table_add(fds, target_fd, FD_REDIRECTED, fname_str))
            {
                exec_set_error(executor, "Failed to track redirected FD %d", target_fd);
                string_destroy(&fname_str);
                goto cleanup_error;
            }
            string_destroy(&fname_str);
            break;
        }

        case REDIR_TARGET_FD: {
            string_t *fd_str = exec_expand_redirection_target(executor, r->data.redirection.target);
            if (!fd_str)
            {
                exec_set_error(executor, "Failed to expand FD target");
                goto cleanup_error;
            }

            const char *lex = string_cstr(fd_str);
            parse_fd_result_t src = parse_fd_number(lex);
            string_destroy(&fd_str);
            if (!src.success)
            {
                exec_set_error(executor, "Invalid source FD: '%s'", lex);
                goto cleanup_error;
            }

            if (src.fd == -1)
            {
                // n<&- or n>&-: explicit close of target fd.
                log_debug("apply(posix): close(%d) explicit n<&- or n>&-", target_fd);
                if (close(target_fd) < 0)
                {
                    exec_set_error(executor, "close(%d) failed: %s", target_fd, strerror(errno));
                    goto cleanup_error;
                }
                fd_table_mark_closed(fds, target_fd);
                fd_table_clear_flag(fds, target_fd, FD_REDIRECTED);
                break;
            }

            log_debug("apply(posix): dup2(%d -> %d) fd-to-fd redirect", src.fd, target_fd);
            if (dup2(src.fd, target_fd) < 0)
            {
                exec_set_error(executor, "dup2(%d, %d) failed: %s", src.fd, target_fd,
                               strerror(errno));
                goto cleanup_error;
            }

            string_t *redir_name = fd_table_generate_name_ex(target_fd, src.fd, FD_REDIRECTED);
            if (!fd_table_add(fds, target_fd, FD_REDIRECTED, redir_name))
            {
                exec_set_error(executor, "Failed to track redirected FD %d", target_fd);
                string_destroy(&redir_name);
                goto cleanup_error;
            }
            string_destroy(&redir_name);

            if (src.close_after_use)
            {
                // n>&m-: move — dup m onto n then close m.
                if (src.fd == target_fd)
                {
                    fprintf(stderr, "Warning: Self-move redirection (%d>&%d-) ignored\n", target_fd,
                            src.fd);
                }
                else if (close(src.fd) < 0)
                {
                    log_warn("close(%d) failed after move redirection: %s", src.fd,
                             strerror(errno));
                }
                else
                {
                    fd_table_mark_closed(fds, src.fd);
                    fd_table_remove(fds, src.fd);
                }
            }
            break;
        }

        case REDIR_TARGET_CLOSE: {
            log_debug("apply(posix): close(%d) REDIR_TARGET_CLOSE", target_fd);
            if (close(target_fd) < 0)
            {
                exec_set_error(executor, "close(%d) failed: %s", target_fd, strerror(errno));
                goto cleanup_error;
            }
            fd_table_mark_closed(fds, target_fd);
            fd_table_clear_flag(fds, target_fd, FD_REDIRECTED);
            break;
        }

        case REDIR_TARGET_BUFFER: {
            string_t *content_str = NULL;
            if (r->data.redirection.buffer)
            {
                content_str = r->data.redirection.buffer_needs_expansion
                                  ? exec_expand_heredoc(executor, r->data.redirection.buffer, false)
                                  : string_create_from(r->data.redirection.buffer);
                if (!content_str)
                {
                    exec_set_error(executor, "Failed to process heredoc");
                    goto cleanup_error;
                }
            }
            const char *content = content_str ? string_cstr(content_str) : "";
            size_t content_len = content_str ? string_length(content_str) : 0;

            log_debug("apply(posix): creating heredoc pipe for fd=%d content_len=%zu", target_fd,
                      content_len);
            int pipefd[2];
            if (pipe(pipefd) < 0)
            {
                exec_set_error(executor, "pipe() failed: %s", strerror(errno));
                string_destroy(&content_str);
                goto cleanup_error;
            }

            ssize_t written = write(pipefd[1], content, content_len);
            if (written < 0 || (size_t)written != content_len)
            {
                exec_set_error(executor, "write to heredoc pipe failed: %s", strerror(errno));
                close(pipefd[0]);
                close(pipefd[1]);
                string_destroy(&content_str);
                goto cleanup_error;
            }
            close(pipefd[1]);

            log_debug("apply(posix): dup2(%d -> %d) wiring heredoc pipe to fd=%d", pipefd[0],
                      target_fd, target_fd);
            if (dup2(pipefd[0], target_fd) < 0)
            {
                exec_set_error(executor, "dup2(%d, %d) for heredoc failed: %s", pipefd[0],
                               target_fd, strerror(errno));
                close(pipefd[0]);
                string_destroy(&content_str);
                goto cleanup_error;
            }
            close(pipefd[0]);
            string_destroy(&content_str);

            string_t *heredoc_name = fd_table_generate_name(target_fd, FD_REDIRECTED);
            if (!fd_table_add(fds, target_fd, FD_REDIRECTED, heredoc_name))
            {
                exec_set_error(executor, "Failed to track heredoc FD %d", target_fd);
                string_destroy(&heredoc_name);
                goto cleanup_error;
            }
            string_destroy(&heredoc_name);
            break;
        }

        default:
            exec_set_error(executor, "Unknown redirection kind %d", r->target_kind);
            goto cleanup_error;
        }
    }

    return EXEC_OK;

cleanup_error:
    exec_restore_redirections_posix(frame);
    return EXEC_ERROR;
}

void exec_restore_redirections_posix(exec_frame_t *frame)
{
    if (!frame)
        return;

    fd_table_t *fds = exec_frame_get_fds(frame);
    if (!fds)
        return;

    // Snapshot saved FDs before mutating the table, since fd_table_remove()
    // compacts in place and would corrupt live iteration.
    size_t saved_count = 0;
    int *saved_fds = fd_table_get_saved_fds(fds, &saved_count);
    if (!saved_fds)
        return;

    log_debug("restore(posix): begin -- %zu saved fd(s) to restore", saved_count);
    for (size_t i = 0; i < saved_count; i++)
    {
        int backup_fd = saved_fds[i];
        int orig_fd = fd_table_get_original_fd(fds, backup_fd);
        log_debug("restore(posix): [%zu] backup_fd=%d orig_fd=%d", i, backup_fd, orig_fd);

        if (orig_fd < 0)
        {
            log_warn("exec_restore_redirections_posix: saved fd %d has no recorded original; "
                     "closing and skipping",
                     backup_fd);
            close(backup_fd);
            fd_table_mark_closed(fds, backup_fd);
            fd_table_remove(fds, backup_fd);
            continue;
        }

        log_debug("restore(posix): dup2(%d -> %d) restoring fd=%d from backup", backup_fd, orig_fd,
                  orig_fd);
        if (dup2(backup_fd, orig_fd) < 0)
        {
            log_warn("restore(posix): dup2(%d, %d) failed: %s", backup_fd, orig_fd,
                     strerror(errno));
        }

        log_debug("restore(posix): close(%d) backup fd", backup_fd);
        close(backup_fd);

        /* Bug 5 fix: after dup2(backup_fd, orig_fd) the original fd is live
         * again — do NOT remove its table entry.  Removing it caused the
         * shell's fd tracking to diverge from reality: subsequent
         * fd_table_has_flag(fds, orig_fd, FD_REDIRECTED) calls returned false
         * even when another redirection was in flight, leading to double-saves
         * and leaked backup fds.
         *
         * The correct cleanup is:
         *   - Clear FD_REDIRECTED from orig_fd so it is known to be "normal"
         *     again.  (Leave any other flags, e.g. FD_CLOEXEC, intact.)
         *   - Remove the backup entry entirely — it is now closed and gone. */
        fd_table_clear_flag(fds, orig_fd, FD_REDIRECTED);

        fd_table_mark_closed(fds, backup_fd);
        fd_table_remove(fds, backup_fd);
    }

    log_debug("restore(posix): complete");
    xfree(saved_fds);
}
#elifdef UCRT_API

/* Lowest FD number used for backup copies on Windows.
 * _dup() always picks the lowest available slot, so without forcing a minimum
 * it can return 3, 4, … which are then clobbered by the very _dup2() calls
 * made during redirection application (Bug 3).  By bumping temporary fds past
 * this threshold we keep backups safely away from the range that redirections
 * normally operate on.  The value matches the common shell convention used by
 * the POSIX path (F_DUPFD_CLOEXEC, min_backup_fd = 10). */
#define UCRT_MIN_BACKUP_FD 10

/**
 * Duplicate fd to a slot >= UCRT_MIN_BACKUP_FD.
 *
 * _dup() has no F_DUPFD equivalent, so we keep calling _dup() until we land
 * above the threshold, closing the intermediate (too-low) duplicates as we go.
 * In practice this needs at most a handful of iterations.
 *
 * Returns the backup fd on success, -1 on failure (errno set by _dup/_close).
 */
static int dup_to_safe_slot(int fd)
{
    /* Temporary storage for fds we allocate below the threshold */
    int spill[UCRT_MIN_BACKUP_FD];
    int spill_count = 0;
    int result = -1;

    for (;;)
    {
        int dup_fd = _dup(fd);
        if (dup_fd < 0)
            break; /* _dup failed; errno already set */

        if (dup_fd >= UCRT_MIN_BACKUP_FD)
        {
            result = dup_fd;
            break;
        }

        /* Too low — park it and try again */
        if (spill_count < UCRT_MIN_BACKUP_FD)
            spill[spill_count++] = dup_fd;
        else
        {
            /* Shouldn't be reachable, but don't leak if it is */
            _close(dup_fd);
            break;
        }
    }

    /* Release the spill slots so the fd space isn't permanently consumed */
    for (int k = 0; k < spill_count; k++)
        _close(spill[k]);

    return result;
}

exec_status_t exec_apply_redirections_ucrt_c(exec_frame_t *frame, const exec_redirections_t *redirs)
{
    if (!frame || !redirs)
    {
        log_fatal(
            "Contract violation at %s:%d - exec_apply_redirections_ucrt_c received NULL argument",
            __FILE__, __LINE__);
        return EXEC_ERROR;
    }

    if (redirs->count == 0)
        return EXEC_OK;

    exec_t *executor = frame->executor;
    fd_table_t *fds = exec_frame_get_fds(frame);

    fflush(stdout);
    fflush(stderr);

    /* -----------------------------------------------------------------------
     * Phase 1 — Pre-pass: save originals before any redirection is applied.
     *
     * The old code saved each target FD lazily, inline with applying that
     * redirection. This created an ordering hazard: given "2>&1 1>file", the
     * backup of fd 1 was made *after* fd 2 had already been duped onto it, so
     * the backup captured the pipe-end rather than the original stdout (Bug 2).
     *
     * Fix: collect every unique target FD first, create all backups, then
     * apply. This mirrors the two-phase approach already used by the POSIX path.
     *
     * We also use dup_to_safe_slot() rather than plain _dup() to ensure the
     * backup fd number is >= UCRT_MIN_BACKUP_FD, preventing subsequent _dup2()
     * calls from clobbering low-numbered backups (Bug 3).
     * ----------------------------------------------------------------------- */
    bool will_redirect[FD_SETSIZE] = {false};
    for (int i = 0; i < (int)redirs->count; i++)
    {
        const exec_redirection_t *r = &redirs->items[i];
        int fd = (r->explicit_fd >= 0 ? r->explicit_fd
                  : (r->type == REDIR_READ || r->type == REDIR_FROM_BUFFER ||
                     r->type == REDIR_FROM_BUFFER_STRIP)
                      ? 0
                      : 1);
        if (fd >= 0 && fd < FD_SETSIZE)
            will_redirect[fd] = true;
    }

    for (int fd = 0; fd < FD_SETSIZE; fd++)
    {
        if (!will_redirect[fd])
            continue;

        /* Skip if already saved by an outer redirection scope — the outermost
         * backup is the one we need to restore to; don't overwrite it. */
        if (fds && fd_table_has_flag(fds, fd, FD_REDIRECTED))
            continue;

        log_debug("apply(ucrt): phase1 saving fd=%d before redirect", fd);
        int backup = dup_to_safe_slot(fd);
        if (backup < 0)
        {
            exec_set_error(executor, "_dup(%d) to safe slot failed: %s", fd, strerror(errno));
            goto error_restore;
        }
        log_debug("apply(ucrt): phase1 _dup(%d) -> backup fd=%d", fd, backup);

        if (fds)
        {
            string_t *saved_name =
                fd_table_generate_saved_fd_name(backup, fd, FD_SAVED | FD_CLOEXEC);
            fd_table_add(fds, backup, FD_SAVED | FD_CLOEXEC, saved_name);
            string_destroy(&saved_name);
            fd_table_mark_saved(fds, backup, fd);
        }
    }

    /* -----------------------------------------------------------------------
     * Phase 2 — Apply redirections left-to-right.
     * ----------------------------------------------------------------------- */
    for (int i = 0; i < (int)redirs->count; i++)
    {
        const exec_redirection_t *r = &redirs->items[i];

        int fd = (r->explicit_fd >= 0 ? r->explicit_fd
                  : (r->type == REDIR_READ || r->type == REDIR_FROM_BUFFER ||
                     r->type == REDIR_FROM_BUFFER_STRIP)
                      ? 0
                      : 1);

        switch (r->target_kind)
        {
        case REDIR_TARGET_FILE: {
            /* expand_redirection_target() can return NULL (unset variable in
             * strict mode, failed glob, allocation error). Guard explicitly so
             * we reach the rollback path with a useful diagnostic rather than
             * crashing on string_cstr(NULL). */
            string_t *expanded_target = NULL;
            if (!r->target.file.is_expanded)
                expanded_target = expand_redirection_target(frame, r->target.file.tok);
            else
                expanded_target = string_create_from(r->target.file.filename);

            if (!expanded_target)
            {
                exec_set_error(executor, "Failed to expand redirection target");
                goto error_restore;
            }

            const char *fname = string_cstr(expanded_target);
            if (strcmp(fname, "/dev/null") == 0 || strcmp(fname, "\\dev\\null") == 0)
                fname = "NUL";

            /* Always use _O_BINARY: Windows text mode silently translates
             * LF<->CRLF, corrupting binary data and producing wrong byte
             * counts.  Shell redirections must be transparent to the stream. */
            int flags = 0;
            int pmode = _S_IREAD | _S_IWRITE;

            switch (r->type)
            {
            case REDIR_READ:
                flags = _O_RDONLY | _O_BINARY;
                break;
            case REDIR_WRITE:
                flags = _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY;
                break;
            case REDIR_APPEND:
                flags = _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY;
                break;
            case REDIR_READWRITE:
                flags = _O_RDWR | _O_CREAT | _O_BINARY;
                break;
            case REDIR_WRITE_FORCE:
                flags = _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY;
                break;
            default:
                exec_set_error(executor, "Invalid filename redirection type");
                string_destroy(&expanded_target);
                goto error_restore;
            }

            log_debug("apply(ucrt): _open('%s') for fd=%d", fname, fd);
            int newfd = _open(fname, flags, pmode);
            if (newfd < 0)
            {
                exec_set_error(executor, "Failed to open '%s': %s", fname, strerror(errno));
                string_destroy(&expanded_target);
                goto error_restore;
            }
            log_debug("apply(ucrt): _open -> newfd=%d", newfd);

            /* Flush before redirecting so any buffered data goes to the old
             * destination, not the new one. */
            if (fd == 1)
            {
                log_debug("apply(ucrt): fflush(stdout) before _dup2");
                fflush(stdout);
            }
            if (fd == 2)
            {
                log_debug("apply(ucrt): fflush(stderr) before _dup2");
                fflush(stderr);
            }

            log_debug("apply(ucrt): _dup2(%d -> %d) wiring fd=%d to '%s'", newfd, fd, fd, fname);
            if (_dup2(newfd, fd) < 0)
            {
                exec_set_error(executor, "_dup2(%d, %d) failed: %s", newfd, fd, strerror(errno));
                _close(newfd);
                string_destroy(&expanded_target);
                goto error_restore;
            }
            log_debug("apply(ucrt): _close(%d) temp file fd", newfd);
            _close(newfd);

            if (fds)
                fd_table_add(fds, fd, FD_REDIRECTED, expanded_target);

            string_destroy(&expanded_target);
            break;
        }

        case REDIR_TARGET_FD: {
            string_t *fd_str = NULL;
            if (r->target.fd.fd_expression)
                fd_str = string_create_from(r->target.fd.fd_expression);
            else if (r->target.fd.fixed_fd >= 0)
            {
                char fd_buf[32];
                snprintf(fd_buf, sizeof(fd_buf), "%d", r->target.fd.fixed_fd);
                fd_str = string_create_from_cstr(fd_buf);
            }

            if (!fd_str)
            {
                exec_set_error(executor, "Failed to expand file descriptor target");
                goto error_restore;
            }

            const char *lex = string_cstr(fd_str);
            parse_fd_result_t src = parse_fd_number(lex);

            if (!src.success)
            {
                exec_set_error(executor, "Invalid file descriptor: '%s'", lex);
                string_destroy(&fd_str);
                goto error_restore;
            }

            if (src.fd == -1)
            {
                /* n<&- or n>&-: explicit close of target fd. */
                log_debug("apply(ucrt): _close(%d) explicit n<&- or n>&-", fd);
                _close(fd);
                if (fds)
                {
                    fd_table_mark_closed(fds, fd);
                    /* Bug 4 fix: clear FD_REDIRECTED so a subsequent
                     * redirection to this fd in the same command correctly
                     * detects it as unredirected and does not skip the
                     * backup-save step. */
                    fd_table_clear_flag(fds, fd, FD_REDIRECTED);
                }
                string_destroy(&fd_str);
                break;
            }

            log_debug("apply(ucrt): _dup2(%d -> %d) fd-to-fd redirect", src.fd, fd);
            if (_dup2(src.fd, fd) < 0)
            {
                exec_set_error(executor, "_dup2(%d, %d) failed: %s", src.fd, fd, strerror(errno));
                string_destroy(&fd_str);
                goto error_restore;
            }

            if (fds)
            {
                string_t *redir_name = fd_table_generate_name_ex(fd, src.fd, FD_REDIRECTED);
                fd_table_add(fds, fd, FD_REDIRECTED, redir_name);
                string_destroy(&redir_name);
            }

            if (src.close_after_use)
            {
                /* n>&m-: move — dup m onto n, then close m. */
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
                else if (fds)
                {
                    fd_table_mark_closed(fds, src.fd);
                    fd_table_remove(fds, src.fd);
                }
            }

            string_destroy(&fd_str);
            break;
        }

        case REDIR_TARGET_CLOSE: {
            log_debug("apply(ucrt): _close(%d) REDIR_TARGET_CLOSE", fd);
            _close(fd);
            if (fds)
            {
                fd_table_mark_closed(fds, fd);
                /* Bug 4 fix: clear FD_REDIRECTED so subsequent redirections
                 * to this fd correctly re-save the (now-closed) original. */
                fd_table_clear_flag(fds, fd, FD_REDIRECTED);
            }
            break;
        }

        case REDIR_TARGET_BUFFER: {
            string_t *content_str = NULL;
            if (r->target.heredoc.content)
            {
                if (r->target.heredoc.needs_expansion)
                    content_str = exec_expand_heredoc(executor, r->target.heredoc.content, false);
                else
                    content_str = string_create_from(r->target.heredoc.content);
            }
            const char *content = content_str ? string_cstr(content_str) : "";
            size_t content_len = strlen(content);

            int pipefd[2];
            log_debug("apply(ucrt): creating heredoc pipe for fd=%d content_len=%zu", fd,
                      content_len);
            if (_pipe(pipefd, (unsigned int)(content_len + 1024), _O_BINARY) < 0)
            {
                exec_set_error(executor, "_pipe() failed: %s", strerror(errno));
                string_destroy(&content_str);
                goto error_restore;
            }

            if (content_len > 0)
                _write(pipefd[1], content, (unsigned int)content_len);
            _close(pipefd[1]);
            string_destroy(&content_str);

            log_debug("apply(ucrt): _dup2(%d -> %d) wiring heredoc pipe to fd=%d", pipefd[0], fd,
                      fd);
            if (_dup2(pipefd[0], fd) < 0)
            {
                exec_set_error(executor, "_dup2(%d, %d) failed for heredoc: %s", pipefd[0], fd,
                               strerror(errno));
                _close(pipefd[0]);
                goto error_restore;
            }
            _close(pipefd[0]);

            if (fds)
            {
                string_t *heredoc_name = fd_table_generate_name(fd, FD_REDIRECTED);
                fd_table_add(fds, fd, FD_REDIRECTED, heredoc_name);
                string_destroy(&heredoc_name);
            }
            break;
        }

        default:
            exec_set_error(executor, "Unsupported redirection operand type in UCRT_API mode");
            goto error_restore;
        }
    }

    return EXEC_OK;

error_restore:
    /* Roll back all redirections applied so far.  The restore function closes
     * every backup and restores every orig_fd, leaving the frame's fd state
     * consistent even when a multi-redirection command partially fails. */
    exec_restore_redirections_ucrt_c(frame);
    return EXEC_ERROR;
}

void exec_restore_redirections_ucrt_c(exec_frame_t *frame)
{
    fd_table_t *fds = exec_frame_get_fds(frame);
    if (!fds)
        return;

    /* Take a snapshot of all saved FDs before mutating the table.
     * fd_table_remove() compacts in place (swap-with-last), so iterating the
     * live table while removing entries would skip or double-visit entries. */
    size_t saved_count = 0;
    int *saved_fds = fd_table_get_saved_fds(fds, &saved_count);
    if (!saved_fds)
        return;

    log_debug("restore(ucrt): begin -- %zu saved fd(s) to restore", saved_count);
    for (size_t i = 0; i < saved_count; i++)
    {
        int backup_fd = saved_fds[i];
        int orig_fd = fd_table_get_original_fd(fds, backup_fd);
        log_debug("restore(ucrt): [%zu] backup_fd=%d orig_fd=%d", i, backup_fd, orig_fd);

        /* Guard against a missing or corrupt table entry.
         * fd_table_get_original_fd() returns -1 when the entry is absent or
         * was never set up with fd_table_mark_saved(). _dup2(backup, -1)
         * always fails, and fd_table_remove(fds, -1) silently no-ops, leaving
         * the backup fd open and tracked forever.  Close and evict it cleanly
         * so we at least don't leak the OS handle. */
        if (orig_fd < 0)
        {
            log_warn("exec_restore_redirections_ucrt_c: saved fd %d has no recorded original; "
                     "closing and skipping",
                     backup_fd);
            _close(backup_fd);
            fd_table_mark_closed(fds, backup_fd);
            fd_table_remove(fds, backup_fd);
            continue;
        }

        /* Flush before restoring so any buffered data written to the
         * redirected destination is committed before the fd is rewired back. */
        if (orig_fd == 1)
        {
            log_debug("restore(ucrt): fflush(stdout) before _dup2");
            fflush(stdout);
        }
        if (orig_fd == 2)
        {
            log_debug("restore(ucrt): fflush(stderr) before _dup2");
            fflush(stderr);
        }

        log_debug("restore(ucrt): _dup2(%d -> %d) restoring fd=%d from backup", backup_fd, orig_fd,
                  orig_fd);
        if (_dup2(backup_fd, orig_fd) < 0)
        {
            log_warn("restore(ucrt): _dup2(%d, %d) failed: %s", backup_fd, orig_fd,
                     strerror(errno));
        }

        log_debug("restore(ucrt): _close(%d) backup fd", backup_fd);
        _close(backup_fd);

        /* Bug 5 fix: after _dup2(backup_fd, orig_fd) the original fd is live
         * again — do NOT remove its table entry.  Removing it caused the
         * shell's fd tracking to diverge from reality: subsequent
         * fd_table_has_flag(fds, orig_fd, FD_REDIRECTED) calls returned false
         * even when another redirection was in flight, leading to double-saves
         * and leaked backup fds.
         *
         * The correct cleanup is:
         *   - Clear FD_REDIRECTED from orig_fd so it is known to be "normal"
         *     again.  (Leave any other flags, e.g. FD_CLOEXEC, intact.)
         *   - Remove the backup entry entirely — it is now closed and gone. */
        fd_table_clear_flag(fds, orig_fd, FD_REDIRECTED);

        fd_table_mark_closed(fds, backup_fd);
        fd_table_remove(fds, backup_fd);
    }

    log_debug("restore(ucrt): complete");
    xfree(saved_fds);
}
#endif

exec_status_t exec_apply_redirections_iso_c(exec_frame_t *frame, const exec_redirections_t *redirs)
{
    Expects_not_null(frame);
    Expects_not_null(redirs);
    exec_t *executor = frame->executor;

    if (redirs->count == 0)
        return EXEC_OK;
    return EXEC_NOT_IMPL;

#if 0
    // In ISO C mode the only I/O primitives available are the FILE* family
    // (fopen, fclose, freopen, fread, fwrite) and the only way to launch an
    // external program is system(). system() runs the command string through
    // the platform shell, so file redirections (< and >) can be embedded
    // directly in the command string that system() receives — the caller in
    // exec_command.c is responsible for assembling that string. There is
    // therefore no mechanism at this layer to redirect the standard streams
    // of an already-running process, and fd-to-fd duplication (>&N, <&N) has
    // no ISO C equivalent at all.
    //
    // The only case we can handle here is REDIR_TARGET_FILE with REDIR_READ,
    // REDIR_WRITE, or REDIR_APPEND on the three standard streams (fd 0/1/2),
    // by calling freopen() — which is the one redirection primitive that ISO C
    // does provide. Everything else is genuinely unsupported and must be
    // rejected so the caller can report a clean error rather than silently
    // producing wrong output.

    for (size_t i = 0; i < redirs->count; i++)
    {
        const exec_redirection_t *r = &redirs->items[i];

        // Resolve target fd: default to stdin (0) for reads, stdout (1) for writes.
        int fd = (r->explicit_fd >= 0 ? r->explicit_fd
                  : (r->type == REDIR_READ || r->type == REDIR_FROM_BUFFER ||
                     r->type == REDIR_FROM_BUFFER_STRIP)
                      ? 0
                      : 1);

        // freopen() can only target the three standard streams.
        if (fd != 0 && fd != 1 && fd != 2)
        {
            exec_set_error(
                executor,
                "ISO C mode: redirection to fd %d is not supported (only stdin/stdout/stderr)", fd);
            return EXEC_NOT_IMPL;
        }

        // FD-to-FD duplication, explicit close, and heredoc pipes have no ISO C equivalent.
        if (r->target_kind != REDIR_TARGET_FILE)
        {
            exec_set_error(executor, "ISO C mode: only file redirections are supported (< > >>), "
                                     "not fd duplications, explicit closes, or heredocs");
            return EXEC_NOT_IMPL;
        }

        // Expand the filename.
        string_t *fname_str = NULL;
        if (!r->target.file.is_expanded)
            fname_str = expand_redirection_target(frame, r->target.file.tok);
        else
            fname_str = string_create_from(r->target.file.filename);

        if (!fname_str)
        {
            exec_set_error(executor, "ISO C mode: failed to expand redirection filename");
            return EXEC_ERROR;
        }

        const char *fname = string_cstr(fname_str);

        // Map redirection type to an fopen mode string.
        const char *mode = NULL;
        switch (r->type)
        {
        case REDIR_READ:
            mode = "r";
            break;
        case REDIR_WRITE:
        case REDIR_WRITE_FORCE:
            mode = "w";
            break;
        case REDIR_APPEND:
            mode = "a";
            break;
        case REDIR_READWRITE:
            mode = "r+";
            break;
        default:
            exec_set_error(executor, "ISO C mode: unsupported redirection type %d for file target",
                           r->type);
            string_destroy(&fname_str);
            return EXEC_NOT_IMPL;
        }

        // freopen() atomically closes the stream, opens the file, and rebinds
        // the standard FILE* handle — it is the only ISO C way to redirect a
        // standard stream. stdin/stdout/stderr are the required C99 macros for
        // the three standard FILE* objects.
        FILE *target_stream = (fd == 0) ? stdin : (fd == 1) ? stdout : stderr;
        if (freopen(fname, mode, target_stream) == NULL)
        {
            exec_set_error(executor, "ISO C mode: freopen(\"%s\", \"%s\") failed: %s", fname, mode,
                           strerror(errno));
            string_destroy(&fname_str);
            return EXEC_ERROR;
        }

        // Record in the fd_table so exec_restore_redirections_iso_c() knows
        // which streams were redirected and need restoring.
        fd_table_t *fds = exec_frame_get_fds(frame);
        if (fds)
        {
            fd_table_add(fds, fd, FD_REDIRECTED, fname_str);
        }

        string_destroy(&fname_str);
    }

    return EXEC_OK;
#endif
}

void exec_restore_redirections_iso_c(exec_frame_t *frame)
{
    // ISO C provides no mechanism to restore a standard stream after freopen().
    //
    // freopen() is destructive: it closes the underlying resource before
    // opening the new file, so the original is gone by the time control
    // returns. ISO C has no dup(), no fileno(), no way to save a copy of a
    // FILE* before redirecting it, and no standard device paths to reopen.
    //
    // The practical consequence is that file redirections in ISO C mode are
    // permanent for the lifetime of the process. This is acceptable because
    // ISO C mode is only used on platforms where the only external-command
    // mechanism is system(), which forks its own shell process. That child
    // process inherits the redirected streams, runs the command, and exits —
    // after which *this* process's streams are no longer meaningful for that
    // command anyway. Builtin commands that run after a redirection will see
    // the redirected streams, which is a known limitation of ISO C mode.
    //
    // We still clean up the fd_table so it accurately reflects reality:
    // the streams remain redirected, so we leave FD_REDIRECTED set but mark
    // them to show that no restoration was possible.
    (void)frame;
}

void exec_restore_redirections(exec_frame_t *frame, const exec_redirections_t *redirections)
{
    (void)frame;
    (void)redirections;
    // No-op stub for linker
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
        exec_redirection_t *new_items =
            xrealloc(redirections->items, new_capacity * sizeof(exec_redirection_t));
        if (!new_items)
            return;

        redirections->items = new_items;
        redirections->capacity = new_capacity;
    }

    /* Copy the redirection */
    redirections->items[redirections->count] = *redir;
    redirections->count++;
}

exec_redirections_t *exec_redirections_clone(const exec_redirections_t *redirs)
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
#ifdef POSIX_API
    exec_status_t st = exec_apply_redirections_posix(frame, redirections);
#elif defined(UCRT_API)
    exec_status_t st = exec_apply_redirections_ucrt_c(frame, redirections);
#else
    exec_status_t st = exec_apply_redirections_iso_c(frame, redirections);
#endif
    return st == EXEC_OK ? 0 : -1;
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
exec_redirections_t *exec_redirections_from_ast(exec_frame_t *frame,
                                                const ast_node_list_t *ast_redirs)
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
                /* If all the parts are PART_LITERAL, let's construct the target.file.filename now.
                 */
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

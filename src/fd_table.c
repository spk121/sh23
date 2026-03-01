/**
 * @file fd_table.c
 * @brief Implementation of file descriptor table management
 */

#include <stdio.h>
#include <string.h>

#include "fd_table.h"

#include "logging.h"
#include "string_t.h"
#include "xalloc.h"

/* Initial capacity for the FD entries array */
#define INITIAL_CAPACITY 16

/* Growth factor when resizing the array */
#define GROWTH_FACTOR 2

static const char *fd_flags_to_string(fd_flags_t flags);

    /*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

fd_table_t *fd_table_get_global(void)
{
    // Stub: return NULL or a static instance if needed
    return NULL;
}

/**
 * @brief Find the index of an entry with the given FD
 *
 * @param table The FD table
 * @param fd File descriptor to search for
 * @return Index of the entry, or -1 if not found
 */
static int find_entry_index(const fd_table_t *table, int fd)
{
    for (size_t i = 0; i < table->count; i++)
    {
        if (table->entries[i].fd == fd)
        {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief Grow the entries array if needed
 *
 * @param table The FD table
 * @return true on success, false on allocation failure
 */
static bool ensure_capacity(fd_table_t *table)
{
    Expects_not_null(table);
    const int margin = 4; // Minimum free slots to maintain

    if (table->count + margin < table->capacity)
    {
        return true;
    }

    size_t new_capacity = table->capacity * GROWTH_FACTOR;
    fd_entry_t *new_entries = xrealloc(table->entries, new_capacity * sizeof(fd_entry_t));
    table->entries = new_entries;
    table->capacity = new_capacity;
    return true;
}

/**
 * @brief Clear an FD entry's resources
 *
 * @param entry The entry to clear
 */
static void clear_entry(fd_entry_t *entry)
{
    // Do not clear or destroy entry->path here; caller is responsible for freeing.
    entry->fd = -1;
    entry->original_fd = -1;
    entry->flags = FD_NONE;
    entry->is_open = false;
    entry->padding[0] = 0;
}

/*
 * ============================================================================
 * Lifecycle Management
 * ============================================================================
 */
fd_table_t *fd_table_create(void)
{
    fd_table_t *table = xmalloc(sizeof(fd_table_t));
    if (table == NULL)
    {
        return NULL;
    }

    table->entries = xmalloc(INITIAL_CAPACITY * sizeof(fd_entry_t));
    if (table->entries == NULL)
    {
        xfree(table);
        return NULL;
    }

    table->capacity = INITIAL_CAPACITY;
    table->count = 0;
    table->highest_fd = -1;

    return table;
}

fd_table_t *fd_table_clone(const fd_table_t *src)
{
    Expects_not_null(src);
    fd_table_t *table = xmalloc(sizeof(fd_table_t)); // Don't use fd_table_create()

    table->capacity = src->capacity;
    table->count = src->count;
    table->highest_fd = src->highest_fd;
    table->entries = xcalloc(table->capacity, sizeof(fd_entry_t));

    for (size_t i = 0; i < src->count; i++)
    {
        table->entries[i].fd = src->entries[i].fd;
        table->entries[i].original_fd = src->entries[i].original_fd;
        table->entries[i].flags = src->entries[i].flags;
        table->entries[i].is_open = src->entries[i].is_open;
        table->entries[i].path =
            src->entries[i].path ? string_create_from(src->entries[i].path) : NULL;
    }
    return table;
}

void fd_table_destroy(fd_table_t **table)
{
    if (table == NULL || *table == NULL)
    {
        return;
    }

    fd_table_t *t = *table;

    /* Clear all entries */
    for (int i = 0; i < t->count; i++)
    {
        clear_entry(&t->entries[i]);
        if (t->entries[i].path)
        {
            string_destroy(&t->entries[i].path);
        }
    }

    xfree(t->entries);
    xfree(t);
    *table = NULL;
}

/*
 * ============================================================================
 * Entry Management
 * ============================================================================
 */

string_t *fd_table_generate_name(int fd, fd_flags_t flags)
{
    char buffer[64];
    if (flags & FD_SAVED)
    {
        /* Shouldn't happen, but fallback */
        snprintf(buffer, sizeof(buffer), "(saved copy of fd %d)", fd);
    }
    else
    {
        if (fd == 0)
        {
            snprintf(buffer, sizeof(buffer), "(stdin)");
        }
        else if (fd == 1)
        {
            snprintf(buffer, sizeof(buffer), "(stdout)");
        }
        else if (fd == 2)
        {
            snprintf(buffer, sizeof(buffer), "(stderr)");
        }
        else if (fd < 0)
        {
            snprintf(buffer, sizeof(buffer), "(invalid fd %d)", fd);
        }
        else
        {
            snprintf(buffer, sizeof(buffer), "(fd %d)", fd);
        }
    }
    return string_create_from_cstr(buffer);
}

string_t *fd_table_generate_name_ex(int new_fd, int orig_fd, fd_flags_t flags)
{
    char buffer[64];
    char *saved_str = (flags & FD_SAVED) ? "saved copy of " : "";
    char *redirected_str = (flags & FD_REDIRECTED) ? "redirected " : "";
    if (orig_fd >= 0)
    {
        snprintf(buffer, sizeof(buffer), "(%s%sfd %d)", saved_str, redirected_str, orig_fd);
        return string_create_from_cstr(buffer);
    }
    /* Shouldn't happen, but fallback */
    return fd_table_generate_name(new_fd, flags);
}

string_t *fd_table_generate_heredoc_name(int target_fd)
{
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "(heredoc to fd %d)", target_fd);
    return string_create_from_cstr(buffer);
}

bool fd_table_add(fd_table_t *table, int fd, fd_flags_t flags, const string_t *path)
{
    Expects_not_null(table);
    Expects_not_null(path);

    /* Check if entry already exists */
    int idx = find_entry_index(table, fd);
    if (idx >= 0)
    {
        /* Update existing entry */
        fd_entry_t *entry = &table->entries[idx];

        /* Clear old path if present */
        if (entry->path != NULL)
        {
            string_set(entry->path, path);
        }
        else
        {
            entry->path = string_create_from(path);
        }

        entry->flags = flags;
        entry->is_open = true;

        /* IMPORTANT: do NOT touch original_fd here.
         * That field is owned exclusively by fd_table_mark_saved() and must
         * survive flag / path updates performed by fd_table_add().  Resetting
         * it to -1 on every FD_SAVED update caused the restore path to lose
         * the original-fd mapping, leaving the redirected descriptor live
         * forever (Bug 1). */
        char *flags_str = xstrdup(fd_flags_to_string(flags));
        log_debug("fd_table_add: fd=%d path='%s' updated flags=%s", fd, string_cstr(path), flags_str);
        xfree(flags_str);
        return true;
    }

    /* Need to add new entry */
    ensure_capacity(table);

    /* Add new entry */
    fd_entry_t *entry = &table->entries[table->count];
    entry->fd = fd;
    entry->original_fd = -1;
    entry->flags = flags;
    entry->path = string_create_from(path);
    entry->is_open = true;

    table->count++;

    /* Update highest_fd */
    if (fd > table->highest_fd)
    {
        table->highest_fd = fd;
    }
    log_debug("fd_table_add: fd=%d path='%s' new entry flags=%s", fd, string_cstr(path), fd_flags_to_string(flags));
    return true;
}

bool fd_table_mark_saved(fd_table_t *table, int saved_fd, int original_fd)
{
    Expects_not_null(table);

    /* Check if entry exists */
    int idx = find_entry_index(table, saved_fd);
    if (idx >= 0)
    {
        /* Update existing entry */
        fd_entry_t *entry = &table->entries[idx];
        entry->original_fd = original_fd;
        entry->flags = (fd_flags_t)(entry->flags | FD_SAVED);
        log_debug("fd_table_mark_saved: fd=%d marked as saved copy of fd=%d", saved_fd,
                  original_fd);
        return true;
    }

    /* Shouldn't happen, but, create new entry for saved FD */
    ensure_capacity(table);

    fd_entry_t *entry = &table->entries[table->count];
    entry->fd = saved_fd;
    entry->original_fd = original_fd;
    entry->flags = FD_SAVED;
    entry->path = string_create_from_cstr("(unknown)");
    entry->is_open = true;

    table->count++;

    if (saved_fd > table->highest_fd)
    {
        table->highest_fd = saved_fd;
    }

    log_warn("fd_table_mark_saved: untracked fd=%d marked as saved copy of fd=%d", saved_fd,
             original_fd);
    return true;
}

bool fd_table_mark_closed(fd_table_t *table, int fd)
{
    Expects_not_null(table);

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        return false;
    }

    table->entries[idx].is_open = false;
    log_debug("fd_table_mark_closed: fd=%d marked as closed", fd);
    return true;
}

bool fd_table_mark_open(fd_table_t *table, int fd)
{
    Expects_not_null(table);
    int idx = find_entry_index(table, fd);
    if (idx < 0)
        return false;
    table->entries[idx].is_open = true;
    log_debug("fd_table_mark_open: fd=%d marked as open", fd);
    return true;
}

bool fd_table_remove(fd_table_t *table, int fd)
{
    Expects_not_null(table);

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        log_warn("fd_table_remove: fd=%d not found in table, cannot remove", fd);
        return false;
    }

    log_debug("fd_table_remove: fd=%d path='%s' removing entry", fd,
              string_cstr(table->entries[idx].path));

    /* Clear entry resources */
    clear_entry(&table->entries[idx]);
    if (table->entries[idx].path)
    {
        string_destroy(&table->entries[idx].path);
    }

    /* Move last entry to fill gap */
    if ((size_t)idx < table->count - 1)
    {
        table->entries[idx] = table->entries[table->count - 1];
    }

    table->count--;

    /* Recalculate highest_fd if we removed it */
    if (fd == table->highest_fd)
    {
        table->highest_fd = -1;
        for (size_t i = 0; i < table->count; i++)
        {
            if (table->entries[i].fd > table->highest_fd)
            {
                table->highest_fd = table->entries[i].fd;
            }
        }
    }

    return true;
}

/*
 * ============================================================================
 * Query Operations
 * ============================================================================
 */

fd_entry_t *fd_table_find(fd_table_t *table, int fd)
{
    Expects_not_null(table);

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        return NULL;
    }

    return &table->entries[idx];
}

bool fd_table_is_open(const fd_table_t *table, int fd)
{
    Expects_not_null(table);

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        return false;
    }

    return table->entries[idx].is_open;
}

fd_flags_t fd_table_get_flags(const fd_table_t *table, int fd)
{
    Expects_not_null(table);

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        return FD_NONE;
    }

    return table->entries[idx].flags;
}

bool fd_table_has_flag(const fd_table_t *table, int fd, fd_flags_t flag)
{
    Expects_not_null(table);

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        return false;
    }

    return (table->entries[idx].flags & flag) != 0;
}

int fd_table_get_original(const fd_table_t *table, int fd)
{
    Expects_not_null(table);

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        return -1;
    }

    return table->entries[idx].original_fd;
}

const string_t *fd_table_get_path(const fd_table_t *table, int fd)
{
    Expects_not_null(table);

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        return NULL;
    }

    return table->entries[idx].path;
}

/*
 * ============================================================================
 * Flag Manipulation
 * ============================================================================
 */

bool fd_table_set_flag(fd_table_t *table, int fd, fd_flags_t flag)
{
    Expects_not_null(table);

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        return false;
    }

    table->entries[idx].flags = (fd_flags_t)(table->entries[idx].flags | flag);
    char *flag_str = xstrdup(fd_flags_to_string(flag));
    char *resulting_str = xstrdup(fd_flags_to_string(table->entries[idx].flags));
    log_debug("fd_table_set_flag: fd=%d set flag=%s resulting_flags=%s", fd, flag_str, resulting_str);
    xfree(flag_str);
    xfree(resulting_str);
    return true;
}

bool fd_table_clear_flag(fd_table_t *table, int fd, fd_flags_t flag)
{
    Expects_not_null(table);

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        return false;
    }

    table->entries[idx].flags = (fd_flags_t)(table->entries[idx].flags & ~flag);
    log_debug("fd_table_clear_flag: fd=%d cleared flag=%s remaining_flags=%s", fd, fd_flags_to_string(flag),
              fd_flags_to_string(table->entries[idx].flags));
    return true;
}

/*
 * ============================================================================
 * Utility Operations
 * ============================================================================
 */

int *fd_table_get_fds_with_flag(const fd_table_t *table, fd_flags_t flag, size_t *out_count)
{
    Expects_not_null(table);
    Expects_not_null(out_count);

    /* First pass: count matching entries */
    size_t count = 0;
    for (size_t i = 0; i < table->count; i++)
    {
        if (table->entries[i].flags & flag)
        {
            count++;
        }
    }

    *out_count = count;

    if (count == 0)
    {
        return NULL;
    }

    /* Allocate result array */
    int *fds = xmalloc(count * sizeof(int));

    /* Second pass: fill array */
    size_t j = 0;
    for (size_t i = 0; i < table->count; i++)
    {
        if (table->entries[i].flags & flag)
        {
            fds[j++] = table->entries[i].fd;
        }
    }

    return fds;
}

int *fd_table_get_saved_fds(const fd_table_t *table, size_t *saved_count)
{
    return fd_table_get_fds_with_flag(table, FD_SAVED, saved_count);
}

int fd_table_get_original_fd(const fd_table_t *table, int saved_fd)
{
    Expects_not_null(table);

    int idx = find_entry_index(table, saved_fd);
    if (idx < 0)
    {
        return -1;
    }

    return table->entries[idx].original_fd;
}

size_t fd_table_count(const fd_table_t *table)
{
    Expects_not_null(table);
    return table->count;
}

int fd_table_get_highest_fd(const fd_table_t *table)
{
    Expects_not_null(table);

    return table->highest_fd;
}

void fd_table_foreach(const fd_table_t *table, fd_table_foreach_cb callback, void *user_data)
{
    Expects_not_null(table);
    Expects_not_null(callback);

    for (size_t i = 0; i < table->count; i++)
    {
        const fd_entry_t *entry = &table->entries[i];

        /* Skip logically removed / invalid entries if you ever have them */
        if (entry->fd < 0)
            continue;

        if (!callback(entry, user_data))
        {
            break; /* User requested early termination */
        }
    }
}

/* Forward declaration of the callback */
static bool fd_table_dump_cb(const fd_entry_t *entry, void *user_data);

/**
 * Dump helper context
 */
struct dump_ctx
{
    const char *prefix;
    FILE *out;
};

void fd_table_dump(const fd_table_t *table, const char *prefix)
{
    if (table == NULL)
    {
        fprintf(stderr, "(null fd_table)\n");
        return;
    }

    struct dump_ctx ctx = {.prefix = prefix ? prefix : "", .out = stderr};

    fprintf(ctx.out, "%sFD Table (%zu entries, highest fd = %d):\n", ctx.prefix, table->count,
            table->highest_fd);

    if (table->count == 0)
    {
        fprintf(ctx.out, "%s  <empty>\n", ctx.prefix);
        return;
    }

    fd_table_foreach(table, fd_table_dump_cb, &ctx);
}

static const char *fd_flags_to_string(fd_flags_t flags)
{
    if (flags == FD_NONE)
    {
        return "none";
    }
    static char buffer[64];
    buffer[0] = '\0';
    bool first = true;
    if (flags & FD_CLOEXEC)
    {
        strcat(buffer, "CLOEXEC");
        first = false;
    }
    if (flags & FD_REDIRECTED)
    {
        strcat(buffer, first ? "REDIR" : "|REDIR");
        first = false;
    }
    if (flags & FD_SAVED)
    {
        strcat(buffer, first ? "SAVED" : "|SAVED");
        first = false;
    }
    return buffer;
}

static bool fd_table_dump_cb(const fd_entry_t *entry, void *user_data)
{
    const struct dump_ctx *ctx = user_data;
    const char *prefix = ctx->prefix;
    FILE *out = ctx->out;

    const char *path_str = entry->path ? string_cstr(entry->path) : "(no path)";

    fprintf(out, "%s  fd %-3d  open=%-5s  flags=", prefix, entry->fd,
            entry->is_open ? "yes" : "no ");

    if (entry->flags == FD_NONE)
    {
        fprintf(out, "none");
    }
    else
    {
        bool first = true;
        if (entry->flags & FD_CLOEXEC)
        {
            fprintf(out, "%sCLOEXEC", first ? "" : "|");
            first = false;
        }
        if (entry->flags & FD_REDIRECTED)
        {
            fprintf(out, "%sREDIR", first ? "" : "|");
            first = false;
        }
        if (entry->flags & FD_SAVED)
        {
            fprintf(out, "%sSAVED", first ? "" : "|");
            first = false;
        }
    }

    fprintf(out, "  orig=%-3d  path=\"%s\"", entry->original_fd, path_str);

    fprintf(out, "\n");

    return true; // continue iterating
}

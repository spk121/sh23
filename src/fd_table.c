/**
 * @file fd_table.c
 * @brief Implementation of file descriptor table management
 */

#include "fd_table.h"
#include "xalloc.h"
#include <string.h>

/* Initial capacity for the FD entries array */
#define INITIAL_CAPACITY 16

/* Growth factor when resizing the array */
#define GROWTH_FACTOR 2

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

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
    if (table->count < table->capacity)
    {
        return true;
    }

    size_t new_capacity = table->capacity * GROWTH_FACTOR;
    fd_entry_t *new_entries = xrealloc(table->entries, new_capacity * sizeof(fd_entry_t));
    if (new_entries == NULL)
    {
        return false;
    }

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
    if (entry->path != NULL)
    {
        string_destroy(&entry->path);
        entry->path = NULL;
    }
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

void fd_table_destroy(fd_table_t **table)
{
    if (table == NULL || *table == NULL)
    {
        return;
    }

    fd_table_t *t = *table;

    /* Free all paths in entries */
    for (size_t i = 0; i < t->count; i++)
    {
        clear_entry(&t->entries[i]);
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

bool fd_table_add(fd_table_t *table, int fd, fd_flags_t flags, string_t *path)
{
    if (table == NULL)
    {
        return false;
    }

    /* Check if entry already exists */
    int idx = find_entry_index(table, fd);
    if (idx >= 0)
    {
        /* Update existing entry */
        fd_entry_t *entry = &table->entries[idx];
        
        /* Clear old path if present */
        if (entry->path != NULL)
        {
            string_destroy(&entry->path);
        }

        entry->flags = flags;
        entry->path = path;
        entry->is_open = true;
        
        /* Update original_fd only if FD_SAVED is set */
        if (!(flags & FD_SAVED))
        {
            entry->original_fd = -1;
        }

        return true;
    }

    /* Need to add new entry */
    if (!ensure_capacity(table))
    {
        /* Cleanup path since we're taking ownership */
        if (path != NULL)
        {
            string_destroy(&path);
        }
        return false;
    }

    /* Add new entry */
    fd_entry_t *entry = &table->entries[table->count];
    entry->fd = fd;
    entry->original_fd = -1;
    entry->flags = flags;
    entry->path = path;
    entry->is_open = true;

    table->count++;

    /* Update highest_fd */
    if (fd > table->highest_fd)
    {
        table->highest_fd = fd;
    }

    return true;
}

bool fd_table_mark_saved(fd_table_t *table, int saved_fd, int original_fd)
{
    if (table == NULL)
    {
        return false;
    }

    /* Check if entry exists */
    int idx = find_entry_index(table, saved_fd);
    if (idx >= 0)
    {
        /* Update existing entry */
        fd_entry_t *entry = &table->entries[idx];
        entry->original_fd = original_fd;
        entry->flags = (fd_flags_t)(entry->flags | FD_SAVED);
        return true;
    }

    /* Create new entry for saved FD */
    if (!ensure_capacity(table))
    {
        return false;
    }

    fd_entry_t *entry = &table->entries[table->count];
    entry->fd = saved_fd;
    entry->original_fd = original_fd;
    entry->flags = FD_SAVED;
    entry->path = NULL;
    entry->is_open = true;

    table->count++;

    if (saved_fd > table->highest_fd)
    {
        table->highest_fd = saved_fd;
    }

    return true;
}

bool fd_table_mark_closed(fd_table_t *table, int fd)
{
    if (table == NULL)
    {
        return false;
    }

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        return false;
    }

    table->entries[idx].is_open = false;
    return true;
}

bool fd_table_remove(fd_table_t *table, int fd)
{
    if (table == NULL)
    {
        return false;
    }

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        return false;
    }

    /* Clear entry resources */
    clear_entry(&table->entries[idx]);

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
    if (table == NULL)
    {
        return NULL;
    }

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        return NULL;
    }

    return &table->entries[idx];
}

bool fd_table_is_open(const fd_table_t *table, int fd)
{
    if (table == NULL)
    {
        return false;
    }

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        return false;
    }

    return table->entries[idx].is_open;
}

fd_flags_t fd_table_get_flags(const fd_table_t *table, int fd)
{
    if (table == NULL)
    {
        return FD_NONE;
    }

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        return FD_NONE;
    }

    return table->entries[idx].flags;
}

bool fd_table_has_flag(const fd_table_t *table, int fd, fd_flags_t flag)
{
    if (table == NULL)
    {
        return false;
    }

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        return false;
    }

    return (table->entries[idx].flags & flag) != 0;
}

int fd_table_get_original(const fd_table_t *table, int fd)
{
    if (table == NULL)
    {
        return -1;
    }

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        return -1;
    }

    return table->entries[idx].original_fd;
}

const string_t *fd_table_get_path(const fd_table_t *table, int fd)
{
    if (table == NULL)
    {
        return NULL;
    }

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
    if (table == NULL)
    {
        return false;
    }

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        return false;
    }

    table->entries[idx].flags = (fd_flags_t)(table->entries[idx].flags | flag);
    return true;
}

bool fd_table_clear_flag(fd_table_t *table, int fd, fd_flags_t flag)
{
    if (table == NULL)
    {
        return false;
    }

    int idx = find_entry_index(table, fd);
    if (idx < 0)
    {
        return false;
    }

    table->entries[idx].flags = (fd_flags_t)(table->entries[idx].flags & ~flag);
    return true;
}

/*
 * ============================================================================
 * Utility Operations
 * ============================================================================
 */

int *fd_table_get_fds_with_flag(const fd_table_t *table, fd_flags_t flag, size_t *out_count)
{
    if (table == NULL || out_count == NULL)
    {
        if (out_count != NULL)
        {
            *out_count = 0;
        }
        return NULL;
    }

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
    if (fds == NULL)
    {
        *out_count = 0;
        return NULL;
    }

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

size_t fd_table_count(const fd_table_t *table)
{
    if (table == NULL)
    {
        return 0;
    }

    return table->count;
}

int fd_table_get_highest_fd(const fd_table_t *table)
{
    if (table == NULL)
    {
        return -1;
    }

    return table->highest_fd;
}

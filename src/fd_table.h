/**
 * @file fd_table.h
 * @brief File descriptor table for tracking shell redirections and FD state
 *
 * This module provides a dynamic table for tracking file descriptors used
 * by the shell, including their flags, origins, and associated paths.
 * It helps manage redirections, close-on-exec behavior, and saved FD copies.
 *
 * The table grows dynamically as needed and tracks:
 * - Which FDs are open
 * - Which FDs should be closed on exec
 * - Which FDs were created by redirections
 * - Which FDs are saved copies of other FDs
 * - The file paths associated with opened FDs
 */

#ifndef FD_TABLE_H
#define FD_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include "string_t.h"

/**
 * @brief Flags that can be associated with file descriptors
 */
typedef enum fd_flags_t
{
    FD_NONE = 0,
    FD_CLOEXEC = 1 << 0,    ///< Close-on-exec flag (should be closed in child)
    FD_REDIRECTED = 1 << 1, ///< FD was created by shell redirection
    FD_SAVED = 1 << 2,      ///< FD is a saved copy of another FD
} fd_flags_t;

/**
 * @brief Entry representing a single file descriptor
 */
typedef struct fd_entry_t
{
    int fd;           ///< File descriptor number
    int original_fd;  ///< If FD_SAVED, what FD was this a copy of? (-1 otherwise)
    fd_flags_t flags; ///< Flags for this FD
    bool is_open;     ///< Whether this FD is currently open
    char padding[3];
    string_t *path;   ///< Path if opened from file (NULL otherwise, takes ownership)
} fd_entry_t;

/**
 * @brief Dynamic table of file descriptor entries
 */
typedef struct fd_table_t
{
    fd_entry_t *entries; ///< Dynamic array of FD entries
    size_t capacity;     ///< Current capacity of array
    size_t count;        ///< Number of entries in use
    int highest_fd;      ///< Highest FD number currently tracked
    int padding;
} fd_table_t;

/*
 * ============================================================================
 * Lifecycle Management
 * ============================================================================
 */

/**
 * @brief Create a new file descriptor table
 *
 * Allocates and initializes a new FD table with default capacity.
 *
 * @return Pointer to new fd_table_t, or NULL on allocation failure
 */
fd_table_t *fd_table_create(void);

/**
 * @brief Create a deep copy of an FD table
 *
 * Copies all entries and their associated data.
 *
 * @param src Source FD table to copy
 * @return Pointer to new fd_table_t copy, or NULL on allocation failure
 */
fd_table_t *fd_table_copy(const fd_table_t *src);

/**
 * @brief Destroy an FD table and free all resources
 *
 * Frees all entries, their associated paths, and the table itself.
 * Does not close the actual file descriptors.
 *
 * @param table Pointer to pointer to fd_table_t (set to NULL after free)
 */
void fd_table_destroy(fd_table_t **table);

/*
 * ============================================================================
 * Entry Management
 * ============================================================================
 */

/**
 * @brief Add or update an entry in the FD table
 *
 * If an entry for the given FD already exists, it will be updated.
 * Otherwise, a new entry is created.
 *
 * @param table The FD table
 * @param fd File descriptor number
 * @param flags Flags to associate with this FD
 * @param path Optional path (takes ownership, may be NULL)
 * @return true on success, false on allocation failure
 */
bool fd_table_add(fd_table_t *table, int fd, fd_flags_t flags, string_t *path);

/**
 * @brief Mark an FD as a saved copy of another FD
 *
 * Records that saved_fd is a duplicate of original_fd (e.g., from dup2).
 * Sets the FD_SAVED flag and records the original FD number.
 *
 * @param table The FD table
 * @param saved_fd The duplicated file descriptor
 * @param original_fd The original file descriptor that was duplicated
 * @return true on success, false on allocation failure
 */
bool fd_table_mark_saved(fd_table_t *table, int saved_fd, int original_fd);

/**
 * @brief Mark an FD as closed in the table
 *
 * Sets is_open to false for the given FD. Does not actually close the FD.
 *
 * @param table The FD table
 * @param fd File descriptor number
 * @return true if FD was found and marked closed, false if not found
 */
bool fd_table_mark_closed(fd_table_t *table, int fd);

/**
 * @brief Remove an entry from the FD table
 *
 * Removes the entry and frees associated resources (like path string).
 * Does not actually close the file descriptor.
 *
 * @param table The FD table
 * @param fd File descriptor number to remove
 * @return true if entry was found and removed, false if not found
 */
bool fd_table_remove(fd_table_t *table, int fd);

/*
 * ============================================================================
 * Query Operations
 * ============================================================================
 */

/**
 * @brief Find an entry in the FD table
 *
 * @param table The FD table
 * @param fd File descriptor number to find
 * @return Pointer to fd_entry_t if found, NULL otherwise
 */
fd_entry_t *fd_table_find(fd_table_t *table, int fd);

/**
 * @brief Check if an FD is marked as open in the table
 *
 * @param table The FD table
 * @param fd File descriptor number
 * @return true if FD exists and is marked open, false otherwise
 */
bool fd_table_is_open(const fd_table_t *table, int fd);

/**
 * @brief Get the flags for a file descriptor
 *
 * @param table The FD table
 * @param fd File descriptor number
 * @return Flags for the FD, or FD_NONE if not found
 */
fd_flags_t fd_table_get_flags(const fd_table_t *table, int fd);

/**
 * @brief Check if an FD has a specific flag set
 *
 * @param table The FD table
 * @param fd File descriptor number
 * @param flag Flag to check for
 * @return true if FD exists and has the flag, false otherwise
 */
bool fd_table_has_flag(const fd_table_t *table, int fd, fd_flags_t flag);

/**
 * @brief Get the original FD number if this is a saved copy
 *
 * @param table The FD table
 * @param fd File descriptor number to query
 * @return Original FD number if FD_SAVED is set, -1 otherwise
 */
int fd_table_get_original(const fd_table_t *table, int fd);

/**
 * @brief Get the path associated with an FD
 *
 * @param table The FD table
 * @param fd File descriptor number
 * @return Pointer to path string_t, or NULL if no path or FD not found
 */
const string_t *fd_table_get_path(const fd_table_t *table, int fd);

/*
 * ============================================================================
 * Flag Manipulation
 * ============================================================================
 */

/**
 * @brief Set a flag on an FD
 *
 * Adds the specified flag to the FD's flags (bitwise OR).
 *
 * @param table The FD table
 * @param fd File descriptor number
 * @param flag Flag to set
 * @return true if FD was found and flag set, false if not found
 */
bool fd_table_set_flag(fd_table_t *table, int fd, fd_flags_t flag);

/**
 * @brief Clear a flag from an FD
 *
 * Removes the specified flag from the FD's flags (bitwise AND NOT).
 *
 * @param table The FD table
 * @param fd File descriptor number
 * @param flag Flag to clear
 * @return true if FD was found and flag cleared, false if not found
 */
bool fd_table_clear_flag(fd_table_t *table, int fd, fd_flags_t flag);

/*
 * ============================================================================
 * Utility Operations
 * ============================================================================
 */

/**
 * @brief Get a list of all FDs with a specific flag
 *
 * Allocates an array containing all FD numbers that have the specified flag.
 * Caller must free the returned array.
 *
 * @param table The FD table
 * @param flag Flag to search for
 * @param out_count Pointer to receive count of FDs found
 * @return Dynamically allocated array of FD numbers, or NULL if none found
 */
int *fd_table_get_fds_with_flag(const fd_table_t *table, fd_flags_t flag, size_t *out_count);

/**
 * @brief Count entries in the FD table
 *
 * @param table The FD table
 * @return Number of entries currently in the table
 */
size_t fd_table_count(const fd_table_t *table);

/**
 * @brief Get the highest FD number in the table
 *
 * @param table The FD table
 * @return Highest FD number, or -1 if table is empty
 */
int fd_table_get_highest_fd(const fd_table_t *table);

#endif /* FD_TABLE_H */

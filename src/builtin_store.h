#ifndef BUILTIN_STORE_H
#define BUILTIN_STORE_H

/**
 * @file builtin_store.h
 * @brief Hash-based registry mapping builtin command names to their
 *        implementations and metadata.
 *
 * This is an internal data structure used by the executor.  Library
 * consumers register builtins through the public exec.h API
 * (exec_register_builtin, etc.) and should not include this header
 * directly.
 *
 * The store uses open addressing with linear probing and a FNV-1a hash
 * of the command name.  It grows automatically when the load factor
 * exceeds a threshold.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "exec_types_internal.h"
#include "miga/type_pub.h"
#include "miga/strlist.h"

/* ── Forward declarations ────────────────────────────────────────────────── */

/* ============================================================================
 * Entry
 * ============================================================================ */

/**
 * State of a hash-table slot.
 */
typedef enum builtin_slot_state_t
{
    BUILTIN_SLOT_EMPTY,    /**< Never used                             */
    BUILTIN_SLOT_OCCUPIED, /**< Contains a live entry                  */
    BUILTIN_SLOT_TOMBSTONE /**< Previously occupied, now deleted        */
} builtin_slot_state_t;

/**
 * A single entry in the builtin store.
 */
typedef struct builtin_entry_t
{
    builtin_slot_state_t state;
    uint32_t hash;               /**< Cached FNV-1a hash of the name  */
    char *name;                  /**< Heap-allocated copy of the name */
    miga_builtin_fn_t fn;             /**< Implementation function         */
    miga_builtin_category_t category; /**< Special or regular              */
} builtin_entry_t;

/* ============================================================================
 * Store
 * ============================================================================ */

/**
 * The builtin store itself.
 */
typedef struct builtin_store_t
{
    builtin_entry_t *entries; /**< Hash table (open addressing)        */
    size_t capacity;          /**< Number of slots allocated           */
    size_t count;             /**< Number of live (OCCUPIED) entries    */
    size_t tombstones;        /**< Number of TOMBSTONE slots           */
} builtin_store_t;

/* ============================================================================
 * Lifecycle
 * ============================================================================ */

/**
 * Create a new, empty builtin store.
 * @return A new store, or NULL on allocation failure.
 */
builtin_store_t *builtin_store_create(void);

/**
 * Destroy a builtin store and free all associated memory.
 * Safe to call with a pointer to NULL.
 */
void builtin_store_destroy(builtin_store_t **store_ptr);

/* ============================================================================
 * Mutation
 * ============================================================================ */

/**
 * Register a builtin.
 *
 * If a builtin with the given name already exists, its function pointer
 * and category are replaced.
 *
 * @param store     The builtin store.
 * @param name      The command name (will be copied).
 * @param fn        The implementation function (must not be NULL).
 * @param category  Special or regular.
 * @return true on success, false on failure (NULL args, alloc failure).
 */
bool builtin_store_set(builtin_store_t *store, const char *name, miga_builtin_fn_t fn,
                       miga_builtin_category_t category);

/**
 * Remove a builtin by name.
 *
 * @return true if the builtin was found and removed, false otherwise.
 */
bool builtin_store_remove(builtin_store_t *store, const char *name);

/**
 * Remove all entries from the store.
 */
void builtin_store_clear(builtin_store_t *store);

/* ============================================================================
 * Lookup
 * ============================================================================ */

/**
 * Check whether a builtin with the given name exists.
 */
bool builtin_store_has(const builtin_store_t *store, const char *name);

/**
 * Look up a builtin's function pointer by name.
 *
 * @return The function pointer, or NULL if not found.
 */
miga_builtin_fn_t builtin_store_get(const builtin_store_t *store, const char *name);

/**
 * Look up a builtin's function pointer and category.
 *
 * @param store         The builtin store.
 * @param name          The command name.
 * @param fn_out        If non-NULL, receives the function pointer.
 * @param category_out  If non-NULL, receives the category.
 * @return true if found, false otherwise.
 */
bool builtin_store_lookup(const builtin_store_t *store, const char *name, miga_builtin_fn_t *fn_out,
                          miga_builtin_category_t *category_out);

/* ============================================================================
 * Queries
 * ============================================================================ */

/**
 * Get the number of registered builtins.
 */
size_t builtin_store_count(const builtin_store_t *store);

/* ============================================================================
 * Iteration
 * ============================================================================ */

/**
 * Callback for iterating over all registered builtins.
 *
 * @param name      The builtin name.
 * @param fn        The function pointer.
 * @param category  Special or regular.
 * @param context   User-provided opaque pointer.
 */
typedef void (*builtin_store_iter_fn_t)(const char *name, miga_builtin_fn_t fn,
                                        miga_builtin_category_t category, void *context);

/**
 * Iterate over all registered builtins, calling the callback for each.
 * The iteration order is unspecified.
 */
void builtin_store_for_each(const builtin_store_t *store, builtin_store_iter_fn_t callback,
                            void *context);

/* ============================================================================
 * Stream Accessors
 * ============================================================================
 *
 * On POSIX and UCRT platforms, builtins use the standard stdin/stdout/stderr
 * file pointers directly.  On ISO C platforms (where fd-level redirection is
 * not available), the frame may carry overridden FILE* pointers that builtins
 * should use instead.  These accessors return the correct stream for the
 * current platform and frame.
 */

FILE *builtin_stdin(miga_frame_t *frame);
FILE *builtin_stdout(miga_frame_t *frame);
FILE *builtin_stderr(miga_frame_t *frame);

/* ============================================================================
 * Default Builtin Registration
 * ============================================================================ */

/**
 * Register all built-in commands (special and regular) into the given store.
 *
 * @param store  The builtin store to populate.
 * @return true if all builtins were registered successfully, false on failure.
 */
bool builtins_init_default(builtin_store_t *store);

#endif /* BUILTIN_STORE_H */

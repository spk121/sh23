#ifndef ALIAS_STORE_H
#define ALIAS_STORE_H

#include "string_t.h"
#include <stdbool.h>

/**
 * @file alias_store.h
 * @brief Public API for the shell alias store.
 *
 * MEMORY SAFETY CONTRACT:
 * ========================
 * The alias store is a self-contained memory silo.
 *
 * 1. INPUTS: All functions that modify the store accept non-immediate arguments
 *    as const pointers and deep-copy any data before incorporating it. The caller
 *    retains full ownership of all arguments passed in.
 *
 * 2. OUTPUTS: Functions that return internal data do so via const pointers.
 *    Returned pointers are valid only until the next mutating operation on
 *    the store. Do not free or modify them.
 *
 * 3. MOVE SEMANTICS: Functions with "move" in the name take ownership of
 *    pointer arguments (passed as T**) and set the source pointer to NULL.
 *
 * The internal alias_t and alias_array_t types are not exposed through this header.
 */

/* Forward declarations -- opaque to public consumers */
typedef struct alias_array_t alias_array_t;

/**
 * Shell alias store.
 */
typedef struct alias_store_t
{
    /** Internal array. Do not access directly. */
    alias_array_t *aliases;
} alias_store_t;

/**
 * Callback function for iterating over aliases.
 *
 * @param name Alias name (const -- do not modify or free).
 * @param value Alias value (const -- do not modify or free).
 * @param user_data User-provided context pointer.
 */
typedef void (*alias_store_foreach_fn)(const string_t *name, const string_t *value,
                                       void *user_data);

/* ============================================================================
 * Validation
 * ============================================================================ */

/**
 * Check if a C-string is a valid alias name.
 *
 * @param name The name to validate.
 * @return true if valid.
 */
bool alias_name_is_valid(const char *name);

/* ============================================================================
 * Constructors and Destructors
 * ============================================================================ */

/**
 * Create a new, empty alias store.
 *
 * @return Newly allocated alias store.
 */
alias_store_t *alias_store_create(void);

/**
 * Create a deep copy of an alias store.
 * The returned store is fully independent (no shared pointers).
 *
 * @param other Source alias store (must not be NULL).
 * @return Newly allocated clone.
 */
alias_store_t *alias_store_clone(const alias_store_t *other);

/**
 * Destroy an alias store and free all resources.
 *
 * @param store Pointer to the store pointer; set to NULL on return.
 */
void alias_store_destroy(alias_store_t **store);

/* ============================================================================
 * Modifiers (deep-copy inputs)
 * ============================================================================ */

/**
 * Add or replace an alias.
 * Both name and value are deep-copied; the caller retains ownership.
 *
 * @param store The alias store.
 * @param name Alias name (deep-copied).
 * @param value Alias value (deep-copied).
 */
void alias_store_add(alias_store_t *store, const string_t *name, const string_t *value);

/**
 * Add or replace an alias using C-strings.
 * Both name and value are deep-copied; the caller retains ownership.
 *
 * @param store The alias store.
 * @param name Alias name (deep-copied).
 * @param value Alias value (deep-copied).
 */
void alias_store_add_cstr(alias_store_t *store, const char *name, const char *value);

/**
 * Remove an alias by name.
 *
 * @param store The alias store.
 * @param name Alias name.
 * @return true if the alias was found and removed.
 */
bool alias_store_remove(alias_store_t *store, const string_t *name);

/**
 * Remove an alias by C-string name.
 *
 * @param store The alias store.
 * @param name Alias name.
 * @return true if the alias was found and removed.
 */
bool alias_store_remove_cstr(alias_store_t *store, const char *name);

/**
 * Clear all aliases from the store.
 *
 * @param store The alias store.
 */
void alias_store_clear(alias_store_t *store);

/* ============================================================================
 * Queries (const outputs)
 *
 * All returned pointers refer to internal store data and are valid only until
 * the next mutating operation on the store. Do not free or modify them.
 * ============================================================================ */

/**
 * Get the number of aliases in the store.
 *
 * @param store The alias store.
 * @return Number of aliases.
 */
int alias_store_size(const alias_store_t *store);

/**
 * Check whether an alias exists.
 *
 * @param store The alias store.
 * @param name Alias name.
 * @return true if the alias exists.
 */
bool alias_store_has_name(const alias_store_t *store, const string_t *name);

/**
 * Check whether an alias exists using a C-string name.
 *
 * @param store The alias store.
 * @param name Alias name.
 * @return true if the alias exists.
 */
bool alias_store_has_name_cstr(const alias_store_t *store, const char *name);

/**
 * Get an alias value by name.
 *
 * Returns a const pointer to the internal string. Valid only until the
 * next mutating operation on the store.
 *
 * @param store The alias store.
 * @param name Alias name.
 * @return Alias value, or NULL if not found.
 */
const string_t *alias_store_get_value(const alias_store_t *store, const string_t *name);

/**
 * Get an alias value by C-string name.
 *
 * Returns a const pointer to the internal string data. Valid only until the
 * next mutating operation on the store.
 *
 * @param store The alias store.
 * @param name Alias name.
 * @return Alias value as C-string, or NULL if not found.
 */
const char *alias_store_get_value_cstr(const alias_store_t *store, const char *name);

/* ============================================================================
 * Iteration
 *
 * Callbacks receive const pointers to internal data. Do not modify or free.
 * ============================================================================ */

/**
 * Iterate over all aliases in the store.
 * The callback receives const pointers to internal data.
 *
 * @param store The alias store.
 * @param callback Function to call for each alias.
 * @param user_data User-provided context pointer passed to callback.
 */
void alias_store_foreach(const alias_store_t *store, alias_store_foreach_fn callback,
                         void *user_data);

#endif

#ifndef MIGA_ALIAS_STORE_H
#define MIGA_ALIAS_STORE_H

#include "miga_cppguard.h"
#include "miga_visibility.h"
#include "miga_string.h"

MIGA_EXTERN_C_START

typedef int (*miga_alias_map_compare_func_t)(const miga_string_t *key, const miga_string_t *name, void *data);

typedef struct miga_alias_map_entry_t
{
    miga_string_t *key;
    miga_string_t *value;
} miga_alias_map_entry_t;

typedef struct miga_alias_map_t
{
    miga_alias_map_entry_t *data;
    int size;
    int capacity;
    miga_alias_map_compare_func_t compare_func;
    void *compare_func_data;
} miga_alias_map_t;

typedef struct miga_alias_map_emplace_result_t
{
    int index;     // index of the inserted or existing element
    bool inserted; // true if a new entry was inserted. false if no insertion occurred.
} miga_alias_map_emplace_result_t;


/* ============================================================================
 * Validation
 * ============================================================================ */

/**
 * Check if a C-string is a valid alias name.
 */
MIGA_API bool
miga_alias_name_is_valid_cstr(const char *name);

/* ============================================================================
 * Constructors and Destructors
 * ============================================================================ */

MIGA_API miga_alias_map_t *
miga_alias_map_create(void);

MIGA_API miga_alias_map_t *
miga_alias_map_clone(const miga_alias_map_t *other);

MIGA_API void
miga_alias_map_destroy(miga_alias_map_t **store);

/* The default comparison function for alias map entries when the store is created
 * with miga_alias_map_create()
 */
MIGA_API int
miga_alias_map_default_compare(const miga_string_t *key, const miga_string_t *name, void *data);

/* ============================================================================
 * Operations
 * ============================================================================ */

MIGA_API void
miga_alias_map_clear(miga_alias_map_t *store);

/**
 * This function attempts to insert a new alias into the store.
 * If an alias with the same name already exists, no insertion occurs.
 */
MIGA_API miga_alias_map_emplace_result_t
miga_alias_map_emplace(miga_alias_map_t *store, const miga_string_t *name, const miga_string_t *value);

/**
 * This function attempts to insert or replace an alias in the store.
 * If an alias with the same name already exists, it is replaced with the new value.
 */
MIGA_API miga_alias_map_emplace_result_t
miga_alias_map_emplace_assign(miga_alias_map_t *store, const miga_string_t *name, const miga_string_t *value);

/* Returns the number of elements with the given name: 0 or 1 */
MIGA_API int
miga_alias_map_count(const miga_alias_map_t *store, const miga_string_t *name);

/* Returns true if the alias with the given name exists */
MIGA_API bool
miga_alias_map_contains(const miga_alias_map_t *store, const miga_string_t *name);

/* Returns the number of elements removed: 0 or 1*/
MIGA_API bool
miga_alias_map_erase(miga_alias_map_t *store, const miga_string_t *name);

/* Returns the value associated with the given name. You must ensure the alias
 * exists before calling this function. */
MIGA_API const miga_string_t *
miga_alias_map_get(const miga_alias_map_t *store, const miga_string_t *name);

MIGA_EXTERN_C_END

#endif /* MIGA_ALIAS_STORE_H */

#ifndef VARIABLE_MAP_H
#define VARIABLE_MAP_H

#include "string_t.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * Represents the mapped value associated with a variable name,
 * including its string value and export/read‑only flags.
 */
typedef struct variable_map_mapped_t
{
    /** The variable's value string. */
    string_t *value;

    /** Whether the variable is exported to the environment. */
    bool exported;

    /** Whether the variable is read‑only. */
    bool read_only;

    /** Padding for alignment. */
    char padding[6];
} variable_map_mapped_t;

/**
 * Represents a single entry in the variable map,
 * containing a key, mapped value, and occupancy flag.
 */
typedef struct variable_map_entry_t
{
    /** The variable name. */
    string_t *key;

    /** The mapped value and metadata. */
    variable_map_mapped_t mapped;

    /** True if this entry is currently occupied. */
    bool occupied;

    /** Padding for alignment. */
    char padding[7];
} variable_map_entry_t;

/**
 * Represents a hash‑map‑like structure storing variable entries.
 */
typedef struct variable_map_t
{
    /** Array of map entries. */
    variable_map_entry_t *entries;

    /** Number of occupied entries. */
    int32_t size;

    /** Total capacity of the entries array. */
    int32_t capacity;
} variable_map_t;

/**
 * Represents the result of an insertion attempt into the variable map.
 */
typedef struct variable_map_insert_result_t
{
    /** Position where the key was inserted or found. */
    int32_t pos;

    /** True if a new key was inserted; false if it already existed. */
    bool success;

    /** Padding for alignment. */
    char padding[3];
} variable_map_insert_result_t;

/**
 * Creates a new, empty variable map.
 *
 * @return Newly allocated variable map.
 */
variable_map_t *variable_map_create(void);

/**
 * Destroys a variable map and all its contents.
 * Sets *map to NULL.
 *
 * @param map Pointer to the map pointer.
 */
void variable_map_destroy(variable_map_t **map);

/**
 * Retrieves the mapped value for a given key.
 *
 * @param map Variable map.
 * @param key Variable name.
 * @return Pointer to mapped value or NULL if not found.
 */
const variable_map_mapped_t *variable_map_at(const variable_map_t *map, const string_t *key);

/**
 * Retrieves a mutable mapped value for a given key.
 *
 * @param map Variable map.
 * @param key Variable name.
 * @return Mutable pointer to mapped value or NULL if not found.
 */
variable_map_mapped_t *variable_map_data_at(variable_map_t *map, const string_t *key);

/**
 * Returns true if the map contains no entries.
 *
 * @param map Variable map.
 * @return true if empty.
 */
bool variable_map_empty(const variable_map_t *map);

/**
 * Returns the number of occupied entries in the map.
 *
 * @param map Variable map.
 * @return Number of entries.
 */
int32_t variable_map_size(const variable_map_t *map);

/**
 * Removes all entries from the variable map.
 *
 * @param map Variable map.
 */
void variable_map_clear(variable_map_t *map);

/**
 * Inserts a new key/value pair into the map, transferring ownership
 * of *mapped. If the key already exists, insertion fails.
 *
 * @param map Variable map.
 * @param key Variable name.
 * @param mapped Mapped value.
 * @return Insert result containing position and success flag.
 */
variable_map_insert_result_t variable_map_insert(variable_map_t *map, const string_t *key,
                                                const variable_map_mapped_t *mapped);

/**
 * Inserts or assigns a key/value pair. It deep-copies *mapped, thus not transferring owernship.
 * If the key exists, its value is replaced.
 *
 * @param map Variable map.
 * @param key Variable name.
 * @param mapped Mapped value
 * @return Position of the entry.
 */
int32_t variable_map_insert_or_assign(variable_map_t *map, const string_t *key,
                                      const variable_map_mapped_t *mapped);

/**
 * Removes the entry with the given key.
 *
 * @param map Variable map.
 * @param key Variable name.
 */
void variable_map_erase(variable_map_t *map, const string_t *key);

/**
 * Removes multiple entries with the given keys. Note that the keys must be unique,
 * or the behavior is undefined.
 *
 * @param map Variable map.
 * @param keys List of variable names to remove.
 */
void variable_map_erase_multiple(variable_map_t *map, const string_list_t *keys);

/**
 * Removes the entry at the given position.
 *
 * @param map Variable map.
 * @param pos Entry index.
 */
void variable_map_erase_at_pos(variable_map_t *map, int32_t pos);

/**
 * Extracts and removes the mapped value for a given key.
 * Ownership of the returned value transfers to the caller.
 *
 * @param map Variable map.
 * @param key Variable name.
 * @return Extracted mapped value or NULL if not found.
 */
variable_map_mapped_t *variable_map_extract(variable_map_t *map, const string_t *key);

/**
 * Extracts and removes the mapped value at a given position.
 * Ownership of the returned value transfers to the caller.
 *
 * @param map Variable map.
 * @param pos Entry index.
 * @return Extracted mapped value or NULL if invalid.
 */
variable_map_mapped_t *variable_map_extract_at_pos(variable_map_t *map, int32_t pos);

/**
 * Returns the number of entries with the given key (0 or 1).
 *
 * @param map Variable map.
 * @param key Variable name.
 * @return 0 if not found, 1 if found.
 */
int32_t variable_map_count(const variable_map_t *map, const string_t *key);

/**
 * Finds the position of a key in the map.
 *
 * @param map Variable map.
 * @param key Variable name.
 * @return Position or -1 if not found.
 */
int32_t variable_map_find(const variable_map_t *map, const string_t *key);

/**
 * Returns true if the map contains the given key.
 *
 * @param map Variable map.
 * @param key Variable name.
 * @return true if present.
 */
bool variable_map_contains(const variable_map_t *map, const string_t *key);

#endif

#ifndef FUNC_MAP_H
#define FUNC_MAP_H

#include "ast.h"
#include "string_t.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * func_map - A hash map for shell function definitions
 *
 * Maps function names (string_t) to function definitions (ast_node_t).
 * Used to store user-defined functions in a POSIX shell.
 */

/**
 * Mapped value stored for each function
 */
typedef struct func_map_mapped_t
{
    ast_node_t *func;   // Function body (AST node, typically AST_FUNCTION_DEF)
    string_t *name;     // Function name (copy stored here for convenience)
} func_map_mapped_t;

/**
 * Hash map entry
 */
typedef struct func_map_entry_t
{
    string_t *key;              // Function name (used as hash key)
    func_map_mapped_t mapped;   // Associated function data
    bool occupied;              // True if this slot is occupied
} func_map_entry_t;

/**
 * Function map structure
 */
typedef struct func_map_t
{
    func_map_entry_t *entries;
    int32_t size;       // Number of entries in the map
    int32_t capacity;   // Total capacity of the hash table
} func_map_t;

/**
 * Result of an insert operation
 */
typedef struct func_map_insert_result_t
{
    int32_t pos;     // Position where the key was inserted or found
    bool success;    // True if new key was inserted, false if key already existed
} func_map_insert_result_t;

/* ============================================================================
 * Lifecycle
 * ============================================================================ */

/**
 * Create a new function map
 */
func_map_t *func_map_create(void);

/**
 * Destroy a function map and free all resources
 */
void func_map_destroy(func_map_t **map);

/* ============================================================================
 * Element access
 * ============================================================================ */

/**
 * Get the mapped value for a given key (read-only)
 * Returns NULL if not found
 */
const func_map_mapped_t *func_map_at(const func_map_t *map, const string_t *key);

/**
 * Get the mapped value for a given key (mutable)
 * Returns NULL if not found
 */
func_map_mapped_t *func_map_data_at(func_map_t *map, const string_t *key);

/* ============================================================================
 * Capacity
 * ============================================================================ */

/**
 * Check if the map is empty
 */
bool func_map_empty(const func_map_t *map);

/**
 * Get the number of elements in the map
 */
int32_t func_map_size(const func_map_t *map);

/* ============================================================================
 * Modifiers
 * ============================================================================ */

/**
 * Clear all entries from the map
 */
void func_map_clear(func_map_t *map);

/**
 * Insert a new entry (moves the mapped value into the map)
 * The map will clone the key. The mapped value is moved (not copied).
 * Returns a result indicating success and position.
 */
func_map_insert_result_t func_map_insert_move(func_map_t *map,
                                               const string_t *key,
                                               func_map_mapped_t *mapped);

/**
 * Insert or update an entry (moves the mapped value into the map)
 * If the key exists, replaces the value. Otherwise, inserts a new entry.
 * Returns the position of the entry.
 */
int32_t func_map_insert_or_assign_move(func_map_t *map,
                                        const string_t *key,
                                        func_map_mapped_t *mapped);

/**
 * Remove an entry by key
 */
void func_map_erase(func_map_t *map, const string_t *key);

/**
 * Remove an entry at a specific position
 */
void func_map_erase_at_pos(func_map_t *map, int32_t pos);

/**
 * Extract (remove and return) the mapped value for a given key
 * Caller takes ownership of the returned value and must free it
 */
func_map_mapped_t *func_map_extract(func_map_t *map, const string_t *key);

/**
 * Extract (remove and return) the mapped value at a given position
 * Caller takes ownership of the returned value and must free it
 */
func_map_mapped_t *func_map_extract_at_pos(func_map_t *map, int32_t pos);

/* ============================================================================
 * Lookup
 * ============================================================================ */

/**
 * Count occurrences of a key (returns 0 or 1)
 */
int32_t func_map_count(const func_map_t *map, const string_t *key);

/**
 * Find the position of a key in the map
 * Returns the position if found, -1 otherwise
 */
int32_t func_map_find(const func_map_t *map, const string_t *key);

/**
 * Check if a key exists in the map
 */
bool func_map_contains(const func_map_t *map, const string_t *key);

/* ============================================================================
 * Iteration
 * ============================================================================ */

/**
 * Callback function for iterating over map entries
 * @param key Function name
 * @param mapped Mapped value (function data)
 * @param user_data User-provided context pointer
 */
typedef void (*func_map_foreach_fn)(const string_t *key, const func_map_mapped_t *mapped, void *user_data);

/**
 * Iterate over all entries in the map
 * @param map The function map
 * @param callback Function to call for each entry
 * @param user_data User-provided context pointer passed to callback
 */
void func_map_foreach(const func_map_t *map, func_map_foreach_fn callback, void *user_data);

#endif /* FUNC_MAP_H */

#ifndef VARIABLE_MAP_H
#define VARIABLE_MAP_H

#include "string_t.h"
#include <stdbool.h>

typedef struct variable_map_mapped_t
{
    string_t *value;
    bool exported;
    bool read_only;
} variable_map_mapped_t;

typedef struct variable_map_entry_t
{
    string_t *key;
    variable_map_mapped_t mapped;
    bool occupied; // Indicates if this entry is occupied
} variable_map_entry_t;

typedef struct variable_map_t
{
    variable_map_entry_t *entries;
    int32_t size;
    int32_t capacity;
} variable_map_t;

typedef struct variable_map_insert_result_t
{
    int32_t pos; // Position where the key was inserted or found
    bool success; // True if a new key was inserted, false if the key already existed
} variable_map_insert_result_t;

variable_map_t *variable_map_create(void);
void variable_map_destroy(variable_map_t **map);

// Returns the value for the given key. Returns NULL if not found.
const variable_map_mapped_t *variable_map_at(const variable_map_t *map, const string_t *key);
variable_map_mapped_t *variable_map_data_at(variable_map_t *map, const string_t *key);

// Capacity
bool variable_map_empty(const variable_map_t *map);
int32_t variable_map_size(const variable_map_t *map);

// Modifiers
void variable_map_clear(variable_map_t *map);
variable_map_insert_result_t variable_map_insert_move(variable_map_t *map,
                                                      const string_t *key,
                                                      variable_map_mapped_t *mapped);
int32_t variable_map_insert_or_assign_move(variable_map_t *map,
                                                      const string_t *key,
                                                      variable_map_mapped_t *mapped);
void variable_map_erase(variable_map_t *map, const string_t *key);
void variable_map_erase_at_pos(variable_map_t *map, int32_t pos);
// Extracts the mapped value for the given key, removing it from the map.
variable_map_mapped_t *variable_map_extract(variable_map_t *map, const string_t *key);
variable_map_mapped_t *variable_map_extract_at_pos(variable_map_t *map, int32_t pos);

///// Lookup
// Returns either 0 (not found) or 1 (found)
int32_t variable_map_count(const variable_map_t *map, const string_t *key);
int32_t variable_map_find(const variable_map_t *map, const string_t *key);
bool variable_map_contains(const variable_map_t *map, const string_t *key);



#endif

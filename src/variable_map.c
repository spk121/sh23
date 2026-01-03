#include "variable_map.h"
#include "string_t.h"
#include "xalloc.h"
#include <stdint.h>

#define VARIABLE_MAP_INITIAL_CAPACITY 16

static uint32_t hash_key(const string_t *key) {
    return string_hash(key);
}

static bool keys_equal(const string_t *a, const string_t *b) {
    return string_eq(a, b);
}

static void destroy_entry(variable_map_entry_t *entry)
{
    if (entry->occupied)
    {
        string_t *key = entry->key;
        string_destroy(&key);
        if (entry->mapped.value) {
            string_destroy(&entry->mapped.value);
        }
    }
}

static void variable_map_resize(variable_map_t *map, int32_t new_capacity) {
    variable_map_entry_t *new_entries = xcalloc(new_capacity, sizeof(variable_map_entry_t));
    for (int32_t i = 0; i < map->capacity; i++) {
        if (map->entries[i].occupied) {
            uint32_t hash = hash_key(map->entries[i].key);
            int32_t pos = hash % new_capacity;
            while (new_entries[pos].occupied) {
                pos = (pos + 1) % new_capacity;
            }
            new_entries[pos] = map->entries[i];
        }
    }
    xfree(map->entries);
    map->entries = new_entries;
    map->capacity = new_capacity;
}

variable_map_t *variable_map_create(void) {
    variable_map_t *map = xmalloc(sizeof(variable_map_t));
    map->capacity = VARIABLE_MAP_INITIAL_CAPACITY;
    map->size = 0;
    map->entries = xcalloc(map->capacity, sizeof(variable_map_entry_t));
    return map;
}

void variable_map_destroy(variable_map_t **map) {
    if (!*map) return;
    for (int32_t i = 0; i < (*map)->capacity; i++) {
        destroy_entry(&(*map)->entries[i]);
    }
    xfree((*map)->entries);
    xfree(*map);
    *map = NULL;
}

const variable_map_mapped_t *variable_map_at(const variable_map_t *map, const string_t *key) {
    uint32_t hash = hash_key(key);
    int32_t pos = hash % map->capacity;
    for (int32_t i = 0; i < map->capacity; i++) {
        if (!map->entries[pos].occupied) {
            return NULL;
        }
        if (keys_equal(map->entries[pos].key, key)) {
            return &map->entries[pos].mapped;
        }
        pos = (pos + 1) % map->capacity;
    }
    return NULL;
}

variable_map_mapped_t *variable_map_data_at(variable_map_t *map, const string_t *key) {
    return (variable_map_mapped_t *)variable_map_at(map, key);
}

bool variable_map_empty(const variable_map_t *map) {
    return map->size == 0;
}

int32_t variable_map_size(const variable_map_t *map) {
    return map->size;
}

void variable_map_clear(variable_map_t *map) {
    for (int32_t i = 0; i < map->capacity; i++) {
        destroy_entry(&map->entries[i]);
        map->entries[i].occupied = false;
    }
    map->size = 0;
}

variable_map_insert_result_t variable_map_insert_move(variable_map_t *map, const string_t *key, variable_map_mapped_t *mapped) {
    if (map->size >= map->capacity * 3 / 4) {
        variable_map_resize(map, map->capacity * 2);
    }
    uint32_t hash = hash_key(key);
    int32_t pos = hash % map->capacity;
    while (map->entries[pos].occupied) {
        if (keys_equal(map->entries[pos].key, key)) {
            return (variable_map_insert_result_t){pos, false};
        }
        pos = (pos + 1) % map->capacity;
    }
    map->entries[pos].key = string_create_from(key);
    map->entries[pos].mapped = *mapped;
    map->entries[pos].occupied = true;
    map->size++;
    return (variable_map_insert_result_t){pos, true};
}

int32_t variable_map_insert_or_assign_move(variable_map_t *map, const string_t *key, variable_map_mapped_t *mapped) {
    if (map->size >= map->capacity * 3 / 4) {
        variable_map_resize(map, map->capacity * 2);
    }
    uint32_t hash = hash_key(key);
    int32_t pos = hash % map->capacity;
    while (map->entries[pos].occupied) {
        if (keys_equal(map->entries[pos].key, key)) {
            if (map->entries[pos].mapped.value) {
                string_destroy(&map->entries[pos].mapped.value);
            }
            map->entries[pos].mapped = *mapped;
            return pos;
        }
        pos = (pos + 1) % map->capacity;
    }
    map->entries[pos].key = string_create_from(key);
    map->entries[pos].mapped = *mapped;
    map->entries[pos].occupied = true;
    map->size++;
    return pos;
}

void variable_map_erase(variable_map_t *map, const string_t *key) {
    int32_t pos = variable_map_find(map, key);
    if (pos != -1) {
        variable_map_erase_at_pos(map, pos);
    }
}

void variable_map_erase_at_pos(variable_map_t *map, int32_t pos) {
    destroy_entry(&map->entries[pos]);
    map->entries[pos].occupied = false;
    map->size--;
    int32_t next = (pos + 1) % map->capacity;
    while (map->entries[next].occupied) {
        variable_map_entry_t temp = map->entries[next];
        map->entries[next].occupied = false;
        map->size--;
        uint32_t hash = hash_key(temp.key);
        int32_t new_pos = hash % map->capacity;
        while (map->entries[new_pos].occupied) {
            new_pos = (new_pos + 1) % map->capacity;
        }
        map->entries[new_pos] = temp;
        map->entries[new_pos].occupied = true;
        map->size++;
        next = (next + 1) % map->capacity;
    }
}

variable_map_mapped_t *variable_map_extract(variable_map_t *map, const string_t *key) {
    int32_t pos = variable_map_find(map, key);
    if (pos != -1) {
        return variable_map_extract_at_pos(map, pos);
    }
    return NULL;
}

variable_map_mapped_t *variable_map_extract_at_pos(variable_map_t *map, int32_t pos) {
    variable_map_mapped_t *result = xmalloc(sizeof(variable_map_mapped_t));
    *result = map->entries[pos].mapped;
    string_destroy(&map->entries[pos].key);
    map->entries[pos].occupied = false;
    map->size--;
    int32_t next = (pos + 1) % map->capacity;
    while (map->entries[next].occupied) {
        variable_map_entry_t temp = map->entries[next];
        map->entries[next].occupied = false;
        map->size--;
        uint32_t hash = hash_key(temp.key);
        int32_t new_pos = hash % map->capacity;
        while (map->entries[new_pos].occupied) {
            new_pos = (new_pos + 1) % map->capacity;
        }
        map->entries[new_pos] = temp;
        map->entries[new_pos].occupied = true;
        map->size++;
        next = (next + 1) % map->capacity;
    }
    return result;
}

int32_t variable_map_count(const variable_map_t *map, const string_t *key) {
    return variable_map_find(map, key) != -1 ? 1 : 0;
}

int32_t variable_map_find(const variable_map_t *map, const string_t *key) {
    uint32_t hash = hash_key(key);
    int32_t pos = hash % map->capacity;
    for (int32_t i = 0; i < map->capacity; i++) {
        if (!map->entries[pos].occupied) {
            return -1;
        }
        if (keys_equal(map->entries[pos].key, key)) {
            return pos;
        }
        pos = (pos + 1) % map->capacity;
    }
    return -1;
}

bool variable_map_contains(const variable_map_t *map, const string_t *key) {
    return variable_map_find(map, key) != -1;
}

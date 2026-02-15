#define FUNC_MAP_INTERNAL
#include "func_map.h"
#include "ast.h"
#include "exec_redirect.h"
#include "string_t.h"
#include "xalloc.h"
#include <stdint.h>

#define FUNC_MAP_INITIAL_CAPACITY 16

/* ============================================================================
 * Internal helpers
 * ============================================================================ */

static uint32_t hash_key(const string_t *key)
{
    return string_hash(key);
}

static bool keys_equal(const string_t *a, const string_t *b)
{
    return string_eq(a, b);
}

static void destroy_entry(func_map_entry_t *entry)
{
    if (entry->occupied)
    {
        // Free the key
        string_t *key = entry->key;
        string_destroy(&key);

        // Free the mapped value components
        if (entry->mapped.name)
        {
            string_destroy(&entry->mapped.name);
        }
        if (entry->mapped.func)
        {
            ast_node_destroy(&entry->mapped.func);
        }
        if (entry->mapped.redirections)
        {
            exec_redirections_destroy(&entry->mapped.redirections);
        }
    }
}

static void func_map_resize(func_map_t *map, int32_t new_capacity)
{
    func_map_entry_t *new_entries = xcalloc(new_capacity, sizeof(func_map_entry_t));

    // Rehash all existing entries
    for (int32_t i = 0; i < map->capacity; i++)
    {
        if (map->entries[i].occupied)
        {
            uint32_t hash = hash_key(map->entries[i].key);
            int32_t pos = hash % new_capacity;

            // Linear probing to find an empty slot
            while (new_entries[pos].occupied)
            {
                pos = (pos + 1) % new_capacity;
            }

            new_entries[pos] = map->entries[i];
        }
    }

    xfree(map->entries);
    map->entries = new_entries;
    map->capacity = new_capacity;
}

/* ============================================================================
 * Lifecycle
 * ============================================================================ */

func_map_t *func_map_create(void)
{
    func_map_t *map = xmalloc(sizeof(func_map_t));
    map->capacity = FUNC_MAP_INITIAL_CAPACITY;
    map->size = 0;
    map->entries = xcalloc(map->capacity, sizeof(func_map_entry_t));
    return map;
}

void func_map_destroy(func_map_t **map)
{
    if (!map || !*map)
        return;

    // Destroy all entries
    for (int32_t i = 0; i < (*map)->capacity; i++)
    {
        destroy_entry(&(*map)->entries[i]);
    }

    xfree((*map)->entries);
    xfree(*map);
    *map = NULL;
}

/* ============================================================================
 * Element access
 * ============================================================================ */

const func_map_mapped_t *func_map_at(const func_map_t *map, const string_t *key)
{
    if (!map || !key)
        return NULL;

    uint32_t hash = hash_key(key);
    int32_t pos = hash % map->capacity;

    // Linear probing to find the key
    for (int32_t i = 0; i < map->capacity; i++)
    {
        if (!map->entries[pos].occupied)
        {
            return NULL; // Not found
        }
        if (keys_equal(map->entries[pos].key, key))
        {
            return &map->entries[pos].mapped;
        }
        pos = (pos + 1) % map->capacity;
    }

    return NULL;
}

func_map_mapped_t *func_map_data_at(func_map_t *map, const string_t *key)
{
    if (!map || !key)
        return NULL;

    uint32_t hash = hash_key(key);
    int32_t pos = hash % map->capacity;

    // Linear probing to find the key
    for (int32_t i = 0; i < map->capacity; i++)
    {
        if (!map->entries[pos].occupied)
        {
            return NULL; // Not found
        }
        if (keys_equal(map->entries[pos].key, key))
        {
            return &map->entries[pos].mapped;
        }
        pos = (pos + 1) % map->capacity;
    }

    return NULL;
}

/* ============================================================================
 * Capacity
 * ============================================================================ */

bool func_map_empty(const func_map_t *map)
{
    return map ? (map->size == 0) : true;
}

int32_t func_map_size(const func_map_t *map)
{
    return map ? map->size : 0;
}

/* ============================================================================
 * Modifiers
 * ============================================================================ */

void func_map_clear(func_map_t *map)
{
    if (!map)
        return;

    for (int32_t i = 0; i < map->capacity; i++)
    {
        destroy_entry(&map->entries[i]);
        map->entries[i].occupied = false;
    }
    map->size = 0;
}

int32_t func_map_insert_or_assign_move(func_map_t *map, const string_t *key,
                                       func_map_mapped_t *mapped)
{
    if (!map || !key || !mapped)
        return -1;

    // Resize if load factor exceeds 75%
    if (map->size >= map->capacity * 3 / 4)
    {
        func_map_resize(map, map->capacity * 2);
    }

    uint32_t hash = hash_key(key);
    int32_t pos = hash % map->capacity;

    // Linear probing
    while (map->entries[pos].occupied)
    {
        if (keys_equal(map->entries[pos].key, key))
        {
            // Key exists - replace the mapped value
            if (map->entries[pos].mapped.name)
            {
                string_destroy(&map->entries[pos].mapped.name);
            }
            if (map->entries[pos].mapped.func)
            {
                ast_node_destroy(&map->entries[pos].mapped.func);
            }
            if (map->entries[pos].mapped.redirections)
            {
                exec_redirections_destroy(&map->entries[pos].mapped.redirections);
            }
            map->entries[pos].mapped = *mapped;
            return pos;
        }
        pos = (pos + 1) % map->capacity;
    }

    // Insert new entry
    map->entries[pos].key = string_create_from(key);
    map->entries[pos].mapped = *mapped;
    map->entries[pos].occupied = true;
    map->size++;

    return pos;
}

void func_map_erase(func_map_t *map, const string_t *key)
{
    if (!map || !key)
        return;

    int32_t pos = func_map_find(map, key);
    if (pos != -1)
    {
        func_map_erase_at_pos(map, pos);
    }
}

void func_map_erase_at_pos(func_map_t *map, int32_t pos)
{
    if (!map || pos < 0 || pos >= map->capacity)
        return;

    if (!map->entries[pos].occupied)
        return;

    // Destroy the entry
    destroy_entry(&map->entries[pos]);
    map->entries[pos].occupied = false;
    map->size--;

    // Rehash subsequent entries to maintain probing chain
    int32_t next = (pos + 1) % map->capacity;
    while (map->entries[next].occupied)
    {
        func_map_entry_t temp = map->entries[next];
        map->entries[next].occupied = false;
        map->size--;

        // Reinsert this entry
        uint32_t hash = hash_key(temp.key);
        int32_t new_pos = hash % map->capacity;

        while (map->entries[new_pos].occupied)
        {
            new_pos = (new_pos + 1) % map->capacity;
        }

        map->entries[new_pos] = temp;
        map->entries[new_pos].occupied = true;
        map->size++;

        next = (next + 1) % map->capacity;
    }
}

func_map_mapped_t *func_map_extract(func_map_t *map, const string_t *key)
{
    if (!map || !key)
        return NULL;

    int32_t pos = func_map_find(map, key);
    if (pos != -1)
    {
        return func_map_extract_at_pos(map, pos);
    }
    return NULL;
}

func_map_mapped_t *func_map_extract_at_pos(func_map_t *map, int32_t pos)
{
    if (!map || pos < 0 || pos >= map->capacity)
        return NULL;

    if (!map->entries[pos].occupied)
        return NULL;

    // Allocate result and transfer ownership
    func_map_mapped_t *result = xmalloc(sizeof(func_map_mapped_t));
    *result = map->entries[pos].mapped;

    // Destroy the key but not the mapped value (we're extracting it)
    string_destroy(&map->entries[pos].key);
    map->entries[pos].occupied = false;
    map->size--;

    // Rehash subsequent entries to maintain probing chain
    int32_t next = (pos + 1) % map->capacity;
    while (map->entries[next].occupied)
    {
        func_map_entry_t temp = map->entries[next];
        map->entries[next].occupied = false;
        map->size--;

        uint32_t hash = hash_key(temp.key);
        int32_t new_pos = hash % map->capacity;

        while (map->entries[new_pos].occupied)
        {
            new_pos = (new_pos + 1) % map->capacity;
        }

        map->entries[new_pos] = temp;
        map->entries[new_pos].occupied = true;
        map->size++;

        next = (next + 1) % map->capacity;
    }

    return result;
}

/* ============================================================================
 * Lookup
 * ============================================================================ */

int32_t func_map_count(const func_map_t *map, const string_t *key)
{
    return func_map_contains(map, key) ? 1 : 0;
}

int32_t func_map_find(const func_map_t *map, const string_t *key)
{
    if (!map || !key)
        return -1;

    uint32_t hash = hash_key(key);
    int32_t pos = hash % map->capacity;

    for (int32_t i = 0; i < map->capacity; i++)
    {
        if (!map->entries[pos].occupied)
        {
            return -1; // Not found
        }
        if (keys_equal(map->entries[pos].key, key))
        {
            return pos;
        }
        pos = (pos + 1) % map->capacity;
    }

    return -1;
}

bool func_map_contains(const func_map_t *map, const string_t *key)
{
    return func_map_find(map, key) != -1;
}

/* ============================================================================
 * Iteration
 * ============================================================================ */

void func_map_foreach(const func_map_t *map, func_map_foreach_fn callback, void *user_data)
{
    if (!map || !callback)
        return;

    for (int32_t i = 0; i < map->capacity; i++)
    {
        if (map->entries[i].occupied)
        {
            callback(map->entries[i].key, &map->entries[i].mapped, user_data);
        }
    }
}

#include <stdint.h>
#include "logging.h"
#include "string_t.h"
#include "variable_map.h"
#include "xalloc.h"

#define VARIABLE_MAP_INITIAL_CAPACITY 16

static uint32_t hash_key(const string_t *key)
{
    Expects_not_null(key);
    return string_hash(key);
}

static bool keys_equal(const string_t *a, const string_t *b)
{
    Expects_not_null(a);
    Expects_not_null(b);

    return string_eq(a, b);
}

static void clear_entry(variable_map_entry_t *entry)
{
    Expects_not_null(entry);

    entry->key = NULL;
    entry->mapped.value = NULL;
    entry->mapped.exported = false;
    entry->mapped.read_only = false;
    entry->occupied = false;
}

static void destroy_entry(variable_map_entry_t *entry)
{
    Expects_not_null(entry);

    if (entry->occupied)
    {
        if (entry->key)
        {
            string_destroy(&entry->key);
        }
        if (entry->mapped.value)
        {
            string_destroy(&entry->mapped.value);
        }
        entry->occupied = false;
    }
}

static void set_entry(variable_map_entry_t *dest, const string_t *key,
                      const variable_map_mapped_t *mapped)
{
    Expects_not_null(dest);
    Expects_not_null(key);
    Expects_not_null(mapped);

    // Clean up existing key if present
    if (dest->key)
    {
        string_destroy(&dest->key);
    }
    dest->key = string_create_from(key);

    // Clean up existing value if present
    if (dest->mapped.value)
    {
        string_destroy(&dest->mapped.value);
    }
    if (mapped->value)
    {
        dest->mapped.value = string_create_from(mapped->value);
    }
    else
    {
        dest->mapped.value = NULL;
    }

    dest->mapped.exported = mapped->exported;
    dest->mapped.read_only = mapped->read_only;
    dest->occupied = true;
}

static void variable_map_resize(variable_map_t *map, int32_t new_capacity)
{
    Expects_not_null(map);
    Expects_gt(new_capacity, map->capacity);

    variable_map_entry_t *new_entries = xcalloc(new_capacity, sizeof(variable_map_entry_t));

    for (int32_t i = 0; i < map->capacity; i++)
    {
        if (map->entries[i].occupied)
        {
            uint32_t hash = hash_key(map->entries[i].key);
            int32_t pos = hash % new_capacity;
            while (new_entries[pos].occupied)
            {
                pos = (pos + 1) % new_capacity;
            }
            // Move the entry directly (no copy needed, just transfer pointers)
            new_entries[pos] = map->entries[i];
        }
    }

    xfree(map->entries);
    map->entries = new_entries;
    map->capacity = new_capacity;
}

/**
 * Perform backward-shift deletion starting from the given empty position.
 * This maintains the linear probing invariant by shifting entries backward
 * when their ideal position is at or before the empty slot.
 */
static void backward_shift_deletion(variable_map_t *map, int32_t empty)
{
    int32_t curr = (empty + 1) % map->capacity;

    while (map->entries[curr].occupied)
    {
        uint32_t ideal = hash_key(map->entries[curr].key) % map->capacity;

        // Determine if the entry at curr should move to empty.
        // An entry should move if its ideal position is "at or before" empty
        // in the probe sequence. We need to handle wrap-around carefully.
        //
        // The entry at curr got there by probing from ideal. For it to be
        // valid at curr, all slots from ideal to curr-1 must have been
        // occupied during insertion. If empty is in that range [ideal, curr),
        // the entry needs to move back to maintain the invariant.
        bool should_move;
        if (curr >= empty)
        {
            // No wrap between empty and curr
            // ideal in [empty, curr) means it should move
            should_move = (ideal <= empty) || (ideal > curr);
        }
        else
        {
            // Wrapped: curr < empty (we wrapped around the array)
            // ideal should be in the range (curr, empty] to NOT move
            should_move = (ideal > curr) && (ideal <= empty);
        }

        if (should_move)
        {
            // Move entry from curr to empty
            map->entries[empty] = map->entries[curr];
            map->entries[curr].occupied = false;
            map->entries[curr].key = NULL;
            map->entries[curr].mapped.value = NULL;
            empty = curr;
        }

        curr = (curr + 1) % map->capacity;
    }
}

variable_map_t *variable_map_create(void)
{
    variable_map_t *map = xmalloc(sizeof(variable_map_t));
    map->capacity = VARIABLE_MAP_INITIAL_CAPACITY;
    map->size = 0;
    map->entries = xcalloc(map->capacity, sizeof(variable_map_entry_t));
    return map;
}

void variable_map_destroy(variable_map_t **map)
{
    if (!map || !*map)
    {
        return;
    }

    for (int32_t i = 0; i < (*map)->capacity; i++)
    {
        destroy_entry(&(*map)->entries[i]);
    }
    xfree((*map)->entries);
    xfree(*map);
    *map = NULL;
}

const variable_map_mapped_t *variable_map_at(const variable_map_t *map, const string_t *key)
{
    Expects_not_null(map);
    Expects_not_null(key);

    uint32_t hash = hash_key(key);
    int32_t pos = hash % map->capacity;

    for (int32_t i = 0; i < map->capacity; i++)
    {
        if (!map->entries[pos].occupied)
        {
            return NULL;
        }
        if (keys_equal(map->entries[pos].key, key))
        {
            return &map->entries[pos].mapped;
        }
        pos = (pos + 1) % map->capacity;
    }

    return NULL;
}

variable_map_mapped_t *variable_map_data_at(variable_map_t *map, const string_t *key)
{
    Expects_not_null(map);
    Expects_not_null(key);

    return (variable_map_mapped_t *)variable_map_at(map, key);
}

bool variable_map_empty(const variable_map_t *map)
{
    Expects_not_null(map);

    return map->size == 0;
}

int32_t variable_map_size(const variable_map_t *map)
{
    Expects_not_null(map);

    return map->size;
}

void variable_map_clear(variable_map_t *map)
{
    Expects_not_null(map);

    for (int32_t i = 0; i < map->capacity; i++)
    {
        destroy_entry(&map->entries[i]);
        clear_entry(&map->entries[i]);
    }
    map->size = 0;
}

variable_map_insert_result_t variable_map_insert(variable_map_t *map, const string_t *key,
                                                 const variable_map_mapped_t *mapped)
{
    Expects_not_null(map);
    Expects_not_null(key);
    Expects_not_null(mapped);

    if (map->size >= map->capacity * 3 / 4)
    {
        variable_map_resize(map, map->capacity * 2);
    }

    uint32_t hash = hash_key(key);
    int32_t pos = hash % map->capacity;

    while (map->entries[pos].occupied)
    {
        if (keys_equal(map->entries[pos].key, key))
        {
            // Key already exists, insertion fails
            return (variable_map_insert_result_t){pos, false};
        }
        pos = (pos + 1) % map->capacity;
    }

    set_entry(&map->entries[pos], key, mapped);
    map->size++;

    return (variable_map_insert_result_t){pos, true};
}

int32_t variable_map_insert_or_assign(variable_map_t *map, const string_t *key,
                                      const variable_map_mapped_t *mapped)
{
    Expects_not_null(map);
    Expects_not_null(key);
    Expects_not_null(mapped);

    if (map->size >= map->capacity * 3 / 4)
    {
        variable_map_resize(map, map->capacity * 2);
    }

    uint32_t hash = hash_key(key);
    int32_t pos = hash % map->capacity;

    while (map->entries[pos].occupied)
    {
        if (keys_equal(map->entries[pos].key, key))
        {
            // Key exists, update the value (key stays the same)
            if (map->entries[pos].mapped.value)
            {
                string_destroy(&map->entries[pos].mapped.value);
            }
            if (mapped->value)
            {
                map->entries[pos].mapped.value = string_create_from(mapped->value);
            }
            else
            {
                map->entries[pos].mapped.value = NULL;
            }
            map->entries[pos].mapped.exported = mapped->exported;
            map->entries[pos].mapped.read_only = mapped->read_only;
            return pos;
        }
        pos = (pos + 1) % map->capacity;
    }

    set_entry(&map->entries[pos], key, mapped);
    map->size++;

    return pos;
}

void variable_map_erase(variable_map_t *map, const string_t *key)
{
    Expects_not_null(map);
    Expects_not_null(key);

    int32_t pos = variable_map_find(map, key);
    if (pos != -1)
    {
        variable_map_erase_at_pos(map, pos);
    }
}

void variable_map_erase_at_pos(variable_map_t *map, int32_t pos)
{
    Expects_not_null(map);
    Expects_ge(pos, 0);
    Expects_lt(pos, map->capacity);

    if (!map->entries[pos].occupied)
    {
        return;
    }

    destroy_entry(&map->entries[pos]);
    clear_entry(&map->entries[pos]);
    map->size--;

    backward_shift_deletion(map, pos);
}

void variable_map_erase_multiple(variable_map_t *map, const string_list_t *keys)
{
    Expects_not_null(map);
    Expects_not_null(keys);

    int count = string_list_size(keys);
    if (count == 0)
    {
        return;
    }

    if (count == 1)
    {
        variable_map_erase(map, string_list_at(keys, 0));
        return;
    }

    // Threshold: if deleting more than 1/8 of entries, rebuild is likely cheaper
    // than repeated backward shifts. This is a heuristic that balances:
    // - Rebuild cost: O(capacity)
    // - Batch single-delete cost: O(count × avg_probe_length)
    // At 75% load factor with count > size/8, rebuild wins.
    bool should_rebuild = (count > map->size / 8) || (count > 16 && count > map->capacity / 16);

    if (!should_rebuild)
    {
        // Batch of individual deletions
        for (int i = 0; i < count; i++)
        {
            variable_map_erase(map, string_list_at(keys, i));
        }
        return;
    }

    // Rebuild approach: mark deletions, then rehash into fresh array
    int deleted = 0;
    for (int i = 0; i < count; i++)
    {
        const string_t *key = string_list_at(keys, i);
        int32_t pos = variable_map_find(map, key);
        if (pos != -1 && map->entries[pos].occupied)
        {
            destroy_entry(&map->entries[pos]);
            map->entries[pos].occupied = false;
            deleted++;
        }
    }

    if (deleted == 0)
    {
        return;
    }

    map->size -= deleted;

    // Rebuild the table
    variable_map_entry_t *old_entries = map->entries;
    int32_t old_capacity = map->capacity;

    map->entries = xcalloc(map->capacity, sizeof(variable_map_entry_t));

    for (int32_t i = 0; i < old_capacity; i++)
    {
        if (old_entries[i].occupied)
        {
            uint32_t hash = hash_key(old_entries[i].key);
            int32_t pos = hash % map->capacity;
            while (map->entries[pos].occupied)
            {
                pos = (pos + 1) % map->capacity;
            }
            map->entries[pos] = old_entries[i];
        }
    }

    xfree(old_entries);
}


variable_map_mapped_t *variable_map_extract(variable_map_t *map, const string_t *key)
{
    Expects_not_null(map);
    Expects_not_null(key);

    int32_t pos = variable_map_find(map, key);
    if (pos != -1)
    {
        return variable_map_extract_at_pos(map, pos);
    }
    return NULL;
}

variable_map_mapped_t *variable_map_extract_at_pos(variable_map_t *map, int32_t pos)
{
    Expects_not_null(map);
    Expects_ge(pos, 0);
    Expects_lt(pos, map->capacity);

    if (!map->entries[pos].occupied)
    {
        return NULL;
    }

    // Extract the mapped value (caller takes ownership)
    variable_map_mapped_t *result = xmalloc(sizeof(variable_map_mapped_t));
    result->value = map->entries[pos].mapped.value;
    result->exported = map->entries[pos].mapped.exported;
    result->read_only = map->entries[pos].mapped.read_only;

    // Destroy the key but not the value (it's been extracted)
    string_destroy(&map->entries[pos].key);
    clear_entry(&map->entries[pos]);
    map->size--;

    backward_shift_deletion(map, pos);

    return result;
}

int32_t variable_map_count(const variable_map_t *map, const string_t *key)
{
    Expects_not_null(map);
    Expects_not_null(key);

    return variable_map_find(map, key) != -1 ? 1 : 0;
}

int32_t variable_map_find(const variable_map_t *map, const string_t *key)
{
    Expects_not_null(map);
    Expects_not_null(key);

    uint32_t hash = hash_key(key);
    int32_t pos = hash % map->capacity;

    for (int32_t i = 0; i < map->capacity; i++)
    {
        if (!map->entries[pos].occupied)
        {
            return -1;
        }
        if (keys_equal(map->entries[pos].key, key))
        {
            return pos;
        }
        pos = (pos + 1) % map->capacity;
    }

    return -1;
}

bool variable_map_contains(const variable_map_t *map, const string_t *key)
{
    Expects_not_null(map);
    Expects_not_null(key);

    return variable_map_find(map, key) != -1;
}

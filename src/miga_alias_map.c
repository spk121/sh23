#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>

#define ALIAS_STORE_INTERNAL
#include "miga_alias_map.h"

#include "miga_alloc.h"
#include "miga_log.h"

// Check if a character is valid for an alias name
static bool is_valid_alias_char(char c)
{
    // Alphabetics and digits from the portable character set (ASCII only)
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        return true;
    // Special characters: '!', '%', ',', '-', '@', '_'
    if (c == '!' || c == '%' || c == ',' || c == '-' || c == '@' || c == '_')
        return true;
    return false;
}

// Check if a string is a valid alias name
bool miga_alias_name_is_valid(const char *name)
{
    MIGA_EXPECTS_NOT_NULL(name);

    // Empty name is not valid
    if (name[0] == '\0')
        return false;

    for (const char *p = name; *p != '\0'; p++)
    {
        if (!is_valid_alias_char(*p))
            return false;
    }
    return true;
}

int miga_alias_map_default_compare(const miga_string_t *key, const miga_string_t *name, void *data)
{
    (void)data;
    return string_compare(key, name);
}

// Constructors
#define INITIAL_CAPACITY 8
#define MAX_CAPACITY (1024 * 1024)
#define GROW_FACTOR 2

static int smallest_bound(int n)
{
    int bound = INITIAL_CAPACITY;
    while (bound < n)
        bound *= GROW_FACTOR;
    if (bound > MAX_CAPACITY)
        return MAX_CAPACITY;
    return bound;
}

static int ensure_capacity(miga_alias_map_t *store, int needed)
{
     MIGA_EXPECTS_NOT_NULL(store);

    if (needed <= store->capacity)
        return store->capacity;

    int new_capacity = smallest_bound(needed);
    if (new_capacity > store->capacity)
    {
        miga_alias_map_entry_t *new_data = miga_realloc(store->data, new_capacity * sizeof(miga_alias_map_entry_t));
        store->data = new_data;

        // Clear any newly allocated slots
        for (int i = store->capacity; i < new_capacity; i++)
        {
            store->data[i].key = NULL;
            store->data[i].value = NULL;
        }
        store->capacity = new_capacity;
    }
    return store->capacity;
}

/* If found, returns the index of the existing element.
 * Otherwise, it returns the index where the element should be inserted:
 * If there is an entry at the insertion point, the new element should be inserted before it.
 */
static int find_insertion_point(miga_alias_map_t *store, const miga_string_t *name, bool *found)
{
    MIGA_EXPECTS_NOT_NULL(store);
    MIGA_EXPECTS_NOT_NULL(name);
    MIGA_EXPECTS_NOT_NULL(found);
    MIGA_EXPECTS_NOT_NULL(store->compare_func);

    if (store->size == 0)
    {
        *found = false;
        return 0;
    }

    MIGA_EXPECTS_NOT_NULL(store->data);

    int left = 0;
    int right = store->size - 1;

    while (left <= right)
    {
        int mid = left + (right - left) / 2;
        const miga_alias_map_entry_t *mid_entry = &store->data[mid];
        int cmp = store->compare_func(mid_entry->key, name, store->compare_func_data);

        if (cmp == 0)
        {
            *found = true;
            return mid; // Exact match found
        }
        else if (cmp < 0)
            left = mid + 1; // Search in the right half
        else
            right = mid - 1; // Search in the left half
    }
    *found = false;
    return left; // Insertion point
}

static void insert_at(miga_alias_map_t *map, int index, const miga_string_t *name, const miga_string_t *value)
{
    ensure_capacity(map, map->size + 1);

    // Shift elements to make room for the new entry
    for (int i = map->size; i > index; i--)
    {
        map->data[i] = map->data[i - 1];
    }

    map->data[index].key = string_create_from(name);
    map->data[index].value = string_create_from(value);
    map->size++;
}

static void overwrite_at(miga_alias_map_t *map, int index, const miga_string_t *name, const miga_string_t *value)
{
    // Could add a check to ensure the name matches, for paranoia.
    (void)name;
    string_set(map->data[index].value, value);
}

miga_alias_map_t *miga_alias_map_create(void)
{
    miga_alias_map_t *store = miga_malloc(sizeof(miga_alias_map_t));
    store->data = miga_calloc(INITIAL_CAPACITY, sizeof(miga_alias_map_entry_t));
    store->size = 0;
    store->capacity = INITIAL_CAPACITY;
    store->compare_func = miga_alias_map_default_compare;
    store->compare_func_data = NULL;

    return store;
}

miga_alias_map_t *miga_alias_map_clone(const miga_alias_map_t *other)
{
    MIGA_EXPECTS_NOT_NULL(other);

    miga_alias_map_t *new_store = miga_alias_map_create();
    int new_capacity = smallest_bound(other->size);
    if (new_capacity > new_store->capacity)
    {
        miga_free(new_store->data);
        new_store->data = miga_calloc(new_capacity, sizeof(miga_alias_map_entry_t));
        new_store->capacity = new_capacity;
    }

    for (int i = 0; i < other->size; i++)
    {
        const miga_alias_map_entry_t *entry = &other->data[i];
        new_store->data[i].key = string_create_from(entry->key);
        new_store->data[i].value = string_create_from(entry->value);
    }
    new_store->size = other->size;
    new_store->compare_func = other->compare_func;
    new_store->compare_func_data = other->compare_func_data;

    return new_store;
}

void miga_alias_map_destroy(miga_alias_map_t **store)
{
    if (!store || !*store)
        return;
    miga_alias_map_t *s = *store;
    for (int i = 0; i < s->size; i++)
    {
        string_destroy(&s->data[i].key);
        string_destroy(&s->data[i].value);
    }
    miga_free(s->data);
    miga_free(s);
    *store = NULL;
}

void
miga_alias_map_clear(miga_alias_map_t *store)
{
    MIGA_EXPECTS_NOT_NULL(store);

    for (int i = 0; i < store->size; i++)
    {
        string_destroy(&store->data[i].key);
        string_destroy(&store->data[i].value);
    }
    store->size = 0;
    if (store->capacity > INITIAL_CAPACITY)
    {
        miga_free(store->data);
        store->data = miga_calloc(INITIAL_CAPACITY, sizeof(miga_alias_map_entry_t));
        store->capacity = INITIAL_CAPACITY;
    }
}

/**
 * This function attempts to insert a new alias into the store.
 * If an alias with the same name already exists, no insertion occurs.
 */
miga_alias_map_emplace_result_t
miga_alias_map_emplace(miga_alias_map_t *store, const miga_string_t *name, const miga_string_t *value)
{
    MIGA_EXPECTS_NOT_NULL(store);
    MIGA_EXPECTS_NOT_NULL(name);
    MIGA_EXPECTS_NOT_NULL(value);

    bool found;
    int index = find_insertion_point(store, name, &found);
    if (found)
    {
        return (miga_alias_map_emplace_result_t){ .index = index, .inserted = false };
    }
    else
    {
        insert_at(store, index, name, value);
        return (miga_alias_map_emplace_result_t){ .index = index, .inserted = true };
    }
}

/**
 * This function attempts to insert or replace an alias in the store.
 * If an alias with the same name already exists, it is replaced with the new value.
 */
miga_alias_map_emplace_result_t
miga_alias_map_emplace_assign(miga_alias_map_t *store, const miga_string_t *name, const miga_string_t *value)
{
    MIGA_EXPECTS_NOT_NULL(store);
    MIGA_EXPECTS_NOT_NULL(name);
    MIGA_EXPECTS_NOT_NULL(value);

    bool found;
    int index = find_insertion_point(store, name, &found);
    if (found)
    {
        overwrite_at(store, index, name, value);
        return (miga_alias_map_emplace_result_t){ .index = index, .inserted = false };
    }
    insert_at(store, index, name, value);
    return (miga_alias_map_emplace_result_t){ .index = index, .inserted = !found };
}

/* Returns the number of elements with the given name: 0 or 1 */
int
miga_alias_map_count(const miga_alias_map_t *store, const miga_string_t *name)
{
    MIGA_EXPECTS_NOT_NULL(store);
    MIGA_EXPECTS_NOT_NULL(name);

    bool found;
    find_insertion_point((miga_alias_map_t *)store, name, &found);
    return found ? 1 : 0;
}

/* Returns true if the alias with the given name exists */
bool
miga_alias_map_contains(const miga_alias_map_t *store, const miga_string_t *name)
{
    MIGA_EXPECTS_NOT_NULL(store);
    MIGA_EXPECTS_NOT_NULL(name);

    bool found;
    find_insertion_point((miga_alias_map_t *)store, name, &found);
    return found;
}

/* Returns the number of elements removed: 0 or 1*/
bool
miga_alias_map_erase(miga_alias_map_t *store, const miga_string_t *name)
{
    MIGA_EXPECTS_NOT_NULL(store);
    MIGA_EXPECTS_NOT_NULL(name);

    bool found;
    int index = find_insertion_point(store, name, &found);
    if (!found)
        return false;

    string_destroy(&store->data[index].key);
    string_destroy(&store->data[index].value);

    // Shift elements to fill the gap
    for (int i = index; i < store->size - 1; i++)
    {
        store->data[i] = store->data[i + 1];
    }
    store->size--;
    return true;
}

/* Returns the value associated with the given name. You must ensure the alias
 * exists before calling this function. */
const miga_string_t *
miga_alias_map_get(const miga_alias_map_t *map, const miga_string_t *name)
{
    MIGA_EXPECTS_NOT_NULL(map);
    MIGA_EXPECTS_NOT_NULL(map->data);
    MIGA_EXPECTS_NOT_NULL(name);

    bool found;
    int index = find_insertion_point((miga_alias_map_t *)map, name, &found);
    if (!found)
    {
        MIGA_LOG_ERROR("Alias with name '%s' not found in store", string_cstr(name));
        return NULL;
    }
    return &map->data[index].value;
}


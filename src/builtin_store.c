/**
 * @file builtin_store.c
 * @brief Hash-based builtin command registry implementation.
 *
 * Uses open addressing with linear probing and FNV-1a hashing.
 * The table grows by doubling when the load factor (live + tombstones)
 * exceeds 70%.  On growth, tombstones are purged.
 */

#include "builtin_store.h"

#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

/* ============================================================================
 * Constants
 * ============================================================================ */

/** Initial number of hash-table slots.  Must be a power of two. */
#define BUILTIN_STORE_INITIAL_CAPACITY 32

/** Load-factor threshold (percent).  Grow when (count + tombstones) * 100 / capacity > this. */
#define BUILTIN_STORE_LOAD_FACTOR_PCT 70

/* ============================================================================
 * FNV-1a Hash
 * ============================================================================ */

/**
 * Compute the FNV-1a hash of a NUL-terminated string.
 *
 * FNV-1a is fast, has good avalanche properties, and produces very few
 * collisions on short identifier-like strings — ideal for command names.
 */
static uint32_t fnv1a_hash(const char *str)
{
    uint32_t hash = 2166136261u; /* FNV offset basis */
    for (const unsigned char *p = (const unsigned char *)str; *p; p++)
    {
        hash ^= *p;
        hash *= 16777619u; /* FNV prime */
    }
    return hash;
}

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/**
 * Find the slot for @p name, or the first empty/tombstone slot where it
 * would be inserted.
 *
 * @param entries   The hash table array.
 * @param capacity  Number of slots (must be a power of two).
 * @param name      The name to look up.
 * @param hash      Precomputed FNV-1a hash of @p name.
 * @return          Index into @p entries.
 */
static size_t find_slot(const builtin_entry_t *entries, size_t capacity, const char *name,
                        uint32_t hash)
{
    size_t mask = capacity - 1; /* capacity is always a power of two */
    size_t idx = hash & mask;
    size_t first_tombstone = SIZE_MAX;

    for (size_t i = 0; i < capacity; i++)
    {
        size_t probe = (idx + i) & mask;
        const builtin_entry_t *e = &entries[probe];

        switch (e->state)
        {
        case BUILTIN_SLOT_EMPTY:
            /* Name is not in the table.  Return the first tombstone we
               passed if there was one, otherwise this empty slot. */
            return (first_tombstone != SIZE_MAX) ? first_tombstone : probe;

        case BUILTIN_SLOT_TOMBSTONE:
            if (first_tombstone == SIZE_MAX)
                first_tombstone = probe;
            break;

        case BUILTIN_SLOT_OCCUPIED:
            if (e->hash == hash && strcmp(e->name, name) == 0)
                return probe; /* Found it. */
            break;
        }
    }

    /* Table is full (should never happen if load factor is maintained). */
    return (first_tombstone != SIZE_MAX) ? first_tombstone : 0;
}

/**
 * Grow the table to @p new_capacity and rehash all live entries.
 * Tombstones are purged during rehash.
 */
static bool grow(builtin_store_t *store, size_t new_capacity)
{
    builtin_entry_t *new_entries = xcalloc(new_capacity, sizeof(builtin_entry_t));
    if (!new_entries)
        return false;

    /* All slots in new_entries start as BUILTIN_SLOT_EMPTY (zero-init). */

    /* Reinsert every live entry. */
    for (size_t i = 0; i < store->capacity; i++)
    {
        builtin_entry_t *old = &store->entries[i];
        if (old->state != BUILTIN_SLOT_OCCUPIED)
        {
            /* Free tombstoned names (shouldn't have any, but be safe). */
            if (old->name)
            {
                xfree(old->name);
                old->name = NULL;
            }
            continue;
        }

        size_t slot = find_slot(new_entries, new_capacity, old->name, old->hash);
        new_entries[slot] = *old; /* Shallow copy — we take ownership of old->name. */
    }

    xfree(store->entries);
    store->entries = new_entries;
    store->capacity = new_capacity;
    store->tombstones = 0; /* All purged during rehash. */

    return true;
}

/**
 * Ensure there is room for at least one more entry, growing if needed.
 */
static bool ensure_capacity(builtin_store_t *store)
{
    size_t used = store->count + store->tombstones;
    if (used * 100 / store->capacity > BUILTIN_STORE_LOAD_FACTOR_PCT)
        return grow(store, store->capacity * 2);
    return true;
}

/* ============================================================================
 * Lifecycle
 * ============================================================================ */

builtin_store_t *builtin_store_create(void)
{
    builtin_store_t *store = xcalloc(1, sizeof(builtin_store_t));
    if (!store)
        return NULL;

    store->entries = xcalloc(BUILTIN_STORE_INITIAL_CAPACITY, sizeof(builtin_entry_t));
    if (!store->entries)
    {
        xfree(store);
        return NULL;
    }

    store->capacity = BUILTIN_STORE_INITIAL_CAPACITY;
    store->count = 0;
    store->tombstones = 0;

    return store;
}

void builtin_store_destroy(builtin_store_t **store_ptr)
{
    if (!store_ptr || !*store_ptr)
        return;

    builtin_store_t *store = *store_ptr;

    for (size_t i = 0; i < store->capacity; i++)
    {
        if (store->entries[i].name)
            xfree(store->entries[i].name);
    }

    xfree(store->entries);
    xfree(store);
    *store_ptr = NULL;
}

/* ============================================================================
 * Mutation
 * ============================================================================ */

bool builtin_store_set(builtin_store_t *store, const char *name, builtin_fn_t fn,
                       builtin_category_t category)
{
    if (!store || !name || !fn)
        return false;

    if (!ensure_capacity(store))
        return false;

    uint32_t hash = fnv1a_hash(name);
    size_t slot = find_slot(store->entries, store->capacity, name, hash);
    builtin_entry_t *e = &store->entries[slot];

    if (e->state == BUILTIN_SLOT_OCCUPIED)
    {
        /* Replace existing entry — keep the name allocation. */
        e->fn = fn;
        e->category = category;
        return true;
    }

    /* New entry.  If we're reusing a tombstone, adjust the count. */
    if (e->state == BUILTIN_SLOT_TOMBSTONE)
        store->tombstones--;

    e->state = BUILTIN_SLOT_OCCUPIED;
    e->hash = hash;
    e->name = xstrdup(name);
    e->fn = fn;
    e->category = category;

    if (!e->name)
    {
        /* Allocation failed — revert. */
        e->state = BUILTIN_SLOT_EMPTY;
        return false;
    }

    store->count++;
    return true;
}

bool builtin_store_remove(builtin_store_t *store, const char *name)
{
    if (!store || !name)
        return false;

    uint32_t hash = fnv1a_hash(name);
    size_t slot = find_slot(store->entries, store->capacity, name, hash);
    builtin_entry_t *e = &store->entries[slot];

    if (e->state != BUILTIN_SLOT_OCCUPIED)
        return false;

    xfree(e->name);
    e->name = NULL;
    e->fn = NULL;
    e->state = BUILTIN_SLOT_TOMBSTONE;

    store->count--;
    store->tombstones++;

    return true;
}

void builtin_store_clear(builtin_store_t *store)
{
    if (!store)
        return;

    for (size_t i = 0; i < store->capacity; i++)
    {
        if (store->entries[i].name)
        {
            xfree(store->entries[i].name);
            store->entries[i].name = NULL;
        }
        store->entries[i].state = BUILTIN_SLOT_EMPTY;
        store->entries[i].fn = NULL;
    }

    store->count = 0;
    store->tombstones = 0;
}

/* ============================================================================
 * Lookup
 * ============================================================================ */

bool builtin_store_has(const builtin_store_t *store, const char *name)
{
    if (!store || !name)
        return false;

    uint32_t hash = fnv1a_hash(name);
    size_t slot = find_slot(store->entries, store->capacity, name, hash);

    return store->entries[slot].state == BUILTIN_SLOT_OCCUPIED;
}

builtin_fn_t builtin_store_get(const builtin_store_t *store, const char *name)
{
    if (!store || !name)
        return NULL;

    uint32_t hash = fnv1a_hash(name);
    size_t slot = find_slot(store->entries, store->capacity, name, hash);
    const builtin_entry_t *e = &store->entries[slot];

    if (e->state == BUILTIN_SLOT_OCCUPIED)
        return e->fn;

    return NULL;
}

bool builtin_store_lookup(const builtin_store_t *store, const char *name, builtin_fn_t *fn_out,
                          builtin_category_t *category_out)
{
    if (!store || !name)
        return false;

    uint32_t hash = fnv1a_hash(name);
    size_t slot = find_slot(store->entries, store->capacity, name, hash);
    const builtin_entry_t *e = &store->entries[slot];

    if (e->state != BUILTIN_SLOT_OCCUPIED)
        return false;

    if (fn_out)
        *fn_out = e->fn;
    if (category_out)
        *category_out = e->category;

    return true;
}

/* ============================================================================
 * Queries
 * ============================================================================ */

size_t builtin_store_count(const builtin_store_t *store)
{
    if (!store)
        return 0;
    return store->count;
}

/* ============================================================================
 * Iteration
 * ============================================================================ */

void builtin_store_for_each(const builtin_store_t *store, builtin_store_iter_fn_t callback,
                            void *context)
{
    if (!store || !callback)
        return;

    for (size_t i = 0; i < store->capacity; i++)
    {
        const builtin_entry_t *e = &store->entries[i];
        if (e->state == BUILTIN_SLOT_OCCUPIED)
            callback(e->name, e->fn, e->category, context);
    }
}

#include "alias_store.h"
#include <stdlib.h>
#include "alias_array.h"

struct AliasStore {
    AliasArray *aliases;
};

// Comparison function for finding Alias by name
static int compare_alias_name(const Alias *alias, const void *name)
{
    return string_compare(alias_get_name(alias), (const String *)name);
}

static int compare_alias_name_cstr(const Alias *alias, const void *name)
{
    return string_compare_cstr(alias_get_name(alias), (const char *)name);
}

// Constructors
AliasStore *alias_store_create(void)
{
    return alias_store_create_with_capacity(0);
}

AliasStore *alias_store_create_with_capacity(size_t capacity)
{
    AliasStore *store = malloc(sizeof(AliasStore));
    if (!store) {
        log_fatal("alias_store_create_with_capacity: out of memory");
        return NULL;
    }

    store->aliases = alias_array_create_with_free((AliasArrayFreeFunc)alias_destroy);
    if (!store->aliases) {
        free(store);
        log_fatal("alias_store_create_with_capacity: failed to create aliases array");
        return NULL;
    }

    if (capacity > 0 && alias_array_resize(store->aliases, capacity) != 0) {
        alias_array_destroy(store->aliases);
        free(store);
        log_fatal("alias_store_create_with_capacity: failed to resize aliases array");
        return NULL;
    }

    return store;
}

// Destructor
void alias_store_destroy(AliasStore *store)
{
    if (store) {
        log_debug("alias_store_destroy: freeing store %p, size %zu",
                  store, alias_array_size(store->aliases));
        alias_array_destroy(store->aliases);
        free(store);
    }
}

// Add name/value pairs
int alias_store_add(AliasStore *store, const String *name, const String *value)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);
    return_val_if_null(value, -1);

    // Check if name exists
    size_t index;
    if (alias_array_find_with_compare(store->aliases, name, compare_alias_name, &index) == 0) {
        // Replace existing alias
        Alias *new_alias = alias_create(name, value);
        if (!new_alias) {
            log_fatal("alias_store_add: failed to create alias");
            return -1;
        }
        return alias_array_set(store->aliases, index, new_alias);
    }

    // Add new alias
    Alias *alias = alias_create(name, value);
    if (!alias) {
        log_fatal("alias_store_add: failed to create alias");
        return -1;
    }

    if (alias_array_append(store->aliases, alias) != 0) {
        alias_destroy(alias);
        log_fatal("alias_store_add: failed to append alias");
        return -1;
    }

    return 0;
}

int alias_store_add_cstr(AliasStore *store, const char *name, const char *value)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);
    return_val_if_null(value, -1);

    // Check if name exists
    size_t index;
    if (alias_array_find_with_compare(store->aliases, name, compare_alias_name_cstr, &index) == 0) {
        // Replace existing alias
        Alias *new_alias = alias_create_from_cstr(name, value);
        if (!new_alias) {
            log_fatal("alias_store_add_cstr: failed to create alias");
            return -1;
        }
        return alias_array_set(store->aliases, index, new_alias);
    }

    // Add new alias
    Alias *alias = alias_create_from_cstr(name, value);
    if (!alias) {
        log_fatal("alias_store_add_cstr: failed to create alias");
        return -1;
    }

    if (alias_array_append(store->aliases, alias) != 0) {
        alias_destroy(alias);
        log_fatal("alias_store_add_cstr: failed to append alias");
        return -1;
    }

    return 0;
}

// Remove by name
int alias_store_remove(AliasStore *store, const String *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    if (alias_array_find_with_compare(store->aliases, name, compare_alias_name, &index) != 0) {
        return -1; // Name not found
    }

    return alias_array_remove(store->aliases, index);
}

int alias_store_remove_cstr(AliasStore *store, const char *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    if (alias_array_find_with_compare(store->aliases, name, compare_alias_name_cstr, &index) != 0) {
        return -1; // Name not found
    }

    return alias_array_remove(store->aliases, index);
}

// Clear all entries
int alias_store_clear(AliasStore *store)
{
    return_val_if_null(store, -1);

    log_debug("alias_store_clear: clearing store %p, size %zu",
              store, alias_array_size(store->aliases));

    return alias_array_clear(store->aliases);
}

// Get size
size_t alias_store_size(const AliasStore *store)
{
    return_val_if_null(store, 0);
    return alias_array_size(store->aliases);
}

// Check if name is defined
int alias_store_has_name(const AliasStore *store, const String *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    return alias_array_find_with_compare(store->aliases, name, compare_alias_name, &index) == 0 ? 1 : 0;
}

int alias_store_has_name_cstr(const AliasStore *store, const char *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    return alias_array_find_with_compare(store->aliases, name, compare_alias_name_cstr, &index) == 0 ? 1 : 0;
}

// Get value by name
const String *alias_store_get_value(const AliasStore *store, const String *name)
{
    return_val_if_null(store, NULL);
    return_val_if_null(name, NULL);

    size_t index;
    if (alias_array_find_with_compare(store->aliases, name, compare_alias_name, &index) != 0) {
        return NULL; // Name not found
    }

    Alias *alias = alias_array_get(store->aliases, index);
    return alias ? alias_get_value(alias) : NULL;
}

const char *alias_store_get_value_cstr(const AliasStore *store, const char *name)
{
    return_val_if_null(store, NULL);
    return_val_if_null(name, NULL);

    size_t index;
    if (alias_array_find_with_compare(store->aliases, name, compare_alias_name_cstr, &index) != 0) {
        return NULL; // Name not found
    }

    Alias *alias = alias_array_get(store->aliases, index);
    return alias ? alias_get_value_cstr(alias) : NULL;
}

#include "alias_store.h"
#include "xalloc.h"
#include "alias_array.h"
#include "logging.h"
#include <ctype.h>

struct AliasStore {
    AliasArray *aliases;
};

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
bool alias_name_is_valid(const char *name)
{
    Expects_not_null(name);

    // Empty name is not valid
    if (name[0] == '\0')
        return false;

    for (const char *p = name; *p != '\0'; p++) {
        if (!is_valid_alias_char(*p))
            return false;
    }
    return true;
}

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
    AliasStore *store = xmalloc(sizeof(AliasStore));
    store->aliases = alias_array_create_with_free((AliasArrayFreeFunc)alias_destroy);

    if (capacity > 0) {
        alias_array_resize(store->aliases, capacity);
    }

    return store;
}

// Destructor
void alias_store_destroy(AliasStore *store)
{
    Expects_not_null(store);

    log_debug("alias_store_destroy: freeing store %p, size %zu",
              store, alias_array_size(store->aliases));
    alias_array_destroy(store->aliases);
    xfree(store);
}

// Add name/value pairs
void alias_store_add(AliasStore *store, const String *name, const String *value)
{
    Expects_not_null(store);
    Expects_not_null(name);
    Expects_not_null(value);

    // Check if name exists
    size_t index;
    if (alias_array_find_with_compare(store->aliases, name, compare_alias_name, &index) == 0) {
        // Replace existing alias
        Alias *new_alias = alias_create(name, value);
        alias_array_set(store->aliases, index, new_alias);
        return;
    }

    // Add new alias
    Alias *alias = alias_create(name, value);
    alias_array_append(store->aliases, alias);
}

void alias_store_add_cstr(AliasStore *store, const char *name, const char *value)
{
    Expects_not_null(store);
    Expects_not_null(name);
    Expects_not_null(value);

    // Check if name exists
    size_t index;
    if (alias_array_find_with_compare(store->aliases, name, compare_alias_name_cstr, &index) == 0) {
        // Replace existing alias
        Alias *new_alias = alias_create_from_cstr(name, value);
        alias_array_set(store->aliases, index, new_alias);
        return;
    }

    // Add new alias
    Alias *alias = alias_create_from_cstr(name, value);
    alias_array_append(store->aliases, alias);
}

// Remove by name
bool alias_store_remove(AliasStore *store, const String *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    size_t index;
    if (alias_array_find_with_compare(store->aliases, name, compare_alias_name, &index) != 0) {
        return false; // Name not found
    }

    alias_array_remove(store->aliases, index);
    return true;
}

bool alias_store_remove_cstr(AliasStore *store, const char *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    size_t index;
    if (alias_array_find_with_compare(store->aliases, name, compare_alias_name_cstr, &index) != 0) {
        return false; // Name not found
    }

    alias_array_remove(store->aliases, index);
    return true;
}

// Clear all entries
void alias_store_clear(AliasStore *store)
{
    Expects_not_null(store);

    log_debug("alias_store_clear: clearing store %p, size %zu",
              store, alias_array_size(store->aliases));

    alias_array_clear(store->aliases);
}

// Get size
size_t alias_store_size(const AliasStore *store)
{
    Expects_not_null(store);
    return alias_array_size(store->aliases);
}

// Check if name is defined
bool alias_store_has_name(const AliasStore *store, const String *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    size_t index;
    return alias_array_find_with_compare(store->aliases, name, compare_alias_name, &index) == 0;
}

bool alias_store_has_name_cstr(const AliasStore *store, const char *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    size_t index;
    return alias_array_find_with_compare(store->aliases, name, compare_alias_name_cstr, &index) == 0;
}

// Get value by name
const String *alias_store_get_value(const AliasStore *store, const String *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    size_t index;
    if (alias_array_find_with_compare(store->aliases, name, compare_alias_name, &index) != 0) {
        return NULL; // Name not found
    }

    Alias *alias = alias_array_get(store->aliases, index);
    return alias_get_value(alias);
}

const char *alias_store_get_value_cstr(const AliasStore *store, const char *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    size_t index;
    if (alias_array_find_with_compare(store->aliases, name, compare_alias_name_cstr, &index) != 0) {
        return NULL; // Name not found
    }

    Alias *alias = alias_array_get(store->aliases, index);
    return alias_get_value_cstr(alias);
}

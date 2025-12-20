#include "alias_store.h"
#include "alias_array.h"
#include "logging.h"
#include "xalloc.h"
#include <ctype.h>

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

    for (const char *p = name; *p != '\0'; p++)
    {
        if (!is_valid_alias_char(*p))
            return false;
    }
    return true;
}

// Comparison function for finding alias_t by name
static int compare_alias_name(const alias_t *alias, const void *name)
{
    return string_compare(alias_get_name(alias), (const string_t *)name);
}

static int compare_alias_name_cstr(const alias_t *alias, const void *name)
{
    return string_compare_cstr(alias_get_name(alias), (const char *)name);
}

// Constructors
alias_store_t *alias_store_create(void)
{
    return alias_store_create_with_capacity(0);
}

alias_store_t *alias_store_create_with_capacity(int capacity)
{
    Expects_ge(capacity, 0);

    alias_store_t *store = xmalloc(sizeof(alias_store_t));
    store->aliases = alias_array_create_with_free((alias_array_free_func_t)alias_destroy);

    if (capacity > 0)
    {
        alias_array_resize(store->aliases, capacity);
    }

    log_debug("alias_store_create_with_capacity: created store %p with capacity %d", store->aliases,
              alias_array_capacity(store->aliases));

    return store;
}

// Destructor
void alias_store_destroy(alias_store_t **store)
{
    Expects_not_null(store);
    Expects_not_null(*store);
    Expects_not_null((*store)->aliases);

    log_debug("alias_store_destroy: freeing store %p, size %zu", *store,
              alias_array_size((*store)->aliases));
    alias_array_destroy(&((*store)->aliases));
    xfree(*store);
    *store = NULL;
}

// Add name/value pairs
void alias_store_add(alias_store_t *store, const string_t *name, const string_t *value)
{
    Expects_not_null(store);
    Expects_not_null(name);
    Expects_not_null(value);

    // Check if name exists
    int index;
    if (alias_array_find_with_compare(store->aliases, name, compare_alias_name, &index) == 0)
    {
        // Replace existing alias
        alias_t *new_alias = alias_create(name, value);
        alias_array_set(store->aliases, index, new_alias);
        log_debug("alias_store_add: replaced alias '%s' = '%s'", string_cstr(name),
                  string_cstr(value));
        return;
    }

    // Add new alias
    alias_t *alias = alias_create(name, value);
    alias_array_append(store->aliases, alias);
    log_debug("alias_store_add: added alias '%s' = '%s'", string_cstr(name), string_cstr(value));
}

void alias_store_add_cstr(alias_store_t *store, const char *name, const char *value)
{
    Expects_not_null(store);
    Expects_not_null(name);
    Expects_not_null(value);

    // Check if name exists
    int index;
    if (alias_array_find_with_compare(store->aliases, name, compare_alias_name_cstr, &index) == 0)
    {
        // Replace existing alias
        alias_t *new_alias = alias_create_from_cstr(name, value);
        alias_array_set(store->aliases, index, new_alias);
        log_debug("alias_store_add_cstr: replaced alias '%s' = '%s'", name, value);
        return;
    }

    // Add new alias
    alias_t *alias = alias_create_from_cstr(name, value);
    alias_array_append(store->aliases, alias);
    log_debug("alias_store_add_cstr: added alias '%s' = '%s'", name, value);
}

// Remove by name
bool alias_store_remove(alias_store_t *store, const string_t *name)
{
    Expects_not_null(store);
    Expects_not_null(store->aliases);
    Expects_not_null(name);

    int index;
    if (alias_array_find_with_compare(store->aliases, name, compare_alias_name, &index) != 0)
    {
        return false; // Name not found
    }

    alias_array_remove(store->aliases, index);
    return true;
}

bool alias_store_remove_cstr(alias_store_t *store, const char *name)
{
    Expects_not_null(store);
    Expects_not_null(store->aliases);
    Expects_not_null(name);

    int index;
    if (alias_array_find_with_compare(store->aliases, name, compare_alias_name_cstr, &index) != 0)
    {
        return false; // Name not found
    }

    alias_array_remove(store->aliases, index);
    return true;
}

// Clear all entries
void alias_store_clear(alias_store_t *store)
{
    Expects_not_null(store);
    Expects_not_null(store->aliases);

    log_debug("alias_store_clear: clearing store %p, size %zu", store,
              alias_array_size(store->aliases));

    alias_array_clear(store->aliases);
}

// Get size
int alias_store_size(const alias_store_t *store)
{
    Expects_not_null(store);
    Expects_not_null(store->aliases);

    return alias_array_size(store->aliases);
}

// Check if name is defined
bool alias_store_has_name(const alias_store_t *store, const string_t *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    int index;
    return alias_array_find_with_compare(store->aliases, name, compare_alias_name, &index) == 0;
}

bool alias_store_has_name_cstr(const alias_store_t *store, const char *name)
{
    Expects_not_null(store);
    Expects_not_null(store->aliases);
    Expects_not_null(name);

    int index;
    return alias_array_find_with_compare(store->aliases, name, compare_alias_name_cstr, &index) ==
           0;
}

// Get value by name
const string_t *alias_store_get_value(const alias_store_t *store, const string_t *name)
{
    Expects_not_null(store);
    Expects_not_null(store->aliases);
    Expects_not_null(name);

    int index;
    if (alias_array_find_with_compare(store->aliases, name, compare_alias_name, &index) != 0)
    {
        return NULL; // Name not found
    }

    alias_t *alias = alias_array_get(store->aliases, index);
    return alias_get_value(alias);
}

const char *alias_store_get_value_cstr(const alias_store_t *store, const char *name)
{
    Expects_not_null(store);
    Expects_not_null(store->aliases);
    Expects_not_null(name);

    int index;
    if (alias_array_find_with_compare(store->aliases, name, compare_alias_name_cstr, &index) != 0)
    {
        return NULL; // Name not found
    }

    alias_t *alias = alias_array_get(store->aliases, index);
    return alias_get_value_cstr(alias);
}

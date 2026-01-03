#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "func_store.h"
#include "func_map.h"
#include "string_t.h"
#include "xalloc.h"
#include "logging.h"

// Simple POSIX-like identifier validator: [A-Za-z_][A-Za-z0-9_]*
static bool is_valid_name_cstr(const char *s)
{
    if (!s || *s == '\0')
        return false;

    if (!(isalpha((unsigned char)*s) || *s == '_'))
        return false;

    for (const unsigned char *p = (const unsigned char *)s + 1; *p; ++p)
    {
        if (!(isalnum(*p) || *p == '_'))
            return false;
    }
    return true;
}

static bool is_valid_name(const string_t *name)
{
    if (!name || string_empty(name))
        return false;

    const char *s = string_cstr(name);
    return is_valid_name_cstr(s);
}

func_store_t *func_store_create(void)
{
    func_store_t *store = xcalloc(1, sizeof(func_store_t));
    store->map = func_map_create();
    return store;
}

func_store_t *func_store_copy(const func_store_t *other)
{
    if (!other || !other->map)
        return NULL;

    func_store_t *new_store = func_store_create();
    if (!new_store)
        return NULL;

    for (int32_t i = 0; i < other->map->capacity; i++)
    {
        if (!other->map->entries[i].occupied)
            continue;

        const string_t *name = other->map->entries[i].key;
        const func_map_mapped_t *mapped = &other->map->entries[i].mapped;

        // Clone the AST node (deep copy because by policy no AST node can have more than 1 owner)
        ast_node_t *func_clone = ast_node_clone(mapped->func);

        // Add to new store
        func_store_add(new_store, name, func_clone);
    }

    return new_store;
}

void func_store_destroy(func_store_t **store)
{
    if (!store || !*store)
        return;

    func_map_destroy(&(*store)->map);
    xfree(*store);
    *store = NULL;
}

void func_store_clear(func_store_t *store)
{
    if (!store || !store->map)
        return;

    func_map_clear(store->map);
}

func_store_error_t func_store_add(func_store_t *store, const string_t *name,
                                   ast_node_t *value)
{
    if (!store || !store->map)
        return FUNC_STORE_ERROR_STORAGE_FAILURE;

    if (!name)
        return FUNC_STORE_ERROR_EMPTY_NAME;

    if (string_empty(name))
        return FUNC_STORE_ERROR_EMPTY_NAME;

    if (!is_valid_name(name))
        return FUNC_STORE_ERROR_NAME_INVALID_CHARACTER;

    if (!value)
        return FUNC_STORE_ERROR_STORAGE_FAILURE;

    // Create mapped value - clone the name and take ownership of the AST node pointer
    // The caller transfers ownership of the value pointer to this function.
    func_map_mapped_t mapped;
    mapped.name = string_create_from(name);
    mapped.func = value; // Take ownership - caller must not use this pointer afterward
    mapped.exported = false; // Default to not exported

    func_map_insert_or_assign_move(store->map, name, &mapped);

    return FUNC_STORE_ERROR_NONE;
}

func_store_error_t func_store_add_cstr(func_store_t *store, const char *name,
                                        ast_node_t *value)
{
    if (!name)
        return FUNC_STORE_ERROR_EMPTY_NAME;

    string_t *name_str = string_create_from_cstr(name);
    func_store_error_t result = func_store_add(store, name_str, value);
    string_destroy(&name_str);

    return result;
}

func_store_error_t func_store_remove(func_store_t *store, const string_t *name)
{
    if (!store || !store->map)
        return FUNC_STORE_ERROR_STORAGE_FAILURE;

    if (!name || string_empty(name))
        return FUNC_STORE_ERROR_EMPTY_NAME;

    if (!func_map_contains(store->map, name))
        return FUNC_STORE_ERROR_NOT_FOUND;

    func_map_erase(store->map, name);
    return FUNC_STORE_ERROR_NONE;
}

func_store_error_t func_store_remove_cstr(func_store_t *store, const char *name)
{
    if (!name)
        return FUNC_STORE_ERROR_EMPTY_NAME;

    string_t *name_str = string_create_from_cstr(name);
    func_store_error_t result = func_store_remove(store, name_str);
    string_destroy(&name_str);

    return result;
}

bool func_store_has_name(const func_store_t *store, const string_t *name)
{
    if (!store || !store->map || !name)
        return false;

    return func_map_contains(store->map, name);
}

bool func_store_has_name_cstr(const func_store_t *store, const char *name)
{
    if (!name)
        return false;

    string_t *name_str = string_create_from_cstr(name);
    bool result = func_store_has_name(store, name_str);
    string_destroy(&name_str);

    return result;
}

const ast_node_t *func_store_get_def(const func_store_t *store, const string_t *name)
{
    if (!store || !store->map || !name)
        return NULL;

    const func_map_mapped_t *mapped = func_map_at(store->map, name);
    if (!mapped)
        return NULL;

    return mapped->func;
}

const ast_node_t *func_store_get_def_cstr(const func_store_t *store, const char *name)
{
    if (!name)
        return NULL;

    string_t *name_str = string_create_from_cstr(name);
    const ast_node_t *result = func_store_get_def(store, name_str);
    string_destroy(&name_str);

    return result;
}

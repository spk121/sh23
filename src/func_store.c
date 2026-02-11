#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define FUNC_MAP_INTERNAL
#include "exec_redirect.h"
#include "func_map.h"
#include "func_store.h"
#include "logging.h"
#include "string_t.h"
#include "xalloc.h"

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

/* Callback context for func_store_clone */
typedef struct clone_ctx_t
{
    func_store_t *dst;
} clone_ctx_t;

static void clone_callback(const string_t *key, const func_map_mapped_t *mapped, void *user_data)
{
    clone_ctx_t *ctx = user_data;

    /* func_store_add_ex deep-copies everything internally */
    func_store_add_ex(ctx->dst, key, mapped->func, mapped->redirections);
}

func_store_t *func_store_clone(const func_store_t *other)
{
    if (!other || !other->map)
        return NULL;

    func_store_t *new_store = func_store_create();
    if (!new_store)
        return NULL;

    clone_ctx_t ctx = {.dst = new_store};
    func_map_foreach(other->map, clone_callback, &ctx);

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
                                  const ast_node_t *value)
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

    func_map_mapped_t mapped;
    mapped.name = string_create_from(name);
    mapped.func = ast_node_clone(value);
    mapped.redirections = NULL;

    func_map_insert_or_assign_move(store->map, name, &mapped);

    return FUNC_STORE_ERROR_NONE;
}

func_store_error_t func_store_add_cstr(func_store_t *store, const char *name,
                                       const ast_node_t *value)
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

const exec_redirections_t *func_store_get_redirections(const func_store_t *store,
                                                  const string_t *name)
{
    Expects_not_null(store);
    Expects_not_null(name);
    const func_map_mapped_t *mapped = func_map_at(store->map, name);
    if (!mapped)
        return NULL;
    return mapped->redirections;
}

func_store_insert_result_t func_store_add_ex(func_store_t *store, const string_t *name,
                                             const ast_node_t *value,
                                             const exec_redirections_t *redirections)
{
    func_store_insert_result_t result;
    result.was_new = false;

    if (!store || !store->map)
    {
        result.error = FUNC_STORE_ERROR_STORAGE_FAILURE;
        return result;
    }

    if (!name)
    {
        result.error = FUNC_STORE_ERROR_EMPTY_NAME;
        return result;
    }

    if (string_empty(name))
    {
        result.error = FUNC_STORE_ERROR_EMPTY_NAME;
        return result;
    }

    if (!is_valid_name(name))
    {
        result.error = FUNC_STORE_ERROR_NAME_INVALID_CHARACTER;
        return result;
    }

    if (!value)
    {
        result.error = FUNC_STORE_ERROR_STORAGE_FAILURE;
        return result;
    }

    // Check if function already exists
    result.was_new = !func_map_contains(store->map, name);

    func_map_mapped_t mapped;
    mapped.name = string_create_from(name);
    mapped.func = ast_node_clone(value);
    mapped.redirections = redirections ? exec_redirections_clone(redirections) : NULL;

    func_map_insert_or_assign_move(store->map, name, &mapped);

    result.error = FUNC_STORE_ERROR_NONE;
    return result;
}

/* ============================================================================
 * Iteration
 * ============================================================================ */

typedef struct func_store_foreach_context_t
{
    func_store_foreach_fn user_callback;
    void *user_data;
} func_store_foreach_context_t;

static void func_store_foreach_wrapper(const string_t *key, const func_map_mapped_t *mapped,
                                       void *user_data)
{
    func_store_foreach_context_t *ctx = (func_store_foreach_context_t *)user_data;
    if (ctx && ctx->user_callback && mapped)
    {
        ctx->user_callback(key, mapped->func, ctx->user_data);
    }
}

void func_store_foreach(const func_store_t *store, func_store_foreach_fn callback, void *user_data)
{
    if (!store || !store->map || !callback)
        return;

    func_store_foreach_context_t ctx;
    ctx.user_callback = callback;
    ctx.user_data = user_data;

    func_map_foreach(store->map, func_store_foreach_wrapper, &ctx);
}

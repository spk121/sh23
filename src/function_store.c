#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "function_store.h"
#include "xalloc.h"

// Simple POSIX-like identifier validator: [A-Za-z_][A-Za-z0-9_]*
static int is_valid_name_cstr(const char *s) {
    Expects_not_null(s);
    Expects(*s != '\0');

    if (!(isalpha((unsigned char)*s) || *s == '_')) return 0;
    for (const unsigned char *p = (const unsigned char *)s + 1; *p; ++p) {
        if (!(isalnum(*p) || *p == '_')) return 0;
    }
    return 1;
}

static void function_free(function_t *func) {
    if (!func) return;
    xfree(func->name);
    if (func->body) ast_node_destroy(func->body);
    xfree(func);
}

function_store_t *function_store_create(void) {
    function_store_t *store = xcalloc(1, sizeof(function_store_t));
    store->functions = function_array_create_with_free(function_free);
    return store;
}

void function_store_destroy(function_store_t *store) {
    if (!store) return;
    function_array_destroy(store->functions); // frees elements via function_free
    xfree(store);
}

int function_store_set(function_store_t *store, const char *name, ast_node_t *body) {
    Expects_not_null(store);
    Expects_not_null(name);
    Expects_not_null(body);

    if (!is_valid_name_cstr(name)) {
        fprintf(stderr, "function_store_set: invalid function name: %s\n", name);
        return -1;
    }

    // Update existing
    size_t n = function_array_size(store->functions);
    for (size_t i = 0; i < n; ++i) {
        function_t *func = function_array_get(store->functions, i);
        if (func && strcmp(func->name, name) == 0) {
            if (func->body) ast_node_destroy(func->body);
            func->body = body;
            return 0;
        }
    }

    // Create new
    function_t *func = xcalloc(1, sizeof(function_t));
    func->name = xstrdup(name);
    func->body = body;

    function_array_append(store->functions, func);
    return 0;
}

const function_t *function_store_get(const function_store_t *store, const char *name) {
    Expects_not_null(store);
    Expects_not_null(name);
    size_t n = function_array_size(store->functions);
    for (size_t i = 0; i < n; ++i) {
        function_t *func = function_array_get(store->functions, i);
        if (func && strcmp(func->name, name) == 0) {
            return func;
        }
    }
    return NULL;
}

void function_store_unset(function_store_t *store, const char *name) {
    Expects_not_null(name);
    Expects_not_null(store);
    size_t n = function_array_size(store->functions);
    for (size_t i = 0; i < n; ++i) {
        function_t *func = function_array_get(store->functions, i);
        if (func && strcmp(func->name, name) == 0) {
            function_array_remove(store->functions, i);
        }
    }
}

size_t function_store_size(const function_store_t *store) {
    Expects_not_null(store);
    return function_array_size(store->functions);
}

const function_t *function_store_at(const function_store_t *store, size_t index) {
    Expects_not_null(store);
    return function_array_get(store->functions, index);
}

bool function_store_exists(const function_store_t *store, const char *name) {
    Expects_not_null(store);
    Expects_not_null(name);
    return function_store_get(store, name) != NULL;
}

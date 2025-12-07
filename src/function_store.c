#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "function_store.h"

// Simple POSIX-like identifier validator: [A-Za-z_][A-Za-z0-9_]*
static int is_valid_name_cstr(const char *s) {
    if (!s || !*s) return 0;
    if (!(isalpha((unsigned char)*s) || *s == '_')) return 0;
    for (const unsigned char *p = (const unsigned char *)s + 1; *p; ++p) {
        if (!(isalnum(*p) || *p == '_')) return 0;
    }
    return 1;
}

static void function_free(Function *func) {
    if (!func) return;
    free(func->name);
    if (func->body) ast_node_destroy(func->body);
    free(func);
}

FunctionStore *function_store_create(void) {
    FunctionStore *store = (FunctionStore *)calloc(1, sizeof(FunctionStore));
    if (!store) return NULL;
    store->functions = function_array_create_with_free(function_free);
    if (!store->functions) {
        free(store);
        return NULL;
    }
    return store;
}

void function_store_destroy(FunctionStore *store) {
    if (!store) return;
    function_array_destroy(store->functions); // frees elements via function_free
    free(store);
}

int function_store_set(FunctionStore *store, const char *name, ASTNode *body) {
    if (!store || !name) return -1;
    if (!is_valid_name_cstr(name)) {
        fprintf(stderr, "function_store_set: invalid function name: %s\n", name ? name : "(null)");
        return -1;
    }

    // Update existing
    size_t n = function_array_size(store->functions);
    for (size_t i = 0; i < n; ++i) {
        Function *func = function_array_get(store->functions, i);
        if (func && strcmp(func->name, name) == 0) {
            if (func->body) ast_node_destroy(func->body);
            func->body = body;
            return 0;
        }
    }

    // Create new
    Function *func = (Function *)calloc(1, sizeof(Function));
    if (!func) return -1;
    func->name = strdup(name);
    if (!func->name) { free(func); return -1; }
    func->body = body;

    if (function_array_append(store->functions, func) != 0) {
        free(func->name);
        if (func->body) ast_node_destroy(func->body);
        free(func);
        return -1;
    }
    return 0;
}

const Function *function_store_get(const FunctionStore *store, const char *name) {
    if (!store || !name) return NULL;
    size_t n = function_array_size(store->functions);
    for (size_t i = 0; i < n; ++i) {
        Function *func = function_array_get(store->functions, i);
        if (func && strcmp(func->name, name) == 0) {
            return func;
        }
    }
    return NULL;
}

int function_store_unset(FunctionStore *store, const char *name) {
    if (!store || !name) return -1;
    size_t n = function_array_size(store->functions);
    for (size_t i = 0; i < n; ++i) {
        Function *func = function_array_get(store->functions, i);
        if (func && strcmp(func->name, name) == 0) {
            return function_array_remove(store->functions, i) == 0 ? 1 : -1;
        }
    }
    return 0;
}

size_t function_store_size(const FunctionStore *store) {
    return store ? function_array_size(store->functions) : 0;
}

const Function *function_store_at(const FunctionStore *store, size_t index) {
    if (!store) return NULL;
    return function_array_get(store->functions, index);
}

bool function_store_exists(const FunctionStore *store, const char *name) {
    return function_store_get(store, name) != NULL;
}

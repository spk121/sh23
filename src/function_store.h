#ifndef FUNCTION_STORE_H
#define FUNCTION_STORE_H

#include "ast.h"
#include "function_array.h"
#include <stdbool.h>
#include <stddef.h>

// A shell function: name and parsed AST body.
// Ownership: function_store_t owns both the function_t and its AST body.
typedef struct function_t
{
    char *name;       // Function name
    ast_node_t *body; // AST node for function body (includes redirects)
} function_t;

typedef struct function_store_t
{
    function_array_t *functions; // Array of function_t*
} function_store_t;

// Create and destroy
function_store_t *function_store_create(void);
// Destroys the store and all contained function_t entries, including their AST bodies.
void function_store_destroy(function_store_t **store);

// Add or update a function.
// Ownership: the store takes ownership of 'body' and will destroy any
// previously stored body for the same name.
// Returns 0 on success, non-zero on error (invalid name or OOM).
int function_store_set(function_store_t *store, const char *name, ast_node_t *body);

// Get a function by name (NULL if not found). Returned pointer is owned by the store.
const function_t *function_store_get(const function_store_t *store, const char *name);

// Remove a function by name. Returns 1 if removed, 0 if not found, -1 on error.
// Destroys the function_t and its AST body on removal.
void function_store_unset(function_store_t *store, const char *name);

// Introspection
size_t function_store_size(const function_store_t *store);
const function_t *function_store_at(const function_store_t *store, size_t index);
bool function_store_exists(const function_store_t *store, const char *name);

#endif

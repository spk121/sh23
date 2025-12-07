#ifndef FUNCTION_STORE_H
#define FUNCTION_STORE_H

#include <stddef.h>
#include <stdbool.h>
#include "ast.h"
#include "function_array.h"

// A shell function: name and parsed AST body.
// Ownership: FunctionStore owns both the Function and its AST body.
typedef struct {
    char *name;     // Function name
    ASTNode *body;  // AST node for function body (includes redirects)
} Function;

typedef struct {
    FunctionArray *functions; // Array of Function*
} FunctionStore;

// Create and destroy
FunctionStore *function_store_create(void);
// Destroys the store and all contained Function entries, including their AST bodies.
void function_store_destroy(FunctionStore *store);

// Add or update a function.
// Ownership: the store takes ownership of 'body' and will destroy any
// previously stored body for the same name.
// Returns 0 on success, non-zero on error (invalid name or OOM).
int function_store_set(FunctionStore *store, const char *name, ASTNode *body);

// Get a function by name (NULL if not found). Returned pointer is owned by the store.
const Function *function_store_get(const FunctionStore *store, const char *name);

// Remove a function by name. Returns 1 if removed, 0 if not found, -1 on error.
// Destroys the Function and its AST body on removal.
int function_store_unset(FunctionStore *store, const char *name);

// Introspection
size_t function_store_size(const FunctionStore *store);
const Function *function_store_at(const FunctionStore *store, size_t index);
bool function_store_exists(const FunctionStore *store, const char *name);

#endif

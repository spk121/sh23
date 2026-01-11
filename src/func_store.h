#ifndef FUNC_STORE_H
#define FUNC_STORE_H

#include <stddef.h>
#include <stdbool.h>
#include "ast.h"
#include "func_map.h"

typedef struct func_store_t
{
    func_map_t *map;
} func_store_t;

typedef enum func_store_error_t
{
    FUNC_STORE_ERROR_NONE = 0,
    FUNC_STORE_ERROR_NOT_FOUND,
    FUNC_STORE_ERROR_EMPTY_NAME,
    FUNC_STORE_ERROR_NAME_TOO_LONG,
    FUNC_STORE_ERROR_NAME_INVALID_CHARACTER,
    FUNC_STORE_ERROR_NAME_STARTS_WITH_DIGIT,
    FUNC_STORE_ERROR_STORAGE_FAILURE,
} func_store_error_t;

// Constructors
func_store_t *func_store_create(void);
func_store_t *func_store_copy(const func_store_t *other);

// Destructor
void func_store_destroy(func_store_t **store);

// Clear all name -> function node mappings
void func_store_clear(func_store_t *store);

// Variable management

// Note: Takes ownership of the value node pointer. Caller must not use it after this call.
func_store_error_t func_store_add(func_store_t *store, const string_t *name,
                                  const ast_node_t *value);
func_store_error_t func_store_add_cstr(func_store_t *store, const char *name,
                                       const ast_node_t *value);
func_store_error_t func_store_remove(func_store_t *store, const string_t *name);
func_store_error_t func_store_remove_cstr(func_store_t *store, const char *name);
bool func_store_has_name(const func_store_t *store, const string_t *name);
bool func_store_has_name_cstr(const func_store_t *store, const char *name);
const ast_node_t *func_store_get_def(const func_store_t *store, const string_t *name);
const ast_node_t *func_store_get_def_cstr(const func_store_t *store, const char *name);

#endif

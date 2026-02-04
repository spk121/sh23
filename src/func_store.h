#ifndef FUNC_STORE_H
#define FUNC_STORE_H

#include <stddef.h>
#include <stdbool.h>
#include "ast.h"
#include "func_map.h"

/* Forward declaration */
typedef struct exec_redirections_t exec_redirections_t;

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

typedef struct func_store_insert_result_t
{
    func_store_error_t error;
    bool was_new;  // true if new function was added, false if existing one was replaced
} func_store_insert_result_t;

/* Forward declaration */
typedef struct exec_redirections_t exec_redirections_t;

// Constructors
func_store_t *func_store_create(void);
func_store_t *func_store_clone(const func_store_t *other);

// Destructor
void func_store_destroy(func_store_t **store);

// Clear all name -> function node mappings
void func_store_clear(func_store_t *store);

// Function management

/**
 * Add or update a function definition. Creates a deep copy (clone) of the AST node.
 * The caller retains ownership of the value parameter and must manage its lifetime.
 * @param store The function store
 * @param name Function name (must be valid POSIX identifier)
 * @param value Function definition AST node (will be cloned)
 * @return Error code indicating success or failure
 */
func_store_error_t func_store_add(func_store_t *store, const string_t *name,
                                  const ast_node_t *value);
func_store_error_t func_store_add_cstr(func_store_t *store, const char *name,
                                       const ast_node_t *value);

/**
 * Add or update a function definition with extended result information.
 * Creates a deep copy (clone) of the AST node and redirections.
 * @param store The function store
 * @param name Function name (must be valid POSIX identifier)
 * @param value Function definition AST node (will be cloned)
 * @param redirections Redirections to apply on invocation (will be cloned; may be NULL)
 * @return Result indicating success/failure and whether function was new or replaced
 */
func_store_insert_result_t func_store_add_ex(func_store_t *store, const string_t *name,
                                             const ast_node_t *value,
                                             const exec_redirections_t *redirections);

func_store_error_t func_store_remove(func_store_t *store, const string_t *name);
func_store_error_t func_store_remove_cstr(func_store_t *store, const char *name);
bool func_store_has_name(const func_store_t *store, const string_t *name);
bool func_store_has_name_cstr(const func_store_t *store, const char *name);
const ast_node_t *func_store_get_def(const func_store_t *store, const string_t *name);
const ast_node_t *func_store_get_def_cstr(const func_store_t *store, const char *name);

/**
 * Get function redirections (may be NULL if no redirections)
 * @param store The function store
 * @param name Function name
 * @return Redirections associated with the function, or NULL
 */
const exec_redirections_t *func_store_get_redirections(const func_store_t *store,
                                                        const string_t *name);
 

/**
 * Callback function for iterating over function definitions
 * @param name Function name
 * @param func Function definition AST node
 * @param user_data User-provided context pointer
 */
typedef void (*func_store_foreach_fn)(const string_t *name, const ast_node_t *func, void *user_data);

/**
 * Iterate over all function definitions in the store
 * @param store The function store
 * @param callback Function to call for each entry
 * @param user_data User-provided context pointer passed to callback
 */
void func_store_foreach(const func_store_t *store, func_store_foreach_fn callback, void *user_data);

/**
 * Get function redirections (may be NULL if no redirections)
 * @param store The function store
 * @param name Function name
 * @return Redirections associated with the function, or NULL
 */
const exec_redirections_t *func_store_get_redirections(const func_store_t *store,
                                                        const string_t *name);

#endif

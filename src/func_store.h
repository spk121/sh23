#ifndef FUNC_STORE_H
#define FUNC_STORE_H

#include "ast.h"
#include "string_t.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * @file func_store.h
 * @brief Public API for the shell function definition store.
 *
 * MEMORY SAFETY CONTRACT:
 * ========================
 * The function store is a self-contained memory silo.
 *
 * 1. INPUTS: All functions that modify the store accept non-immediate arguments
 *    as const pointers and deep-copy (clone) any data before incorporating it.
 *    The caller retains full ownership of all arguments passed in.
 *
 * 2. OUTPUTS: Functions that return internal data do so via const pointers.
 *    Returned pointers are valid only until the next mutating operation on
 *    the store. Do not free or modify them.
 *
 * 3. MOVE SEMANTICS: Functions with "move" in the name take ownership of
 *    pointer arguments (passed as T**) and set the source pointer to NULL.
 *
 * The internal func_map is not exposed through this header.
 */

/* Forward declarations -- opaque to public consumers */
typedef struct func_map_t func_map_t;
typedef struct exec_redirections_t exec_redirections_t;

/**
 * Shell function store.
 */
typedef struct func_store_t
{
    /** Internal map. Do not access directly. */
    func_map_t *map;
} func_store_t;

/**
 * Error codes returned by function store operations.
 */
typedef enum func_store_error_t
{
    FUNC_STORE_ERROR_NONE = 0,
    FUNC_STORE_ERROR_NOT_FOUND,
    FUNC_STORE_ERROR_EMPTY_NAME,
    FUNC_STORE_ERROR_NAME_TOO_LONG,
    FUNC_STORE_ERROR_NAME_INVALID_CHARACTER,
    FUNC_STORE_ERROR_NAME_STARTS_WITH_DIGIT,
    FUNC_STORE_ERROR_STORAGE_FAILURE,
    FUNC_STORE_ERROR_PARSE_FAILURE
} func_store_error_t;

/**
 * Result of an insert operation.
 */
typedef struct func_store_insert_result_t
{
    func_store_error_t error;
    bool was_new; /**< true if new function was added, false if existing one was replaced */
} func_store_insert_result_t;

/* ============================================================================
 * Constructors and Destructors
 * ============================================================================ */

/**
 * Create a new, empty function store.
 *
 * @return Newly allocated function store.
 */
func_store_t *func_store_create(void);

/**
 * Create a deep copy of a function store.
 * The returned store is fully independent (no shared pointers).
 *
 * @param other Source function store (must not be NULL).
 * @return Newly allocated clone, or NULL on failure.
 */
func_store_t *func_store_clone(const func_store_t *other);

/**
 * Destroy a function store and free all resources.
 *
 * @param store Pointer to the store pointer; set to NULL on return.
 */
void func_store_destroy(func_store_t **store);

/**
 * Clear all function definitions from the store.
 *
 * @param store The function store.
 */
void func_store_clear(func_store_t *store);

/* ============================================================================
 * Modifiers (deep-copy inputs)
 * ============================================================================ */

/**
 * Add or update a function definition.
 * Creates a deep copy (clone) of the AST node.
 * The caller retains ownership of all arguments.
 *
 * @param store The function store.
 * @param name Function name (must be valid POSIX identifier; deep-copied).
 * @param value Function definition AST node (will be cloned).
 * @return Error code indicating success or failure.
 */
func_store_error_t func_store_add(func_store_t *store, const string_t *name,
                                  const ast_node_t *value);

/**
 * Add or update a function definition using a C-string name.
 * Creates a deep copy (clone) of the AST node.
 * The caller retains ownership of all arguments.
 *
 * @param store The function store.
 * @param name Function name (must be valid POSIX identifier; deep-copied).
 * @param value Function definition AST node (will be cloned).
 * @return Error code indicating success or failure.
 */
func_store_error_t func_store_add_cstr(func_store_t *store, const char *name,
                                       const ast_node_t *value);

/**
 * Add or update a function definition with redirections and extended result.
 * Creates deep copies (clones) of the AST node and redirections.
 * The caller retains ownership of all arguments.
 *
 * @param store The function store.
 * @param name Function name (must be valid POSIX identifier; deep-copied).
 * @param value Function definition AST node (will be cloned).
 * @param redirections Redirections to apply on invocation (will be cloned; may be NULL).
 * @return Result indicating success/failure and whether function was new or replaced.
 */
func_store_insert_result_t func_store_add_ex(func_store_t *store, const string_t *name,
                                             const ast_node_t *value,
                                             const exec_redirections_t *redirections);

/**
 * Remove a function by name.
 *
 * @param store The function store.
 * @param name Function name.
 * @return Error code.
 */
func_store_error_t func_store_remove(func_store_t *store, const string_t *name);

/**
 * Remove a function by C-string name.
 *
 * @param store The function store.
 * @param name Function name.
 * @return Error code.
 */
func_store_error_t func_store_remove_cstr(func_store_t *store, const char *name);

/* ============================================================================
 * Queries (const outputs)
 *
 * All returned pointers refer to internal store data and are valid only until
 * the next mutating operation on the store. Do not free or modify them.
 * ============================================================================ */

/**
 * Check whether a function exists.
 *
 * @param store The function store.
 * @param name Function name.
 * @return true if the function exists.
 */
bool func_store_has_name(const func_store_t *store, const string_t *name);

/**
 * Check whether a function exists using a C-string name.
 *
 * @param store The function store.
 * @param name Function name.
 * @return true if the function exists.
 */
bool func_store_has_name_cstr(const func_store_t *store, const char *name);

/**
 * Get the function definition AST node.
 *
 * Returns a const pointer to the internal AST node. Valid only until the
 * next mutating operation on the store.
 *
 * @param store The function store.
 * @param name Function name.
 * @return Function definition AST node, or NULL if not found.
 */
const ast_node_t *func_store_get_def(const func_store_t *store, const string_t *name);

/**
 * Get the function definition AST node using a C-string name.
 *
 * @param store The function store.
 * @param name Function name.
 * @return Function definition AST node, or NULL if not found.
 */
const ast_node_t *func_store_get_def_cstr(const func_store_t *store, const char *name);

/**
 * Get function redirections.
 *
 * Returns a const pointer to the internal redirections. Valid only until the
 * next mutating operation on the store.
 *
 * @param store The function store.
 * @param name Function name.
 * @return Redirections associated with the function, or NULL if none or not found.
 */
const exec_redirections_t *func_store_get_redirections(const func_store_t *store,
                                                  const string_t *name);

/* ============================================================================
 * Iteration
 *
 * Callbacks receive const pointers to internal data. Do not modify or free.
 * ============================================================================ */

/**
 * Callback function for iterating over function definitions.
 *
 * @param name Function name (const -- do not modify or free).
 * @param func Function definition AST node (const -- do not modify or free).
 * @param user_data User-provided context pointer.
 */
typedef void (*func_store_foreach_fn)(const string_t *name, const ast_node_t *func,
                                      void *user_data);

/**
 * Iterate over all function definitions in the store.
 * The callback receives const pointers to internal data.
 *
 * @param store The function store.
 * @param callback Function to call for each entry.
 * @param user_data User-provided context pointer passed to callback.
 */
void func_store_foreach(const func_store_t *store, func_store_foreach_fn callback, void *user_data);

#endif /* FUNC_STORE_H */

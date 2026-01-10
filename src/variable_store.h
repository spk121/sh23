#ifndef VARIABLE_STORE_H
#define VARIABLE_STORE_H

#include "ast.h"
#include "logging.h"
#include "string_t.h"
#include "variable_map.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * Represents a shell variable store containing name/value pairs,
 * along with a cached environment array for execve().
 */
typedef struct variable_store_t
{
    /** Map of variable names to values and metadata. */
    variable_map_t *map;

    /**
     * Cached NULL‑terminated environment array.
     * Owned by the store and rebuilt on demand.
     */
    char **cached_envp;
} variable_store_t;

/**
 * Error codes returned by variable store operations.
 */
typedef enum var_store_error_t
{
    VAR_STORE_ERROR_NONE = 0,               /**< Operation succeeded. */
    VAR_STORE_ERROR_NOT_FOUND,              /**< Variable does not exist. */
    VAR_STORE_ERROR_READ_ONLY,              /**< Variable is read‑only. */
    VAR_STORE_ERROR_EMPTY_NAME,             /**< Variable name is empty. */
    VAR_STORE_ERROR_NAME_TOO_LONG,          /**< Variable name exceeds limits. */
    VAR_STORE_ERROR_NAME_STARTS_WITH_DIGIT, /**< Variable name begins with a digit. */
    VAR_STORE_ERROR_NAME_INVALID_CHARACTER, /**< Variable name contains invalid characters. */
    VAR_STORE_ERROR_VALUE_TOO_LONG          /**< Variable value exceeds limits. */
} var_store_error_t;

/**
 * Creates an empty variable store.
 *
 * @return Newly allocated variable store.
 */
variable_store_t *variable_store_create(void);

/**
 * Creates a variable store initialized from an envp array.
 *
 * @param envp NULL‑terminated environment array.
 * @return Newly allocated variable store.
 */
variable_store_t *variable_store_create_from_envp(char **envp);

/**
 * Destroys a variable store and frees all associated memory.
 *
 * @param store Pointer to the store pointer; set to NULL on return.
 */
void variable_store_destroy(variable_store_t **store);

/**
 * Removes all variables and parameters from the store.
 *
 * @param store Variable store to clear.
 */
void variable_store_clear(variable_store_t *store);

/**
 * Adds or updates a variable using string_t values.
 *
 * @param store Variable store.
 * @param name Variable name.
 * @param value Variable value.
 * @param exported Whether the variable should be exported.
 * @param read_only Whether the variable should be read‑only.
 * @return Error code indicating success or failure.
 */
var_store_error_t variable_store_add(variable_store_t *store, const string_t *name,
                                     const string_t *value, bool exported, bool read_only);

/**
 * Adds or updates a variable using C‑string values.
 *
 * @param store Variable store.
 * @param name Variable name.
 * @param value Variable value.
 * @param exported Whether the variable should be exported.
 * @param read_only Whether the variable should be read‑only.
 * @return Error code indicating success or failure.
 */
var_store_error_t variable_store_add_cstr(variable_store_t *store, const char *name,
                                          const char *value, bool exported, bool read_only);

/**
 * Adds a variable from a raw "NAME=VALUE" environment string.
 *
 * @param store Variable store.
 * @param env Environment string.
 */
void variable_store_add_env(variable_store_t *store, const char *env);

/**
 * Removes a variable by name.
 *
 * @param store Variable store.
 * @param name Variable name.
 */
void variable_store_remove(variable_store_t *store, const string_t *name);

/**
 * Removes a variable by C‑string name.
 *
 * @param store Variable store.
 * @param name Variable name.
 */
void variable_store_remove_cstr(variable_store_t *store, const char *name);

/**
 * Checks whether a variable exists.
 *
 * @param store Variable store.
 * @param name Variable name.
 * @return true if the variable exists.
 */
bool variable_store_has_name(const variable_store_t *store, const string_t *name);

/**
 * Checks whether a variable exists using a C‑string name.
 *
 * @param store Variable store.
 * @param name Variable name.
 * @return true if the variable exists.
 */
bool variable_store_has_name_cstr(const variable_store_t *store, const char *name);

/**
 * Retrieves a variable entry by name.
 *
 * @param store Variable store.
 * @param name Variable name.
 * @return Pointer to the variable entry or NULL.
 */
const variable_map_entry_t *variable_store_get_variable(const variable_store_t *store,
                                                        const string_t *name);

/**
 * Retrieves a variable entry by C‑string name.
 *
 * @param store Variable store.
 * @param name Variable name.
 * @return Pointer to the variable entry or NULL.
 */
const variable_map_entry_t *variable_store_get_variable_cstr(const variable_store_t *store,
                                                             const char *name);

/**
 * Retrieves a variable's value.
 *
 * @param store Variable store.
 * @param name Variable name.
 * @return Value string or NULL.
 */
const string_t *variable_store_get_value(const variable_store_t *store, const string_t *name);

/**
 * Retrieves a variable's value as a C‑string.
 *
 * @param store Variable store.
 * @param name Variable name.
 * @return Value string or NULL.
 */
const char *variable_store_get_value_cstr(const variable_store_t *store, const char *name);

/**
 * Checks whether a variable is read‑only.
 *
 * @param store Variable store.
 * @param name Variable name.
 * @return true if read‑only.
 */
bool variable_store_is_read_only(const variable_store_t *store, const string_t *name);

/**
 * Checks whether a variable is read‑only using a C‑string name.
 *
 * @param store Variable store.
 * @param name Variable name.
 * @return true if read‑only.
 */
bool variable_store_is_read_only_cstr(const variable_store_t *store, const char *name);

/**
 * Checks whether a variable is exported.
 *
 * @param store Variable store.
 * @param name Variable name.
 * @return true if exported.
 */
bool variable_store_is_exported(const variable_store_t *store, const string_t *name);

/**
 * Checks whether a variable is exported using a C‑string name.
 *
 * @param store Variable store.
 * @param name Variable name.
 * @return true if exported.
 */
bool variable_store_is_exported_cstr(const variable_store_t *store, const char *name);

/**
 * Sets or clears the read‑only flag on a variable.
 *
 * @param store Variable store.
 * @param name Variable name.
 * @param read_only New read‑only state.
 * @return Error code.
 */
var_store_error_t variable_store_set_read_only(variable_store_t *store, const string_t *name,
                                               bool read_only);

/**
 * Sets or clears the read‑only flag using a C‑string name.
 *
 * @param store Variable store.
 * @param name Variable name.
 * @param read_only New read‑only state.
 * @return Error code.
 */
var_store_error_t variable_store_set_read_only_cstr(variable_store_t *store, const char *name,
                                                    bool read_only);

/**
 * Sets or clears the exported flag on a variable.
 *
 * @param store Variable store.
 * @param name Variable name.
 * @param exported New exported state.
 * @return Error code.
 */
var_store_error_t variable_store_set_exported(variable_store_t *store, const string_t *name,
                                              bool exported);

/**
 * Sets or clears the exported flag using a C‑string name.
 *
 * @param store Variable store.
 * @param name Variable name.
 * @param exported New exported state.
 * @return Error code.
 */
var_store_error_t variable_store_set_exported_cstr(variable_store_t *store, const char *name,
                                                   bool exported);

/**
 * Returns the length of a variable's value.
 *
 * @param store Variable store.
 * @param name Variable name.
 * @return Length of the value or -1 if not found.
 */
int32_t variable_store_get_value_length(const variable_store_t *store, const string_t *name);

/**
 * Rebuilds and returns the environment array for execve().
 * The returned pointer remains valid until the next update call.
 *
 * @param vs Variable store.
 * @return NULL‑terminated environment array.
 */
char *const *variable_store_update_envp(variable_store_t *vs);

/**
 * Rebuilds the environment array, inheriting exported variables
 * from a parent store.
 *
 * @param vs Child variable store.
 * @param parent Parent variable store.
 * @return NULL‑terminated environment array.
 */
char *const *variable_store_update_envp_with_parent(variable_store_t *vs,
                                                    const variable_store_t *parent);

#endif

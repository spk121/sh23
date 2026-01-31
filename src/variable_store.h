#ifndef VARIABLE_STORE_H
#define VARIABLE_STORE_H

#include <stdbool.h>
#include <stdint.h>
#include "ast.h"
#include "logging.h"
#include "string_t.h"
#include "variable_map.h"

/**
 * Represents a shell variable store containing name/value pairs,
 * along with a cached environment array for execve().
 */
typedef struct variable_store_t
{
    /** Map of variable names to values and metadata. */
    variable_map_t *map;

    /** Increment on any modification */
    uint32_t generation;
    /** Cached environment array generation info. */
    uint32_t cached_generation;
    /** Parent generation info for cache validation. */
    uint32_t cached_parent_gen;
    /** Parent store that we built the cache against for cache validation. */
    const struct variable_store_t *cached_parent;
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

variable_store_t *variable_store_clone(const variable_store_t *src);

variable_store_t *variable_store_clone_exported(const variable_store_t *src);

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
 * Copies all variables from one store to another.
 *
 * Iterates through all variables in the source store and adds them to the
 * destination store, preserving their values, export status, and read-only
 * flags. This is a deep copy operation - values are cloned, not shared.
 *
 * This function is commonly used when creating child execution environments
 * or building temporary variable stores for command execution with prefix
 * assignments.
 *
 * @param dst Destination variable store (must not be NULL).
 * @param src Source variable store (must not be NULL).
 */
void variable_store_copy_all(variable_store_t *dst, const variable_store_t *src);

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

typedef void (*var_store_iter_fn)(const string_t *name, const string_t *value, bool exported,
                                  bool read_only, void *user_data);


/**
 * Iterates over all variables in the store, calling the provided function for each.
 * Does not modify the store.
 *
 * @param store Variable store.
 * @param fn Function to call for each variable.
 * @param user_data User data passed to the function.
 */
void variable_store_for_each(const variable_store_t *store, var_store_iter_fn fn, void *user_data);

/**
 * Actions that can be taken during variable store mapping.
 */
typedef enum
{
    VAR_STORE_MAP_ACTION_SKIP = 0, /**< Do not modify the variable. */
    VAR_STORE_MAP_ACTION_UPDATE,   /**< Update the variable with new values. */
    VAR_STORE_MAP_ACTION_REMOVE    /**< Remove the variable from the store. */
} var_store_map_action_t;

typedef var_store_map_action_t (*var_store_map_fn)(string_t *name, string_t *value, bool *exported,
                                                   bool *read_only, void *user_data);

/**
 * Iterates over all variables in the store, allowing modification via the provided function.
 * Stop iteration if the function returns an error.
 * N.B.: you cannot use this function to remove an entry from the store.
 *
 * @param store Variable store.
 * @param fn Function to call for each variable.
 * @param user_data User data passed to the function.
 * @param failed_name If non-NULL, set to the unmodified name of the variable that caused an error.
 */
var_store_error_t variable_store_map(variable_store_t *store, var_store_map_fn fn, void *user_data,
                                     string_t **failed_name);

    /**
 * Prints all exported variables to the log at DEBUG level.
 *
 * @param store Variable store.
 */
void variable_store_debug_print_exported(const variable_store_t *store);

/**
 * Returns the environment array for the variable store.
 * The returned pointer remains valid until the next update call.
 *
 * @param vs Variable store.
 * @return NULL‑terminated environment array.
 */
char *const *variable_store_get_envp(variable_store_t *vs);

/**
 * Write the environment variables from `vars` to the env file specified by the
 * MGSH_ENV_FILE variable, if set. This is used in ISO C mode where envp cannot
 * be passed to the child process via system() - instead, the environment is
 * written to a file that the child process can source.
 *
 * Note that the `vars` parameter is mutable because we may need to update its
 * internal state when generating the envp array.
 *
 * @param vars Variable store to export.
 * @return Path to the env file as a newly allocated string, or NULL on failure
 *         or if MGSH_ENV_FILE is not set.
 */
string_t *variable_store_write_env_file(variable_store_t *vars);

#endif

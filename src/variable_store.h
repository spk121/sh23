#ifndef VARIABLE_STORE_H
#define VARIABLE_STORE_H

#include <stdbool.h>
#include <stdint.h>
#include "string_t.h"
#include "logging.h"
#include "variable_map.h"
#include "ast.h"

typedef struct variable_store_t
{
    variable_map_t *map;
    char **cached_envp; // NULL-terminated array of malloc'd strings
} variable_store_t;

typedef enum var_store_error_t
{
    VAR_STORE_ERROR_NONE = 0,
    VAR_STORE_ERROR_NOT_FOUND,
    VAR_STORE_ERROR_READ_ONLY,
    VAR_STORE_ERROR_EMPTY_NAME,
    VAR_STORE_ERROR_NAME_TOO_LONG,
    VAR_STORE_ERROR_NAME_STARTS_WITH_DIGIT,
    VAR_STORE_ERROR_NAME_INVALID_CHARACTER,
    VAR_STORE_ERROR_VALUE_TOO_LONG,
} var_store_error_t;

// Constructors
variable_store_t *variable_store_create(void);
variable_store_t *variable_store_create_from_envp(char **envp);

// Destructor
void variable_store_destroy(variable_store_t **store);

// Clear all variables and parameters
void variable_store_clear(variable_store_t *store);

// Variable management
var_store_error_t variable_store_add(variable_store_t *store, const string_t *name, const string_t *value, bool exported, bool read_only);
var_store_error_t variable_store_add_cstr(variable_store_t *store, const char *name, const char *value, bool exported, bool read_only);
void variable_store_add_env(variable_store_t *store, const char *env);
void variable_store_remove(variable_store_t *store, const string_t *name);
void variable_store_remove_cstr(variable_store_t *store, const char *name);
bool variable_store_has_name(const variable_store_t *store, const string_t *name);
bool variable_store_has_name_cstr(const variable_store_t *store, const char *name);
const variable_map_entry_t *variable_store_get_variable(const variable_store_t *store,
                                                        const string_t *name);
const variable_map_entry_t *variable_store_get_variable_cstr(const variable_store_t *store,
                                                             const char *name);
const string_t *variable_store_get_value(const variable_store_t *store, const string_t *name);
const char *variable_store_get_value_cstr(const variable_store_t *store, const char *name);
bool variable_store_is_read_only(const variable_store_t *store, const string_t *name);
bool variable_store_is_read_only_cstr(const variable_store_t *store, const char *name);
bool variable_store_is_exported(const variable_store_t *store, const string_t *name);
bool variable_store_is_exported_cstr(const variable_store_t *store, const char *name);
var_store_error_t variable_store_set_read_only(variable_store_t *store, const string_t *name, bool read_only);
var_store_error_t variable_store_set_read_only_cstr(variable_store_t *store, const char *name,
                                                    bool read_only);
var_store_error_t variable_store_set_exported(variable_store_t *store, const string_t *name,
                                              bool exported);
var_store_error_t variable_store_set_exported_cstr(variable_store_t *store, const char *name,
                                                   bool exported);

// Value helper wrappers
int32_t variable_store_get_value_length(const variable_store_t *store, const string_t *name);

// Environment array management
/**
 * Rebuild and return the envp array for execve().
 * The returned pointer is owned by the variable store and remains valid
 * until the next call to variable_store_update_envp().
 */

char *const *variable_store_update_envp(variable_store_t *vs);
char *const *variable_store_update_envp_with_parent(variable_store_t *vs,
                                                    const variable_store_t *parent);

#endif

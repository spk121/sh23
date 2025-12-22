#ifndef VARIABLE_STORE_H
#define VARIABLE_STORE_H

#include <stdbool.h>
#include "string_t.h"
#include "logging.h"
#include "variable.h"
#include "variable_array.h"

typedef struct variable_store_t {
    variable_array_t *variables;
    char **cached_envp; // NULL-terminated array of malloc'd strings
} variable_store_t;

// Constructors
variable_store_t *variable_store_create(void);
variable_store_t *variable_store_create_from_envp(char **envp);

// Destructor
void variable_store_destroy(variable_store_t **store);

// Clear all variables and parameters
int variable_store_clear(variable_store_t *store);

// Variable management
void variable_store_add(variable_store_t *store, const string_t *name, const string_t *value, bool exported, bool read_only);
void variable_store_add_cstr(variable_store_t *store, const char *name, const char *value, bool exported, bool read_only);
void variable_store_remove(variable_store_t *store, const string_t *name);
void variable_store_remove_cstr(variable_store_t *store, const char *name);
int variable_store_has_name(const variable_store_t *store, const string_t *name);
int variable_store_has_name_cstr(const variable_store_t *store, const char *name);
const variable_t *variable_store_get_variable(const variable_store_t *store, const string_t *name);
const variable_t *variable_store_get_variable_cstr(const variable_store_t *store, const char *name);
const string_t *variable_store_get_value(const variable_store_t *store, const string_t *name);
const char *variable_store_get_value_cstr(const variable_store_t *store, const char *name);
int variable_store_is_read_only(const variable_store_t *store, const string_t *name);
int variable_store_is_read_only_cstr(const variable_store_t *store, const char *name);
int variable_store_is_exported(const variable_store_t *store, const string_t *name);
int variable_store_is_exported_cstr(const variable_store_t *store, const char *name);
int variable_store_set_read_only(variable_store_t *store, const string_t *name, bool read_only);
int variable_store_set_read_only_cstr(variable_store_t *store, const char *name, bool read_only);
int variable_store_set_exported(variable_store_t *store, const string_t *name, bool exported);
int variable_store_set_exported_cstr(variable_store_t *store, const char *name, bool exported);

// Value helper wrappers
int variable_store_get_variable_length(const variable_store_t *store, const string_t *name);
string_t *variable_store_get_value_removing_smallest_suffix(const variable_store_t *store, const string_t *name, const string_t *pattern);
string_t *variable_store_get_value_removing_largest_suffix(const variable_store_t *store, const string_t *name, const string_t *pattern);
string_t *variable_store_get_value_removing_smallest_prefix(const variable_store_t *store, const string_t *name, const string_t *pattern);
string_t *variable_store_get_value_removing_largest_prefix(const variable_store_t *store, const string_t *name, const string_t *pattern);

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

#ifndef VARIABLE_STORE_H
#define VARIABLE_STORE_H

#include <stdbool.h>
#include "string_t.h"
#include "logging.h"
#include "variable.h"
#include "variable_array.h"

typedef struct variable_store_t {
    variable_array_t *variables;
    variable_array_t *positional_params;
    string_t *status_str;
    long pid;
    string_t *shell_name;
    long last_bg_pid;
    string_t *options;
} variable_store_t;

// Constructors
variable_store_t *variable_store_create(const char *shell_name);
variable_store_t *variable_store_create_from_envp(const char *shell_name, char **envp);

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

// Positional parameters
void variable_store_set_positional_params(variable_store_t *store, const string_t *params[], size_t count);
void variable_store_set_positional_params_cstr(variable_store_t *store, const char *params[], size_t count);
const variable_t *variable_store_get_positional_param(const variable_store_t *store, size_t index);
const char *variable_store_get_positional_param_cstr(const variable_store_t *store, size_t index);
size_t variable_store_positional_param_count(const variable_store_t *store);

// Special parameter getters
const string_t *variable_store_get_status(const variable_store_t *store);
long variable_store_get_pid(const variable_store_t *store);
const string_t *variable_store_get_shell_name(const variable_store_t *store);
long variable_store_get_last_bg_pid(const variable_store_t *store);
const string_t *variable_store_get_options(const variable_store_t *store);
const char *variable_store_get_status_cstr(const variable_store_t *store);
const char *variable_store_get_shell_name_cstr(const variable_store_t *store);
const char *variable_store_get_options_cstr(const variable_store_t *store);

// Special parameter setters
void variable_store_set_status(variable_store_t *store, const string_t *status);
void variable_store_set_status_cstr(variable_store_t *store, const char *status);
void variable_store_set_shell_name(variable_store_t *store, const string_t *shell_name);
void variable_store_set_shell_name_cstr(variable_store_t *store, const char *shell_name);
void variable_store_set_last_bg_pid(variable_store_t *store, long last_bg_pid);
void variable_store_set_options(variable_store_t *store, const string_t *options);
void variable_store_set_options_cstr(variable_store_t *store, const char *options);

#endif

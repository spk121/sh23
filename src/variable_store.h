#ifndef VARIABLE_STORE_H
#define VARIABLE_STORE_H

#include <stdbool.h>
#include "string.h"
#include "logging.h"
#include "variable.h"

typedef struct VariableStore VariableStore;

// Constructors
VariableStore *variable_store_create(const char *shell_name);
VariableStore *variable_store_create_from_envp(const char *shell_name, char **envp);

// Destructor
void variable_store_destroy(VariableStore *store);

// Clear all variables and parameters
int variable_store_clear(VariableStore *store);

// Variable management
int variable_store_add(VariableStore *store, const String *name, const String *value, bool exported, bool read_only);
int variable_store_add_cstr(VariableStore *store, const char *name, const char *value, bool exported, bool read_only);
int variable_store_remove(VariableStore *store, const String *name);
int variable_store_remove_cstr(VariableStore *store, const char *name);
int variable_store_has_name(const VariableStore *store, const String *name);
int variable_store_has_name_cstr(const VariableStore *store, const char *name);
const Variable *variable_store_get_variable(const VariableStore *store, const String *name);
const Variable *variable_store_get_variable_cstr(const VariableStore *store, const char *name);
const String *variable_store_get_value(const VariableStore *store, const String *name);
const char *variable_store_get_value_cstr(const VariableStore *store, const char *name);
int variable_store_is_read_only(const VariableStore *store, const String *name);
int variable_store_is_read_only_cstr(const VariableStore *store, const char *name);
int variable_store_is_exported(const VariableStore *store, const String *name);
int variable_store_is_exported_cstr(const VariableStore *store, const char *name);
int variable_store_set_read_only(VariableStore *store, const String *name, bool read_only);
int variable_store_set_read_only_cstr(VariableStore *store, const char *name, bool read_only);
int variable_store_set_exported(VariableStore *store, const String *name, bool exported);
int variable_store_set_exported_cstr(VariableStore *store, const char *name, bool exported);

// Positional parameters
int variable_store_set_positional_params(VariableStore *store, const String *params[], size_t count);
int variable_store_set_positional_params_cstr(VariableStore *store, const char *params[], size_t count);
const Variable *variable_store_get_positional_param(const VariableStore *store, size_t index);
const char *variable_store_get_positional_param_cstr(const VariableStore *store, size_t index);
size_t variable_store_positional_param_count(const VariableStore *store);

// Special parameter getters
const String *variable_store_get_status(const VariableStore *store);
long variable_store_get_pid(const VariableStore *store);
const String *variable_store_get_shell_name(const VariableStore *store);
long variable_store_get_last_bg_pid(const VariableStore *store);
const String *variable_store_get_options(const VariableStore *store);
const char *variable_store_get_status_cstr(const VariableStore *store);
const char *variable_store_get_shell_name_cstr(const VariableStore *store);
const char *variable_store_get_options_cstr(const VariableStore *store);

// Special parameter setters
int variable_store_set_status(VariableStore *store, const String *status);
int variable_store_set_status_cstr(VariableStore *store, const char *status);
int variable_store_set_shell_name(VariableStore *store, const String *shell_name);
int variable_store_set_shell_name_cstr(VariableStore *store, const char *shell_name);
int variable_store_set_last_bg_pid(VariableStore *store, long last_bg_pid);
int variable_store_set_options(VariableStore *store, const String *options);
int variable_store_set_options_cstr(VariableStore *store, const char *options);

#endif

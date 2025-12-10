#ifndef VARIABLES_H
#define VARIABLES_H

#include <stdbool.h>
#include "string.h"
#include "logging.h"
#include "variable_array.h"

typedef struct {
    string_t *name;
    string_t *value;
    bool exported; // true if exported, false otherwise
    bool read_only; // true if value cannot be, false if value can be modified
} Variable;

typedef struct {
    VariableArray *variables;      // Regular variables
    VariableArray *positional_params; // $1, $2, ...
    string_t *status_str;       // $?
    long pid;                 // $$ - really a pid_t
    string_t *shell_name;       // $0
    long last_bg_pid;         // $! - really a pid_t
    string_t *options;          // $-
} VariableStore;

VariableStore *variable_store_create(const char *shell_name);
void variable_store_destroy(VariableStore *self);
void variable_store_set_variable(VariableStore *self, const char *name, const char *value);
void variable_store_export_variable(VariableStore *self, const char *name);
void variable_store_unset_variable(VariableStore *self, const char *name);
const char *variable_store_get_variable(VariableStore *self, const char *name);
void variable_store_make_readonly(VariableStore *self, const char *name);
void variable_store_dump_variables(VariableStore *self);
void variable_store_set_status(VariableStore *self, int status);
void variable_store_set_positional_params(VariableStore *self, int argc, char **argv);
void variable_store_set_background_pid(VariableStore *self, long pid);
void variable_store_set_options(VariableStore *self, const char *opts);

// New parameter expansion functions
const char *variable_store_default_value(VariableStore *self, const char *name, const char *word);
const char *variable_store_assign_default(VariableStore *self, const char *name, const char *word);
const char *variable_store_indicate_error(VariableStore *self, const char *name, const char *word);
const char *variable_store_alternative_value(VariableStore *self, const char *name, const char *word);
size_t variable_store_length(VariableStore *self, const char *name);
const char *variable_store_remove_suffix(VariableStore *self, const char *name, const char *pattern, bool longest);
const char *variable_store_remove_prefix(VariableStore *self, const char *name, const char *pattern, bool longest);

#endif

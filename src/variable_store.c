#include "variable_store.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

struct VariableStore {
    VariableArray *variables;
    VariableArray *positional_params;
    String *status_str;
    long pid;
    String *shell_name;
    long last_bg_pid;
    String *options;
};

// Comparison function for finding Variable by name
static int compare_variable_name(const Variable *variable, const void *name)
{
    return string_compare(variable_get_name(variable), (const String *)name);
}

static int compare_variable_name_cstr(const Variable *variable, const void *name)
{
    return string_compare_cstr(variable_get_name(variable), (const char *)name);
}

// Constructors
VariableStore *variable_store_create(const char *shell_name)
{
    return_val_if_null(shell_name, NULL);

    VariableStore *store = malloc(sizeof(VariableStore));
    if (!store) {
        log_fatal("variable_store_create: out of memory");
        return NULL;
    }

    store->variables = variable_array_create_with_free((VariableArrayFreeFunc)variable_destroy);
    if (!store->variables) {
        free(store);
        log_fatal("variable_store_create: failed to create variables array");
        return NULL;
    }

    store->positional_params = variable_array_create_with_free((VariableArrayFreeFunc)variable_destroy);
    if (!store->positional_params) {
        variable_array_destroy(store->variables);
        free(store);
        log_fatal("variable_store_create: failed to create positional_params array");
        return NULL;
    }

    store->status_str = string_create_from_cstr("0");
    if (!store->status_str) {
        variable_array_destroy(store->positional_params);
        variable_array_destroy(store->variables);
        free(store);
        log_fatal("variable_store_create: failed to create status_str");
        return NULL;
    }

    store->pid = (long)getpid();
    store->shell_name = string_create_from_cstr(shell_name);
    if (!store->shell_name) {
        string_destroy(store->status_str);
        variable_array_destroy(store->positional_params);
        variable_array_destroy(store->variables);
        free(store);
        log_fatal("variable_store_create: failed to create shell_name");
        return NULL;
    }

    store->last_bg_pid = 0;
    store->options = string_create_from_cstr("");
    if (!store->options) {
        string_destroy(store->shell_name);
        string_destroy(store->status_str);
        variable_array_destroy(store->positional_params);
        variable_array_destroy(store->variables);
        free(store);
        log_fatal("variable_store_create: failed to create options");
        return NULL;
    }

    return store;
}

VariableStore *variable_store_create_from_envp(const char *shell_name, char **envp)
{
    VariableStore *store = variable_store_create(shell_name);
    if (!store) {
        return NULL;
    }

    if (envp) {
        for (char **env = envp; *env; env++) {
            char *name = *env;
            char *eq = strchr(name, '=');
            if (!eq) {
                continue;
            }
            *eq = '\0';
            char *value = eq + 1;
            if (variable_store_add_cstr(store, name, value, true, false) != 0) {
                log_fatal("variable_store_create_from_envp: failed to add env %s", name);
                variable_store_destroy(store);
                return NULL;
            }
            *eq = '='; // Restore for safety
        }
    }

    return store;
}

// Destructor
void variable_store_destroy(VariableStore *store)
{
    if (store) {
        log_debug("variable_store_destroy: freeing store %p, variables %zu, params %zu",
                  store,
                  variable_array_size(store->variables),
                  variable_array_size(store->positional_params));
        string_destroy(store->options);
        string_destroy(store->shell_name);
        string_destroy(store->status_str);
        variable_array_destroy(store->positional_params);
        variable_array_destroy(store->variables);
        free(store);
    }
}

// Clear all variables and parameters
int variable_store_clear(VariableStore *store)
{
    return_val_if_null(store, -1);

    log_debug("variable_store_clear: clearing store %p, variables %zu, params %zu",
              store,
              variable_array_size(store->variables),
              variable_array_size(store->positional_params));

    if (variable_array_clear(store->variables) != 0) {
        log_fatal("variable_store_clear: failed to clear variables");
        return -1;
    }
    if (variable_array_clear(store->positional_params) != 0) {
        log_fatal("variable_store_clear: failed to clear positional_params");
        return -1;
    }

    String *new_status = string_create_from_cstr("0");
    if (!new_status) {
        log_fatal("variable_store_clear: failed to create status_str");
        return -1;
    }
    string_destroy(store->status_str);
    store->status_str = new_status;

    store->last_bg_pid = 0;

    String *new_options = string_create_from_cstr("");
    if (!new_options) {
        log_fatal("variable_store_clear: failed to create options");
        return -1;
    }
    string_destroy(store->options);
    store->options = new_options;

    return 0;
}

// Variable management
int variable_store_add(VariableStore *store, const String *name, const String *value, bool exported, bool read_only)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);
    return_val_if_null(value, -1);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name, &index) == 0) {
        // Replace existing variable
        Variable *new_var = variable_create(name, value, exported, read_only);
        if (!new_var) {
            log_fatal("variable_store_add: failed to create variable");
            return -1;
        }
        return variable_array_set(store->variables, index, new_var);
    }

    // Add new variable
    Variable *var = variable_create(name, value, exported, read_only);
    if (!var) {
        log_fatal("variable_store_add: failed to create variable");
        return -1;
    }

    if (variable_array_append(store->variables, var) != 0) {
        variable_destroy(var);
        log_fatal("variable_store_add: failed to append variable");
        return -1;
    }

    return 0;
}

int variable_store_add_cstr(VariableStore *store, const char *name, const char *value, bool exported, bool read_only)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);
    return_val_if_null(value, -1);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name_cstr, &index) == 0) {
        // Replace existing variable
        Variable *new_var = variable_create_from_cstr(name, value, exported, read_only);
        if (!new_var) {
            log_fatal("variable_store_add_cstr: failed to create variable");
            return -1;
        }
        return variable_array_set(store->variables, index, new_var);
    }

    // Add new variable
    Variable *var = variable_create_from_cstr(name, value, exported, read_only);
    if (!var) {
        log_fatal("variable_store_add_cstr: failed to create variable");
        return -1;
    }

    if (variable_array_append(store->variables, var) != 0) {
        variable_destroy(var);
        log_fatal("variable_store_add_cstr: failed to append variable");
        return -1;
    }

    return 0;
}

int variable_store_remove(VariableStore *store, const String *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name, &index) != 0) {
        return -1; // Name not found
    }

    return variable_array_remove(store->variables, index);
}

int variable_store_remove_cstr(VariableStore *store, const char *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name_cstr, &index) != 0) {
        return -1; // Name not found
    }

    return variable_array_remove(store->variables, index);
}

int variable_store_has_name(const VariableStore *store, const String *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    return variable_array_find_with_compare(store->variables, name, compare_variable_name, &index) == 0 ? 1 : 0;
}

int variable_store_has_name_cstr(const VariableStore *store, const char *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    return variable_array_find_with_compare(store->variables, name, compare_variable_name_cstr, &index) == 0 ? 1 : 0;
}

const Variable *variable_store_get_variable(const VariableStore *store, const String *name)
{
    return_val_if_null(store, NULL);
    return_val_if_null(name, NULL);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name, &index) != 0) {
        return NULL; // Name not found
    }

    return variable_array_get(store->variables, index);
}

const Variable *variable_store_get_variable_cstr(const VariableStore *store, const char *name)
{
    return_val_if_null(store, NULL);
    return_val_if_null(name, NULL);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name_cstr, &index) != 0) {
        return NULL; // Name not found
    }

    return variable_array_get(store->variables, index);
}

const String *variable_store_get_value(const VariableStore *store, const String *name)
{
    const Variable *var = variable_store_get_variable(store, name);
    return var ? variable_get_value(var) : NULL;
}

const char *variable_store_get_value_cstr(const VariableStore *store, const char *name)
{
    const Variable *var = variable_store_get_variable_cstr(store, name);
    return var ? variable_get_value_cstr(var) : NULL;
}

int variable_store_is_read_only(const VariableStore *store, const String *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    const Variable *var = variable_store_get_variable(store, name);
    if (!var) {
        return -1; // Name not found
    }

    return variable_is_read_only(var) ? 1 : 0;
}

int variable_store_is_read_only_cstr(const VariableStore *store, const char *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    const Variable *var = variable_store_get_variable_cstr(store, name);
    if (!var) {
        return -1; // Name not found
    }

    return variable_is_read_only(var) ? 1 : 0;
}

int variable_store_is_exported(const VariableStore *store, const String *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    const Variable *var = variable_store_get_variable(store, name);
    if (!var) {
        return -1; // Name not found
    }

    return variable_is_exported(var) ? 1 : 0;
}

int variable_store_is_exported_cstr(const VariableStore *store, const char *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    const Variable *var = variable_store_get_variable_cstr(store, name);
    if (!var) {
        return -1; // Name not found
    }

    return variable_is_exported(var) ? 1 : 0;
}

int variable_store_set_read_only(VariableStore *store, const String *name, bool read_only)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name, &index) != 0) {
        log_fatal("variable_store_set_read_only: variable %s not found", string_data(name));
        return -1; // Name not found
    }

    Variable *var = (Variable *)variable_array_get(store->variables, index);
    return variable_set_read_only(var, read_only);
}

int variable_store_set_read_only_cstr(VariableStore *store, const char *name, bool read_only)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name_cstr, &index) != 0) {
        log_fatal("variable_store_set_read_only_cstr: variable %s not found", name);
        return -1; // Name not found
    }

    Variable *var = (Variable *)variable_array_get(store->variables, index);
    return variable_set_read_only(var, read_only);
}

int variable_store_set_exported(VariableStore *store, const String *name, bool exported)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name, &index) != 0) {
        log_fatal("variable_store_set_exported: variable %s not found", string_data(name));
        return -1; // Name not found
    }

    Variable *var = (Variable *)variable_array_get(store->variables, index);
    return variable_set_exported(var, exported);
}

int variable_store_set_exported_cstr(VariableStore *store, const char *name, bool exported)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name_cstr, &index) != 0) {
        log_fatal("variable_store_set_exported_cstr: variable %s not found", name);
        return -1; // Name not found
    }

    Variable *var = (Variable *)variable_array_get(store->variables, index);
    return variable_set_exported(var, exported);
}

// Positional parameters
int variable_store_set_positional_params(VariableStore *store, const String *params[], size_t count)
{
    return_val_if_null(store, -1);

    if (variable_array_clear(store->positional_params) != 0) {
        log_fatal("variable_store_set_positional_params: failed to clear positional_params");
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        if (!params[i]) {
            log_fatal("variable_store_set_positional_params: null parameter at index %zu", i);
            return -1;
        }
        char index_str[32];
        snprintf(index_str, sizeof(index_str), "%zu", i + 1);
        Variable *var = variable_create_from_cstr(index_str, string_data(params[i]), false, false);
        if (!var) {
            log_fatal("variable_store_set_positional_params: failed to create variable for index %zu", i + 1);
            return -1;
        }
        if (variable_array_append(store->positional_params, var) != 0) {
            variable_destroy(var);
            log_fatal("variable_store_set_positional_params: failed to append variable at index %zu", i + 1);
            return -1;
        }
    }

    return 0;
}

int variable_store_set_positional_params_cstr(VariableStore *store, const char *params[], size_t count)
{
    return_val_if_null(store, -1);

    if (variable_array_clear(store->positional_params) != 0) {
        log_fatal("variable_store_set_positional_params_cstr: failed to clear positional_params");
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        if (!params[i]) {
            log_fatal("variable_store_set_positional_params_cstr: null parameter at index %zu", i);
            return -1;
        }
        char index_str[32];
        snprintf(index_str, sizeof(index_str), "%zu", i + 1);
        Variable *var = variable_create_from_cstr(index_str, params[i], false, false);
        if (!var) {
            log_fatal("variable_store_set_positional_params_cstr: failed to create variable for index %zu", i + 1);
            return -1;
        }
        if (variable_array_append(store->positional_params, var) != 0) {
            variable_destroy(var);
            log_fatal("variable_store_set_positional_params_cstr: failed to append variable at index %zu", i + 1);
            return -1;
        }
    }

    return 0;
}

const Variable *variable_store_get_positional_param(const VariableStore *store, size_t index)
{
    return_val_if_null(store, NULL);
    return variable_array_get(store->positional_params, index);
}

const char *variable_store_get_positional_param_cstr(const VariableStore *store, size_t index)
{
    const Variable *var = variable_store_get_positional_param(store, index);
    return var ? variable_get_value_cstr(var) : NULL;
}

size_t variable_store_positional_param_count(const VariableStore *store)
{
    return_val_if_null(store, 0);
    return variable_array_size(store->positional_params);
}

// Special parameter getters
const String *variable_store_get_status(const VariableStore *store)
{
    return_val_if_null(store, NULL);
    return store->status_str;
}

long variable_store_get_pid(const VariableStore *store)
{
    return_val_if_null(store, 0);
    return store->pid;
}

const String *variable_store_get_shell_name(const VariableStore *store)
{
    return_val_if_null(store, NULL);
    return store->shell_name;
}

long variable_store_get_last_bg_pid(const VariableStore *store)
{
    return_val_if_null(store, 0);
    return store->last_bg_pid;
}

const String *variable_store_get_options(const VariableStore *store)
{
    return_val_if_null(store, NULL);
    return store->options;
}

const char *variable_store_get_status_cstr(const VariableStore *store)
{
    return_val_if_null(store, NULL);
    return string_data(store->status_str);
}

const char *variable_store_get_shell_name_cstr(const VariableStore *store)
{
    return_val_if_null(store, NULL);
    return string_data(store->shell_name);
}

const char *variable_store_get_options_cstr(const VariableStore *store)
{
    return_val_if_null(store, NULL);
    return string_data(store->options);
}

// Special parameter setters
int variable_store_set_status(VariableStore *store, const String *status)
{
    return_val_if_null(store, -1);
    return_val_if_null(status, -1);

    String *new_status = string_create_from((String *)status);
    if (!new_status) {
        log_fatal("variable_store_set_status: failed to create status");
        return -1;
    }

    string_destroy(store->status_str);
    store->status_str = new_status;
    return 0;
}

int variable_store_set_status_cstr(VariableStore *store, const char *status)
{
    return_val_if_null(store, -1);
    return_val_if_null(status, -1);

    String *new_status = string_create_from_cstr(status);
    if (!new_status) {
        log_fatal("variable_store_set_status_cstr: failed to create status");
        return -1;
    }

    string_destroy(store->status_str);
    store->status_str = new_status;
    return 0;
}

int variable_store_set_shell_name(VariableStore *store, const String *shell_name)
{
    return_val_if_null(store, -1);
    return_val_if_null(shell_name, -1);

    String *new_shell_name = string_create_from((String *)shell_name);
    if (!new_shell_name) {
        log_fatal("variable_store_set_shell_name: failed to create shell_name");
        return -1;
    }

    string_destroy(store->shell_name);
    store->shell_name = new_shell_name;
    return 0;
}

int variable_store_set_shell_name_cstr(VariableStore *store, const char *shell_name)
{
    return_val_if_null(store, -1);
    return_val_if_null(shell_name, -1);

    String *new_shell_name = string_create_from_cstr(shell_name);
    if (!new_shell_name) {
        log_fatal("variable_store_set_shell_name_cstr: failed to create shell_name");
        return -1;
    }

    string_destroy(store->shell_name);
    store->shell_name = new_shell_name;
    return 0;
}

int variable_store_set_last_bg_pid(VariableStore *store, long last_bg_pid)
{
    return_val_if_null(store, -1);
    store->last_bg_pid = last_bg_pid;
    return 0;
}

int variable_store_set_options(VariableStore *store, const String *options)
{
    return_val_if_null(store, -1);
    return_val_if_null(options, -1);

    String *new_options = string_create_from((String *)options);
    if (!new_options) {
        log_fatal("variable_store_set_options: failed to create options");
        return -1;
    }

    string_destroy(store->options);
    store->options = new_options;
    return 0;
}

int variable_store_set_options_cstr(VariableStore *store, const char *options)
{
    return_val_if_null(store, -1);
    return_val_if_null(options, -1);

    String *new_options = string_create_from_cstr(options);
    if (!new_options) {
        log_fatal("variable_store_set_options_cstr: failed to create options");
        return -1;
    }

    string_destroy(store->options);
    store->options = new_options;
    return 0;
}

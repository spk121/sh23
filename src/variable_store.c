#include "variable_store.h"
#include "xalloc.h"
#include <unistd.h>
#include <string.h>

// Comparison function for finding variable_t by name
static int compare_variable_name(const variable_t *variable, const void *name)
{
    return string_compare(variable_get_name(variable), (const string_t *)name);
}

static int compare_variable_name_cstr(const variable_t *variable, const void *name)
{
    return string_compare_cstr(variable_get_name(variable), (const char *)name);
}

// Constructors
variable_store_t *variable_store_create(const char *shell_name)
{
    Expects_not_null(shell_name);

    variable_store_t *store = xmalloc(sizeof(variable_store_t));

    store->variables = variable_array_create_with_free((variable_array_free_func_t)variable_destroy);
    store->positional_params = variable_array_create_with_free((variable_array_free_func_t)variable_destroy);
    store->status_str = string_create_from_cstr("0");
    store->pid = (long)getpid();
    store->shell_name = string_create_from_cstr(shell_name);
    store->last_bg_pid = 0;
    store->options = string_create_from_cstr("");

    return store;
}

variable_store_t *variable_store_create_from_envp(const char *shell_name, char **envp)
{
    variable_store_t *store = variable_store_create(shell_name);

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
void variable_store_destroy(variable_store_t *store)
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
        xfree(store);
    }
}

// Clear all variables and parameters
int variable_store_clear(variable_store_t *store)
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

    string_t *new_status = string_create_from_cstr("0");
    string_destroy(store->status_str);
    store->status_str = new_status;

    store->last_bg_pid = 0;

    string_t *new_options = string_create_from_cstr("");
    string_destroy(store->options);
    store->options = new_options;

    return 0;
}

// variable_t management
int variable_store_add(variable_store_t *store, const string_t *name, const string_t *value, bool exported, bool read_only)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);
    return_val_if_null(value, -1);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name, &index) == 0) {
        // Replace existing variable
        variable_t *new_var = variable_create(name, value, exported, read_only);
        return variable_array_set(store->variables, index, new_var);
    }

    // Add new variable
    variable_t *var = variable_create(name, value, exported, read_only);

    if (variable_array_append(store->variables, var) != 0) {
        variable_destroy(var);
        log_fatal("variable_store_add: failed to append variable");
        return -1;
    }

    return 0;
}

int variable_store_add_cstr(variable_store_t *store, const char *name, const char *value, bool exported, bool read_only)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);
    return_val_if_null(value, -1);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name_cstr, &index) == 0) {
        // Replace existing variable
        variable_t *new_var = variable_create_from_cstr(name, value, exported, read_only);
        return variable_array_set(store->variables, index, new_var);
    }

    // Add new variable
    variable_t *var = variable_create_from_cstr(name, value, exported, read_only);

    if (variable_array_append(store->variables, var) != 0) {
        variable_destroy(var);
        log_fatal("variable_store_add_cstr: failed to append variable");
        return -1;
    }

    return 0;
}

int variable_store_remove(variable_store_t *store, const string_t *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name, &index) != 0) {
        return -1; // Name not found
    }

    return variable_array_remove(store->variables, index);
}

int variable_store_remove_cstr(variable_store_t *store, const char *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name_cstr, &index) != 0) {
        return -1; // Name not found
    }

    return variable_array_remove(store->variables, index);
}

int variable_store_has_name(const variable_store_t *store, const string_t *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    return variable_array_find_with_compare(store->variables, name, compare_variable_name, &index) == 0 ? 1 : 0;
}

int variable_store_has_name_cstr(const variable_store_t *store, const char *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    return variable_array_find_with_compare(store->variables, name, compare_variable_name_cstr, &index) == 0 ? 1 : 0;
}

const variable_t *variable_store_get_variable(const variable_store_t *store, const string_t *name)
{
    return_val_if_null(store, NULL);
    return_val_if_null(name, NULL);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name, &index) != 0) {
        return NULL; // Name not found
    }

    return variable_array_get(store->variables, index);
}

const variable_t *variable_store_get_variable_cstr(const variable_store_t *store, const char *name)
{
    return_val_if_null(store, NULL);
    return_val_if_null(name, NULL);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name_cstr, &index) != 0) {
        return NULL; // Name not found
    }

    return variable_array_get(store->variables, index);
}

const string_t *variable_store_get_value(const variable_store_t *store, const string_t *name)
{
    const variable_t *var = variable_store_get_variable(store, name);
    return var ? variable_get_value(var) : NULL;
}

const char *variable_store_get_value_cstr(const variable_store_t *store, const char *name)
{
    const variable_t *var = variable_store_get_variable_cstr(store, name);
    return var ? variable_get_value_cstr(var) : NULL;
}

int variable_store_is_read_only(const variable_store_t *store, const string_t *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    const variable_t *var = variable_store_get_variable(store, name);
    if (!var) {
        return -1; // Name not found
    }

    return variable_is_read_only(var) ? 1 : 0;
}

int variable_store_is_read_only_cstr(const variable_store_t *store, const char *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    const variable_t *var = variable_store_get_variable_cstr(store, name);
    if (!var) {
        return -1; // Name not found
    }

    return variable_is_read_only(var) ? 1 : 0;
}

int variable_store_is_exported(const variable_store_t *store, const string_t *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    const variable_t *var = variable_store_get_variable(store, name);
    if (!var) {
        return -1; // Name not found
    }

    return variable_is_exported(var) ? 1 : 0;
}

int variable_store_is_exported_cstr(const variable_store_t *store, const char *name)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    const variable_t *var = variable_store_get_variable_cstr(store, name);
    if (!var) {
        return -1; // Name not found
    }

    return variable_is_exported(var) ? 1 : 0;
}

int variable_store_set_read_only(variable_store_t *store, const string_t *name, bool read_only)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name, &index) != 0) {
        log_fatal("variable_store_set_read_only: variable %s not found", string_data(name));
        return -1; // Name not found
    }

    variable_t *var = (variable_t *)variable_array_get(store->variables, index);
    return variable_set_read_only(var, read_only);
}

int variable_store_set_read_only_cstr(variable_store_t *store, const char *name, bool read_only)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name_cstr, &index) != 0) {
        log_fatal("variable_store_set_read_only_cstr: variable %s not found", name);
        return -1; // Name not found
    }

    variable_t *var = (variable_t *)variable_array_get(store->variables, index);
    return variable_set_read_only(var, read_only);
}

int variable_store_set_exported(variable_store_t *store, const string_t *name, bool exported)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name, &index) != 0) {
        log_fatal("variable_store_set_exported: variable %s not found", string_data(name));
        return -1; // Name not found
    }

    variable_t *var = (variable_t *)variable_array_get(store->variables, index);
    return variable_set_exported(var, exported);
}

int variable_store_set_exported_cstr(variable_store_t *store, const char *name, bool exported)
{
    return_val_if_null(store, -1);
    return_val_if_null(name, -1);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name_cstr, &index) != 0) {
        log_fatal("variable_store_set_exported_cstr: variable %s not found", name);
        return -1; // Name not found
    }

    variable_t *var = (variable_t *)variable_array_get(store->variables, index);
    return variable_set_exported(var, exported);
}

// Positional parameters
int variable_store_set_positional_params(variable_store_t *store, const string_t *params[], size_t count)
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
        variable_t *var = variable_create_from_cstr(index_str, string_data(params[i]), false, false);
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

int variable_store_set_positional_params_cstr(variable_store_t *store, const char *params[], size_t count)
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
        variable_t *var = variable_create_from_cstr(index_str, params[i], false, false);
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

const variable_t *variable_store_get_positional_param(const variable_store_t *store, size_t index)
{
    return_val_if_null(store, NULL);
    return variable_array_get(store->positional_params, index);
}

const char *variable_store_get_positional_param_cstr(const variable_store_t *store, size_t index)
{
    const variable_t *var = variable_store_get_positional_param(store, index);
    return var ? variable_get_value_cstr(var) : NULL;
}

size_t variable_store_positional_param_count(const variable_store_t *store)
{
    return_val_if_null(store, 0);
    return variable_array_size(store->positional_params);
}

// Special parameter getters
const string_t *variable_store_get_status(const variable_store_t *store)
{
    return_val_if_null(store, NULL);
    return store->status_str;
}

long variable_store_get_pid(const variable_store_t *store)
{
    return_val_if_null(store, 0);
    return store->pid;
}

const string_t *variable_store_get_shell_name(const variable_store_t *store)
{
    return_val_if_null(store, NULL);
    return store->shell_name;
}

long variable_store_get_last_bg_pid(const variable_store_t *store)
{
    return_val_if_null(store, 0);
    return store->last_bg_pid;
}

const string_t *variable_store_get_options(const variable_store_t *store)
{
    return_val_if_null(store, NULL);
    return store->options;
}

const char *variable_store_get_status_cstr(const variable_store_t *store)
{
    return_val_if_null(store, NULL);
    return string_data(store->status_str);
}

const char *variable_store_get_shell_name_cstr(const variable_store_t *store)
{
    return_val_if_null(store, NULL);
    return string_data(store->shell_name);
}

const char *variable_store_get_options_cstr(const variable_store_t *store)
{
    return_val_if_null(store, NULL);
    return string_data(store->options);
}

// Special parameter setters
int variable_store_set_status(variable_store_t *store, const string_t *status)
{
    return_val_if_null(store, -1);
    return_val_if_null(status, -1);

    string_t *new_status = string_clone(status);

    string_destroy(store->status_str);
    store->status_str = new_status;
    return 0;
}

int variable_store_set_status_cstr(variable_store_t *store, const char *status)
{
    return_val_if_null(store, -1);
    return_val_if_null(status, -1);

    string_t *new_status = string_create_from_cstr(status);
    if (!new_status) {
        log_fatal("variable_store_set_status_cstr: failed to create status");
        return -1;
    }

    string_destroy(store->status_str);
    store->status_str = new_status;
    return 0;
}

int variable_store_set_shell_name(variable_store_t *store, const string_t *shell_name)
{
    return_val_if_null(store, -1);
    return_val_if_null(shell_name, -1);

    string_t *new_shell_name = string_clone(shell_name);

    string_destroy(store->shell_name);
    store->shell_name = new_shell_name;
    return 0;
}

int variable_store_set_shell_name_cstr(variable_store_t *store, const char *shell_name)
{
    return_val_if_null(store, -1);
    return_val_if_null(shell_name, -1);

    string_t *new_shell_name = string_create_from_cstr(shell_name);
    if (!new_shell_name) {
        log_fatal("variable_store_set_shell_name_cstr: failed to create shell_name");
        return -1;
    }

    string_destroy(store->shell_name);
    store->shell_name = new_shell_name;
    return 0;
}

int variable_store_set_last_bg_pid(variable_store_t *store, long last_bg_pid)
{
    return_val_if_null(store, -1);
    store->last_bg_pid = last_bg_pid;
    return 0;
}

int variable_store_set_options(variable_store_t *store, const string_t *options)
{
    return_val_if_null(store, -1);
    return_val_if_null(options, -1);

    string_t *new_options = string_clone(options);

    string_destroy(store->options);
    store->options = new_options;
    return 0;
}

int variable_store_set_options_cstr(variable_store_t *store, const char *options)
{
    return_val_if_null(store, -1);
    return_val_if_null(options, -1);

    string_t *new_options = string_create_from_cstr(options);
    if (!new_options) {
        log_fatal("variable_store_set_options_cstr: failed to create options");
        return -1;
    }

    string_destroy(store->options);
    store->options = new_options;
    return 0;
}

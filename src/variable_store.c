#include "variable_store.h"
#include "xalloc.h"
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
variable_store_t *variable_store_create(void)
{
    variable_store_t *store = xmalloc(sizeof(variable_store_t));
    store->variables =
        variable_array_create_with_free((variable_array_free_func_t)variable_destroy);
    return store;
}

variable_store_t *variable_store_create_from_envp(char **envp)
{
    variable_store_t *store = variable_store_create();

    if (envp)
    {
        for (char **env = envp; *env; env++)
        {
            char *name = *env;
            char *eq = strchr(name, '=');
            if (!eq)
            {
                continue;
            }
            *eq = '\0';
            char *value = eq + 1;
            variable_store_add_cstr(store, name, value, true, false);
            *eq = '='; // Restore for safety
        }
    }

    return store;
}

// Destructor
void variable_store_destroy(variable_store_t **store)
{
    Expects_not_null(store);
    variable_store_t *s = *store;
    Expects_not_null(s);

    log_debug("variable_store_destroy: freeing store %p, variables %zu", s,
              variable_array_size(s->variables));
    variable_array_destroy(&s->variables);
    xfree(s);
    *store = NULL;
}

// Clear all variables and parameters
int variable_store_clear(variable_store_t *store)
{
    Expects_not_null(store);

    log_debug("variable_store_clear: clearing store %p, variables %zu", store,
              variable_array_size(store->variables));

    variable_array_clear(store->variables);
    return 0;
}

// variable_t management
void variable_store_add(variable_store_t *store, const string_t *name, const string_t *value,
                        bool exported, bool read_only)
{
    Expects_not_null(store);
    Expects_not_null(name);
    Expects_not_null(value);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name, &index) ==
        0)
    {
        // Replace existing variable
        variable_t *new_var = variable_create(name, value, exported, read_only);
        variable_array_set(store->variables, index, new_var);
    }
    else
    {
        // Add new variable
        variable_t *var = variable_create(name, value, exported, read_only);
        variable_array_append(store->variables, var);
    }
}

void variable_store_add_cstr(variable_store_t *store, const char *name, const char *value,
                             bool exported, bool read_only)
{
    Expects_not_null(store);
    Expects_not_null(name);
    Expects_not_null(value);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name_cstr,
                                         &index) == 0)
    {
        // Replace existing variable
        variable_t *new_var = variable_create_from_cstr(name, value, exported, read_only);
        variable_array_set(store->variables, index, new_var);
    }
    else
    {
        // Add new variable
        variable_t *var = variable_create_from_cstr(name, value, exported, read_only);
        variable_array_append(store->variables, var);
    }
}

void variable_store_remove(variable_store_t *store, const string_t *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name, &index) ==
        0)
    {
        variable_array_remove(store->variables, index);
    }
}

void variable_store_remove_cstr(variable_store_t *store, const char *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name_cstr,
                                         &index) == 0)
    {
        variable_array_remove(store->variables, index);
    }
}

int variable_store_has_name(const variable_store_t *store, const string_t *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    size_t index;
    return variable_array_find_with_compare(store->variables, name, compare_variable_name,
                                            &index) == 0
               ? 1
               : 0;
}

int variable_store_has_name_cstr(const variable_store_t *store, const char *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    size_t index;
    return variable_array_find_with_compare(store->variables, name, compare_variable_name_cstr,
                                            &index) == 0
               ? 1
               : 0;
}

const variable_t *variable_store_get_variable(const variable_store_t *store, const string_t *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name, &index) !=
        0)
    {
        return NULL; // Name not found
    }

    return variable_array_get(store->variables, index);
}

const variable_t *variable_store_get_variable_cstr(const variable_store_t *store, const char *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name_cstr,
                                         &index) != 0)
    {
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
    Expects_not_null(store);
    Expects_not_null(name);

    const variable_t *var = variable_store_get_variable(store, name);
    if (!var)
    {
        return -1; // Name not found
    }

    return variable_is_read_only(var) ? 1 : 0;
}

int variable_store_is_read_only_cstr(const variable_store_t *store, const char *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    const variable_t *var = variable_store_get_variable_cstr(store, name);
    if (!var)
    {
        return -1; // Name not found
    }

    return variable_is_read_only(var) ? 1 : 0;
}

int variable_store_is_exported(const variable_store_t *store, const string_t *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    const variable_t *var = variable_store_get_variable(store, name);
    if (!var)
    {
        return -1; // Name not found
    }

    return variable_is_exported(var) ? 1 : 0;
}

int variable_store_is_exported_cstr(const variable_store_t *store, const char *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    const variable_t *var = variable_store_get_variable_cstr(store, name);
    if (!var)
    {
        return -1; // Name not found
    }

    return variable_is_exported(var) ? 1 : 0;
}

int variable_store_set_read_only(variable_store_t *store, const string_t *name, bool read_only)
{
    Expects_not_null(store);
    Expects_not_null(name);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name, &index) !=
        0)
    {
        log_fatal("variable_store_set_read_only: variable %s not found", string_cstr(name));
        return -1; // Name not found
    }

    variable_t *var = (variable_t *)variable_array_get(store->variables, index);
    return variable_set_read_only(var, read_only);
}

int variable_store_set_read_only_cstr(variable_store_t *store, const char *name, bool read_only)
{
    Expects_not_null(store);
    Expects_not_null(name);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name_cstr,
                                         &index) != 0)
    {
        log_fatal("variable_store_set_read_only_cstr: variable %s not found", name);
        return -1; // Name not found
    }

    variable_t *var = (variable_t *)variable_array_get(store->variables, index);
    return variable_set_read_only(var, read_only);
}

int variable_store_set_exported(variable_store_t *store, const string_t *name, bool exported)
{
    Expects_not_null(store);
    Expects_not_null(name);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name, &index) !=
        0)
    {
        log_fatal("variable_store_set_exported: variable %s not found", string_cstr(name));
        return -1; // Name not found
    }

    variable_t *var = (variable_t *)variable_array_get(store->variables, index);
    return variable_set_exported(var, exported);
}

int variable_store_set_exported_cstr(variable_store_t *store, const char *name, bool exported)
{
    Expects_not_null(store);
    Expects_not_null(name);

    size_t index;
    if (variable_array_find_with_compare(store->variables, name, compare_variable_name_cstr,
                                         &index) != 0)
    {
        log_fatal("variable_store_set_exported_cstr: variable %s not found", name);
        return -1; // Name not found
    }

    variable_t *var = (variable_t *)variable_array_get(store->variables, index);
    return variable_set_exported(var, exported);
}

// Value helper wrappers
int variable_store_get_variable_length(const variable_store_t *store, const string_t *name)
{
    Expects_not_null(store);
    Expects_not_null(name);
    const variable_t *var = variable_store_get_variable(store, name);
    if (!var)
    {
        return -1;
    }
    return variable_get_value_length(var);
}

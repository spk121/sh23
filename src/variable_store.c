#include "variable_store.h"
#include "string_t.h"
#include "variable_map.h"
#include "xalloc.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// POSIX limits for variable names and values
#define MAX_VAR_NAME_LENGTH 1024
#define MAX_VAR_VALUE_LENGTH (128 * 1024) // 128KB

// Helper function to validate variable name according to POSIX rules
static var_store_error_t validate_variable_name(const string_t *name)
{
    if (string_empty(name))
    {
        return VAR_STORE_ERROR_EMPTY_NAME;
    }

    int len = string_length(name);
    if (len > MAX_VAR_NAME_LENGTH)
    {
        return VAR_STORE_ERROR_NAME_TOO_LONG;
    }

    const char *str = string_cstr(name);

    if (len == 1)
    {
        // Single character name may be special parameters.
        // '@', '*', '#', and digits are handled by the positional parameters stack.
        // There are four other special parameters may be allowed as single character names.
        char ch = str[0];
        if (ch == '?' || ch == '-' || ch == '$' || ch == '!')
        {
            return VAR_STORE_ERROR_NONE;
        }
    }

    // First character must be a letter or underscore
    if (!isalpha((unsigned char)str[0]) && str[0] != '_')
    {
        if (isdigit((unsigned char)str[0]))
        {
            return VAR_STORE_ERROR_NAME_STARTS_WITH_DIGIT;
        }
        return VAR_STORE_ERROR_NAME_INVALID_CHARACTER;
    }

    // Remaining characters must be alphanumeric or underscore
    for (int i = 1; i < len; i++)
    {
        if (!isalnum((unsigned char)str[i]) && str[i] != '_')
        {
            return VAR_STORE_ERROR_NAME_INVALID_CHARACTER;
        }
    }

    return VAR_STORE_ERROR_NONE;
}

// Helper function to validate variable value
static var_store_error_t validate_variable_value(const string_t *value)
{
    if (value && string_length(value) > MAX_VAR_VALUE_LENGTH)
    {
        return VAR_STORE_ERROR_VALUE_TOO_LONG;
    }
    return VAR_STORE_ERROR_NONE;
}

// Helper function to free cached envp array
static void free_cached_envp(variable_store_t *store)
{
    if (store->cached_envp)
    {
        for (int i = 0; store->cached_envp[i] != NULL; i++)
        {
            xfree(store->cached_envp[i]);
        }
        xfree(store->cached_envp);
        store->cached_envp = NULL;
    }
}

variable_store_t *variable_store_create(void)
{
    variable_store_t *store = xmalloc(sizeof(variable_store_t));
    store->map = variable_map_create();
    store->cached_envp = NULL;
    return store;
}

variable_store_t *variable_store_create_from_envp(char **envp)
{
    variable_store_t *store = variable_store_create();

    if (envp)
    {
        for (int i = 0; envp[i] != NULL; i++)
        {
            // Parse "NAME=VALUE" format
            char *equals = strchr(envp[i], '=');
            if (equals)
            {
                intptr_t name_len = equals - envp[i];
                if (name_len <= 0 || name_len > INT_MAX)
                {
                    continue; // Skip invalid entries
                }
                string_t *name = string_create_from_cstr_len(envp[i], (int)name_len);
                string_t *value = string_create_from_cstr(equals + 1);

                // Environment variables are exported by default and not read-only
                variable_store_add(store, name, value, true, false);

                string_destroy(&name);
                string_destroy(&value);
            }
        }
    }

    return store;
}

void variable_store_destroy(variable_store_t **store)
{
    if (!store || !*store)
    {
        return;
    }

    free_cached_envp(*store);
    variable_map_destroy(&(*store)->map);
    xfree(*store);
    *store = NULL;
}

void variable_store_clear(variable_store_t *store)
{
    if (!store)
    {
        return;
    }

    variable_map_clear(store->map);
    free_cached_envp(store);
}

var_store_error_t variable_store_add(variable_store_t *store, const string_t *name,
                                     const string_t *value, bool exported, bool read_only)
{
    if (!store || !name)
    {
        return VAR_STORE_ERROR_NOT_FOUND;
    }

    // Validate name
    var_store_error_t err = validate_variable_name(name);
    if (err != VAR_STORE_ERROR_NONE)
    {
        return err;
    }

    // Validate value
    err = validate_variable_value(value);
    if (err != VAR_STORE_ERROR_NONE)
    {
        return err;
    }

    // Check if variable exists and is read-only
    const variable_map_entry_t *existing = variable_store_get_variable(store, name);
    if (existing && existing->mapped.read_only)
    {
        return VAR_STORE_ERROR_READ_ONLY;
    }

    // Create mapped value
    variable_map_mapped_t mapped;
    if (value)
    {
        mapped.value = string_create_from(value);
    }
    else
    {
        mapped.value = string_create();
    }
    mapped.exported = exported;
    mapped.read_only = read_only;

    // Insert or update the variable
    variable_map_insert_or_assign_move(store->map, name, &mapped);

    // Invalidate cached envp
    free_cached_envp(store);

    return VAR_STORE_ERROR_NONE;
}

var_store_error_t variable_store_add_cstr(variable_store_t *store, const char *name,
                                          const char *value, bool exported, bool read_only)
{
    if (!name)
    {
        return VAR_STORE_ERROR_EMPTY_NAME;
    }

    string_t *name_str = string_create_from_cstr(name);
    string_t *value_str = value ? string_create_from_cstr(value) : NULL;

    var_store_error_t err = variable_store_add(store, name_str, value_str, exported, read_only);

    string_destroy(&name_str);
    if (value_str)
    {
        string_destroy(&value_str);
    }

    return err;
}

void variable_store_add_env(variable_store_t *store, const char *env)
{
    if (!store || !env)
    {
        return;
    }

    // Parse "NAME=VALUE" format
    char *equals = strchr(env, '=');
    if (equals)
    {
        intptr_t name_len = equals - env;
        if (name_len <= 0 || name_len > INT_MAX)
        {
            return; // Skip invalid entries
        }
        string_t *name = string_create_from_cstr_len(env, (int)name_len);
        string_t *value = string_create_from_cstr(equals + 1);

        // Environment variables are exported by default and not read-only
        variable_store_add(store, name, value, true, false);

        string_destroy(&name);
        string_destroy(&value);
    }
}

void variable_store_remove(variable_store_t *store, const string_t *name)
{
    if (!store || !name)
    {
        return;
    }

    variable_map_erase(store->map, name);
    free_cached_envp(store);
}

void variable_store_remove_cstr(variable_store_t *store, const char *name)
{
    if (!name)
    {
        return;
    }

    string_t *name_str = string_create_from_cstr(name);
    variable_store_remove(store, name_str);
    string_destroy(&name_str);
}

bool variable_store_has_name(const variable_store_t *store, const string_t *name)
{
    if (!store || !name)
    {
        return false;
    }

    return variable_map_contains(store->map, name);
}

bool variable_store_has_name_cstr(const variable_store_t *store, const char *name)
{
    if (!name)
    {
        return false;
    }

    string_t *name_str = string_create_from_cstr(name);
    bool result = variable_store_has_name(store, name_str);
    string_destroy(&name_str);
    return result;
}

const variable_map_entry_t *variable_store_get_variable(const variable_store_t *store,
                                                        const string_t *name)
{
    if (!store || !name)
    {
        return NULL;
    }

    int32_t pos = variable_map_find(store->map, name);
    if (pos == -1)
    {
        return NULL;
    }

    return &store->map->entries[pos];
}

const variable_map_entry_t *variable_store_get_variable_cstr(const variable_store_t *store,
                                                             const char *name)
{
    if (!name)
    {
        return NULL;
    }

    string_t *name_str = string_create_from_cstr(name);
    const variable_map_entry_t *result = variable_store_get_variable(store, name_str);
    string_destroy(&name_str);
    return result;
}

const string_t *variable_store_get_value(const variable_store_t *store, const string_t *name)
{
    const variable_map_entry_t *entry = variable_store_get_variable(store, name);
    return entry ? entry->mapped.value : NULL;
}

const char *variable_store_get_value_cstr(const variable_store_t *store, const char *name)
{
    const string_t *value = NULL;

    if (name)
    {
        string_t *name_str = string_create_from_cstr(name);
        value = variable_store_get_value(store, name_str);
        string_destroy(&name_str);
    }

    return value ? string_cstr(value) : NULL;
}

bool variable_store_is_read_only(const variable_store_t *store, const string_t *name)
{
    const variable_map_entry_t *entry = variable_store_get_variable(store, name);
    return entry ? entry->mapped.read_only : false;
}

bool variable_store_is_read_only_cstr(const variable_store_t *store, const char *name)
{
    if (!name)
    {
        return false;
    }

    string_t *name_str = string_create_from_cstr(name);
    bool result = variable_store_is_read_only(store, name_str);
    string_destroy(&name_str);
    return result;
}

bool variable_store_is_exported(const variable_store_t *store, const string_t *name)
{
    const variable_map_entry_t *entry = variable_store_get_variable(store, name);
    return entry ? entry->mapped.exported : false;
}

bool variable_store_is_exported_cstr(const variable_store_t *store, const char *name)
{
    if (!name)
    {
        return false;
    }

    string_t *name_str = string_create_from_cstr(name);
    bool result = variable_store_is_exported(store, name_str);
    string_destroy(&name_str);
    return result;
}

var_store_error_t variable_store_set_read_only(variable_store_t *store, const string_t *name,
                                               bool read_only)
{
    if (!store || !name)
    {
        return VAR_STORE_ERROR_NOT_FOUND;
    }

    variable_map_mapped_t *mapped = variable_map_data_at(store->map, name);
    if (!mapped)
    {
        return VAR_STORE_ERROR_NOT_FOUND;
    }

    // Cannot unset read-only flag on a read-only variable
    if (mapped->read_only && !read_only)
    {
        return VAR_STORE_ERROR_READ_ONLY;
    }

    mapped->read_only = read_only;
    return VAR_STORE_ERROR_NONE;
}

var_store_error_t variable_store_set_read_only_cstr(variable_store_t *store, const char *name,
                                                    bool read_only)
{
    if (!name)
    {
        return VAR_STORE_ERROR_EMPTY_NAME;
    }

    string_t *name_str = string_create_from_cstr(name);
    var_store_error_t err = variable_store_set_read_only(store, name_str, read_only);
    string_destroy(&name_str);
    return err;
}

var_store_error_t variable_store_set_exported(variable_store_t *store, const string_t *name,
                                              bool exported)
{
    if (!store || !name)
    {
        return VAR_STORE_ERROR_NOT_FOUND;
    }

    variable_map_mapped_t *mapped = variable_map_data_at(store->map, name);
    if (!mapped)
    {
        return VAR_STORE_ERROR_NOT_FOUND;
    }

    if (mapped->read_only)
    {
        return VAR_STORE_ERROR_READ_ONLY;
    }

    mapped->exported = exported;
    free_cached_envp(store);

    return VAR_STORE_ERROR_NONE;
}

var_store_error_t variable_store_set_exported_cstr(variable_store_t *store, const char *name,
                                                   bool exported)
{
    if (!name)
    {
        return VAR_STORE_ERROR_EMPTY_NAME;
    }

    string_t *name_str = string_create_from_cstr(name);
    var_store_error_t err = variable_store_set_exported(store, name_str, exported);
    string_destroy(&name_str);
    return err;
}

int32_t variable_store_get_value_length(const variable_store_t *store, const string_t *name)
{
    const string_t *value = variable_store_get_value(store, name);
    return value ? string_length(value) : 0;
}

char *const *variable_store_update_envp(variable_store_t *store)
{
    return variable_store_update_envp_with_parent(store, NULL);
}

char *const *variable_store_update_envp_with_parent(variable_store_t *store,
                                                    const variable_store_t *parent)
{
    if (!store)
    {
        return NULL;
    }

    // Free old cached envp
    free_cached_envp(store);

    // Count exported variables from both stores
    int count = 0;

    // Count from current store
    for (int32_t i = 0; i < store->map->capacity; i++)
    {
        if (store->map->entries[i].occupied && store->map->entries[i].mapped.exported)
        {
            count++;
        }
    }

    // Count from parent store (only if not overridden)
    if (parent)
    {
        for (int32_t i = 0; i < parent->map->capacity; i++)
        {
            if (parent->map->entries[i].occupied && parent->map->entries[i].mapped.exported)
            {
                // Check if this variable is overridden in the current store
                if (!variable_map_contains(store->map, parent->map->entries[i].key))
                {
                    count++;
                }
            }
        }
    }

    // Allocate envp array (NULL-terminated)
    store->cached_envp = xcalloc(count + 1, sizeof(char *));

    int idx = 0;

    // Add exported variables from current store
    for (int32_t i = 0; i < store->map->capacity; i++)
    {
        if (store->map->entries[i].occupied && store->map->entries[i].mapped.exported)
        {
            const string_t *name = store->map->entries[i].key;
            const string_t *value = store->map->entries[i].mapped.value;

            // Format: NAME=VALUE
            int name_len = string_length(name);
            int value_len = value ? string_length(value) : 0;
            int total_len = name_len + 1 + value_len + 1; // name + '=' + value + '\0'

            char *env_str = xmalloc(total_len);
            memcpy(env_str, string_cstr(name), name_len);
            env_str[name_len] = '=';
            if (value)
            {
                memcpy(env_str + name_len + 1, string_cstr(value), value_len);
            }
            env_str[name_len + 1 + value_len] = '\0';

            store->cached_envp[idx++] = env_str;
        }
    }

    // Add exported variables from parent (if not overridden)
    if (parent)
    {
        for (int32_t i = 0; i < parent->map->capacity; i++)
        {
            if (parent->map->entries[i].occupied && parent->map->entries[i].mapped.exported)
            {
                const string_t *name = parent->map->entries[i].key;

                // Skip if overridden in current store
                if (variable_map_contains(store->map, name))
                {
                    continue;
                }

                const string_t *value = parent->map->entries[i].mapped.value;

                // Format: NAME=VALUE
                int name_len = string_length(name);
                int value_len = value ? string_length(value) : 0;
                int total_len = name_len + 1 + value_len + 1;

                char *env_str = xmalloc(total_len);
                memcpy(env_str, string_cstr(name), name_len);
                env_str[name_len] = '=';
                if (value)
                {
                    memcpy(env_str + name_len + 1, string_cstr(value), value_len);
                }
                env_str[name_len + 1 + value_len] = '\0';

                store->cached_envp[idx++] = env_str;
            }
        }
    }

    store->cached_envp[idx] = NULL; // NULL-terminate the array

    return store->cached_envp;
}

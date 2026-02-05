#include <ctype.h>
#include "logging.h"
#include "string_t.h"
#include "string_list.h"
#include "variable_map.h"
#include "variable_store.h"
#include "xalloc.h"

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

/**
 * Helper to construct a "NAME=VALUE" environment string.
 */
static char *make_env_cstr(const string_t *name, const string_t *value)
{
    int name_len = string_length(name);
    int value_len = value ? string_length(value) : 0;
    int total_len = name_len + 1 + value_len + 1; // name + '=' + value + '\0'

    char *env_str = xmalloc(total_len);
    memcpy(env_str, string_cstr(name), name_len);
    env_str[name_len] = '=';
    if (value_len)
    {
        memcpy(env_str + name_len + 1, string_cstr(value), value_len);
    }
    env_str[name_len + 1 + value_len] = '\0';

    return env_str;
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
    store->generation = 0;
    store->cached_generation = 0;
    store->cached_parent_gen = 0;
    store->cached_parent = NULL;
    store->cached_envp = NULL;
    return store;
}

/**
 * Parse a "NAME=VALUE" environment string into separate name and value strings.
 *
 * Validates that the name and value conform to POSIX shell variable rules.
 * Non-conforming entries are logged and skipped.
 *
 * On success, *out_name and *out_value are set to newly allocated strings
 * that the caller must free with string_destroy().
 *
 * Returns true on success, false if the input is malformed or fails validation.
 * On failure, *out_name and *out_value are set to NULL.
 */
static bool parse_env_cstr(const char *env_str, string_t **out_name, string_t **out_value)
{
    *out_name = NULL;
    *out_value = NULL;

    if (!env_str || env_str[0] == '\0')
    {
        return false;
    }

    // POSIX names cannot start with '=' or be empty before '='
    if (env_str[0] == '=')
    {
        log_debug("Skipping environment variable: name starts with '=' (%s)", env_str);
        return false;
    }

    const char *equals = strchr(env_str, '=');
    if (!equals)
    {
        log_debug("Skipping environment variable: no '=' delimiter (%s)", env_str);
        return false;
    }

    intptr_t name_len = equals - env_str;
    if (name_len <= 0 || name_len > INT_MAX)
    {
        log_debug("Skipping environment variable: invalid name length (%s)", env_str);
        return false;
    }

    string_t *name = string_create_from_cstr_len(env_str, (int)name_len);
    string_t *value = string_create_from_cstr(equals + 1);

    // Validate name conforms to POSIX rules
    var_store_error_t name_err = validate_variable_name(name);
    if (name_err != VAR_STORE_ERROR_NONE)
    {
        log_debug("Skipping environment variable: invalid name '%s' (error %d)", string_cstr(name),
                  name_err);
        string_destroy(&name);
        string_destroy(&value);
        return false;
    }

    // Validate value conforms to limits
    var_store_error_t value_err = validate_variable_value(value);
    if (value_err != VAR_STORE_ERROR_NONE)
    {
        log_debug("Skipping environment variable: invalid value for '%s' (error %d)",
                  string_cstr(name), value_err);
        string_destroy(&name);
        string_destroy(&value);
        return false;
    }

    *out_name = name;
    *out_value = value;

    return true;
}

variable_store_t *variable_store_create_from_envp(char **envp)
{
    variable_store_t *store = variable_store_create();

    if (envp)
    {
        for (int i = 0; envp[i] != NULL; i++)
        {
            string_t *name;
            string_t *value;

            if (parse_env_cstr(envp[i], &name, &value))
            {
                // Environment variables are exported by default and not read-only
                variable_store_add(store, name, value, true, false);

                string_destroy(&name);
                string_destroy(&value);
            }
        }
    }

    return store;
}

variable_store_t* variable_store_clone(const variable_store_t* src)
{
    Expects_not_null(src);
    variable_store_t *clone = variable_store_create();
    variable_map_iterator_t it = variable_map_begin(src->map);
    while (!variable_map_iterator_equal(it, variable_map_end(src->map)))
    {
        const variable_map_entry_t *entry = variable_map_iterator_deref(it);
        variable_store_add(clone, entry->key, entry->mapped.value,
                           entry->mapped.exported, entry->mapped.read_only);
        variable_map_iterator_increment(&it);
    }
    return clone;
}

variable_store_t* variable_store_clone_exported(const variable_store_t* src)
{
    Expects_not_null(src);
    variable_store_t *clone = variable_store_create();
    variable_map_iterator_t it = variable_map_begin(src->map);

    while (!variable_map_iterator_equal(it, variable_map_end(src->map)))
    {
        const variable_map_entry_t *entry = variable_map_iterator_deref(it);
        if (entry->mapped.exported)
        {
            variable_store_add(clone, entry->key, entry->mapped.value,
                               entry->mapped.exported, entry->mapped.read_only);
        }
        variable_map_iterator_increment(&it);
    }
    return clone;
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
    Expects_not_null(store);

    variable_map_clear(store->map);
    store->generation++;
    store->cached_generation = 0;
    store->cached_parent_gen = 0;
    store->cached_parent = NULL;
    // We choose not to free cached_envp here; it will be freed when accessed next time
}

var_store_error_t variable_store_add(variable_store_t *store, const string_t *name,
                                     const string_t *value, bool exported, bool read_only)
{
    Expects_not_null(store);
    Expects_not_null(name);

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
    variable_map_mapped_t mapped = {0};
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

    // Insert or update the variable; insert_or_assign deep-copies so we still own mapped.value
    variable_map_insert_or_assign(store->map, name, &mapped);
    string_destroy(&mapped.value);

    // Invalidate cached envp
    store->generation++;

    return VAR_STORE_ERROR_NONE;
}

var_store_error_t variable_store_add_cstr(variable_store_t *store, const char *name,
                                          const char *value, bool exported, bool read_only)
{
    Expects_not_null(store);
    Expects_not_null(name);

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
    Expects_not_null(store);
    Expects_not_null(env);

    string_t *name;
    string_t *value;

    if (parse_env_cstr(env, &name, &value))
    {
        // Environment variables are exported by default and not read-only
        variable_store_add(store, name, value, true, false);

        string_destroy(&name);
        string_destroy(&value);
    }
}

void variable_store_remove(variable_store_t *store, const string_t *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    variable_map_erase(store->map, name);
    // Invalidate cached envp
    store->generation++;
}

void variable_store_remove_cstr(variable_store_t *store, const char *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    variable_store_remove(store, name_str);
    string_destroy(&name_str);
}

bool variable_store_has_name(const variable_store_t *store, const string_t *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    return variable_map_contains(store->map, name);
}

bool variable_store_has_name_cstr(const variable_store_t *store, const char *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    bool result = variable_store_has_name(store, name_str);
    string_destroy(&name_str);
    return result;
}


const variable_map_entry_t *variable_store_get_variable(const variable_store_t *store,
                                                        const string_t *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

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
    Expects_not_null(store);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    const variable_map_entry_t *result = variable_store_get_variable(store, name_str);
    string_destroy(&name_str);
    return result;
}

const string_t *variable_store_get_value(const variable_store_t *store, const string_t *name)
{
    Expects_not_null(store);
    Expects_not_null(name);
    const variable_map_entry_t *entry = variable_store_get_variable(store, name);
    return entry ? entry->mapped.value : NULL;
}

const char *variable_store_get_value_cstr(const variable_store_t *store, const char *name)
{
    Expects_not_null(store);
    Expects_not_null(name);
    const string_t *value = NULL;

    string_t *name_str = string_create_from_cstr(name);
    value = variable_store_get_value(store, name_str);
    string_destroy(&name_str);

    return value ? string_cstr(value) : NULL;
}

bool variable_store_is_read_only(const variable_store_t *store, const string_t *name)
{
    Expects_not_null(store);
    Expects_not_null(name);
    const variable_map_entry_t *entry = variable_store_get_variable(store, name);
    return entry ? entry->mapped.read_only : false;
}

bool variable_store_is_read_only_cstr(const variable_store_t *store, const char *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    bool result = variable_store_is_read_only(store, name_str);
    string_destroy(&name_str);
    return result;
}

bool variable_store_is_exported(const variable_store_t *store, const string_t *name)
{
    Expects_not_null(store);
    Expects_not_null(name);
    const variable_map_entry_t *entry = variable_store_get_variable(store, name);
    return entry ? entry->mapped.exported : false;
}

bool variable_store_is_exported_cstr(const variable_store_t *store, const char *name)
{
    Expects_not_null(store);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    bool result = variable_store_is_exported(store, name_str);
    string_destroy(&name_str);
    return result;
}

var_store_error_t variable_store_set_read_only(variable_store_t *store, const string_t *name,
                                               bool read_only)
{
    Expects_not_null(store);
    Expects_not_null(name);

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
    // Invalidate cached envp
    store->generation++;
    return VAR_STORE_ERROR_NONE;
}

var_store_error_t variable_store_set_read_only_cstr(variable_store_t *store, const char *name,
                                                    bool read_only)
{
    Expects_not_null(store);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    var_store_error_t err = variable_store_set_read_only(store, name_str, read_only);
    string_destroy(&name_str);
    return err;
}

var_store_error_t variable_store_set_exported(variable_store_t *store, const string_t *name,
                                              bool exported)
{
    Expects_not_null(store);
    Expects_not_null(name);

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
    // Invalidate cached envp
    store->generation++;

    return VAR_STORE_ERROR_NONE;
}

var_store_error_t variable_store_set_exported_cstr(variable_store_t *store, const char *name,
                                                   bool exported)
{
    Expects_not_null(store);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    var_store_error_t err = variable_store_set_exported(store, name_str, exported);
    string_destroy(&name_str);
    return err;
}

int32_t variable_store_get_value_length(const variable_store_t *store, const string_t *name)
{
    Expects_not_null(store);
    Expects_not_null(name);
    const variable_map_entry_t *entry = variable_store_get_variable(store, name);
    if (!entry)
    {
        return -1;
    }
    return entry->mapped.value ? string_length(entry->mapped.value) : 0;
}

void variable_store_for_each(const variable_store_t *store, var_store_iter_fn fn, void *user_data)
{
    Expects_not_null(store);
    Expects_not_null(fn);
    for (int32_t i = 0; i < store->map->capacity; i++)
    {
        if (store->map->entries[i].occupied)
        {
            const string_t *name = store->map->entries[i].key;
            const string_t *value = store->map->entries[i].mapped.value;
            bool exported = store->map->entries[i].mapped.exported;
            bool read_only = store->map->entries[i].mapped.read_only;
            fn(name, value, exported, read_only, user_data);
        }
    }
}

var_store_error_t variable_store_map(variable_store_t *store, var_store_map_fn fn, void *user_data,
                                     string_t **failed_name)
{
    Expects_not_null(store);
    Expects_not_null(fn);

    // Collect keys to remove after iteration to avoid invalidating the iteration
    string_list_t *keys_to_remove = NULL;

    // Collect renames: parallel lists for new names and values, plus flag arrays
    string_list_t *rename_new_names = NULL;
    string_list_t *rename_values = NULL;
    bool *rename_exported = NULL;
    bool *rename_read_only = NULL;
    int rename_count = 0;
    int rename_capacity = 0;

    var_store_error_t err = VAR_STORE_ERROR_NONE;

    for (int32_t i = 0; i < store->map->capacity; i++)
    {
        if (!store->map->entries[i].occupied)
        {
            continue;
        }

        string_t *name = string_create_from(store->map->entries[i].key);
        string_t *value = store->map->entries[i].mapped.value
                              ? string_create_from(store->map->entries[i].mapped.value)
                              : NULL;
        bool exported = store->map->entries[i].mapped.exported;
        bool read_only = store->map->entries[i].mapped.read_only;

        var_store_map_action_t action = fn(name, value, &exported, &read_only, user_data);

        if (action == VAR_STORE_MAP_ACTION_SKIP)
        {
            string_destroy(&name);
            if (value)
            {
                string_destroy(&value);
            }
            continue;
        }

        if (action == VAR_STORE_MAP_ACTION_REMOVE)
        {
            if (!keys_to_remove)
            {
                keys_to_remove = string_list_create();
            }
            string_list_push_back(keys_to_remove, store->map->entries[i].key);

            string_destroy(&name);
            if (value)
            {
                string_destroy(&value);
            }
            continue;
        }

        // VAR_STORE_MAP_ACTION_UPDATE
        err = validate_variable_name(name);
        if (err != VAR_STORE_ERROR_NONE)
        {
            if (failed_name)
            {
                *failed_name = string_create_from(store->map->entries[i].key);
            }
            string_destroy(&name);
            if (value)
            {
                string_destroy(&value);
            }
            goto cleanup;
        }

        err = validate_variable_value(value);
        if (err != VAR_STORE_ERROR_NONE)
        {
            if (failed_name)
            {
                *failed_name = string_create_from(store->map->entries[i].key);
            }
            string_destroy(&name);
            if (value)
            {
                string_destroy(&value);
            }
            goto cleanup;
        }

        // Check if the name was changed
        if (!string_eq(name, store->map->entries[i].key))
        {
            // Rename: delete old key, insert new key with updated values
            if (!keys_to_remove)
            {
                keys_to_remove = string_list_create();
            }
            string_list_push_back(keys_to_remove, store->map->entries[i].key);

            // Store the rename for later insertion
            if (!rename_new_names)
            {
                rename_new_names = string_list_create();
                rename_values = string_list_create();
            }

            if (rename_count >= rename_capacity)
            {
                int new_capacity = rename_capacity == 0 ? 8 : rename_capacity * 2;
                rename_exported = xrealloc(rename_exported, new_capacity * sizeof(bool));
                rename_read_only = xrealloc(rename_read_only, new_capacity * sizeof(bool));
                rename_capacity = new_capacity;
            }

            string_list_move_push_back(rename_new_names, &name);
            string_list_move_push_back(rename_values, &value);
            rename_exported[rename_count] = exported;
            rename_read_only[rename_count] = read_only;
            rename_count++;
        }
        else
        {
            // No rename, just update value and flags in place
            if (store->map->entries[i].mapped.value)
            {
                if (value)
                {
                    string_set(store->map->entries[i].mapped.value, value);
                }
                else
                {
                    string_destroy(&store->map->entries[i].mapped.value);
                }
            }
            else if (value)
            {
                store->map->entries[i].mapped.value = string_create_from(value);
            }

            store->map->entries[i].mapped.exported = exported;
            store->map->entries[i].mapped.read_only = read_only;

            string_destroy(&name);
            if (value)
            {
                string_destroy(&value);
            }
        }
    }

    // Perform deferred removals (includes both explicit removes and rename sources)
    if (keys_to_remove)
    {
        variable_map_erase_multiple(store->map, keys_to_remove);
        string_list_destroy(&keys_to_remove);
    }

    // Perform deferred insertions for renames
    for (int i = 0; i < rename_count; i++)
    {
        variable_map_mapped_t mapped = {0};
        mapped.value = string_create_from(string_list_at(rename_values, i));
        mapped.exported = rename_exported[i];
        mapped.read_only = rename_read_only[i];

        variable_map_insert_or_assign(store->map, string_list_at(rename_new_names, i), &mapped);
        string_destroy(&mapped.value);
    }

    string_list_destroy(&rename_new_names);
    string_list_destroy(&rename_values);
    xfree(rename_exported);
    xfree(rename_read_only);

    store->generation++;
    return VAR_STORE_ERROR_NONE;

cleanup:
    string_list_destroy(&keys_to_remove);
    string_list_destroy(&rename_new_names);
    string_list_destroy(&rename_values);
    xfree(rename_exported);
    xfree(rename_read_only);
    return err;
}


static void dbg_print_export(const string_t *var, const string_t *val, bool exported,
                             bool read_only, void *user_data)
{
    (void)read_only;
    (void)user_data;

    if (exported)
    {
        if (val)
        {
            log_debug("export %s=\"%s\"", string_cstr(var), string_cstr(val));
        }
        else
        {
            log_debug("export %s", string_cstr(var));
        }
    }
}

void variable_store_debug_print_exported(const variable_store_t *store)
{
    if (!store)
    {
        return;
    }
    variable_store_for_each(store, dbg_print_export, NULL);
}

/**
 * Check if the cached envp is still valid.
 */
static bool envp_cache_valid(const variable_store_t *store, const variable_store_t *parent)
{
    // No cache exists
    if (!store->cached_envp)
    {
        return false;
    }

    // Our own data changed
    if (store->cached_generation != store->generation)
    {
        return false;
    }

    // Parent situation changed
    if (store->cached_parent != parent)
    {
        return false;
    }

    // Parent's data changed since we cached
    if (parent && store->cached_parent_gen != parent->generation)
    {
        return false;
    }

    return true;
}

char *const *variable_store_get_envp(variable_store_t *store)
{
    if (!store)
    {
        return NULL;
    }

    // Check if cached envp is still valid (with no parent)
    if (envp_cache_valid(store, NULL))
    {
        return store->cached_envp;
    }

    // Free old cached envp
    free_cached_envp(store);

    // Allocate for worst case: all variables from current store could be exported
    int max_count = store->map->size;
    store->cached_envp = xcalloc(max_count + 1, sizeof(char *));

    int idx = 0;

    // Add exported variables from current store
    for (int32_t i = 0; i < store->map->capacity; i++)
    {
        if (store->map->entries[i].occupied && store->map->entries[i].mapped.exported)
        {
            store->cached_envp[idx++] =
                make_env_cstr(store->map->entries[i].key, store->map->entries[i].mapped.value);
        }
    }

    store->cached_envp[idx] = NULL; // NULL-terminate

    // Update cache validity tracking
    store->cached_generation = store->generation;
    store->cached_parent = NULL;
    store->cached_parent_gen = 0;

    return store->cached_envp;
}

void variable_store_copy_all(variable_store_t *dst, const variable_store_t *src)
{
    if (!dst || !src || !src->map)
        return;

    for (int32_t i = 0; i < src->map->capacity; i++)
    {
        if (!src->map->entries[i].occupied)
            continue;

        const string_t *name = src->map->entries[i].key;
        const string_t *value = src->map->entries[i].mapped.value;
        bool exported = src->map->entries[i].mapped.exported;
        bool read_only = src->map->entries[i].mapped.read_only;

        variable_store_add(dst, name, value, exported, read_only);
    }
}

string_t *variable_store_write_env_file(variable_store_t *vars)
{
    Expects_not_null(vars);

    if (!variable_store_has_name_cstr(vars, "MGSH_ENV_FILE"))
        return NULL;

    const char *fname = variable_store_get_value_cstr(vars, "MGSH_ENV_FILE");

    if (!fname || *fname == '\0')
    {
        if (fname && *fname == '\0')
            log_debug("variable_store_write_env_file: MGSH_ENV_FILE is empty");
        return NULL;
    }

    FILE *fp = fopen(fname, "w");
    if (!fp)
    {
        log_debug("variable_store_write_env_file: failed to open env file %s for writing", fname);
        return NULL;
    }

    string_t *result = string_create_from_cstr(fname);
    char *const *envp = variable_store_get_envp(vars);

    for (char *const *env = envp; *env; env++)
    {
        if (fprintf(fp, "%s\n", *env) < 0)
        {
            log_debug("variable_store_write_env_file: failed to write to env file %s", fname);
            fclose(fp);
            string_destroy(&result);
            return NULL;
        }
    }

    fclose(fp);
    return result;
}

bool variable_store_debug_verify_independent(const variable_store_t *store_a,
                                             const variable_store_t *store_b)
{
    if (!store_a || !store_b)
    {
        log_error("variable_store_debug_verify_independent: NULL store pointer(s)");
        return false;
    }

    if (!store_a->map || !store_b->map)
    {
        log_error("variable_store_debug_verify_independent: NULL map pointer(s)");
        return false;
    }

    bool all_ok = true;

    // Check that both stores have the same number of variables
    if (store_a->map->size != store_b->map->size)
    {
        log_error("variable_store_debug_verify_independent: store sizes differ (%d vs %d)",
                  store_a->map->size, store_b->map->size);
        all_ok = false;
    }

    // Iterate through store_a and verify each variable in store_b
    for (int32_t i = 0; i < store_a->map->capacity; i++)
    {
        if (!store_a->map->entries[i].occupied)
            continue;

        const string_t *key_a = store_a->map->entries[i].key;
        const string_t *value_a = store_a->map->entries[i].mapped.value;

        // Find matching entry in store_b
        int32_t pos_b = variable_map_find(store_b->map, key_a);
        if (pos_b == -1)
        {
            log_error("variable_store_debug_verify_independent: key '%s' exists in store_a but not in store_b",
                      string_cstr(key_a));
            all_ok = false;
            continue;
        }

        const string_t *key_b = store_b->map->entries[pos_b].key;
        const string_t *value_b = store_b->map->entries[pos_b].mapped.value;

        // 1. Check that keys have the same content
        if (!string_eq(key_a, key_b))
        {
            log_error("variable_store_debug_verify_independent: key content mismatch: '%s' vs '%s'",
                      string_cstr(key_a), string_cstr(key_b));
            all_ok = false;
        }

        // 3. Check that keys don't have identical string_t pointers
        if (key_a == key_b)
        {
            log_error("variable_store_debug_verify_independent: key '%s' has identical string_t pointer in both stores (%p)",
                      string_cstr(key_a), (void*)key_a);
            all_ok = false;
        }
        else
        {
            // Check that keys don't have identical data pointers
            if (string_data(key_a) == string_data(key_b))
            {
                log_error("variable_store_debug_verify_independent: key '%s' has identical data pointer in both stores (%p)",
                          string_cstr(key_a), (void*)string_data(key_a));
                all_ok = false;
            }
        }

        // 2. Check that values have the same content
        if (value_a == NULL && value_b == NULL)
        {
            // Both NULL - OK
        }
        else if (value_a == NULL || value_b == NULL)
        {
            log_error("variable_store_debug_verify_independent: value mismatch for key '%s': one is NULL, other is not",
                      string_cstr(key_a));
            all_ok = false;
        }
        else
        {
            if (!string_eq(value_a, value_b))
            {
                log_error("variable_store_debug_verify_independent: value content mismatch for key '%s': '%s' vs '%s'",
                          string_cstr(key_a), string_cstr(value_a), string_cstr(value_b));
                all_ok = false;
            }

            // 4. Check that values don't have identical string_t pointers
            if (value_a == value_b)
            {
                log_error("variable_store_debug_verify_independent: value for key '%s' has identical string_t pointer in both stores (%p)",
                          string_cstr(key_a), (void*)value_a);
                all_ok = false;
            }
            else
            {
                // Check that values don't have identical data pointers
                if (string_data(value_a) == string_data(value_b))
                {
                    log_error("variable_store_debug_verify_independent: value for key '%s' has identical data pointer in both stores (%p)",
                              string_cstr(key_a), (void*)string_data(value_a));
                    all_ok = false;
                }
            }
        }

        // Also check flags match
        if (store_a->map->entries[i].mapped.exported != store_b->map->entries[pos_b].mapped.exported)
        {
            log_warn("variable_store_debug_verify_independent: exported flag mismatch for key '%s': %d vs %d",
                     string_cstr(key_a), 
                     store_a->map->entries[i].mapped.exported,
                     store_b->map->entries[pos_b].mapped.exported);
        }

        if (store_a->map->entries[i].mapped.read_only != store_b->map->entries[pos_b].mapped.read_only)
        {
            log_warn("variable_store_debug_verify_independent: read_only flag mismatch for key '%s': %d vs %d",
                     string_cstr(key_a),
                     store_a->map->entries[i].mapped.read_only,
                     store_b->map->entries[pos_b].mapped.read_only);
        }
    }

    // Check for variables in store_b that aren't in store_a
    for (int32_t i = 0; i < store_b->map->capacity; i++)
    {
        if (!store_b->map->entries[i].occupied)
            continue;

        const string_t *key_b = store_b->map->entries[i].key;
        int32_t pos_a = variable_map_find(store_a->map, key_b);
        if (pos_a == -1)
        {
            log_error("variable_store_debug_verify_independent: key '%s' exists in store_b but not in store_a",
                      string_cstr(key_b));
            all_ok = false;
        }
    }

    if (all_ok)
    {
        log_debug("variable_store_debug_verify_independent: SUCCESS - stores are equal and independent (%d variables)",
                  store_a->map->size);
    }
    else
    {
        log_error("variable_store_debug_verify_independent: FAILED - violations found");
    }

    return all_ok;
}




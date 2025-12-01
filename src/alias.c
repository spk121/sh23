#include "alias.h"
#include <stdlib.h>

struct Alias {
    String *name;
    String *value;
};

// Constructors
Alias *alias_create(const String *name, const String *value)
{
    return_val_if_null(name, NULL);
    return_val_if_null(value, NULL);

    Alias *alias = malloc(sizeof(Alias));
    if (!alias) {
        log_fatal("alias_create: out of memory");
        return NULL;
    }

    alias->name = string_create_from((String *)name);
    if (!alias->name) {
        free(alias);
        log_fatal("alias_create: failed to create name");
        return NULL;
    }

    alias->value = string_create_from((String *)value);
    if (!alias->value) {
        string_destroy(alias->name);
        free(alias);
        log_fatal("alias_create: failed to create value");
        return NULL;
    }

    return alias;
}

Alias *alias_create_from_cstr(const char *name, const char *value)
{
    return_val_if_null(name, NULL);
    return_val_if_null(value, NULL);

    Alias *alias = malloc(sizeof(Alias));
    if (!alias) {
        log_fatal("alias_create_from_cstr: out of memory");
        return NULL;
    }

    alias->name = string_create_from_cstr(name);
    if (!alias->name) {
        free(alias);
        log_fatal("alias_create_from_cstr: failed to create name");
        return NULL;
    }

    alias->value = string_create_from_cstr(value);
    if (!alias->value) {
        string_destroy(alias->name);
        free(alias);
        log_fatal("alias_create_from_cstr: failed to create value");
        return NULL;
    }

    return alias;
}

// Destructor
void alias_destroy(Alias *alias)
{
    if (alias) {
        log_debug("alias_destroy: freeing alias %p, name = %s, value = %s",
                  alias,
                  alias->name ? string_data(alias->name) : "(null)",
                  alias->value ? string_data(alias->value) : "(null)");
        string_destroy(alias->name);
        string_destroy(alias->value);
        free(alias);
    }
}

// Getters
const String *alias_get_name(const Alias *alias)
{
    return_val_if_null(alias, NULL);
    return alias->name;
}

const String *alias_get_value(const Alias *alias)
{
    return_val_if_null(alias, NULL);
    return alias->value;
}

const char *alias_get_name_cstr(const Alias *alias)
{
    return_val_if_null(alias, NULL);
    return string_data(alias->name);
}

const char *alias_get_value_cstr(const Alias *alias)
{
    return_val_if_null(alias, NULL);
    return string_data(alias->value);
}

// Setters
int alias_set_name(Alias *alias, const String *name)
{
    return_val_if_null(alias, -1);
    return_val_if_null(name, -1);

    String *new_name = string_create_from((String *)name);
    if (!new_name) {
        log_fatal("alias_set_name: failed to create name");
        return -1;
    }

    string_destroy(alias->name);
    alias->name = new_name;
    return 0;
}

int alias_set_value(Alias *alias, const String *value)
{
    return_val_if_null(alias, -1);
    return_val_if_null(value, -1);

    String *new_value = string_create_from((String *)value);
    if (!new_value) {
        log_fatal("alias_set_value: failed to create value");
        return -1;
    }

    string_destroy(alias->value);
    alias->value = new_value;
    return 0;
}

int alias_set_name_cstr(Alias *alias, const char *name)
{
    return_val_if_null(alias, -1);
    return_val_if_null(name, -1);

    String *new_name = string_create_from_cstr(name);
    if (!new_name) {
        log_fatal("alias_set_name_cstr: failed to create name");
        return -1;
    }

    string_destroy(alias->name);
    alias->name = new_name;
    return 0;
}

int alias_set_value_cstr(Alias *alias, const char *value)
{
    return_val_if_null(alias, -1);
    return_val_if_null(value, -1);

    String *new_value = string_create_from_cstr(value);
    if (!new_value) {
        log_fatal("alias_set_value_cstr: failed to create value");
        return -1;
    }

    string_destroy(alias->value);
    alias->value = new_value;
    return 0;
}

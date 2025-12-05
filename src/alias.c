#include "alias.h"
#include "xalloc.h"

struct Alias {
    String *name;
    String *value;
};

// Constructors
Alias *alias_create(const String *name, const String *value)
{
    Expects_not_null(name);
    Expects_not_null(value);

    Alias *alias = xmalloc(sizeof(Alias));
    alias->name = string_clone(name);
    alias->value = string_clone(value);

    return alias;
}

Alias *alias_create_from_cstr(const char *name, const char *value)
{
    Expects_not_null(name);
    Expects_not_null(value);

    Alias *alias = xmalloc(sizeof(Alias));
    alias->name = string_create_from_cstr(name);
    alias->value = string_create_from_cstr(value);

    return alias;
}

// Destructor
void alias_destroy(Alias *alias)
{
    Expects_not_null(alias);
    log_debug("alias_destroy: freeing alias %p, name = %s, value = %s",
              alias,
              string_data(alias->name),
              string_data(alias->value));
    string_destroy(alias->name);
    string_destroy(alias->value);
    xfree(alias);
}

// Getters
const String *alias_get_name(const Alias *alias)
{
    Expects_not_null(alias);
    return alias->name;
}

const String *alias_get_value(const Alias *alias)
{
    Expects_not_null(alias);
    return alias->value;
}

const char *alias_get_name_cstr(const Alias *alias)
{
    Expects_not_null(alias);
    return string_data(alias->name);
}

const char *alias_get_value_cstr(const Alias *alias)
{
    Expects_not_null(alias);
    return string_data(alias->value);
}

// Setters
void alias_set_name(Alias *alias, const String *name)
{
    Expects_not_null(alias);
    Expects_not_null(name);

    String *new_name = string_clone(name);
    string_destroy(alias->name);
    alias->name = new_name;
}

void alias_set_value(Alias *alias, const String *value)
{
    Expects_not_null(alias);
    Expects_not_null(value);

    String *new_value = string_clone(value);
    string_destroy(alias->value);
    alias->value = new_value;
}

void alias_set_name_cstr(Alias *alias, const char *name)
{
    Expects_not_null(alias);
    Expects_not_null(name);

    String *new_name = string_create_from_cstr(name);
    string_destroy(alias->name);
    alias->name = new_name;
}

void alias_set_value_cstr(Alias *alias, const char *value)
{
    Expects_not_null(alias);
    Expects_not_null(value);

    String *new_value = string_create_from_cstr(value);
    string_destroy(alias->value);
    alias->value = new_value;
}

#define ALIAS_STORE_INTERNAL
#include "alias.h"

#include "xalloc.h"

// Constructors
alias_t *alias_create(const string_t *name, const string_t *value)
{
    Expects_not_null(name);
    Expects_not_null(value);

    alias_t *alias = xmalloc(sizeof(alias_t));
    alias->name = string_create_from(name);
    alias->value = string_create_from(value);

    return alias;
}

alias_t *alias_create_from_cstr(const char *name, const char *value)
{
    Expects_not_null(name);
    Expects_not_null(value);

    alias_t *alias = xmalloc(sizeof(alias_t));
    alias->name = string_create_from_cstr(name);
    alias->value = string_create_from_cstr(value);

    return alias;
}

// Destructor
void alias_destroy(alias_t **alias)
{
    Expects_not_null(alias);
    Expects_not_null(*alias);
    Expects_not_null((*alias)->name);
    Expects_not_null((*alias)->value);

    alias_t *a = *alias;

    log_debug("alias_destroy: freeing alias %p, name = %s, value = %s", a, string_cstr(a->name),
              string_cstr(a->value));
    string_destroy(&(a->name));
    string_destroy(&(a->value));
    xfree(a);
    *alias = NULL;
}

// Getters
const string_t *alias_get_name(const alias_t *alias)
{
    Expects_not_null(alias);
    return alias->name;
}

const string_t *alias_get_value(const alias_t *alias)
{
    Expects_not_null(alias);
    return alias->value;
}

const char *alias_get_name_cstr(const alias_t *alias)
{
    Expects_not_null(alias);
    Expects_not_null(alias->name);
    return string_cstr(alias->name);
}

const char *alias_get_value_cstr(const alias_t *alias)
{
    Expects_not_null(alias);
    Expects_not_null(alias->value);
    return string_cstr(alias->value);
}

// Setters
void alias_set_name(alias_t *alias, const string_t *name)
{
    Expects_not_null(alias);
    Expects_not_null(alias->name);
    Expects_not_null(name);

    string_set(alias->name, name);
}

void alias_set_value(alias_t *alias, const string_t *value)
{
    Expects_not_null(alias);
    Expects_not_null(alias->value);
    Expects_not_null(value);

    string_set(alias->value, value);
}

void alias_set_name_cstr(alias_t *alias, const char *name)
{
    Expects_not_null(alias);
    Expects_not_null(alias->name);
    Expects_not_null(name);

    string_set_cstr(alias->name, name);
}

void alias_set_value_cstr(alias_t *alias, const char *value)
{
    Expects_not_null(alias);
    Expects_not_null(alias->value);
    Expects_not_null(value);

    string_set_cstr(alias->value, value);
}

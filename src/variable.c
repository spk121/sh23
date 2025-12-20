#include "variable.h"
#include "pattern_removal.h"
#include "xalloc.h"

// Constructors
variable_t *variable_create(const string_t *name, const string_t *value, bool exported, bool read_only)
{
    Expects_not_null(name);
    Expects_not_null(value);

    variable_t *variable = xmalloc(sizeof(variable_t));

    variable->name = string_create_from(name);
    variable->value = string_create_from(value);
    variable->exported = exported;
    variable->read_only = read_only;
    return variable;
}

variable_t *variable_create_from_cstr(const char *name, const char *value, bool exported, bool read_only)
{
    Expects_not_null(name);
    Expects_not_null(value);

    variable_t *variable = xmalloc(sizeof(variable_t));

    variable->name = string_create_from_cstr(name);
    variable->value = string_create_from_cstr(value);
    variable->exported = exported;
    variable->read_only = read_only;
    return variable;
}

// Destructor
void variable_destroy(variable_t **variable)
{
    Expects_not_null(variable);
    variable_t *v = *variable;
    Expects_not_null(v);

    log_debug("variable_destroy: freeing variable %p, name = %s, value = %s, exported = %d, read_only = %d",
              v,
              v->name ? string_data(v->name) : "(null)",
              v->value ? string_data(v->value) : "(null)",
              v->exported,
              v->read_only);
    string_destroy(&v->name);
    string_destroy(&v->value);
    xfree(v);
    *variable = NULL;
}

// Getters
const string_t *variable_get_name(const variable_t *variable)
{
    Expects_not_null(variable);
    return variable->name;
}

const string_t *variable_get_value(const variable_t *variable)
{
    Expects_not_null(variable);
    return variable->value;
}

const char *variable_get_name_cstr(const variable_t *variable)
{
    Expects_not_null(variable);
    return string_data(variable->name);
}

const char *variable_get_value_cstr(const variable_t *variable)
{
    Expects_not_null(variable);
    return string_data(variable->value);
}

bool variable_is_exported(const variable_t *variable)
{
    Expects_not_null(variable);
    return variable->exported;
}

bool variable_is_read_only(const variable_t *variable)
{
    Expects_not_null(variable);
    return variable->read_only;
}

// Setters
int variable_set_name(variable_t *variable, const string_t *name)
{
    Expects_not_null(variable);
    Expects_not_null(name);

    string_t *new_name = string_create_from(name);

    string_destroy(&variable->name);
    variable->name = new_name;
    return 0;
}

int variable_set_value(variable_t *variable, const string_t *value)
{
    Expects_not_null(variable);
    Expects_not_null(value);

    if (variable->read_only) {
        log_fatal("variable_set_value: cannot modify read-only variable");
        return -1;
    }

    string_t *new_value = string_create_from(value);

    string_destroy(&variable->value);
    variable->value = new_value;
    return 0;
}

int variable_set_name_cstr(variable_t *variable, const char *name)
{
    Expects_not_null(variable);
    Expects_not_null(name);

    string_t *new_name = string_create_from_cstr(name);

    string_destroy(&variable->name);
    variable->name = new_name;
    return 0;
}

int variable_set_value_cstr(variable_t *variable, const char *value)
{
    Expects_not_null(variable);
    Expects_not_null(value);

    if (variable->read_only) {
        log_fatal("variable_set_value_cstr: cannot modify read-only variable");
        return -1;
    }

    string_t *new_value = string_create_from_cstr(value);

    string_destroy(&variable->value);
    variable->value = new_value;
    return 0;
}

int variable_set_exported(variable_t *variable, bool exported)
{
    Expects_not_null(variable);
    variable->exported = exported;
    return 0;
}

int variable_set_read_only(variable_t *variable, bool read_only)
{
    Expects_not_null(variable);
    variable->read_only = read_only;
    return 0;
}

int variable_get_value_length(const variable_t *variable)
{
    Expects_not_null(variable);
    const string_t *v = variable_get_value(variable);
    return v ? string_length(v) : 0;
}

#include "variable.h"
#include "xalloc.h"

// Constructors
variable_t *variable_create(const string_t *name, const string_t *value, bool exported, bool read_only)
{
    Expects_not_null(name);
    Expects_not_null(value);

    variable_t *variable = xmalloc(sizeof(variable_t));

    variable->name = string_clone(name);
    variable->value = string_clone(value);
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
void variable_destroy(variable_t *variable)
{
    if (variable) {
        log_debug("variable_destroy: freeing variable %p, name = %s, value = %s, exported = %d, read_only = %d",
                  variable,
                  variable->name ? string_data(variable->name) : "(null)",
                  variable->value ? string_data(variable->value) : "(null)",
                  variable->exported,
                  variable->read_only);
        string_destroy(variable->name);
        string_destroy(variable->value);
        xfree(variable);
    }
}

// Getters
const string_t *variable_get_name(const variable_t *variable)
{
    return_val_if_null(variable, NULL);
    return variable->name;
}

const string_t *variable_get_value(const variable_t *variable)
{
    return_val_if_null(variable, NULL);
    return variable->value;
}

const char *variable_get_name_cstr(const variable_t *variable)
{
    return_val_if_null(variable, NULL);
    return string_data(variable->name);
}

const char *variable_get_value_cstr(const variable_t *variable)
{
    return_val_if_null(variable, NULL);
    return string_data(variable->value);
}

bool variable_is_exported(const variable_t *variable)
{
    return_val_if_null(variable, false);
    return variable->exported;
}

bool variable_is_read_only(const variable_t *variable)
{
    return_val_if_null(variable, false);
    return variable->read_only;
}

// Setters
int variable_set_name(variable_t *variable, const string_t *name)
{
    return_val_if_null(variable, -1);
    return_val_if_null(name, -1);

    string_t *new_name = string_clone(name);

    string_destroy(variable->name);
    variable->name = new_name;
    return 0;
}

int variable_set_value(variable_t *variable, const string_t *value)
{
    return_val_if_null(variable, -1);
    return_val_if_null(value, -1);

    if (variable->read_only) {
        log_fatal("variable_set_value: cannot modify read-only variable");
        return -1;
    }

    string_t *new_value = string_clone(value);

    string_destroy(variable->value);
    variable->value = new_value;
    return 0;
}

int variable_set_name_cstr(variable_t *variable, const char *name)
{
    return_val_if_null(variable, -1);
    return_val_if_null(name, -1);

    string_t *new_name = string_create_from_cstr(name);

    string_destroy(variable->name);
    variable->name = new_name;
    return 0;
}

int variable_set_value_cstr(variable_t *variable, const char *value)
{
    return_val_if_null(variable, -1);
    return_val_if_null(value, -1);

    if (variable->read_only) {
        log_fatal("variable_set_value_cstr: cannot modify read-only variable");
        return -1;
    }

    string_t *new_value = string_create_from_cstr(value);

    string_destroy(variable->value);
    variable->value = new_value;
    return 0;
}

int variable_set_exported(variable_t *variable, bool exported)
{
    return_val_if_null(variable, -1);
    variable->exported = exported;
    return 0;
}

int variable_set_read_only(variable_t *variable, bool read_only)
{
    return_val_if_null(variable, -1);
    variable->read_only = read_only;
    return 0;
}

#include "variable.h"
#include <stdlib.h>

struct Variable {
    String *name;
    String *value;
    bool exported;
    bool read_only;
};

// Constructors
Variable *variable_create(const String *name, const String *value, bool exported, bool read_only)
{
    return_val_if_null(name, NULL);
    return_val_if_null(value, NULL);

    Variable *variable = malloc(sizeof(Variable));
    if (!variable) {
        log_fatal("variable_create: out of memory");
        return NULL;
    }

    variable->name = string_create_from((String *)name);
    if (!variable->name) {
        free(variable);
        log_fatal("variable_create: failed to create name");
        return NULL;
    }

    variable->value = string_create_from((String *)value);
    if (!variable->value) {
        string_destroy(variable->name);
        free(variable);
        log_fatal("variable_create: failed to create value");
        return NULL;
    }

    variable->exported = exported;
    variable->read_only = read_only;
    return variable;
}

Variable *variable_create_from_cstr(const char *name, const char *value, bool exported, bool read_only)
{
    return_val_if_null(name, NULL);
    return_val_if_null(value, NULL);

    Variable *variable = malloc(sizeof(Variable));
    if (!variable) {
        log_fatal("variable_create_from_cstr: out of memory");
        return NULL;
    }

    variable->name = string_create_from_cstr(name);
    if (!variable->name) {
        free(variable);
        log_fatal("variable_create_from_cstr: failed to create name");
        return NULL;
    }

    variable->value = string_create_from_cstr(value);
    if (!variable->value) {
        string_destroy(variable->name);
        free(variable);
        log_fatal("variable_create_from_cstr: failed to create value");
        return NULL;
    }

    variable->exported = exported;
    variable->read_only = read_only;
    return variable;
}

// Destructor
void variable_destroy(Variable *variable)
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
        free(variable);
    }
}

// Getters
const String *variable_get_name(const Variable *variable)
{
    return_val_if_null(variable, NULL);
    return variable->name;
}

const String *variable_get_value(const Variable *variable)
{
    return_val_if_null(variable, NULL);
    return variable->value;
}

const char *variable_get_name_cstr(const Variable *variable)
{
    return_val_if_null(variable, NULL);
    return string_data(variable->name);
}

const char *variable_get_value_cstr(const Variable *variable)
{
    return_val_if_null(variable, NULL);
    return string_data(variable->value);
}

bool variable_is_exported(const Variable *variable)
{
    return_val_if_null(variable, false);
    return variable->exported;
}

bool variable_is_read_only(const Variable *variable)
{
    return_val_if_null(variable, false);
    return variable->read_only;
}

// Setters
int variable_set_name(Variable *variable, const String *name)
{
    return_val_if_null(variable, -1);
    return_val_if_null(name, -1);

    String *new_name = string_create_from((String *)name);
    if (!new_name) {
        log_fatal("variable_set_name: failed to create name");
        return -1;
    }

    string_destroy(variable->name);
    variable->name = new_name;
    return 0;
}

int variable_set_value(Variable *variable, const String *value)
{
    return_val_if_null(variable, -1);
    return_val_if_null(value, -1);

    if (variable->read_only) {
        log_fatal("variable_set_value: cannot modify read-only variable");
        return -1;
    }

    String *new_value = string_create_from((String *)value);
    if (!new_value) {
        log_fatal("variable_set_value: failed to create value");
        return -1;
    }

    string_destroy(variable->value);
    variable->value = new_value;
    return 0;
}

int variable_set_name_cstr(Variable *variable, const char *name)
{
    return_val_if_null(variable, -1);
    return_val_if_null(name, -1);

    String *new_name = string_create_from_cstr(name);
    if (!new_name) {
        log_fatal("variable_set_name_cstr: failed to create name");
        return -1;
    }

    string_destroy(variable->name);
    variable->name = new_name;
    return 0;
}

int variable_set_value_cstr(Variable *variable, const char *value)
{
    return_val_if_null(variable, -1);
    return_val_if_null(value, -1);

    if (variable->read_only) {
        log_fatal("variable_set_value_cstr: cannot modify read-only variable");
        return -1;
    }

    String *new_value = string_create_from_cstr(value);
    if (!new_value) {
        log_fatal("variable_set_value_cstr: failed to create value");
        return -1;
    }

    string_destroy(variable->value);
    variable->value = new_value;
    return 0;
}

int variable_set_exported(Variable *variable, bool exported)
{
    return_val_if_null(variable, -1);
    variable->exported = exported;
    return 0;
}

int variable_set_read_only(Variable *variable, bool read_only)
{
    return_val_if_null(variable, -1);
    variable->read_only = read_only;
    return 0;
}

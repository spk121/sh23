#ifndef VARIABLE_H
#define VARIABLE_H

#include <stdbool.h>
#include "string_t.h"
#include "logging.h"

typedef struct variable_t {
    string_t *name;
    string_t *value;
    bool exported;
    bool read_only;
} variable_t;

// Constructors
variable_t *variable_create(const string_t *name, const string_t *value, bool exported, bool read_only);
variable_t *variable_create_from_cstr(const char *name, const char *value, bool exported, bool read_only);

// Destructor
void variable_destroy(variable_t *variable);

// Getters
const string_t *variable_get_name(const variable_t *variable);
const string_t *variable_get_value(const variable_t *variable);
const char *variable_get_name_cstr(const variable_t *variable);
const char *variable_get_value_cstr(const variable_t *variable);
bool variable_is_exported(const variable_t *variable);
bool variable_is_read_only(const variable_t *variable);

// Setters
int variable_set_name(variable_t *variable, const string_t *name);
int variable_set_value(variable_t *variable, const string_t *value);
int variable_set_name_cstr(variable_t *variable, const char *name);
int variable_set_value_cstr(variable_t *variable, const char *value);
int variable_set_exported(variable_t *variable, bool exported);
int variable_set_read_only(variable_t *variable, bool read_only);

#endif

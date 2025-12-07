#ifndef VARIABLE_H
#define VARIABLE_H

#include <stdbool.h>
#include "string.h"
#include "logging.h"

typedef struct Variable Variable;

// Constructors
Variable *variable_create(const String *name, const String *value, bool exported, bool read_only);
Variable *variable_create_from_cstr(const char *name, const char *value, bool exported, bool read_only);

// Destructor
void variable_destroy(Variable *variable);

// Getters
const String *variable_get_name(const Variable *variable);
const String *variable_get_value(const Variable *variable);
const char *variable_get_name_cstr(const Variable *variable);
const char *variable_get_value_cstr(const Variable *variable);
bool variable_is_exported(const Variable *variable);
bool variable_is_read_only(const Variable *variable);

// Setters
int variable_set_name(Variable *variable, const String *name);
int variable_set_value(Variable *variable, const String *value);
int variable_set_name_cstr(Variable *variable, const char *name);
int variable_set_value_cstr(Variable *variable, const char *value);
int variable_set_exported(Variable *variable, bool exported);
int variable_set_read_only(Variable *variable, bool read_only);

#endif

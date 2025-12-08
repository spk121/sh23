#ifndef ALIAS_H
#define ALIAS_H

#include "logging.h"
#include "string_t.h"

typedef struct alias_t alias_t;

// Constructors
alias_t *alias_create(const String *name, const String *value);
alias_t *alias_create_from_cstr(const char *name, const char *value);

// Destructor
void alias_destroy(alias_t *alias);

// Getters
const String *alias_get_name(const alias_t *alias);
const String *alias_get_value(const alias_t *alias);
const char *alias_get_name_cstr(const alias_t *alias);
const char *alias_get_value_cstr(const alias_t *alias);

// Setters
void alias_set_name(alias_t *alias, const String *name);
void alias_set_value(alias_t *alias, const String *value);
void alias_set_name_cstr(alias_t *alias, const char *name);
void alias_set_value_cstr(alias_t *alias, const char *value);

#endif

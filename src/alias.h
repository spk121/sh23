#ifndef ALIAS_H
#define ALIAS_H

#ifndef ALIAS_STORE_INTERNAL
#error                                                                                             \
    "alias.h is an internal header. Only alias_store.c, alias.c, and alias_array.c may include it."
#endif

#include "logging.h"
#include "string_t.h"

struct alias_t
{
    string_t *name;
    string_t *value;
};

typedef struct alias_t alias_t;

// Constructors
alias_t *alias_create(const string_t *name, const string_t *value);
alias_t *alias_create_from_cstr(const char *name, const char *value);

// Destructor
void alias_destroy(alias_t **alias);

// Getters
const string_t *alias_get_name(const alias_t *alias);
const string_t *alias_get_value(const alias_t *alias);
const char *alias_get_name_cstr(const alias_t *alias);
const char *alias_get_value_cstr(const alias_t *alias);

// Setters
void alias_set_name(alias_t *alias, const string_t *name);
void alias_set_value(alias_t *alias, const string_t *value);
void alias_set_name_cstr(alias_t *alias, const char *name);
void alias_set_value_cstr(alias_t *alias, const char *value);

#endif

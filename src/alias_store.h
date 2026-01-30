#ifndef ALIAS_STORE_H
#define ALIAS_STORE_H

#include "alias.h"
#include "string_t.h"
#include "alias_array.h"
#include <stdbool.h>

struct alias_store_t
{
    alias_array_t *aliases;
};

typedef struct alias_store_t alias_store_t;

// alias_t name validation
bool alias_name_is_valid(const char *name);

// Constructors
alias_store_t *alias_store_create(void);
alias_store_t *alias_store_create_with_capacity(int capacity);
alias_store_t *alias_store_clone(const alias_store_t *other);

// Destructor
void alias_store_destroy(alias_store_t **store);

// Add name/value pairs
void alias_store_add(alias_store_t *store, const string_t *name, const string_t *value);
void alias_store_add_cstr(alias_store_t *store, const char *name, const char *value);

// Remove by name
bool alias_store_remove(alias_store_t *store, const string_t *name);
bool alias_store_remove_cstr(alias_store_t *store, const char *name);

// Clear all entries
void alias_store_clear(alias_store_t *store);

// Get size
int alias_store_size(const alias_store_t *store);

// Check if name is defined
bool alias_store_has_name(const alias_store_t *store, const string_t *name);
bool alias_store_has_name_cstr(const alias_store_t *store, const char *name);

// Get value by name
const string_t *alias_store_get_value(const alias_store_t *store, const string_t *name);
const char *alias_store_get_value_cstr(const alias_store_t *store, const char *name);

#endif

#ifndef ALIAS_STORE_H
#define ALIAS_STORE_H

#include "alias.h"
#include "string.h"

typedef struct AliasStore AliasStore;

// Constructors
AliasStore *alias_store_create(void);
AliasStore *alias_store_create_with_capacity(size_t capacity);

// Destructor
void alias_store_destroy(AliasStore *store);

// Add name/value pairs
int alias_store_add(AliasStore *store, const String *name, const String *value);
int alias_store_add_cstr(AliasStore *store, const char *name, const char *value);

// Remove by name
int alias_store_remove(AliasStore *store, const String *name);
int alias_store_remove_cstr(AliasStore *store, const char *name);

// Clear all entries
int alias_store_clear(AliasStore *store);

// Get size
size_t alias_store_size(const AliasStore *store);

// Check if name is defined
int alias_store_has_name(const AliasStore *store, const String *name);
int alias_store_has_name_cstr(const AliasStore *store, const char *name);

// Get value by name
const String *alias_store_get_value(const AliasStore *store, const String *name);
const char *alias_store_get_value_cstr(const AliasStore *store, const char *name);

#endif

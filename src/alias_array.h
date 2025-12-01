#ifndef ALIAS_ARRAY_H
#define ALIAS_ARRAY_H

#include <stddef.h>
#include "alias.h"
/* INCLUDE2 */

typedef struct AliasArray AliasArray;

// Function pointer types
typedef void (*AliasArrayFreeFunc)(Alias * element);
typedef void (*AliasArrayApplyFunc)(Alias * element, void *user_data);
typedef int (*AliasArrayCompareFunc)(const Alias * element, const void *user_data);

// Create and destroy
AliasArray *alias_array_create(void);
AliasArray *alias_array_create_with_free(AliasArrayFreeFunc free_func);
void alias_array_destroy(AliasArray *array);

// Accessors
size_t alias_array_size(const AliasArray *array);
size_t alias_array_capacity(const AliasArray *array);
Alias * alias_array_get(const AliasArray *array, size_t index);
int alias_array_is_empty(const AliasArray *array);

// Modification
int alias_array_append(AliasArray *array, Alias * element);
int alias_array_set(AliasArray *array, size_t index, Alias * element);
int alias_array_remove(AliasArray *array, size_t index);
int alias_array_clear(AliasArray *array);
int alias_array_resize(AliasArray *array, size_t new_capacity);

// Operations
void alias_array_foreach(AliasArray *array, AliasArrayApplyFunc apply_func, void *user_data);
int alias_array_find(AliasArray *array, Alias * element, size_t *index);
int alias_array_find_with_compare(AliasArray *array, const void *data, AliasArrayCompareFunc compare_func, size_t *index);

#endif

#ifndef ALIAS_ARRAY_H
#define ALIAS_ARRAY_H

#include "alias.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct alias_array_t alias_array_t;

// Function pointer types
typedef void (*alias_array_free_func_t)(alias_t *element);
typedef void (*alias_array_tApplyFunc)(alias_t *element, void *user_data);
typedef int (*alias_array_tCompareFunc)(const alias_t *element, const void *user_data);

// Create and destroy
alias_array_t *alias_array_create(void);
alias_array_t *alias_array_create_with_free(alias_array_free_func_t free_func);
void alias_array_destroy(alias_array_t *array);

// Accessors
size_t alias_array_size(const alias_array_t *array);
size_t alias_array_capacity(const alias_array_t *array);
alias_t *alias_array_get(const alias_array_t *array, size_t index);
bool alias_array_is_empty(const alias_array_t *array);

// Modification
void alias_array_append(alias_array_t *array, alias_t *element);
void alias_array_set(alias_array_t *array, size_t index, alias_t *element);
void alias_array_remove(alias_array_t *array, size_t index);
void alias_array_clear(alias_array_t *array);
void alias_array_resize(alias_array_t *array, size_t new_capacity);

// Operations
void alias_array_foreach(alias_array_t *array, alias_array_tApplyFunc apply_func, void *user_data);
int alias_array_find(alias_array_t *array, alias_t *element, size_t *index);
int alias_array_find_with_compare(alias_array_t *array, const void *data, alias_array_tCompareFunc compare_func,
                                  size_t *index);

#endif

#ifndef VARIABLE_ARRAY_H
#define VARIABLE_ARRAY_H

#include "variable.h"
#include <stddef.h>

// Optional element free function signature
typedef void (*variable_array_free_func_t)(variable_t **element);

// Type-specific dynamic array of variable_t*
typedef struct variable_array_t
{
    variable_t **data;
    size_t len;
    size_t cap;
    variable_array_free_func_t free_func;
} variable_array_t;

// Apply/compare helpers (optional; useful for iteration and search)
typedef void (*variable_array_apply_func_t)(variable_t *element, void *user_data);
typedef int (*variable_array_compare_func_t)(const variable_t *element, const void *user_data);

// Create/destroy
variable_array_t *variable_array_create(void);
variable_array_t *variable_array_create_with_free(variable_array_free_func_t free_func);
void variable_array_destroy(variable_array_t **array);

// Accessors
size_t variable_array_size(const variable_array_t *array);
size_t variable_array_capacity(const variable_array_t *array);
variable_t *variable_array_get(const variable_array_t *array, size_t index);
int variable_array_is_empty(const variable_array_t *array);

// Modification
void variable_array_append(variable_array_t *array, variable_t *element);
void variable_array_set(variable_array_t *array, size_t index, variable_t *element);
void variable_array_remove(variable_array_t *array,
                           size_t index);           // Compacts; frees element if free_func set
void variable_array_clear(variable_array_t *array); // Frees all if free_func set; len -> 0
void variable_array_resize(variable_array_t *array, size_t new_capacity);

// Operations
void variable_array_foreach(variable_array_t *array, variable_array_apply_func_t apply_func,
                            void *user_data);
int variable_array_find(variable_array_t *array, variable_t *element, size_t *index);
int variable_array_find_with_compare(variable_array_t *array, const void *data,
                                     variable_array_compare_func_t compare_func, size_t *index);

#endif

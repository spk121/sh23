#ifndef VARIABLE_ARRAY_H
#define VARIABLE_ARRAY_H

#include <stddef.h>
#include "variable.h"

// Type-specific dynamic array of Variable*
typedef struct VariableArray VariableArray;

// Optional element free function signature
typedef void (*VariableArrayFreeFunc)(Variable *element);

// Apply/compare helpers (optional; useful for iteration and search)
typedef void (*VariableArrayApplyFunc)(Variable *element, void *user_data);
typedef int  (*VariableArrayCompareFunc)(const Variable *element, const void *user_data);

// Create/destroy
VariableArray *variable_array_create(void);
VariableArray *variable_array_create_with_free(VariableArrayFreeFunc free_func);
void variable_array_destroy(VariableArray *array);

// Accessors
size_t   variable_array_size(const VariableArray *array);
size_t   variable_array_capacity(const VariableArray *array);
Variable *variable_array_get(const VariableArray *array, size_t index);
int      variable_array_is_empty(const VariableArray *array);

// Modification
int variable_array_append(VariableArray *array, Variable *element);
int variable_array_set(VariableArray *array, size_t index, Variable *element);
int variable_array_remove(VariableArray *array, size_t index);     // Compacts; frees element if free_func set
int variable_array_clear(VariableArray *array);                     // Frees all if free_func set; len -> 0
int variable_array_resize(VariableArray *array, size_t new_capacity);

// Operations
void variable_array_foreach(VariableArray *array, VariableArrayApplyFunc apply_func, void *user_data);
int  variable_array_find(VariableArray *array, Variable *element, size_t *index);
int  variable_array_find_with_compare(VariableArray *array, const void *data, VariableArrayCompareFunc compare_func, size_t *index);

#endif

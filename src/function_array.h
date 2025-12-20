#ifndef FUNCTION_ARRAY_H
#define FUNCTION_ARRAY_H

#include <stddef.h>

// Forward declaration to avoid circular includes
typedef struct function_t function_t;

// Optional element free function signature
typedef void (*function_array_free_func_t)(function_t **element);

// Type-specific dynamic array of function_t*
typedef struct function_array_t
{
    function_t **data;
    size_t len;
    size_t cap;
    function_array_free_func_t free_func;
} function_array_t;

// Apply/compare helpers (optional; useful for iteration and search)
typedef void (*function_array_apply_func_t)(function_t *element, void *user_data);
typedef int (*function_array_compare_func_t)(const function_t *element, const void *user_data);

// Create/destroy
function_array_t *function_array_create(void);
function_array_t *function_array_create_with_free(function_array_free_func_t free_func);
void function_array_destroy(function_array_t **array);

// Accessors
size_t function_array_size(const function_array_t *array);
size_t function_array_capacity(const function_array_t *array);
function_t *function_array_get(const function_array_t *array, size_t index);
int function_array_is_empty(const function_array_t *array);

// Modification
void function_array_append(function_array_t *array, function_t *element);
void function_array_set(function_array_t *array, size_t index, function_t *element);
void function_array_remove(function_array_t *array,
                           size_t index);           // Compacts tail; frees element if free_func set
void function_array_clear(function_array_t *array); // Frees all if free_func set; len -> 0
void function_array_resize(function_array_t *array, size_t new_capacity);

// Operations
void function_array_foreach(function_array_t *array, function_array_apply_func_t apply_func,
                            void *user_data);
int function_array_find(function_array_t *array, function_t *element, size_t *index);
int function_array_find_with_compare(function_array_t *array, const void *data,
                                     function_array_compare_func_t compare_func, size_t *index);

#endif

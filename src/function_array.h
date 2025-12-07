#ifndef FUNCTION_ARRAY_H
#define FUNCTION_ARRAY_H

#include <stddef.h>

// Forward declaration to avoid circular includes
typedef struct Function Function;

// Type-specific dynamic array of Function*
typedef struct FunctionArray FunctionArray;

// Optional element free function signature
typedef void (*FunctionArrayFreeFunc)(Function *element);
// Apply/compare helpers (optional; useful for iteration and search)
typedef void (*FunctionArrayApplyFunc)(Function *element, void *user_data);
typedef int  (*FunctionArrayCompareFunc)(const Function *element, const void *user_data);

// Create/destroy
FunctionArray *function_array_create(void);
FunctionArray *function_array_create_with_free(FunctionArrayFreeFunc free_func);
void function_array_destroy(FunctionArray *array);

// Accessors
size_t   function_array_size(const FunctionArray *array);
size_t   function_array_capacity(const FunctionArray *array);
Function *function_array_get(const FunctionArray *array, size_t index);
int      function_array_is_empty(const FunctionArray *array);

// Modification
int function_array_append(FunctionArray *array, Function *element);
int function_array_set(FunctionArray *array, size_t index, Function *element);
int function_array_remove(FunctionArray *array, size_t index);     // Compacts tail; frees element if free_func set
int function_array_clear(FunctionArray *array);                     // Frees all if free_func set; len -> 0
int function_array_resize(FunctionArray *array, size_t new_capacity);

// Operations
void function_array_foreach(FunctionArray *array, FunctionArrayApplyFunc apply_func, void *user_data);
int  function_array_find(FunctionArray *array, Function *element, size_t *index);
int  function_array_find_with_compare(FunctionArray *array, const void *data, FunctionArrayCompareFunc compare_func, size_t *index);

#endif

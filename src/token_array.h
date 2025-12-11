#ifndef TOKEN_ARRAY_H
#define TOKEN_ARRAY_H

#include "token_wip.h"
#include <stddef.h>

typedef struct token_array_t
{
    token_t **data;               // array of Token pointers
    size_t size;                  // current number of elements
    size_t capacity;              // allocated capacity
    TokenArrayFreeFunc free_func; // optional function to free elements
} token_array_t;

// Function pointer types
typedef void (*TokenArrayFreeFunc)(token_t *element);
typedef void (*TokenArrayApplyFunc)(token_t *element, void *user_data);
typedef int (*TokenArrayCompareFunc)(const token_t *element, const void *user_data);

// Create and destroy
token_array_t *token_array_create(void);
token_array_t *token_array_create_with_free(TokenArrayFreeFunc free_func);
void token_array_destroy(token_array_t **array);

// Accessors
size_t token_array_size(const token_array_t *array);
size_t token_array_capacity(const token_array_t *array);
token_t *token_array_get(const token_array_t *array, size_t index);
int token_array_is_empty(const token_array_t *array);

// Modification
int token_array_append(token_array_t *array, token_t *element);
int token_array_set(token_array_t *array, size_t index, token_t *element);
int token_array_remove(token_array_t *array, size_t index);
int token_array_clear(token_array_t *array);
int token_array_resize(token_array_t *array, size_t new_capacity);

// Operations
void token_array_foreach(token_array_t *array, TokenArrayApplyFunc apply_func, void *user_data);
int token_array_find(token_array_t *array, token_t *element, size_t *index);
int token_array_find_with_compare(token_array_t *array, const void *data,
                                  TokenArrayCompareFunc compare_func, size_t *index);

#endif

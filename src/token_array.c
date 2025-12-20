#include "token_array.h"
#include "logging.h"
#include "token_wip.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 16
#define GROW_FACTOR 2

// Helper: Ensure capacity
static int token_array_ensure_capacity(token_array_t *array, size_t needed)
{
    return_val_if_null(array, -1);
    return_val_if_lt(needed, 0, -1);

    if (needed <= array->capacity)
        return 0;

    size_t new_capacity = array->capacity ? array->capacity : INITIAL_CAPACITY;
    while (new_capacity < needed)
        new_capacity *= GROW_FACTOR;

    token_t **new_data = realloc(array->data, new_capacity * sizeof(token_t *));
    if (!new_data)
    {
        log_fatal("token_array_ensure_capacity: memory allocation failure");
        return -1;
    }

    array->data = new_data;
    array->capacity = new_capacity;
    return 0;
}

// Create and destroy
token_array_t *token_array_create(void)
{
    return token_array_create_with_free(NULL);
}

token_array_t *token_array_create_with_free(token_array_tFreeFunc free_func)
{
    token_array_t *array = malloc(sizeof(token_array_t));
    if (!array)
    {
        log_fatal("token_array_create_with_free: out of memory");
        return NULL;
    }

    array->data = NULL;
    array->size = 0;
    array->capacity = 0;
    array->free_func = free_func;

    if (token_array_ensure_capacity(array, INITIAL_CAPACITY) != 0)
    {
        free(array);
        log_fatal("token_array_create_with_free: out of memory");
        return NULL;
    }

    return array;
}

void token_array_destroy(token_array_t **array)
{
    if (!array)
        return;
    token_array_t *a = *array;

    if (a)
    {
        log_debug("token_array_destroy: freeing array %p, size %zu", a, a->size);
        if (a->free_func)
        {
            for (size_t i = 0; i < a->size; i++)
            {
                if (a->data[i])
                {
                    a->free_func(&a->data[i]);
                }
            }
        }
        free(a->data);
        free(a);
        *array = NULL;
    }
}

// Accessors
size_t token_array_size(const token_array_t *array)
{
    return_val_if_null(array, 0);
    return array->size;
}

size_t token_array_capacity(const token_array_t *array)
{
    return_val_if_null(array, 0);
    return array->capacity;
}

token_t *token_array_get(const token_array_t *array, size_t index)
{
    return_val_if_null(array, NULL);
    return_val_if_ge(index, array->size, NULL);
    return array->data[index];
}

int token_array_is_empty(const token_array_t *array)
{
    return_val_if_null(array, 1);
    return array->size == 0;
}

// Modification
int token_array_append(token_array_t *array, token_t *element)
{
    return_val_if_null(array, -1);
    if (token_array_ensure_capacity(array, array->size + 1) != 0)
    {
        log_fatal("token_array_append: out of memory");
        return -1;
    }

    array->data[array->size] = element;
    array->size++;
    return 0;
}

int token_array_set(token_array_t *array, size_t index, token_t *element)
{
    return_val_if_null(array, -1);
    return_val_if_ge(index, array->size, -1);

    if (array->free_func && array->data[index])
    {
        array->free_func(&array->data[index]);
    }
    array->data[index] = element;
    return 0;
}

int token_array_remove(token_array_t *array, size_t index)
{
    return_val_if_null(array, -1);
    return_val_if_ge(index, array->size, -1);

    if (array->free_func && array->data[index])
    {
        array->free_func(&array->data[index]);
    }

    // Shift elements to fill the gap
    for (size_t i = index; i < array->size - 1; i++)
    {
        array->data[i] = array->data[i + 1];
    }
    array->size--;
    array->data[array->size] = NULL; // Clear the last slot
    return 0;
}

int token_array_clear(token_array_t *array)
{
    return_val_if_null(array, -1);

    if (array->free_func)
    {
        for (size_t i = 0; i < array->size; i++)
        {
            if (array->data[i])
            {
                array->free_func(&array->data[i]);
            }
        }
    }
    array->size = 0;
    // Keep capacity and data allocated, just clear pointers
    for (size_t i = 0; i < array->capacity; i++)
    {
        array->data[i] = NULL;
    }
    return 0;
}

int token_array_resize(token_array_t *array, size_t new_capacity)
{
    return_val_if_null(array, -1);
    return_val_if_lt(new_capacity, 0, -1);

    if (new_capacity < array->size)
    {
        // Free elements that won't fit in the new capacity
        if (array->free_func)
        {
            for (size_t i = new_capacity; i < array->size; i++)
            {
                if (array->data[i])
                {
                    array->free_func(&array->data[i]);
                }
            }
        }
        array->size = new_capacity;
    }

    if (token_array_ensure_capacity(array, new_capacity) != 0)
    {
        log_fatal("token_array_resize: out of memory");
        return -1;
    }

    // Clear any newly allocated slots
    for (size_t i = array->capacity; i < new_capacity; i++)
    {
        array->data[i] = NULL;
    }
    array->capacity = new_capacity;
    return 0;
}

// Operations
void token_array_foreach(token_array_t *array, token_array_tApplyFunc apply_func, void *user_data)
{
    if (!array || !apply_func)
    {
        log_fatal("token_array_foreach: argument 'array' or 'apply_func' is null");
        return;
    }

    for (size_t i = 0; i < array->size; i++)
    {
        apply_func(array->data[i], user_data);
    }
}

int token_array_find(token_array_t *array, token_t *element, size_t *index)
{
    return_val_if_null(array, -1);
    return_val_if_null(index, -1);

    for (size_t i = 0; i < array->size; i++)
    {
        if (array->data[i] == element)
        {
            *index = i;
            return 0;
        }
    }
    return -1;
}

int token_array_find_with_compare(token_array_t *array, const void *data,
                                  token_array_tCompareFunc compare_func, size_t *index)
{
    return_val_if_null(array, -1);
    return_val_if_null(data, -1);
    return_val_if_null(compare_func, -1);
    return_val_if_null(index, -1);

    for (size_t i = 0; i < array->size; i++)
    {
        if (compare_func(array->data[i], data) == 0)
        {
            *index = i;
            return 0;
        }
    }
    return -1;
}

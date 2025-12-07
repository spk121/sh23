#include "alias_array.h"
#include "alias.h"
#include "logging.h"
#include "xalloc.h"
#include <string.h>

#define INITIAL_CAPACITY 16
#define GROW_FACTOR 2

struct AliasArray
{
    Alias **data;
    size_t size;
    size_t capacity;
    AliasArrayFreeFunc free_func;
};

// Helper: Ensure capacity
static void alias_array_ensure_capacity(AliasArray *array, size_t needed)
{
    Expects_not_null(array);

    if (needed <= array->capacity)
        return;

    size_t new_capacity = array->capacity ? array->capacity : INITIAL_CAPACITY;
    while (new_capacity < needed)
        new_capacity *= GROW_FACTOR;

    Alias **new_data = xrealloc(array->data, new_capacity * sizeof(Alias *));
    array->data = new_data;
    array->capacity = new_capacity;
}

// Create and destroy
AliasArray *alias_array_create(void)
{
    return alias_array_create_with_free(NULL);
}

AliasArray *alias_array_create_with_free(AliasArrayFreeFunc free_func)
{
    AliasArray *array = xmalloc(sizeof(AliasArray));

    array->data = xcalloc(INITIAL_CAPACITY, sizeof(Alias *));
    array->size = 0;
    array->capacity = INITIAL_CAPACITY;
    array->free_func = free_func;

    return array;
}

void alias_array_destroy(AliasArray *array)
{
    Expects_not_null(array);

    log_debug("alias_array_destroy: freeing array %p, size %zu", array, array->size);
    if (array->free_func)
    {
        for (size_t i = 0; i < array->size; i++)
        {
            if (array->data[i])
            {
                array->free_func(array->data[i]);
            }
        }
    }
    xfree(array->data);
    xfree(array);
}

// Accessors
size_t alias_array_size(const AliasArray *array)
{
    Expects_not_null(array);
    return array->size;
}

size_t alias_array_capacity(const AliasArray *array)
{
    Expects_not_null(array);
    return array->capacity;
}

Alias *alias_array_get(const AliasArray *array, size_t index)
{
    Expects_not_null(array);
    Expects(index < array->size);
    return array->data[index];
}

bool alias_array_is_empty(const AliasArray *array)
{
    Expects_not_null(array);
    return array->size == 0;
}

// Modification
void alias_array_append(AliasArray *array, Alias *element)
{
    Expects_not_null(array);
    alias_array_ensure_capacity(array, array->size + 1);

    array->data[array->size] = element;
    array->size++;
}

void alias_array_set(AliasArray *array, size_t index, Alias *element)
{
    Expects_not_null(array);
    Expects(index < array->size);

    if (array->free_func && array->data[index])
    {
        array->free_func(array->data[index]);
    }
    array->data[index] = element;
}

void alias_array_remove(AliasArray *array, size_t index)
{
    Expects_not_null(array);
    Expects(index < array->size);

    if (array->free_func && array->data[index])
    {
        array->free_func(array->data[index]);
    }

    // Shift elements to fill the gap
    for (size_t i = index; i < array->size - 1; i++)
    {
        array->data[i] = array->data[i + 1];
    }
    array->size--;
    array->data[array->size] = NULL; // Clear the last slot
}

void alias_array_clear(AliasArray *array)
{
    Expects_not_null(array);

    if (array->free_func)
    {
        for (size_t i = 0; i < array->size; i++)
        {
            if (array->data[i])
            {
                array->free_func(array->data[i]);
            }
        }
    }
    array->size = 0;
    // Keep capacity and data allocated, just clear pointers
    for (size_t i = 0; i < array->capacity; i++)
    {
        array->data[i] = NULL;
    }
}

void alias_array_resize(AliasArray *array, size_t new_capacity)
{
    Expects_not_null(array);

    if (new_capacity < array->size)
    {
        // Free elements that won't fit in the new capacity
        if (array->free_func)
        {
            for (size_t i = new_capacity; i < array->size; i++)
            {
                if (array->data[i])
                {
                    array->free_func(array->data[i]);
                }
            }
        }
        array->size = new_capacity;
    }

    alias_array_ensure_capacity(array, new_capacity);

    // Clear any newly allocated slots
    for (size_t i = array->capacity; i < new_capacity; i++)
    {
        array->data[i] = NULL;
    }
    array->capacity = new_capacity;
}

// Operations
void alias_array_foreach(AliasArray *array, AliasArrayApplyFunc apply_func, void *user_data)
{
    Expects_not_null(array);
    Expects_not_null(apply_func);

    for (size_t i = 0; i < array->size; i++)
    {
        apply_func(array->data[i], user_data);
    }
}

int alias_array_find(AliasArray *array, Alias *element, size_t *index)
{
    Expects_not_null(array);
    Expects_not_null(index);

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

int alias_array_find_with_compare(AliasArray *array, const void *data, AliasArrayCompareFunc compare_func,
                                  size_t *index)
{
    Expects_not_null(array);
    Expects_not_null(data);
    Expects_not_null(compare_func);
    Expects_not_null(index);

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

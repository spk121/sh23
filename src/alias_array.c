#include "alias_array.h"
#include "alias.h"
#include "logging.h"
#include "xalloc.h"
#include <string.h>

#define INITIAL_CAPACITY 16
#define GROW_FACTOR 2

// Helper: Ensure capacity
static void alias_array_ensure_capacity(alias_array_t *array, int needed)
{
    Expects_not_null(array);
    Expects_ge(needed, 0);

    if (needed <= array->capacity)
        return;

    int new_capacity = array->capacity ? array->capacity : INITIAL_CAPACITY;
    while (new_capacity < needed)
        new_capacity *= GROW_FACTOR;

    alias_t **new_data = xrealloc(array->data, new_capacity * sizeof(alias_t *));
    array->data = new_data;
    array->capacity = new_capacity;
}

// Create and destroy
alias_array_t *alias_array_create(void)
{
    alias_array_t *array = xmalloc(sizeof(alias_array_t));

    array->data = xcalloc(INITIAL_CAPACITY, sizeof(alias_t *));
    array->size = 0;
    array->capacity = INITIAL_CAPACITY;

    return array;
}

void alias_array_destroy(alias_array_t **array)
{
    Expects_not_null(array);
    Expects_not_null(*array);
    alias_array_t *a = *array;

    log_debug("alias_array_destroy: freeing array %p, size %zu", a, a->size);
    for (int i = (int)a->size - 1; i >= 0; i--)
    {
        if (a->data[i])
        {
            alias_destroy(&(a->data[i]));
        }
    }
    xfree(a->data);
    xfree(a);
    *array = NULL;
}

// Accessors
int alias_array_size(const alias_array_t *array)
{
    Expects_not_null(array);
    return array->size;
}

int alias_array_capacity(const alias_array_t *array)
{
    Expects_not_null(array);
    return array->capacity;
}

alias_t *alias_array_get(const alias_array_t *array, int index)
{
    Expects_not_null(array);
    Expects_not_null(array->data);
    Expects_ge(index, 0);
    Expects_lt(index, array->size);

    return array->data[index];
}

bool alias_array_is_empty(const alias_array_t *array)
{
    Expects_not_null(array);
    return array->size == 0;
}

// Modification
void alias_array_append(alias_array_t *array, alias_t *element)
{
    Expects_not_null(array);
    alias_array_ensure_capacity(array, array->size + 1);

    array->data[array->size] = element;
    array->size++;
}

void alias_array_set(alias_array_t *array, int index, alias_t *element)
{
    Expects_not_null(array);
    Expects_ge(index, 0);
    Expects_lt(index, array->size);
    Expects_not_null(array->data);
    Expects_not_null(element);

    alias_destroy(&array->data[index]);
    array->data[index] = element;
}

void alias_array_remove(alias_array_t *array, int index)
{
    Expects_not_null(array);
    Expects_ge(index, 0);
    Expects_lt(index, array->size);
    Expects_not_null(array->data);

    alias_destroy(&array->data[index]);

    // Shift elements to fill the gap
    for (int i = index; i < array->size - 1; i++)
    {
        array->data[i] = array->data[i + 1];
    }
    array->size--;
    array->data[array->size] = NULL; // Clear the last slot
}

void alias_array_clear(alias_array_t *array)
{
    Expects_not_null(array);
    Expects_not_null(array->data);

    for (int i = 0; i < array->size; i++)
    {
        if (array->data[i])
        {
            alias_destroy(&array->data[i]);
        }
    }
    array->size = 0;
    // Keep capacity and data allocated, just clear pointers
    for (int i = 0; i < array->capacity; i++)
    {
        array->data[i] = NULL;
    }
}

void alias_array_resize(alias_array_t *array, int new_capacity)
{
    Expects_not_null(array);
    Expects_not_null(array->data);
    Expects_ge(new_capacity, 0);

    if (new_capacity < array->size)
    {
        // Free elements that won't fit in the new capacity
        for (int i = new_capacity; i < array->size; i++)
        {
            if (array->data[i])
            {
                alias_destroy(&array->data[i]);
            }
        }
        array->size = new_capacity;
    }

    alias_array_ensure_capacity(array, new_capacity);

    // Clear any newly allocated slots
    for (int i = array->capacity; i < new_capacity; i++)
    {
        array->data[i] = NULL;
    }
    array->capacity = new_capacity;
}

// Operations
void alias_array_foreach(alias_array_t *array, alias_array_apply_func_t apply_func, void *user_data)
{
    Expects_not_null(array);
    Expects_not_null(apply_func);

    for (int i = 0; i < array->size; i++)
    {
        apply_func(array->data[i], user_data);
    }
}

int alias_array_find(alias_array_t *array, alias_t *element, int *index)
{
    Expects_not_null(array);
    Expects_not_null(array->data);
    Expects_not_null(element);
    Expects_not_null(index);

    for (int i = 0; i < array->size; i++)
    {
        if (array->data[i] == element)
        {
            *index = i;
            return 0;
        }
    }
    return -1;
}

int alias_array_find_with_compare(alias_array_t *array, const void *data, alias_array_compare_func_t compare_func,
                                  int *index)
{
    Expects_not_null(array);
    Expects_not_null(array->data);
    Expects_not_null(compare_func);
    Expects_not_null(index);

    for (int i = 0; i < array->size; i++)
    {
        if (compare_func(array->data[i], data) == 0)
        {
            *index = i;
            return 0;
        }
    }
    return -1;
}

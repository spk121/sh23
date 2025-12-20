#include "function_array.h"
#include "logging.h"
#include "xalloc.h"
#include <string.h>

static size_t grow_cap(size_t cap)
{
    return cap ? cap * 2 : 8;
}

function_array_t *function_array_create(void)
{
    function_array_t *a = xcalloc(1, sizeof *a);
    return a;
}
function_array_t *function_array_create_with_free(function_array_free_func_t free_func)
{
    function_array_t *a = function_array_create();
    if (a)
        a->free_func = free_func;
    return a;
}

void function_array_destroy(function_array_t **array)
{
    if (!array)
        return;
    function_array_t *a = *array;

    if (!a)
        return;
    if (a->free_func)
    {
        for (size_t i = 0; i < a->len; ++i)
        {
            if (a->data[i])
                a->free_func(&a->data[i]);
        }
    }
    xfree(a->data);
    xfree(a);
    *array = NULL;
}

size_t function_array_size(const function_array_t *array)
{
    Expects_not_null(array);
    return array->len;
}

size_t function_array_capacity(const function_array_t *array)
{
    Expects_not_null(array);
    return array->cap;
}

function_t *function_array_get(const function_array_t *array, size_t index)
{
    Expects_not_null(array);
    Expects_lt(index, array->len);
    return array->data[index];
}

int function_array_is_empty(const function_array_t *array)
{
    Expects_not_null(array);
    return array->len == 0;
}

void function_array_resize(function_array_t *array, size_t new_capacity)
{
    Expects_not_null(array);
    Expects(new_capacity >= array->len);

    function_t **newv;
    if (array->data == NULL)
    {
        // Initial allocation
        newv = xmalloc(new_capacity * sizeof *newv);
    }
    else
    {
        // Resize existing allocation
        newv = xrealloc(array->data, new_capacity * sizeof *newv);
    }

    array->data = newv;
    array->cap = new_capacity;
}

void function_array_append(function_array_t *array, function_t *element)
{
    Expects_not_null(array);
    if (array->len == array->cap)
    {
        function_array_resize(array, grow_cap(array->cap));
    }
    array->data[array->len++] = element;
}

void function_array_set(function_array_t *array, size_t index, function_t *element)
{
    Expects_not_null(array);
    Expects_lt(index, array->len);
    if (array->free_func && array->data[index] && array->data[index] != element)
    {
        array->free_func(&array->data[index]);
    }
    array->data[index] = element;
}

void function_array_remove(function_array_t *array, size_t index)
{
    Expects_not_null(array);
    Expects_lt(index, array->len);
    if (array->free_func && array->data[index])
    {
        array->free_func(&array->data[index]);
    }
    if (index + 1 < array->len)
    {
        memmove(&array->data[index], &array->data[index + 1],
                (array->len - index - 1) * sizeof *array->data);
    }
    array->len--;
}

void function_array_clear(function_array_t *array)
{
    Expects_not_null(array);
    if (array->free_func)
    {
        for (size_t i = 0; i < array->len; ++i)
        {
            if (array->data[i])
                array->free_func(&array->data[i]);
        }
    }
    array->len = 0;
}

void function_array_foreach(function_array_t *array, function_array_apply_func_t apply_func,
                            void *user_data)
{
    Expects_not_null(array);
    Expects_not_null(apply_func);
    for (size_t i = 0; i < array->len; ++i)
    {
        apply_func(array->data[i], user_data);
    }
}

int function_array_find(function_array_t *array, function_t *element, size_t *index)
{
    Expects_not_null(array);
    for (size_t i = 0; i < array->len; ++i)
    {
        if (array->data[i] == element)
        {
            if (index)
                *index = i;
            return 0;
        }
    }
    return 1;
}

int function_array_find_with_compare(function_array_t *array, const void *data,
                                     function_array_compare_func_t compare_func, size_t *index)
{
    Expects_not_null(array);
    Expects_not_null(compare_func);
    for (size_t i = 0; i < array->len; ++i)
    {
        if (compare_func(array->data[i], data) == 0)
        {
            if (index)
                *index = i;
            return 0;
        }
    }
    return 1;
}

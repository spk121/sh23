#include "function_array.h"
#include "xalloc.h"
#include <string.h>

static size_t grow_cap(size_t cap) { return cap ? cap * 2 : 8; }

function_array_t *function_array_create(void) {
    function_array_t *a = xcalloc(1, sizeof *a);
    return a;
}
function_array_t *function_array_create_with_free(function_array_free_func_t free_func) {
    function_array_t *a = function_array_create();
    if (a) a->free_func = free_func;
    return a;
}

void function_array_destroy(function_array_t *array) {
    if (!array) return;
    if (array->free_func) {
        for (size_t i = 0; i < array->len; ++i) {
            if (array->data[i]) array->free_func(array->data[i]);
        }
    }
    xfree(array->data);
    xfree(array);
}

size_t function_array_size(const function_array_t *array) { return array ? array->len : 0; }
size_t function_array_capacity(const function_array_t *array) { return array ? array->cap : 0; }
function_t *function_array_get(const function_array_t *array, size_t index) {
    if (!array || index >= array->len) return NULL;
    return array->data[index];
}
int function_array_is_empty(const function_array_t *array) { return array ? (array->len == 0) : 1; }

int function_array_resize(function_array_t *array, size_t new_capacity) {
    if (!array) return -1;
    if (new_capacity < array->len) return -1;
    function_t **newv = xrealloc(array->data, new_capacity * sizeof *newv);
    if (!newv && new_capacity) return -1;
    array->data = newv;
    array->cap = new_capacity;
    return 0;
}

int function_array_append(function_array_t *array, function_t *element) {
    if (!array) return -1;
    if (array->len == array->cap) {
        if (function_array_resize(array, grow_cap(array->cap)) != 0) return -1;
    }
    array->data[array->len++] = element;
    return 0;
}

int function_array_set(function_array_t *array, size_t index, function_t *element) {
    if (!array || index >= array->len) return -1;
    if (array->free_func && array->data[index] && array->data[index] != element) {
        array->free_func(array->data[index]);
    }
    array->data[index] = element;
    return 0;
}

int function_array_remove(function_array_t *array, size_t index) {
    if (!array || index >= array->len) return -1;
    if (array->free_func && array->data[index]) {
        array->free_func(array->data[index]);
    }
    if (index + 1 < array->len) {
        memmove(&array->data[index], &array->data[index + 1], (array->len - index - 1) * sizeof *array->data);
    }
    array->len--;
    return 0;
}

int function_array_clear(function_array_t *array) {
    if (!array) return -1;
    if (array->free_func) {
        for (size_t i = 0; i < array->len; ++i) {
            if (array->data[i]) array->free_func(array->data[i]);
        }
    }
    array->len = 0;
    return 0;
}

void function_array_foreach(function_array_t *array, function_array_apply_func_t apply_func, void *user_data) {
    if (!array || !apply_func) return;
    for (size_t i = 0; i < array->len; ++i) {
        apply_func(array->data[i], user_data);
    }
}

int function_array_find(function_array_t *array, function_t *element, size_t *index) {
    if (!array) return -1;
    for (size_t i = 0; i < array->len; ++i) {
        if (array->data[i] == element) {
            if (index) *index = i;
            return 0;
        }
    }
    return 1;
}

int function_array_find_with_compare(function_array_t *array, const void *data, function_array_compare_func_t compare_func, size_t *index) {
    if (!array || !compare_func) return -1;
    for (size_t i = 0; i < array->len; ++i) {
        if (compare_func(array->data[i], data) == 0) {
            if (index) *index = i;
            return 0;
        }
    }
    return 1;
}

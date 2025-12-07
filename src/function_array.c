#include "function_array.h"
#include <stdlib.h>
#include <string.h>

struct FunctionArray {
    Function **data;
    size_t len;
    size_t cap;
    FunctionArrayFreeFunc free_func;
};

static void *xrealloc(void *p, size_t n) { void *q = realloc(p, n); return q; }
static size_t grow_cap(size_t cap) { return cap ? cap * 2 : 8; }

FunctionArray *function_array_create(void) {
    FunctionArray *a = calloc(1, sizeof *a);
    return a;
}
FunctionArray *function_array_create_with_free(FunctionArrayFreeFunc free_func) {
    FunctionArray *a = function_array_create();
    if (a) a->free_func = free_func;
    return a;
}

void function_array_destroy(FunctionArray *array) {
    if (!array) return;
    if (array->free_func) {
        for (size_t i = 0; i < array->len; ++i) {
            if (array->data[i]) array->free_func(array->data[i]);
        }
    }
    free(array->data);
    free(array);
}

size_t function_array_size(const FunctionArray *array) { return array ? array->len : 0; }
size_t function_array_capacity(const FunctionArray *array) { return array ? array->cap : 0; }
Function *function_array_get(const FunctionArray *array, size_t index) {
    if (!array || index >= array->len) return NULL;
    return array->data[index];
}
int function_array_is_empty(const FunctionArray *array) { return array ? (array->len == 0) : 1; }

int function_array_resize(FunctionArray *array, size_t new_capacity) {
    if (!array) return -1;
    if (new_capacity < array->len) return -1;
    Function **newv = xrealloc(array->data, new_capacity * sizeof *newv);
    if (!newv && new_capacity) return -1;
    array->data = newv;
    array->cap = new_capacity;
    return 0;
}

int function_array_append(FunctionArray *array, Function *element) {
    if (!array) return -1;
    if (array->len == array->cap) {
        if (function_array_resize(array, grow_cap(array->cap)) != 0) return -1;
    }
    array->data[array->len++] = element;
    return 0;
}

int function_array_set(FunctionArray *array, size_t index, Function *element) {
    if (!array || index >= array->len) return -1;
    if (array->free_func && array->data[index] && array->data[index] != element) {
        array->free_func(array->data[index]);
    }
    array->data[index] = element;
    return 0;
}

int function_array_remove(FunctionArray *array, size_t index) {
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

int function_array_clear(FunctionArray *array) {
    if (!array) return -1;
    if (array->free_func) {
        for (size_t i = 0; i < array->len; ++i) {
            if (array->data[i]) array->free_func(array->data[i]);
        }
    }
    array->len = 0;
    return 0;
}

void function_array_foreach(FunctionArray *array, FunctionArrayApplyFunc apply_func, void *user_data) {
    if (!array || !apply_func) return;
    for (size_t i = 0; i < array->len; ++i) {
        apply_func(array->data[i], user_data);
    }
}

int function_array_find(FunctionArray *array, Function *element, size_t *index) {
    if (!array) return -1;
    for (size_t i = 0; i < array->len; ++i) {
        if (array->data[i] == element) {
            if (index) *index = i;
            return 0;
        }
    }
    return 1;
}

int function_array_find_with_compare(FunctionArray *array, const void *data, FunctionArrayCompareFunc compare_func, size_t *index) {
    if (!array || !compare_func) return -1;
    for (size_t i = 0; i < array->len; ++i) {
        if (compare_func(array->data[i], data) == 0) {
            if (index) *index = i;
            return 0;
        }
    }
    return 1;
}

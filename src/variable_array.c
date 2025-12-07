#include "variable_array.h"
#include <stdlib.h>
#include <string.h>

struct VariableArray {
    Variable **data;
    size_t len;
    size_t cap;
    VariableArrayFreeFunc free_func;
};

static void *xrealloc(void *p, size_t n) { void *q = realloc(p, n); return q; }
static size_t grow_cap(size_t cap) { return cap ? cap * 2 : 8; }

VariableArray *variable_array_create(void) {
    VariableArray *a = (VariableArray *)calloc(1, sizeof *a);
    return a;
}

VariableArray *variable_array_create_with_free(VariableArrayFreeFunc free_func) {
    VariableArray *a = variable_array_create();
    if (a) a->free_func = free_func;
    return a;
}

void variable_array_destroy(VariableArray *array) {
    if (!array) return;
    if (array->free_func) {
        for (size_t i = 0; i < array->len; ++i) {
            if (array->data[i]) array->free_func(array->data[i]);
        }
    }
    free(array->data);
    free(array);
}

size_t variable_array_size(const VariableArray *array) { return array ? array->len : 0; }
size_t variable_array_capacity(const VariableArray *array) { return array ? array->cap : 0; }

Variable *variable_array_get(const VariableArray *array, size_t index) {
    if (!array || index >= array->len) return NULL;
    return array->data[index];
}

int variable_array_is_empty(const VariableArray *array) { return array ? (array->len == 0) : 1; }

int variable_array_resize(VariableArray *array, size_t new_capacity) {
    if (!array) return -1;
    if (new_capacity < array->len) return -1;
    Variable **newv = (Variable **)xrealloc(array->data, new_capacity * sizeof *newv);
    if (!newv && new_capacity) return -1;
    array->data = newv;
    array->cap = new_capacity;
    return 0;
}

int variable_array_append(VariableArray *array, Variable *element) {
    if (!array) return -1;
    if (array->len == array->cap) {
        if (variable_array_resize(array, grow_cap(array->cap)) != 0) return -1;
    }
    array->data[array->len++] = element;
    return 0;
}

int variable_array_set(VariableArray *array, size_t index, Variable *element) {
    if (!array || index >= array->len) return -1;
    if (array->free_func && array->data[index] && array->data[index] != element) {
        array->free_func(array->data[index]);
    }
    array->data[index] = element;
    return 0;
}

int variable_array_remove(VariableArray *array, size_t index) {
    if (!array || index >= array->len) return -1;
    if (array->free_func && array->data[index]) {
        array->free_func(array->data[index]);
    }
    if (index + 1 < array->len) {
        memmove(&array->data[index], &array->data[index + 1],
                (array->len - index - 1) * sizeof *array->data);
    }
    array->len--;
    return 0;
}

int variable_array_clear(VariableArray *array) {
    if (!array) return -1;
    if (array->free_func) {
        for (size_t i = 0; i < array->len; ++i) {
            if (array->data[i]) array->free_func(array->data[i]);
        }
    }
    array->len = 0;
    return 0;
}

void variable_array_foreach(VariableArray *array, VariableArrayApplyFunc apply_func, void *user_data) {
    if (!array || !apply_func) return;
    for (size_t i = 0; i < array->len; ++i) {
        apply_func(array->data[i], user_data);
    }
}

int variable_array_find(VariableArray *array, Variable *element, size_t *index) {
    if (!array) return -1;
    for (size_t i = 0; i < array->len; ++i) {
        if (array->data[i] == element) {
            if (index) *index = i;
            return 0;
        }
    }
    return 1;
}

int variable_array_find_with_compare(VariableArray *array, const void *data, VariableArrayCompareFunc compare_func, size_t *index) {
    if (!array || !compare_func) return -1;
    for (size_t i = 0; i < array->len; ++i) {
        if (compare_func(array->data[i], data) == 0) {
            if (index) *index = i;
            return 0;
        }
    }
    return 1;
}

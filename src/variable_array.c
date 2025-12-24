#include "variable_array.h"
#include "xalloc.h"
#include <string.h>

static size_t grow_cap(size_t cap) { return cap ? cap * 2 : 8; }

variable_array_t *variable_array_create(void) {
    variable_array_t *a = xcalloc(1, sizeof *a);
    return a;
}

variable_array_t *variable_array_create_with_free(variable_array_free_func_t free_func) {
    variable_array_t *a = variable_array_create();
    if (a) a->free_func = free_func;
    return a;
}

void variable_array_destroy(variable_array_t **array) {
    Expects_not_null(array);
    variable_array_t *a = *array;

    if (!a) return;
    if (a->free_func) {
        for (size_t i = 0; i < a->len; ++i) {
            if (a->data[i]) a->free_func(&a->data[i]);
        }
    }
    xfree(a->data);
    xfree(a);
    *array = NULL;
}

size_t variable_array_size(const variable_array_t *array) {
    Expects_not_null(array);
    return array->len;
}

size_t variable_array_capacity(const variable_array_t *array) {
    Expects_not_null(array);
    return array->cap;
}

variable_t *variable_array_get(const variable_array_t *array, size_t index) {
    Expects_not_null(array);
    Expects_lt(index, array->len);

    return array->data[index];
}

int variable_array_is_empty(const variable_array_t *array) {
    Expects_not_null(array);
    return array->len == 0;
}

void variable_array_resize(variable_array_t *array, size_t new_capacity) {
    Expects_not_null(array);
    Expects_ge(new_capacity, array->len);

    variable_t **newv;
    if (array->data == NULL) {
        // Initial allocation
        newv = xmalloc(new_capacity * sizeof *newv);
    } else {
        // Resize existing allocation
        newv = xrealloc(array->data, new_capacity * sizeof *newv);
    }

    array->data = newv;
    array->cap = new_capacity;
}

void variable_array_append(variable_array_t *array, variable_t *element) {
    Expects_not_null(array);
    if (array->len == array->cap) {
        variable_array_resize(array, grow_cap(array->cap));
    }
    array->data[array->len++] = element;
}

void variable_array_set(variable_array_t *array, size_t index, variable_t *element) {
    Expects_not_null(array);
    Expects(index < array->len);
    if (array->free_func && array->data[index] && array->data[index] != element) {
        array->free_func(&array->data[index]);
    }
    array->data[index] = element;
}

void variable_array_remove(variable_array_t *array, size_t index) {
    Expects_not_null(array);
    Expects(index < array->len);
    if (array->free_func && array->data[index]) {
        array->free_func(&array->data[index]);
    }
    if (index + 1 < array->len) {
        memmove(&array->data[index], &array->data[index + 1],
                (array->len - index - 1) * sizeof *array->data);
    }
    array->len--;
}

void variable_array_clear(variable_array_t *array) {
    Expects_not_null(array);
    if (array->free_func) {
        for (size_t i = 0; i < array->len; ++i) {
            if (array->data[i]) array->free_func(&array->data[i]);
        }
    }
    array->len = 0;
}

void variable_array_foreach(variable_array_t *array, variable_array_apply_func_t apply_func, void *user_data) {
    Expects_not_null(array);
    Expects_not_null(apply_func);
    for (size_t i = 0; i < array->len; ++i) {
        apply_func(array->data[i], user_data);
    }
}

int variable_array_find(variable_array_t *array, variable_t *element, size_t *index) {
    Expects_not_null(array);
    for (size_t i = 0; i < array->len; ++i) {
        if (array->data[i] == element) {
            if (index) *index = i;
            return 0;
        }
    }
    return 1;
}

int variable_array_find_with_compare(variable_array_t *array, const void *data, variable_array_compare_func_t compare_func, size_t *index) {
    Expects_not_null(array);
    Expects_not_null(compare_func);
    for (size_t i = 0; i < array->len; ++i) {
        if (compare_func(array->data[i], data) == 0) {
            if (index) *index = i;
            return 0;
        }
    }
    return 1;
}

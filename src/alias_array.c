#include "alias_array.h"
#include "logging.h"
#include <stdlib.h>
#include <string.h>
#include "alias.h"
/* INCLUDE2 */

#define INITIAL_CAPACITY 16
#define GROW_FACTOR 2

struct AliasArray {
    Alias * *data;
    size_t size;
    size_t capacity;
    AliasArrayFreeFunc free_func;
};

// Helper: Ensure capacity
static int alias_array_ensure_capacity(AliasArray *array, size_t needed)
{
    return_val_if_null(array, -1);
    return_val_if_lt(needed, 0, -1);

    if (needed <= array->capacity)
        return 0;

    size_t new_capacity = array->capacity ? array->capacity : INITIAL_CAPACITY;
    while (new_capacity < needed)
        new_capacity *= GROW_FACTOR;

    Alias * *new_data = realloc(array->data, new_capacity * sizeof(Alias *));
    if (!new_data) {
        log_fatal("alias_array_ensure_capacity: memory allocation failure");
        return -1;
    }

    array->data = new_data;
    array->capacity = new_capacity;
    return 0;
}

// Create and destroy
AliasArray *alias_array_create(void)
{
    return alias_array_create_with_free(NULL);
}

AliasArray *alias_array_create_with_free(AliasArrayFreeFunc free_func)
{
    AliasArray *array = malloc(sizeof(AliasArray));
    if (!array) {
        log_fatal("alias_array_create_with_free: out of memory");
        return NULL;
    }

    array->data = NULL;
    array->size = 0;
    array->capacity = 0;
    array->free_func = free_func;

    if (alias_array_ensure_capacity(array, INITIAL_CAPACITY) != 0) {
        free(array);
        log_fatal("alias_array_create_with_free: out of memory");
        return NULL;
    }

    return array;
}

void alias_array_destroy(AliasArray *array)
{
    if (array) {
        log_debug("alias_array_destroy: freeing array %p, size %zu", array, array->size);
        if (array->free_func) {
            for (size_t i = 0; i < array->size; i++) {
                if (array->data[i]) {
                    array->free_func(array->data[i]);
                }
            }
        }
        free(array->data);
        free(array);
    }
}

// Accessors
size_t alias_array_size(const AliasArray *array)
{
    return_val_if_null(array, 0);
    return array->size;
}

size_t alias_array_capacity(const AliasArray *array)
{
    return_val_if_null(array, 0);
    return array->capacity;
}

Alias * alias_array_get(const AliasArray *array, size_t index)
{
    return_val_if_null(array, NULL);
    return_val_if_ge(index, array->size, NULL);
    return array->data[index];
}

int alias_array_is_empty(const AliasArray *array)
{
    return_val_if_null(array, 1);
    return array->size == 0;
}

// Modification
int alias_array_append(AliasArray *array, Alias * element)
{
    return_val_if_null(array, -1);
    if (alias_array_ensure_capacity(array, array->size + 1) != 0) {
        log_fatal("alias_array_append: out of memory");
        return -1;
    }

    array->data[array->size] = element;
    array->size++;
    return 0;
}

int alias_array_set(AliasArray *array, size_t index, Alias * element)
{
    return_val_if_null(array, -1);
    return_val_if_ge(index, array->size, -1);

    if (array->free_func && array->data[index]) {
        array->free_func(array->data[index]);
    }
    array->data[index] = element;
    return 0;
}

int alias_array_remove(AliasArray *array, size_t index)
{
    return_val_if_null(array, -1);
    return_val_if_ge(index, array->size, -1);

    if (array->free_func && array->data[index]) {
        array->free_func(array->data[index]);
    }

    // Shift elements to fill the gap
    for (size_t i = index; i < array->size - 1; i++) {
        array->data[i] = array->data[i + 1];
    }
    array->size--;
    array->data[array->size] = NULL; // Clear the last slot
    return 0;
}

int alias_array_clear(AliasArray *array)
{
    return_val_if_null(array, -1);

    if (array->free_func) {
        for (size_t i = 0; i < array->size; i++) {
            if (array->data[i]) {
                array->free_func(array->data[i]);
            }
        }
    }
    array->size = 0;
    // Keep capacity and data allocated, just clear pointers
    for (size_t i = 0; i < array->capacity; i++) {
        array->data[i] = NULL;
    }
    return 0;
}

int alias_array_resize(AliasArray *array, size_t new_capacity)
{
    return_val_if_null(array, -1);
    return_val_if_lt(new_capacity, 0, -1);

    if (new_capacity < array->size) {
        // Free elements that won't fit in the new capacity
        if (array->free_func) {
            for (size_t i = new_capacity; i < array->size; i++) {
                if (array->data[i]) {
                    array->free_func(array->data[i]);
                }
            }
        }
        array->size = new_capacity;
    }

    if (alias_array_ensure_capacity(array, new_capacity) != 0) {
        log_fatal("alias_array_resize: out of memory");
        return -1;
    }

    // Clear any newly allocated slots
    for (size_t i = array->capacity; i < new_capacity; i++) {
        array->data[i] = NULL;
    }
    array->capacity = new_capacity;
    return 0;
}

// Operations
void alias_array_foreach(AliasArray *array, AliasArrayApplyFunc apply_func, void *user_data)
{
    if (!array || !apply_func) {
        log_fatal("alias_array_foreach: argument 'array' or 'apply_func' is null");
        return;
    }

    for (size_t i = 0; i < array->size; i++) {
        apply_func(array->data[i], user_data);
    }
}

int alias_array_find(AliasArray *array, Alias * element, size_t *index)
{
    return_val_if_null(array, -1);
    return_val_if_null(index, -1);

    for (size_t i = 0; i < array->size; i++) {
        if (array->data[i] == element) {
            *index = i;
            return 0;
        }
    }
    return -1;
}

int alias_array_find_with_compare(AliasArray *array, const void *data, AliasArrayCompareFunc compare_func, size_t *index)
{
    return_val_if_null(array, -1);
    return_val_if_null(data, -1);
    return_val_if_null(compare_func, -1);
    return_val_if_null(index, -1);

    for (size_t i = 0; i < array->size; i++) {
        if (compare_func(array->data[i], data) == 0) {
            *index = i;
            return 0;
        }
    }
    return -1;
}

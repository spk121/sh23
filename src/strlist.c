#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <string.h>

#include "miga/strlist.h"

#include "logging.h"
#include "miga/string_t.h"
#include "miga/xalloc.h"

/* ============================================================================
 * Internal Constants
 * ============================================================================ */

static const int strlist_INITIAL_CAPACITY = strlist_T_INITIAL_CAPACITY;
static const int strlist_GROW_FACTOR = 2;

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

static inline bool strlist_is_inline(const strlist_t *list)
{
    return list->capacity <= strlist_INITIAL_CAPACITY;
}

static inline bool strlist_is_heap_allocated(const strlist_t *list)
{
    return list->capacity > strlist_INITIAL_CAPACITY;
}

static inline void strlist_normalize_capacity(strlist_t *list, int new_capacity)
{
    if (strlist_is_heap_allocated(list) && new_capacity <= strlist_INITIAL_CAPACITY)
    {
        // Move to inline storage
        int old_size = list->size;
        int new_size = old_size < strlist_INITIAL_CAPACITY ? old_size : strlist_INITIAL_CAPACITY;
        list->size = new_size;
        list->capacity = strlist_INITIAL_CAPACITY;
        memcpy(list->inline_strings, list->strings, new_size * sizeof(string_t *));
        xfree(list->strings);
        list->strings = list->inline_strings;
    }
    else if (strlist_is_heap_allocated(list) && new_capacity > strlist_INITIAL_CAPACITY)
    {
        // Resize heap allocation
        string_t **new_strings = (string_t **)xmalloc(new_capacity * sizeof(string_t *));
        memcpy(new_strings, list->strings, list->size * sizeof(string_t *));
        xfree(list->strings);
        list->strings = new_strings;
        list->capacity = new_capacity;
    }
    else if (strlist_is_inline(list) && new_capacity > strlist_INITIAL_CAPACITY)
    {
        // Move to heap allocation
        string_t **new_strings = (string_t **)xmalloc(new_capacity * sizeof(string_t *));
        memcpy(new_strings, list->inline_strings, list->size * sizeof(string_t *));
        list->strings = new_strings;
        list->capacity = new_capacity;
    }
    // else: already inline with sufficient capacity
}

/* ============================================================================
 * Constructors and Destructors
 * ============================================================================ */

strlist_t *strlist_create(void)
{
    strlist_t *list = (strlist_t *)xmalloc(sizeof(strlist_t));
    list->size = 0;
    list->capacity = strlist_INITIAL_CAPACITY;
    list->strings = list->inline_strings;
    return list;
}

strlist_t *strlist_create_from_cstr_array(const char **strv, int len)
{
    Expects_not_null(strv);
    Expects(len >= -1);
    strlist_t *lst = strlist_create();

    int count = 0;

    if (len >= 0) {
        count = len;
    }
    else {
        /* NULL‑terminated list */
        while (strv[count])
            count++;
    }

    for (int i = 0; i < count; i++) {
        const char *cstr = strv[i];
        string_t *s;
        if (!cstr)
            s = string_create();
        else
            s = string_create_from_cstr(cstr);

        strlist_move_push_back(lst, &s);
    }

    return lst;
}

strlist_t *strlist_create_from_system_env(void)
{
    strlist_t *lst = strlist_create();
    char * const *env;
#ifdef MIGA_POSIX_API
    extern char **environ;
    env = (char * const *)environ;
#elifdef MIGA_UCRT_API
    extern char **_environ;
    env = (char * const *)_environ;
#else
    // There is no `environ` in ISO C.  There is only getenv().
    env = NULL;
#endif

    if (!env)
        return lst;

    for (int i = 0; env[i]; i++)
    {
        string_t *s = string_create_from_cstr(env[i]);
        strlist_move_push_back(lst, &s);
    }

    return lst;
}

/* Deep copy */
strlist_t *strlist_create_from(const strlist_t *other)
{
    Expects_not_null(other);
    strlist_t *lst = strlist_create();
    for (int i = 0; i < other->size; i++)
    {
        const string_t *src_str = other->strings[i];
        string_t *new_str = string_create_from(src_str);
        strlist_move_push_back(lst, &new_str);
    }
    return lst;
}

strlist_t *strlist_create_from_string_split_char(const string_t *str, char separator)
{
    Expects_not_null(str);
    Expects_ne(separator, '\0');

    strlist_t *lst = strlist_create();
    string_t *separator_str = string_create_from_n_chars(separator, 1);
    int begin = 0;

    while (true)
    {
        int end = string_find_first_of_at(str, separator_str, begin);
        string_t *substr = string_substring(str, begin, end);
        strlist_move_push_back(lst, &substr);
        if (end == -1)
            break;
        begin = end + 1;
    }
    string_destroy(&separator_str);
    return lst;
}

strlist_t *strlist_create_from_string_split_cstr(const string_t *str,
                                                         const char *separators)
{
    Expects_not_null(str);
    Expects_not_null(separators);
    Expects_ne(separators[0], '\0');

    strlist_t *lst = strlist_create();
    int begin = 0;

    while (true)
    {
        int end = string_find_first_of_cstr_at(str, separators, begin);
        string_t *substr = string_create_from_range(str, begin, end);
        strlist_move_push_back(lst, &substr);
        if (end == -1)
            break;
        begin = end + 1;
    }
    return lst;
}

strlist_t *strlist_create_slice(const strlist_t *list, int start, int end)
{
    Expects_not_null(list);

    /* Clamp start to valid range */
    if (start < 0)
        start = 0;
    if (start > list->size)
        start = list->size;

    /* Handle end == -1 meaning "to end of list" */
    if (end < 0)
        end = list->size;

    /* Clamp end to valid range */
    if (end < start)
        end = start;
    if (end > list->size)
        end = list->size;

    /* Create new list and copy the slice */
    strlist_t *result = strlist_create();
    for (int i = start; i < end; i++)
    {
        const string_t *src_str = list->strings[i];
        string_t *new_str = string_create_from(src_str);
        strlist_move_push_back(result, &new_str);
    }

    return result;
}

void strlist_destroy(strlist_t **list)
{
    if (!list || !*list)
        return;

    strlist_t *l = *list;

    // Destroy all contained strings
    for (int i = 0; i < l->size; i++)
    {
        if (l->strings[i])
        {
            string_destroy(&l->strings[i]);
        }
    }

    // Free heap-allocated array if present
    if (strlist_is_heap_allocated(l))
    {
        xfree(l->strings);
    }

    xfree(l);
    *list = NULL;
}

/* ============================================================================
 * Capacity
 * ============================================================================ */

int strlist_size(const strlist_t *list)
{
    Expects_not_null(list);
    return list->size;
}

/* ============================================================================
 * Element Access
 * ============================================================================ */

const string_t *strlist_at(const strlist_t *list, int index)
{
    Expects_not_null(list);

    if (index < 0 || index >= list->size)
    {
        return NULL;
    }

    return list->strings[index];
}

/* ============================================================================
 * Modifiers
 * ============================================================================ */

void strlist_push_back(strlist_t *list, const string_t *str)
{
    Expects_not_null(list);

    if (!str)
    {
        return;
    }

    // Grow if needed
    if (list->size >= list->capacity)
    {
        int new_capacity = list->capacity * strlist_GROW_FACTOR;
        strlist_normalize_capacity(list, new_capacity);
    }

    list->strings[list->size] = string_create_from(str);
    list->size++;
}

void strlist_move_push_back(strlist_t *list, string_t **str)
{
    Expects_not_null(list);

    if (!str || !*str)
    {
        return;
    }

    // Grow if needed
    if (list->size >= list->capacity)
    {
        int new_capacity = list->capacity * strlist_GROW_FACTOR;
        strlist_normalize_capacity(list, new_capacity);
    }

    list->strings[list->size] = *str;
    *str = NULL;
    list->size++;
}

void strlist_insert(strlist_t *list, int index, const string_t *str)
{
    Expects_not_null(list);

    // Clamp index
    if (index < 0)
        index = 0;
    if (index > list->size)
        index = list->size;

    // Grow if needed
    if (list->size >= list->capacity)
    {
        int new_capacity = list->capacity * strlist_GROW_FACTOR;
        strlist_normalize_capacity(list, new_capacity);
    }

    // Shift elements to make room
    for (int i = list->size; i > index; i--)
    {
        list->strings[i] = list->strings[i - 1];
    }

    // Insert the new string
    if (str)
    {
        list->strings[index] = string_create_from(str);
    }
    else
    {
        list->strings[index] = string_create();
    }

    list->size++;
}

void strlist_move_insert(strlist_t *list, int index, string_t **str)
{
    Expects_not_null(list);

    if (!str || !*str)
    {
        return;
    }

    // Clamp index
    if (index < 0)
        index = 0;
    if (index > list->size)
        index = list->size;

    // Grow if needed
    if (list->size >= list->capacity)
    {
        int new_capacity = list->capacity * strlist_GROW_FACTOR;
        strlist_normalize_capacity(list, new_capacity);
    }

    // Shift elements to make room
    for (int i = list->size; i > index; i--)
    {
        list->strings[i] = list->strings[i - 1];
    }

    // Move the string in
    list->strings[index] = *str;
    *str = NULL;
    list->size++;
}

void strlist_erase(strlist_t *list, int index)
{
    Expects_not_null(list);
    Expects(index >= 0 && index < list->size);

    // Destroy the string at this index
    if (list->strings[index])
    {
        string_destroy(&list->strings[index]);
    }

    // Shift remaining elements down
    for (int i = index; i < list->size - 1; i++)
    {
        list->strings[i] = list->strings[i + 1];
    }

    list->size--;
}

void strlist_clear(strlist_t *list)
{
    Expects_not_null(list);

    // Destroy all strings
    for (int i = 0; i < list->size; i++)
    {
        if (list->strings[i])
        {
            string_destroy(&list->strings[i]);
        }
    }

    list->size = 0;
}

/* ============================================================================
 * Conversion and Utility
 * ============================================================================ */

char **strlist_to_cstr_array(const strlist_t *list, int *out_size)
{
    Expects_not_null(list);
    int size = list->size;
    // Allocate null-terminated array
    char **array = (char **)xcalloc((size + 1), sizeof(char *));
    // Convert each string to C-string
    for (int i = 0; i < size; i++)
    {
        if (list->strings[i])
        {
            array[i] = xstrdup(string_cstr(list->strings[i]));
        }
        else
        {
            array[i] = xstrdup("");
        }
    }
    array[size] = NULL;
    if (out_size)
        *out_size = size;
    return array;
}

char **strlist_release_cstr_array(strlist_t **list, int *out_size)
{
    Expects_not_null(list);
    Expects_not_null(*list);

    strlist_t *l = *list;
    int size = l->size;

    // Allocate null-terminated array
    char **array = (char **)xcalloc((size + 1), sizeof(char *));

    // Convert each string to C-string
    for (int i = 0; i < size; i++)
    {
        if (l->strings[i])
        {
            array[i] = string_release(&l->strings[i]);
        }
        else
        {
            array[i] = xstrdup("");
        }
    }
    array[size] = NULL;

    if (out_size)
        *out_size = size;

    // Destroy the now-empty list
    strlist_destroy(list);

    return array;
}

string_t* strlist_join(const strlist_t* list, const char* separator)
{
    Expects_not_null(list);
    string_t* result = string_create();
    if (!separator)
        separator = "";
    for (int i = 0; i < list->size; i++)
    {
        if (i > 0)
        {
            string_append_cstr(result, separator);
        }
        if (list->strings[i])
        {
            string_append(result, list->strings[i]);
        }
    }
    return result;
}

string_t *strlist_join_move(strlist_t **list, const char *separator)
{
    Expects_not_null(list);
    Expects_not_null(*list);

    strlist_t *l = *list;
    string_t *result = string_create();

    if (!separator)
        separator = "";

    for (int i = 0; i < l->size; i++)
    {
        if (i > 0)
        {
            string_append_cstr(result, separator);
        }

        if (l->strings[i])
        {
            string_append(result, l->strings[i]);
        }
    }

    // Destroy the list
    strlist_destroy(list);

    return result;
}

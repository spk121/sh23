#include "string_t.h"
#include "logging.h"
#include "xalloc.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Internal helper functions for small string optimization
// ============================================================================

static inline bool string_is_inline(const string_t *str)
{
    return str->capacity <= STRING_INITIAL_CAPACITY;
}

static inline bool string_is_heap_allocated(const string_t *str)
{
    return str->capacity > STRING_INITIAL_CAPACITY;
}

/**
 * Normalize capacity and manage transitions between inline and heap storage.
 */
static inline void string_normalize_capacity(string_t *str, int new_capacity)
{
    if (string_is_heap_allocated(str) && new_capacity <= STRING_INITIAL_CAPACITY)
    {
        // Move from heap to inline storage
        int new_length =
            str->length < STRING_INITIAL_CAPACITY ? str->length : STRING_INITIAL_CAPACITY - 1;
        memcpy(str->inline_data, str->data, new_length);
        str->inline_data[new_length] = '\0';
        xfree(str->data);
        str->data = str->inline_data;
        str->length = new_length;
        str->capacity = STRING_INITIAL_CAPACITY;
    }
    else if (string_is_heap_allocated(str) && new_capacity > STRING_INITIAL_CAPACITY)
    {
        // Resize heap allocation
        char *new_data = (char *)xrealloc(str->data, new_capacity * sizeof(char));
        str->data = new_data;
        str->capacity = new_capacity;
    }
    else if (string_is_inline(str) && new_capacity > STRING_INITIAL_CAPACITY)
    {
        // Move from inline to heap storage
        char *new_data = (char *)xmalloc(new_capacity * sizeof(char));
        memcpy(new_data, str->inline_data, str->length);
        new_data[str->length] = '\0';
        str->data = new_data;
        str->capacity = new_capacity;
    }
}

static inline bool string_list_is_inline(const string_list_t *list)
{
    return list->capacity <= STRING_LIST_INITIAL_CAPACITY;
}

static inline bool string_list_is_heap_allocated(const string_list_t *list)
{
    return list->capacity > STRING_LIST_INITIAL_CAPACITY;
}

static inline void string_list_normalize_capacity(string_list_t *list, int new_capacity)
{
    if (string_list_is_heap_allocated(list) && new_capacity <= STRING_LIST_INITIAL_CAPACITY)
    {
        int old_size = list->size;
        int new_size =
            old_size < STRING_LIST_INITIAL_CAPACITY ? old_size : STRING_LIST_INITIAL_CAPACITY;
        memcpy(list->inline_strings, list->strings, new_size * sizeof(string_t *));
        xfree(list->strings);
        list->strings = list->inline_strings;
        list->size = new_size;
        list->capacity = STRING_LIST_INITIAL_CAPACITY;
    }
    else if (string_list_is_heap_allocated(list) && new_capacity > STRING_LIST_INITIAL_CAPACITY)
    {
        string_t **new_strings = (string_t **)xrealloc(list->strings, new_capacity * sizeof(string_t *));
        list->strings = new_strings;
        list->capacity = new_capacity;
    }
    else if (string_list_is_inline(list) && new_capacity > STRING_LIST_INITIAL_CAPACITY)
    {
        string_t **new_strings = (string_t **)xmalloc(new_capacity * sizeof(string_t *));
        memcpy(new_strings, list->inline_strings, list->size * sizeof(string_t *));
        list->strings = new_strings;
        list->capacity = new_capacity;
    }
}

// ============================================================================
// Range clamping helper
// ============================================================================

static inline void clamp_range(int len, int begin, int end, int *out_begin, int *out_end)
{
    if (end == -1)
        end = len;
    if (begin < 0)
        begin = 0;
    if (begin > len)
        begin = len;
    if (end < 0)
        end = 0;
    if (end > len)
        end = len;
    if (end <= begin)
    {
        *out_begin = *out_end = begin;
        return;
    }
    *out_begin = begin;
    *out_end = end;
}

// ============================================================================
// Capacity management
// ============================================================================

static void string_ensure_capacity(string_t *str, int needed)
{
    Expects_not_null(str);
    return_if_lt(needed, 0);

    if (needed <= str->capacity)
        return;

    int new_capacity = str->capacity ? str->capacity : STRING_INITIAL_CAPACITY;

    while (new_capacity < needed)
    {
        if (new_capacity > INT_MAX / STRING_GROW_FACTOR)
        {
            new_capacity = needed;
            break;
        }
        else
        {
            new_capacity *= STRING_GROW_FACTOR;
        }
    }

    string_normalize_capacity(str, new_capacity);
}

// ============================================================================
// Constructors
// ============================================================================

string_t *string_create(void)
{
    string_t *str = xcalloc(1, sizeof(string_t));
    str->data = str->inline_data;
    str->capacity = STRING_INITIAL_CAPACITY;
    str->length = 0;
    str->inline_data[0] = '\0';
    return str;
}

string_t *string_create_from_n_chars(int count, char ch)
{
    if (count <= 0)
        return string_create();

    string_t *str = xcalloc(1, sizeof(string_t));
    str->data = str->inline_data;
    str->capacity = STRING_INITIAL_CAPACITY;
    str->length = 0;
    str->inline_data[0] = '\0';

    int needed_capacity = count + 1;
    string_ensure_capacity(str, needed_capacity);

    for (int i = 0; i < count; i++)
        str->data[i] = ch;
    str->data[count] = '\0';
    str->length = count;

    return str;
}

string_t *string_create_from_cstr(const char *data)
{
    if (data == NULL)
        return string_create();
    size_t len = strlen(data);
    if (len > INT_MAX)
        len = INT_MAX - 1;
    return string_create_from_cstr_len(data, (int)len);
}

string_t *string_create_from_cstr_len(const char *data, int len)
{
    if (data == NULL || len <= 0)
        return string_create();

    string_t *str = xcalloc(1, sizeof(string_t));
    str->data = str->inline_data;
    str->capacity = STRING_INITIAL_CAPACITY;
    str->length = 0;
    str->inline_data[0] = '\0';

    int needed_capacity = len + 1;
    string_ensure_capacity(str, needed_capacity);

    memmove(str->data, data, len);
    str->data[len] = '\0';
    str->length = len;

    return str;
}

string_t *string_create_from(const string_t *other)
{
    Expects_not_null(other);
    if (other->length == 0)
        return string_create();
    return string_create_from_range(other, 0, other->length);
}

string_t *string_create_from_range(const string_t *str, int start, int end)
{
    Expects_not_null(str);

    if (str->length == 0)
        return string_create();

    int b, e;
    clamp_range(str->length, start, end, &b, &e);

    if (b == e)
        return string_create();

    int length = e - b;

    string_t *result = xcalloc(1, sizeof(string_t));
    result->data = result->inline_data;
    result->capacity = STRING_INITIAL_CAPACITY;
    result->length = 0;
    result->inline_data[0] = '\0';

    int needed_capacity = length + 1;
    string_ensure_capacity(result, needed_capacity);

    memcpy(result->data, str->data + b, length);
    result->data[length] = '\0';
    result->length = length;

    return result;
}

// ============================================================================
// Destructors
// ============================================================================

void string_destroy(string_t **str)
{
    return_if_null(str);
    return_if_null(*str);

    if (string_is_heap_allocated(*str))
        xfree((*str)->data);

    xfree(*str);
    *str = NULL;
}

char *string_release(string_t **str)
{
    return_val_if_null(str, NULL);
    return_val_if_null(*str, NULL);

    char *result = NULL;

    if (string_is_heap_allocated(*str))
    {
        result = (*str)->data;
    }
    else
    {
        result = xmalloc((*str)->length + 1);
        memcpy(result, (*str)->inline_data, (*str)->length + 1);
    }

    (*str)->data = NULL;
    (*str)->length = 0;
    (*str)->capacity = 0;
    xfree(*str);
    *str = NULL;

    return result;
}

// ============================================================================
// Setters
// ============================================================================

void string_set(string_t *str, const string_t *str2)
{
    Expects_not_null(str);

    if (str2 == NULL || str2->length == 0)
    {
        str->length = 0;
        str->data[0] = '\0';
        return;
    }

    if (str2 == str)
        return;

    int needed_capacity = str2->length + 1;
    string_ensure_capacity(str, needed_capacity);

    memcpy(str->data, str2->data, str2->length);
    str->length = str2->length;
    str->data[str->length] = '\0';
}

void string_move(string_t *str, string_t *str2)
{
    Expects_not_null(str);
    Expects_not_null(str2);

    if (str == str2)
        return;

    if (string_is_heap_allocated(str))
        xfree(str->data);

    if (string_is_heap_allocated(str2))
    {
        str->data = str2->data;
        str->length = str2->length;
        str->capacity = str2->capacity;
    }
    else
    {
        str->length = str2->length;
        str->capacity = STRING_INITIAL_CAPACITY;
        memcpy(str->inline_data, str2->inline_data, str2->length + 1);
        str->data = str->inline_data;
    }

    str2->data = str2->inline_data;
    str2->length = 0;
    str2->capacity = STRING_INITIAL_CAPACITY;
    str2->inline_data[0] = '\0';
}

void string_consume(string_t *str, string_t **str2)
{
    Expects_not_null(str);
    return_if_null(str2);
    return_if_null(*str2);

    string_move(str, *str2);
    string_destroy(str2);
}

void string_set_cstr(string_t *str, const char *cstr)
{
    Expects_not_null(str);

    if (cstr == NULL)
    {
        str->length = 0;
        str->data[0] = '\0';
        return;
    }

    size_t slen = strlen(cstr);
    if (slen > INT_MAX - 1)
        slen = INT_MAX - 1;
    int len = (int)slen;

    int needed_capacity = len + 1;
    string_ensure_capacity(str, needed_capacity);

    memcpy(str->data, cstr, len);
    str->length = len;
    str->data[str->length] = '\0';
}

void string_set_char(string_t *str, char ch)
{
    Expects_not_null(str);

    if (ch == '\0')
    {
        str->length = 0;
        str->data[0] = '\0';
        return;
    }

    str->data[0] = ch;
    str->length = 1;
    str->data[1] = '\0';
}

void string_set_data(string_t *str, const char *data, int n)
{
    Expects_not_null(str);

    if (data == NULL || n <= 0)
    {
        str->length = 0;
        str->data[0] = '\0';
        return;
    }

    if (n > INT_MAX - 1)
        n = INT_MAX - 1;

    int actual_len = 0;
    while (actual_len < n && data[actual_len] != '\0')
        actual_len++;

    int needed_capacity = actual_len + 1;
    string_ensure_capacity(str, needed_capacity);

    memcpy(str->data, data, actual_len);
    str->data[actual_len] = '\0';
    str->length = actual_len;
}

void string_set_n_chars(string_t *str, int count, char ch)
{
    Expects_not_null(str);

    if (count <= 0 || ch == '\0')
    {
        str->length = 0;
        str->data[0] = '\0';
        return;
    }

    if (count > INT_MAX - 1)
        count = INT_MAX - 1;

    int needed_capacity = count + 1;
    string_ensure_capacity(str, needed_capacity);

    memset(str->data, ch, count);
    str->length = count;
    str->data[str->length] = '\0';
}

void string_set_substring(string_t *str, const string_t *str2, int begin2, int end2)
{
    Expects_not_null(str);

    if (str2 == NULL || str2->length == 0)
    {
        str->length = 0;
        str->data[0] = '\0';
        return;
    }

    int b, e;
    clamp_range(str2->length, begin2, end2, &b, &e);

    if (b == e)
    {
        str->length = 0;
        str->data[0] = '\0';
        return;
    }

    int substring_length = e - b;
    int needed_capacity = substring_length + 1;
    string_ensure_capacity(str, needed_capacity);

    memcpy(str->data, str2->data + b, substring_length);
    str->length = substring_length;
    str->data[str->length] = '\0';
}

// ============================================================================
// Accessors
// ============================================================================

char string_at(const string_t *str, int index)
{
    Expects_not_null(str);
    if (index < 0 || index >= str->length)
        return '\0';
    return str->data[index];
}

char string_front(const string_t *str)
{
    Expects_not_null(str);
    if (str->length <= 0)
        return '\0';
    return str->data[0];
}

char string_back(const string_t *str)
{
    Expects_not_null(str);
    if (str->length <= 0)
        return '\0';
    return str->data[str->length - 1];
}

char *string_data(string_t *str)
{
    Expects_not_null(str);
    return str->data;
}

char *string_data_at(string_t *str, int pos)
{
    Expects_not_null(str);
    if (pos < 0 || pos > str->length)
        return NULL;
    return str->data + pos;
}

const char *string_cstr(const string_t *str)
{
    Expects_not_null(str);
    return str->data;
}

// ============================================================================
// Capacity
// ============================================================================

bool string_empty(const string_t *str)
{
    Expects_not_null(str);
    return str->length == 0;
}

int string_length(const string_t *str)
{
    Expects_not_null(str);
    return str->length;
}

void string_reserve(string_t *str, int new_cap)
{
    Expects_not_null(str);
    return_if_lt(new_cap, 0);

    if (new_cap > INT_MAX - 1)
        new_cap = INT_MAX - 1;

    if (new_cap + 1 > str->capacity)
        string_ensure_capacity(str, new_cap + 1);
}

int string_capacity(const string_t *str)
{
    Expects_not_null(str);
    return str->capacity;
}

void string_shrink_to_fit(string_t *str)
{
    Expects_not_null(str);

    int needed = str->length + 1;

    if (needed <= STRING_INITIAL_CAPACITY)
    {
        if (string_is_heap_allocated(str))
            string_normalize_capacity(str, STRING_INITIAL_CAPACITY);
    }
    else if (str->capacity > needed)
    {
        char *new_data = xrealloc(str->data, needed);
        str->data = new_data;
        str->capacity = needed;
    }
}

// ============================================================================
// Modifiers
// ============================================================================

void string_clear(string_t *str)
{
    Expects_not_null(str);
    str->length = 0;
    str->data[0] = '\0';
}

void string_insert(string_t *str, int pos, const string_t *other)
{
    Expects_not_null(str);

    if (other == NULL || other->length == 0)
        return;

    if (pos < 0)
        pos = 0;
    if (pos > str->length)
        pos = str->length;

    int other_len = other->length;
    int needed_capacity = str->length + other_len + 1;
    string_ensure_capacity(str, needed_capacity);

    memmove(str->data + pos + other_len, str->data + pos, str->length - pos);
    memcpy(str->data + pos, other->data, other_len);

    str->length += other_len;
    str->data[str->length] = '\0';
}

void string_insert_n_chars(string_t *str, int pos, int count, char ch)
{
    Expects_not_null(str);

    if (count <= 0)
        return;

    if (ch == '\0')
    {
        if (pos < 0)
            pos = 0;
        if (pos > str->length)
            pos = str->length;
        str->length = pos;
        str->data[str->length] = '\0';
        return;
    }

    if (pos < 0)
        pos = 0;
    if (pos > str->length)
        pos = str->length;

    int needed_capacity = str->length + count + 1;
    string_ensure_capacity(str, needed_capacity);

    memmove(str->data + pos + count, str->data + pos, str->length - pos);
    memset(str->data + pos, ch, count);

    str->length += count;
    str->data[str->length] = '\0';
}

void string_insert_cstr(string_t *str, int pos, const char *cstr)
{
    Expects_not_null(str);

    if (cstr == NULL || cstr[0] == '\0')
        return;

    if (pos < 0)
        pos = 0;
    if (pos > str->length)
        pos = str->length;

    int cstr_len = (int)strlen(cstr);
    int needed_capacity = str->length + cstr_len + 1;
    string_ensure_capacity(str, needed_capacity);

    memmove(str->data + pos + cstr_len, str->data + pos, str->length - pos);
    memcpy(str->data + pos, cstr, cstr_len);

    str->length += cstr_len;
    str->data[str->length] = '\0';
}

void string_insert_data(string_t *str, int pos, const char *data, int len)
{
    Expects_not_null(str);

    if (data == NULL || len <= 0)
        return;

    if (pos < 0)
        pos = 0;
    if (pos > str->length)
        pos = str->length;

    int actual_len = 0;
    while (actual_len < len && data[actual_len] != '\0')
        actual_len++;

    if (actual_len == 0)
        return;

    int needed_capacity = str->length + actual_len + 1;
    string_ensure_capacity(str, needed_capacity);

    memmove(str->data + pos + actual_len, str->data + pos, str->length - pos);
    memcpy(str->data + pos, data, actual_len);

    str->length += actual_len;
    str->data[str->length] = '\0';
}

void string_erase(string_t *str, int pos, int len)
{
    Expects_not_null(str);

    if (pos < 0)
        pos = 0;
    if (pos >= str->length)
        return;
    if (len <= 0)
        return;
    if (pos + len > str->length)
        len = str->length - pos;

    memmove(str->data + pos, str->data + pos + len, str->length - pos - len);
    str->length -= len;
    str->data[str->length] = '\0';

    if (string_is_heap_allocated(str) && str->capacity - str->length > STRING_REDUCE_THRESHOLD)
        string_shrink_to_fit(str);
}

void string_push_back(string_t *str, char ch)
{
    Expects_not_null(str);

    if (ch == '\0')
        return;
    if (str->length >= INT_MAX - 2)
        return;

    int needed_capacity = str->length + 1 + 1;
    string_ensure_capacity(str, needed_capacity);

    str->data[str->length] = ch;
    str->length += 1;
    str->data[str->length] = '\0';
}

char string_pop_back(string_t *str)
{
    Expects_not_null(str);

    if (str->length <= 0)
        return '\0';

    char ch = str->data[str->length - 1];
    str->length--;
    str->data[str->length] = '\0';

    return ch;
}

void string_append(string_t *str, const string_t *other)
{
    Expects_not_null(str);

    if (other == NULL || other->length == 0)
        return;

    int other_length = other->length;
    if (other_length > INT_MAX - 1 - str->length)
        other_length = INT_MAX - 1 - str->length;

    int needed_capacity = str->length + other_length + 1;
    string_ensure_capacity(str, needed_capacity);

    memcpy(str->data + str->length, other->data, other_length);
    str->length += other_length;
    str->data[str->length] = '\0';
}

void string_append_substring(string_t *str, const string_t *other, int begin, int end)
{
    Expects_not_null(str);

    if (other == NULL || other->length == 0)
        return;

    int b, e;
    clamp_range(other->length, begin, end, &b, &e);

    if (b == e)
        return;

    int substring_length = e - b;
    int needed_capacity = str->length + substring_length + 1;
    string_ensure_capacity(str, needed_capacity);

    memcpy(str->data + str->length, other->data + b, substring_length);
    str->length += substring_length;
    str->data[str->length] = '\0';
}

void string_append_cstr(string_t *str, const char *cstr)
{
    Expects_not_null(str);

    if (cstr == NULL || cstr[0] == '\0')
        return;

    int cstr_len = (int)strlen(cstr);
    if (cstr_len > INT_MAX - 1 - str->length)
        cstr_len = INT_MAX - 1 - str->length;

    int needed_capacity = str->length + cstr_len + 1;
    string_ensure_capacity(str, needed_capacity);

    memcpy(str->data + str->length, cstr, cstr_len);
    str->length += cstr_len;
    str->data[str->length] = '\0';
}

void string_append_n_chars(string_t *str, int count, char ch)
{
    Expects_not_null(str);

    if (count <= 0 || ch == '\0')
        return;

    if (count > INT_MAX - 1 - str->length)
        count = INT_MAX - 1 - str->length;

    int needed_capacity = str->length + count + 1;
    string_ensure_capacity(str, needed_capacity);

    memset(str->data + str->length, ch, count);
    str->length += count;
    str->data[str->length] = '\0';
}

void string_append_data(string_t *str, const char *data, int len)
{
    Expects_not_null(str);

    if (data == NULL || len <= 0)
        return;

    int actual_len = 0;
    while (actual_len < len && data[actual_len] != '\0')
        actual_len++;

    if (actual_len == 0)
        return;

    if (actual_len > INT_MAX - 1 - str->length)
        actual_len = INT_MAX - 1 - str->length;

    int needed_capacity = str->length + actual_len + 1;
    string_ensure_capacity(str, needed_capacity);

    memcpy(str->data + str->length, data, actual_len);
    str->length += actual_len;
    str->data[str->length] = '\0';
}

void string_replace(string_t *str, int pos, int len, const string_t *other)
{
    Expects_not_null(str);

    if (pos < 0)
        pos = 0;
    if (pos >= str->length)
        return;
    if (len <= 0)
        return;
    if (pos + len > str->length)
        len = str->length - pos;

    int other_len = (other == NULL) ? 0 : other->length;

    int new_length = str->length - len + other_len;
    int needed_capacity = new_length + 1;
    string_ensure_capacity(str, needed_capacity);

    memmove(str->data + pos + other_len, str->data + pos + len, str->length - pos - len);

    if (other_len > 0)
        memcpy(str->data + pos, other->data, other_len);

    str->length = new_length;
    str->data[str->length] = '\0';
}

void string_replace_substring(string_t *str, int pos, int len, const string_t *other, int begin2, int end2)
{
    Expects_not_null(str);

    if (pos < 0)
        pos = 0;
    if (pos >= str->length)
        return;
    if (len <= 0)
        return;
    if (pos + len > str->length)
        len = str->length - pos;

    int b = 0, e = 0;
    int other_len = 0;
    if (other != NULL && other->length > 0)
    {
        clamp_range(other->length, begin2, end2, &b, &e);
        other_len = e - b;
    }

    int new_length = str->length - len + other_len;
    int needed_capacity = new_length + 1;
    string_ensure_capacity(str, needed_capacity);

    memmove(str->data + pos + other_len, str->data + pos + len, str->length - pos - len);

    if (other_len > 0)
        memcpy(str->data + pos, other->data + b, other_len);

    str->length = new_length;
    str->data[str->length] = '\0';
}

void string_replace_cstr(string_t *str, int pos, int len, const char *cstr)
{
    Expects_not_null(str);

    if (pos < 0)
        pos = 0;
    if (pos >= str->length)
        return;
    if (len <= 0)
        return;
    if (pos + len > str->length)
        len = str->length - pos;

    int cstr_len = (cstr == NULL) ? 0 : (int)strlen(cstr);

    int new_length = str->length - len + cstr_len;
    int needed_capacity = new_length + 1;
    string_ensure_capacity(str, needed_capacity);

    memmove(str->data + pos + cstr_len, str->data + pos + len, str->length - pos - len);

    if (cstr_len > 0)
        memcpy(str->data + pos, cstr, cstr_len);

    str->length = new_length;
    str->data[str->length] = '\0';
}

void string_replace_n_chars(string_t *str, int pos, int len, int count, char ch)
{
    Expects_not_null(str);

    if (pos < 0)
        pos = 0;
    if (pos >= str->length)
        return;
    if (len <= 0)
        return;
    if (ch == '\0')
        return;
    if (pos + len > str->length)
        len = str->length - pos;

    int replacement_len = (count <= 0) ? 0 : count;

    int new_length = str->length - len + replacement_len;
    int needed_capacity = new_length + 1;
    string_ensure_capacity(str, needed_capacity);

    memmove(str->data + pos + replacement_len, str->data + pos + len, str->length - pos - len);

    if (replacement_len > 0)
        memset(str->data + pos, ch, replacement_len);

    str->length = new_length;
    str->data[str->length] = '\0';
}

void string_replace_data(string_t *str, int pos, int len, const char *data, int data_len)
{
    Expects_not_null(str);

    if (pos < 0)
        pos = 0;
    if (pos >= str->length)
        return;
    if (len <= 0)
        return;
    if (data == NULL)
        return;
    if (pos + len > str->length)
        len = str->length - pos;

    int actual_data_len = 0;
    if (data_len > 0)
    {
        while (actual_data_len < data_len && data[actual_data_len] != '\0')
            actual_data_len++;
    }

    int new_length = str->length - len + actual_data_len;
    int needed_capacity = new_length + 1;
    string_ensure_capacity(str, needed_capacity);

    memmove(str->data + pos + actual_data_len, str->data + pos + len, str->length - pos - len);

    if (actual_data_len > 0)
        memcpy(str->data + pos, data, actual_data_len);

    str->length = new_length;
    str->data[str->length] = '\0';
}

void string_copy_to_cstr(const string_t *str, char *dest, int count)
{
    Expects_not_null(str);
    Expects_not_null(dest);

    if (count <= 0)
        return;

    int to_copy = (str->length < count) ? str->length : count - 1;
    memcpy(dest, str->data, to_copy);
    dest[to_copy] = '\0';
}

void string_copy_to_cstr_at(const string_t *str, int pos, char *dest, int count)
{
    Expects_not_null(str);
    Expects_not_null(dest);

    if (count <= 0)
        return;

    if (pos < 0)
        pos = 0;
    if (pos >= str->length)
    {
        dest[0] = '\0';
        return;
    }

    int available = str->length - pos;
    int to_copy = (available < count - 1) ? available : count - 1;
    memcpy(dest, str->data + pos, to_copy);
    dest[to_copy] = '\0';
}

void string_resize(string_t *str, int new_size)
{
    Expects_not_null(str);

    if (new_size < 0)
        new_size = 0;
    if (new_size >= str->length)
        return;

    str->length = new_size;
    str->data[new_size] = '\0';
}

void string_resize_with_char(string_t *str, int new_size, char ch)
{
    Expects_not_null(str);

    if (new_size < 0)
        new_size = 0;

    if (new_size <= str->length)
    {
        str->length = new_size;
        str->data[new_size] = '\0';
        return;
    }

    int needed = new_size + 1;
    string_ensure_capacity(str, needed);

    memset(str->data + str->length, ch, new_size - str->length);
    str->length = new_size;
    str->data[new_size] = '\0';
}

// ============================================================================
// String operations - Find
// ============================================================================

int string_find(const string_t *str, const string_t *substr)
{
    Expects_not_null(str);
    if (substr == NULL || substr->length == 0)
        return 0;
    return string_find_at(str, substr, 0);
}

int string_find_at(const string_t *str, const string_t *substr, int pos)
{
    Expects_not_null(str);

    if (pos < 0)
        pos = 0;
    if (pos > str->length)
        pos = str->length;

    if (substr == NULL || substr->length == 0)
        return pos;

    if (substr->length > str->length - pos)
        return -1;

    for (int i = pos; i <= str->length - substr->length; i++)
    {
        if (memcmp(str->data + i, substr->data, substr->length) == 0)
            return i;
    }

    return -1;
}

int string_find_cstr(const string_t *str, const char *substr)
{
    Expects_not_null(str);
    if (substr == NULL || substr[0] == '\0')
        return 0;
    return string_find_cstr_at(str, substr, 0);
}

int string_find_cstr_at(const string_t *str, const char *substr, int pos)
{
    Expects_not_null(str);

    if (pos < 0)
        pos = 0;
    if (pos > str->length)
        pos = str->length;

    if (substr == NULL || substr[0] == '\0')
        return pos;

    size_t sublen = strlen(substr);

    if (sublen > (size_t)(str->length - pos))
        return -1;

    for (int i = pos; i <= str->length - (int)sublen; i++)
    {
        if (memcmp(str->data + i, substr, sublen) == 0)
            return i;
    }

    return -1;
}

int string_rfind(const string_t *str, const string_t *substr)
{
    Expects_not_null(str);
    if (substr == NULL || substr->length == 0)
        return str->length;
    return string_rfind_at(str, substr, str->length);
}

int string_rfind_at(const string_t *str, const string_t *substr, int pos)
{
    Expects_not_null(str);

    if (pos < 0)
        pos = 0;
    if (pos > str->length)
        pos = str->length;

    if (substr == NULL || substr->length == 0)
        return pos;

    int start = pos;
    if (start > str->length - substr->length)
        start = str->length - substr->length;

    for (int i = start; i >= 0; i--)
    {
        if (memcmp(str->data + i, substr->data, substr->length) == 0)
            return i;
    }

    return -1;
}

int string_rfind_cstr(const string_t *str, const char *substr)
{
    Expects_not_null(str);
    if (substr == NULL || substr[0] == '\0')
        return str->length;
    return string_rfind_cstr_at(str, substr, str->length);
}

int string_rfind_cstr_at(const string_t *str, const char *substr, int pos)
{
    Expects_not_null(str);

    if (pos < 0)
        pos = 0;
    if (pos > str->length)
        pos = str->length;

    if (substr == NULL || substr[0] == '\0')
        return pos;

    int sublen = (int)strlen(substr);

    int start = pos;
    if (start > str->length - sublen)
        start = str->length - sublen;

    for (int i = start; i >= 0; i--)
    {
        if (memcmp(str->data + i, substr, sublen) == 0)
            return i;
    }

    return -1;
}

int string_find_first_of(const string_t *str, const string_t *chars)
{
    return string_find_first_of_at(str, chars, 0);
}

int string_find_first_of_at(const string_t *str, const string_t *chars, int pos)
{
    Expects_not_null(str);

    if (chars == NULL || chars->length == 0)
        return -1;

    if (pos < 0)
        pos = 0;
    if (pos >= str->length)
        return -1;

    for (int i = pos; i < str->length; i++)
    {
        for (int j = 0; j < chars->length; j++)
        {
            if (str->data[i] == chars->data[j])
                return i;
        }
    }

    return -1;
}

int string_find_first_of_cstr(const string_t *str, const char *chars)
{
    return string_find_first_of_cstr_at(str, chars, 0);
}

int string_find_first_of_cstr_at(const string_t *str, const char *chars, int pos)
{
    Expects_not_null(str);

    if (chars == NULL || chars[0] == '\0')
        return -1;

    if (pos < 0)
        pos = 0;
    if (pos >= str->length)
        return -1;

    for (int i = pos; i < str->length; i++)
    {
        if (strchr(chars, str->data[i]) != NULL)
            return i;
    }

    return -1;
}

int string_find_first_of_predicate(const string_t *str, bool (*predicate)(char))
{
    return string_find_first_of_predicate_at(str, predicate, 0);
}

int string_find_first_of_predicate_at(const string_t *str, bool (*predicate)(char), int pos)
{
    Expects_not_null(str);

    if (predicate == NULL)
        return -1;

    if (pos < 0)
        pos = 0;
    if (pos >= str->length)
        return -1;

    for (int i = pos; i < str->length; i++)
    {
        if (predicate(str->data[i]))
            return i;
    }

    return -1;
}

int string_find_first_not_of(const string_t *str, const string_t *chars)
{
    return string_find_first_not_of_at(str, chars, 0);
}

int string_find_first_not_of_at(const string_t *str, const string_t *chars, int pos)
{
    Expects_not_null(str);

    if (pos < 0)
        pos = 0;
    if (pos > str->length)
        pos = str->length;

    if (chars == NULL || chars->length == 0)
        return (pos < str->length) ? pos : -1;

    for (int i = pos; i < str->length; i++)
    {
        bool found = false;
        for (int j = 0; j < chars->length; j++)
        {
            if (str->data[i] == chars->data[j])
            {
                found = true;
                break;
            }
        }
        if (!found)
            return i;
    }

    return -1;
}

int string_find_first_not_of_cstr(const string_t *str, const char *chars)
{
    return string_find_first_not_of_cstr_at(str, chars, 0);
}

int string_find_first_not_of_cstr_at(const string_t *str, const char *chars, int pos)
{
    Expects_not_null(str);

    if (pos < 0)
        pos = 0;
    if (pos > str->length)
        pos = str->length;

    if (chars == NULL || chars[0] == '\0')
        return (pos < str->length) ? pos : -1;

    for (int i = pos; i < str->length; i++)
    {
        if (strchr(chars, str->data[i]) == NULL)
            return i;
    }

    return -1;
}

int string_find_first_not_of_predicate(const string_t *str, bool (*predicate)(char))
{
    return string_find_first_not_of_predicate_at(str, predicate, 0);
}

int string_find_first_not_of_predicate_at(const string_t *str, bool (*predicate)(char), int pos)
{
    Expects_not_null(str);

    if (pos < 0)
        pos = 0;
    if (pos > str->length)
        pos = str->length;

    if (predicate == NULL)
        return (pos < str->length) ? pos : -1;

    for (int i = pos; i < str->length; i++)
    {
        if (!predicate(str->data[i]))
            return i;
    }

    return -1;
}

int string_find_last_of(const string_t *str, const string_t *chars)
{
    Expects_not_null(str);
    return string_find_last_of_at(str, chars, str->length - 1);
}

int string_find_last_of_at(const string_t *str, const string_t *chars, int pos)
{
    Expects_not_null(str);

    if (chars == NULL || chars->length == 0)
        return -1;

    if (pos < 0)
        return -1;
    if (pos >= str->length)
        pos = str->length - 1;

    for (int i = pos; i >= 0; i--)
    {
        for (int j = 0; j < chars->length; j++)
        {
            if (str->data[i] == chars->data[j])
                return i;
        }
    }

    return -1;
}

int string_find_last_of_cstr(const string_t *str, const char *chars)
{
    Expects_not_null(str);
    return string_find_last_of_cstr_at(str, chars, str->length - 1);
}

int string_find_last_of_cstr_at(const string_t *str, const char *chars, int pos)
{
    Expects_not_null(str);

    if (chars == NULL || chars[0] == '\0')
        return -1;

    if (pos < 0)
        return -1;
    if (pos >= str->length)
        pos = str->length - 1;

    for (int i = pos; i >= 0; i--)
    {
        if (strchr(chars, str->data[i]) != NULL)
            return i;
    }

    return -1;
}

int string_find_last_not_of(const string_t *str, const string_t *chars)
{
    Expects_not_null(str);
    return string_find_last_not_of_at(str, chars, str->length - 1);
}

int string_find_last_not_of_at(const string_t *str, const string_t *chars, int pos)
{
    Expects_not_null(str);

    if (pos < 0)
        return -1;
    if (pos >= str->length)
        pos = str->length - 1;

    if (chars == NULL || chars->length == 0)
        return (str->length > 0) ? pos : -1;

    for (int i = pos; i >= 0; i--)
    {
        bool found = false;
        for (int j = 0; j < chars->length; j++)
        {
            if (str->data[i] == chars->data[j])
            {
                found = true;
                break;
            }
        }
        if (!found)
            return i;
    }

    return -1;
}

int string_find_last_not_of_cstr(const string_t *str, const char *chars)
{
    Expects_not_null(str);
    return string_find_last_not_of_cstr_at(str, chars, str->length - 1);
}

int string_find_last_not_of_cstr_at(const string_t *str, const char *chars, int pos)
{
    Expects_not_null(str);

    if (pos < 0)
        return -1;
    if (pos >= str->length)
        pos = str->length - 1;

    if (chars == NULL || chars[0] == '\0')
        return (str->length > 0) ? pos : -1;

    for (int i = pos; i >= 0; i--)
    {
        if (strchr(chars, str->data[i]) == NULL)
            return i;
    }

    return -1;
}

// ============================================================================
// Comparison functions
// ============================================================================

int string_compare(const string_t *str1, const string_t *str2)
{
    const char *buf1 = (str1 == NULL) ? "" : str1->data;
    const char *buf2 = (str2 == NULL) ? "" : str2->data;
    return strcmp(buf1, buf2);
}

int string_compare_at(const string_t *str1, int pos1, const string_t *str2, int pos2)
{
    Expects_not_null(str1);
    Expects_not_null(str2);

    if (pos1 < 0)
        pos1 = 0;
    if (pos2 < 0)
        pos2 = 0;

    const char *p1 = str1->data + (pos1 < str1->length ? pos1 : str1->length);
    const char *p2 = str2->data + (pos2 < str2->length ? pos2 : str2->length);
    const char *end1 = str1->data + str1->length;
    const char *end2 = str2->data + str2->length;

    while (p1 < end1 && p2 < end2)
    {
        if (*p1 != *p2)
            return (unsigned char)*p1 - (unsigned char)*p2;
        ++p1;
        ++p2;
    }

    if ((p1 == end1) && (p2 == end2))
        return 0;
    else if (p1 == end1)
        return -1;
    else
        return 1;
}

int string_compare_cstr(const string_t *str, const char *cstr)
{
    const char *buf1 = (str == NULL) ? "" : str->data;
    const char *buf2 = (cstr == NULL) ? "" : cstr;
    return strcmp(buf1, buf2);
}

int string_compare_cstr_at(const string_t *str, int pos1, const char *cstr, int pos2)
{
    const char *buf1 = "";
    int len1 = 0;
    if (str != NULL)
    {
        if (pos1 < 0)
            pos1 = 0;
        if (pos1 < str->length)
        {
            buf1 = str->data + pos1;
            len1 = str->length - pos1;
        }
    }

    const char *buf2 = "";
    int len2 = 0;
    if (cstr != NULL)
    {
        if (pos2 < 0)
            pos2 = 0;
        int full_len = (int)strlen(cstr);
        if (pos2 < full_len)
        {
            buf2 = cstr + pos2;
            len2 = full_len - pos2;
        }
    }

    int i = 0;
    while (i < len1 && i < len2)
    {
        if ((unsigned char)buf1[i] != (unsigned char)buf2[i])
            return (unsigned char)buf1[i] - (unsigned char)buf2[i];
        ++i;
    }

    if (len1 < len2)
        return -1;
    if (len1 > len2)
        return 1;
    return 0;
}

int string_compare_substring(const string_t *str1, int begin1, int end1, const string_t *str2,
                             int begin2, int end2)
{
    Expects_not_null(str1);
    Expects_not_null(str2);

    int b1, e1, b2, e2;
    clamp_range(str1->length, begin1, end1, &b1, &e1);
    clamp_range(str2->length, begin2, end2, &b2, &e2);

    int len1 = e1 - b1;
    int len2 = e2 - b2;

    const char *p1 = (len1 > 0) ? str1->data + b1 : "";
    const char *p2 = (len2 > 0) ? str2->data + b2 : "";

    int min_len = len1 < len2 ? len1 : len2;
    int cmp = strncmp(p1, p2, min_len);

    if (cmp != 0)
        return cmp;

    if (len1 < len2)
        return -1;
    if (len1 > len2)
        return 1;
    return 0;
}

int string_compare_cstr_substring(const string_t *str, int begin1, int end1, const char *cstr,
                                  int begin2, int end2)
{
    Expects_not_null(str);

    int b1, e1;
    clamp_range(str->length, begin1, end1, &b1, &e1);
    int len1 = e1 - b1;

    int len2 = 0;
    int b2 = 0;
    if (cstr != NULL)
    {
        int cstr_len = (int)strlen(cstr);
        int dummy_e2;
        clamp_range(cstr_len, begin2, end2, &b2, &dummy_e2);
        len2 = dummy_e2 - b2;
    }

    const char *p1 = (len1 > 0) ? str->data + b1 : "";
    const char *p2 = (len2 > 0) ? cstr + b2 : "";

    int min_len = len1 < len2 ? len1 : len2;
    int cmp = strncmp(p1, p2, min_len);

    if (cmp != 0)
        return cmp;

    if (len1 < len2)
        return -1;
    if (len1 > len2)
        return 1;
    return 0;
}

bool string_starts_with(const string_t *str, const string_t *prefix)
{
    Expects_not_null(str);

    if (prefix == NULL || prefix->length == 0)
        return true;

    if (prefix->length > str->length)
        return false;

    return memcmp(str->data, prefix->data, prefix->length) == 0;
}

bool string_starts_with_cstr(const string_t *str, const char *prefix)
{
    Expects_not_null(str);

    if (prefix == NULL || prefix[0] == '\0')
        return true;

    int prefix_len = (int)strlen(prefix);
    if (prefix_len > str->length)
        return false;

    return memcmp(str->data, prefix, prefix_len) == 0;
}

bool string_ends_with(const string_t *str, const string_t *suffix)
{
    Expects_not_null(str);

    if (suffix == NULL || suffix->length == 0)
        return true;

    if (suffix->length > str->length)
        return false;

    return memcmp(str->data + str->length - suffix->length, suffix->data, suffix->length) == 0;
}

bool string_ends_with_cstr(const string_t *str, const char *suffix)
{
    Expects_not_null(str);

    if (suffix == NULL || suffix[0] == '\0')
        return true;

    int suffix_len = (int)strlen(suffix);
    if (suffix_len > str->length)
        return false;

    return memcmp(str->data + str->length - suffix_len, suffix, suffix_len) == 0;
}

bool string_contains(const string_t *str, const string_t *substr)
{
    Expects_not_null(str);

    if (substr == NULL || substr->length == 0)
        return true;

    return string_find(str, substr) >= 0;
}

bool string_contains_cstr(const string_t *str, const char *substr)
{
    Expects_not_null(str);

    if (substr == NULL || substr[0] == '\0')
        return true;

    return string_find_cstr(str, substr) >= 0;
}

string_t *string_substring(const string_t *str, int begin, int end)
{
    return string_create_from_range(str, begin, end);
}

// ============================================================================
// Comparison operators
// ============================================================================

bool string_eq(const string_t *str1, const string_t *str2)
{
    return string_compare(str1, str2) == 0;
}

bool string_ne(const string_t *str1, const string_t *str2)
{
    return string_compare(str1, str2) != 0;
}

bool string_lt(const string_t *str1, const string_t *str2)
{
    return string_compare(str1, str2) < 0;
}

bool string_le(const string_t *str1, const string_t *str2)
{
    return string_compare(str1, str2) <= 0;
}

bool string_gt(const string_t *str1, const string_t *str2)
{
    return string_compare(str1, str2) > 0;
}

bool string_ge(const string_t *str1, const string_t *str2)
{
    return string_compare(str1, str2) >= 0;
}

int string_cmp(const string_t *str1, const string_t *str2)
{
    return string_compare(str1, str2);
}

// ============================================================================
// Formatted I/O
// ============================================================================

void string_printf(string_t *str, const char *format, ...)
{
    Expects_not_null(str);

    va_list args;
    va_start(args, format);

    if (format == NULL)
    {
        str->length = 0;
        str->data[0] = '\0';
        va_end(args);
        return;
    }

    string_vprintf(str, format, args);
    va_end(args);
}

void string_vprintf(string_t *str, const char *format, va_list args)
{
    Expects_not_null(str);

    if (format == NULL)
    {
        str->length = 0;
        str->data[0] = '\0';
        return;
    }

    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    if (needed < 0)
        return;

    str->length = 0;
    string_ensure_capacity(str, needed + 1);

    int written = vsnprintf(str->data, str->capacity, format, args);
    if (written >= 0)
        str->length = written;
}

void string_getline(string_t *str, FILE *stream)
{
    string_getline_delim(str, '\n', stream);
}

void string_getline_delim(string_t *str, char delim, FILE *stream)
{
    Expects_not_null(str);

    if (stream == NULL)
    {
        str->length = 0;
        str->data[0] = '\0';
        return;
    }

    str->length = 0;
    str->data[0] = '\0';

    int c;
    while ((c = fgetc(stream)) != EOF)
    {
        if (delim != '\0' && c == delim)
            break;

        string_push_back(str, (char)c);
    }
}

// ============================================================================
// Numeric conversions
// ============================================================================

int string_atoi(const string_t *str)
{
    Expects_not_null(str);
    return string_atoi_at(str, 0, NULL);
}

int string_atoi_at(const string_t *str, int pos, int *endpos)
{
    Expects_not_null(str);

    if (pos < 0)
        pos = 0;
    if (pos > str->length)
        pos = str->length;

    char *end_ptr = NULL;
    int value = (int)strtol(str->data + pos, &end_ptr, 10);

    if (endpos != NULL)
        *endpos = (int)(end_ptr - str->data);

    return value;
}

long string_atol(const string_t *str)
{
    Expects_not_null(str);
    return string_atol_at(str, 0, NULL);
}

long string_atol_at(const string_t *str, int pos, int *endpos)
{
    Expects_not_null(str);

    if (pos < 0)
        pos = 0;
    if (pos > str->length)
        pos = str->length;

    char *end_ptr = NULL;
    long value = strtol(str->data + pos, &end_ptr, 10);

    if (endpos != NULL)
        *endpos = (int)(end_ptr - str->data);

    return value;
}

long long string_atoll(const string_t *str)
{
    Expects_not_null(str);
    return string_atoll_at(str, 0, NULL);
}

long long string_atoll_at(const string_t *str, int pos, int *endpos)
{
    Expects_not_null(str);

    if (pos < 0)
        pos = 0;
    if (pos > str->length)
        pos = str->length;

    char *end_ptr = NULL;
    long long value = strtoll(str->data + pos, &end_ptr, 10);

    if (endpos != NULL)
        *endpos = (int)(end_ptr - str->data);

    return value;
}

double string_atof(const string_t *str)
{
    Expects_not_null(str);
    return string_atof_at(str, 0, NULL);
}

double string_atof_at(const string_t *str, int pos, int *endpos)
{
    Expects_not_null(str);

    if (pos < 0)
        pos = 0;
    if (pos > str->length)
        pos = str->length;

    char *end_ptr = NULL;
    double value = strtod(str->data + pos, &end_ptr);

    if (endpos != NULL)
        *endpos = (int)(end_ptr - str->data);

    return value;
}

string_t *string_from_int(int value)
{
    string_t *str = string_create();
    string_printf(str, "%d", value);
    return str;
}

string_t *string_from_long(long value)
{
    string_t *str = string_create();
    string_printf(str, "%ld", value);
    return str;
}

string_t *string_from_double(double value)
{
    string_t *str = string_create();
    string_printf(str, "%g", value);
    return str;
}

// ============================================================================
// Hash function
// ============================================================================

uint32_t string_hash(const string_t *str)
{
    if (str == NULL || str->length == 0)
        return 0x811c9dc5;

    uint32_t h = 0x811c9dc5;
    for (int i = 0; i < str->length; i++)
    {
        h ^= (uint32_t)(unsigned char)str->data[i];
        h *= 0x01000193;
    }
    return h ? h : 1;
}

// ============================================================================
// String list functions
// ============================================================================

string_list_t *string_list_create(void)
{
    string_list_t *list = xcalloc(1, sizeof(string_list_t));
    list->capacity = STRING_LIST_INITIAL_CAPACITY;
    list->strings = list->inline_strings;
    list->size = 0;
    return list;
}

void string_list_destroy(string_list_t **list)
{
    if (list == NULL || *list == NULL)
        return;

    for (int i = 0; i < (*list)->size; i++)
        string_destroy(&(*list)->strings[i]);

    if (string_list_is_heap_allocated(*list))
        xfree((*list)->strings);

    xfree(*list);
    *list = NULL;
}

void string_list_move_push_back(string_list_t *list, string_t **str)
{
    Expects_not_null(list);

    if (str == NULL || *str == NULL)
        return;

    if (list->size >= list->capacity)
    {
        int new_capacity = list->capacity * STRING_LIST_GROW_FACTOR;
        if (new_capacity <= list->size)
            new_capacity = list->size + 1;
        string_list_normalize_capacity(list, new_capacity);
    }

    list->strings[list->size++] = *str;
    *str = NULL;
}

void string_list_push_back(string_list_t *list, const string_t *str)
{
    Expects_not_null(list);

    if (str == NULL)
        return;

    string_t *cloned = string_create_from(str);
    string_list_move_push_back(list, &cloned);
}

int string_list_size(const string_list_t *list)
{
    Expects_not_null(list);
    return list->size;
}

const string_t *string_list_at(const string_list_t *list, int index)
{
    Expects_not_null(list);

    if (index < 0 || index >= list->size)
        return NULL;

    return list->strings[index];
}

void string_list_insert(string_list_t *list, int index, const string_t *str)
{
    Expects_not_null(list);

    if (index < 0)
        index = 0;
    if (index > list->size)
        index = list->size;

    string_t *to_insert = (str == NULL) ? string_create() : string_create_from(str);
    string_list_move_insert(list, index, &to_insert);
}

void string_list_move_insert(string_list_t *list, int index, string_t **str)
{
    Expects_not_null(list);

    if (str == NULL || *str == NULL)
        return;

    if (index < 0)
        index = 0;
    if (index > list->size)
        index = list->size;

    if (list->size >= list->capacity)
    {
        int new_capacity = list->capacity * STRING_LIST_GROW_FACTOR;
        if (new_capacity <= list->size)
            new_capacity = list->size + 1;
        string_list_normalize_capacity(list, new_capacity);
    }

    if (index < list->size)
    {
        memmove(&list->strings[index + 1], &list->strings[index],
                (list->size - index) * sizeof(string_t *));
    }

    list->strings[index] = *str;
    list->size++;
    *str = NULL;
}

void string_list_erase(string_list_t *list, int index)
{
    Expects_not_null(list);

    if (index < 0 || index >= list->size)
        return;

    string_destroy(&list->strings[index]);

    if (index < list->size - 1)
    {
        memmove(&list->strings[index], &list->strings[index + 1],
                (list->size - index - 1) * sizeof(string_t *));
    }

    list->size--;
}

void string_list_clear(string_list_t *list)
{
    Expects_not_null(list);

    for (int i = 0; i < list->size; i++)
        string_destroy(&list->strings[i]);

    list->size = 0;
}

char **string_list_release_cstr_array(string_list_t **list, int *out_size)
{
    Expects_not_null(list);
    Expects_not_null(*list);
    Expects_not_null(out_size);
    string_list_t *slist = *list;

    if (slist->size == 0)
    {
        *list = NULL;
        *out_size = 0;
        if (string_list_is_heap_allocated(slist))
            xfree(slist->strings);
        xfree(slist);
        return NULL;
    }

    char **cstr_array = xcalloc(slist->size + 1, sizeof(char *));
    *out_size = slist->size;
    for (int i = 0; i < slist->size; i++)
        cstr_array[i] = string_release(&slist->strings[i]);
    if (string_list_is_heap_allocated(slist))
        xfree(slist->strings);
    xfree(slist);
    *list = NULL;
    *out_size = slist->size;
    return cstr_array;
}
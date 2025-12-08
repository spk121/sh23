#include "string_t.h"
#include "logging.h"
#include "xalloc.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Helper: Ensure capacity
static void string_ensure_capacity(string_t *str, int needed)
{
    Expects_not_null(str);
    Expects(needed >= 0);

    if (needed < str->capacity)
        return;

    int new_capacity = str->capacity ? str->capacity : INITIAL_CAPACITY;
    while (new_capacity < needed)
    {
        if (new_capacity > INT_MAX / GROW_FACTOR)
        {
            new_capacity = needed; // prevent overflow
        }
        else
        {
            new_capacity *= GROW_FACTOR;
        }
    }

    char *new_data = xrealloc(str->data, new_capacity);
    str->data = new_data;
    str->capacity = new_capacity;
}

// Create and destroy
string_t *string_create_from_cstr(const char *data)
{
    Expects_not_null(data);
    int len = strlen(data);
    string_t *str = string_create_empty(len + 1);
    string_set_cstr(str, data);
    return str;
}

string_t *string_create_from_cstr_len(const char *data, int len)
{
    Expects_not_null(data);
    Expects(len >= 0);

    string_t *str = string_create_empty(len + 1);
    memcpy(str->data, data, len);
    str->data[len] = '\0';
    str->length = len;
    return str;
}

string_t *string_vcreate(const char *format, va_list args)
{
    Expects_not_null(format);

    // First, determine required length
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    string_t *str = string_create_empty(needed + 1);
    int written = vsnprintf(str->data, str->capacity, format, args);
    str->length = written;
    return str;
}

string_t *string_create_from_format(const char *format, ...)
{
    Expects_not_null(format);

    va_list args;
    va_start(args, format);
    string_t *str = string_vcreate(format, args);
    va_end(args);

    return str;
}

string_t *string_create_empty(int min_capacity)
{
    Expects(min_capacity >= 0);
    string_t *str = xcalloc(1, sizeof(string_t));

    // Can't use string_ensure_capacity here because str->data is NULL.
    int needed = min_capacity < INITIAL_CAPACITY ? INITIAL_CAPACITY : min_capacity;
    int new_capacity;
    if (needed >= INT_MAX / GROW_FACTOR)
    {
        new_capacity = needed;
    }
    else
    {
        new_capacity = INITIAL_CAPACITY;
        while (new_capacity < needed)
        {
            new_capacity *= GROW_FACTOR;
        }
    }
    str->data = xcalloc(new_capacity, 1);
    str->capacity = new_capacity;
    return str;
}

string_t *string_clone(const string_t *other)
{
    Expects_not_null(other);
    Expects(other->data != NULL);
    Expects(other->length >= 0);

    string_t *str = string_create_empty(other->length + 1);
    string_set_cstr(str, string_data(other));
    return str;
}

string_t *string_create_from_range(const string_t *str, int start, int length)
{
    Expects_not_null(str);
    Expects(start >= 0);
    Expects(length >= 0);
    Expects(start + length <= str->length);

    string_t *result = string_create_empty(length + 1);
    memcpy(result->data, str->data + start, length);
    result->data[length] = '\0';
    result->length = length;
    return result;
}

void string_destroy(string_t *str)
{
    Expects_not_null(str);
    Expects(str->length >= 0);
    Expects(str->capacity >= 0);
    Expects(str->data != NULL);

    xfree(str->data);
    xfree(str);
}

// Accessors
const char *string_data(const string_t *str)
{
    Expects_not_null(str);
    Expects(str->length >= 0);
    Expects(str->capacity >= 0);
    Expects(str->data != NULL);

    return str->data;
}

char string_char_at(const string_t *str, int index)
{
    Expects_not_null(str);
    Expects(index >= 0);
    Expects(index < str->length);

    return str->data[index];
}

int string_length(const string_t *str)
{
    Expects_not_null(str);

    return str->length;
}

int string_capacity(const string_t *str)
{
    Expects_not_null(str);

    return str->capacity;
}

bool string_is_empty(const string_t *str)
{
    Expects_not_null(str);

    return str->length == 0;
}

// Modification
void string_append_cstr(string_t *str, const char *data)
{
    Expects_not_null(str);
    Expects_not_null(str->data);
    Expects_not_null(data);

    int append_len = strlen(data);
    int new_len = str->length + append_len + 1;

    string_ensure_capacity(str, new_len);

    memcpy(str->data + str->length, data, append_len);
    str->length += append_len;
    str->data[str->length] = '\0';
}

void string_append_ascii_char(string_t *str, char c)
{
    Expects_not_null(str);
    Expects(c >= 0);

    string_ensure_capacity(str, str->length + 1);
    str->data[str->length] = c;
    str->length++;
    str->data[str->length] = '\0';
}

void string_append(string_t *str, const string_t *other)
{
    Expects_not_null(str);
    Expects_not_null(other);
    string_append_cstr(str, string_data(other));
}

void string_clear(string_t *str)
{
    Expects_not_null(str);
    Expects_not_null(str->data);

    memset(str->data, 0, str->capacity);
    str->length = 0;
}

void string_set_cstr(string_t *str, const char *data)
{
    Expects_not_null(str);
    Expects_not_null(data);
    int new_len = strlen(data);

    string_ensure_capacity(str, new_len + 1);
    memcpy(str->data, data, new_len);
    str->data[new_len] = '\0';
    str->length = new_len;
}

void string_resize(string_t *str, int new_capacity)
{
    Expects_not_null(str);
    Expects(new_capacity >= 0);
    if (new_capacity < str->length + 1)
        new_capacity = str->length + 1;

    if (new_capacity > str->capacity)
    {
        string_ensure_capacity(str, new_capacity);
        return;
    }
    else if (new_capacity == str->capacity)
    {
        return; // no change
    }
    else
    {
        int shrink_to = str->capacity;
        while (shrink_to - str->length > REDUCE_THRESHOLD && shrink_to / GROW_FACTOR >= str->length + 1)
        {
            shrink_to /= GROW_FACTOR;
        }
        if (shrink_to < str->capacity)
        {
            char *new_data = xrealloc(str->data, shrink_to);
            str->data = new_data;
            str->capacity = shrink_to;
        }
    }
}

void string_drop_front(string_t *str, int n)
{
    Expects_not_null(str);
    Expects(n >= 0);
    Expects(n <= str->length);

    if (n == 0)
        return;

    memmove(str->data, str->data + n, str->length - n);
    str->length -= n;
    str->data[str->length] = '\0';
}

void string_drop_back(string_t *str, int n)
{
    Expects_not_null(str);
    Expects(n >= 0);
    Expects(n <= str->length);

    if (n == 0)
        return;

    str->length -= n;
    str->data[str->length] = '\0';
}

// Operations
string_t *string_substring(const string_t *str, int start, int length)
{
    Expects_not_null(str);
    Expects(start >= 0);
    Expects(length >= 0);
    Expects(start < str->length);

    if (start + length > str->length)
        length = str->length - start;

    string_t *result = string_create_empty(length + 1);

    memcpy(result->data, str->data + start, length);
    result->data[length] = '\0';
    result->length = length;
    return result;
}

int string_compare(const string_t *str1, const string_t *str2)
{
    Expects_not_null(str1);
    Expects_not_null(str2);
    Expects(str1->data != NULL);
    Expects(str2->data != NULL);

    return strcmp(string_data(str1), string_data(str2));
}

int string_compare_cstr(const string_t *str, const char *data)
{
    Expects_not_null(str);
    Expects_not_null(data);
    Expects(str->data != NULL);

    return strcmp(string_data(str), data);
}

char *string_find_cstr(const string_t *str, const char *substr)
{
    Expects_not_null(str);
    Expects_not_null(str->data);
    Expects_not_null(substr);

    return strstr(str->data, substr);
}

bool string_starts_with_cstr_at(const string_t *str, const char *prefix, int pos)
{
    Expects_not_null(str);
    Expects_not_null(str->data);
    Expects_not_null(prefix);
    Expects(pos >= 0);
    Expects(pos <= str->length);

    int prefix_len = strlen(prefix);
    if (pos + prefix_len > str->length)
        return false;

    return strncmp(str->data + pos, prefix, prefix_len) == 0;
}

bool string_contains_glob(const string_t *str)
{
    Expects_not_null(str);
    Expects_not_null(str->data);
    int len = str->length;
    const char *s = string_data(str);
    for (int i = 0; i < len; i++)
    {
        if (s[i] == '*' || s[i] == '?' || s[i] == '[')
            return true;
    }
    return false;
}

// UTF-8 specific
static bool is_utf8_continuation_byte(uint8_t c)
{
    return (c & 0xC0) == 0x80;
}

static int utf8_char_size(uint8_t c)
{
    if ((c & 0x80) == 0)
        return 1; // ASCII
    if ((c & 0xE0) == 0xC0)
        return 2; // 2-byte
    if ((c & 0xF0) == 0xE0)
        return 3; // 3-byte
    if ((c & 0xF8) == 0xF0)
        return 4; // 4-byte
    return 0;     // Invalid
}

int string_utf8_length(const string_t *str)
{
    Expects_not_null(str);
    Expects_not_null(str->data);

    int count = 0;
    for (int i = 0; i < str->length;)
    {
        int size = utf8_char_size((uint8_t)str->data[i]);
        if (size == 0 || i + size > str->length)
            return count; // Invalid or truncated
        i += size;
        count++;
    }
    return count;
}

bool string_is_valid_utf8(const string_t *str)
{
    Expects_not_null(str);
    Expects_not_null(str->data);

    for (int i = 0; i < str->length;)
    {
        uint8_t c = (uint8_t)str->data[i];
        int size = utf8_char_size(c);
        if (size == 0 || i + size > str->length)
            return false;

        // Check continuation bytes
        for (int j = 1; j < size; j++)
        {
            if (!is_utf8_continuation_byte((uint8_t)str->data[i + j]))
                return false;
        }

        i += size;
    }
    return true;
}

bool string_utf8_char_at(const string_t *str, int char_index, char *buffer, int buffer_size)
{
    Expects_not_null(str);
    Expects_not_null(str->data);
    Expects_not_null(buffer);
    Expects(buffer_size >= 5); // Need space for max 4-byte char + null

    int byte_pos = 0;
    int current_char = 0;

    while (byte_pos < str->length)
    {
        if (current_char == char_index)
        {
            int size = utf8_char_size((uint8_t)str->data[byte_pos]);
            if (size == 0 || byte_pos + size > str->length || size > buffer_size - 1)
                return false;

            memcpy(buffer, str->data + byte_pos, size);
            buffer[size] = '\0';
            return true;
        }

        int size = utf8_char_size((uint8_t)str->data[byte_pos]);
        if (size == 0 || byte_pos + size > str->length)
            return false;

        byte_pos += size;
        current_char++;
    }

    return false; // Index out of range
}

// ============================================================================
// String List Functions
// ============================================================================

string_list_t *string_list_create(void)
{
    string_list_t *list = xcalloc(1, sizeof(string_list_t));
    list->capacity = INITIAL_CAPACITY;
    list->strings = xcalloc(list->capacity, sizeof(string_t *));
    list->size = 0;
    return list;
}

void string_list_destroy(string_list_t *list)
{
    if (list == NULL)
        return;
    
    for (int i = 0; i < list->size; i++)
    {
        string_destroy(list->strings[i]);
    }
    
    xfree(list->strings);
    xfree(list);
}

void string_list_take_append(string_list_t *list, string_t *str)
{
    Expects_not_null(list);
    Expects_not_null(str);
    
    if (list->size >= list->capacity)
    {
        list->capacity *= GROW_FACTOR;
        list->strings = xrealloc(list->strings, list->capacity * sizeof(string_t *));
    }
    
    list->strings[list->size++] = str;
}

void string_list_clone_append(string_list_t *list, const string_t *str)
{
    Expects_not_null(list);
    Expects_not_null(str);
    
    string_t *cloned = string_clone(str);
    string_list_take_append(list, cloned);
}

int string_list_size(const string_list_t *list)
{
    return_val_if_null(list, 0);
    return list->size;
}

const string_t *string_list_get(const string_list_t *list, int index)
{
    return_val_if_null(list, NULL);
    return_val_if_lt(index, 0, NULL);
    return_val_if_ge(index, list->size, NULL);
    
    return list->strings[index];
}

void string_list_take_replace(string_list_t *list, int index, string_t *str)
{
    Expects_not_null(list);
    Expects_not_null(str);
    Expects_ge(index, 0);
    Expects_lt(index, list->size);
    
    string_destroy(list->strings[index]);
    list->strings[index] = str;
}

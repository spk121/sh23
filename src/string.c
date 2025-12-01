#include "string.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "logging.h"

struct String
{
    char *data;      // UTF-8 encoded string
    int length;      // Byte length (excluding null terminator)
    int capacity;    // Total allocated bytes (including null terminator)
};

#define INITIAL_CAPACITY 16
#define GROW_FACTOR 2

// Helper: Ensure capacity
static int string_ensure_capacity(String *str, int needed)
{
    return_val_if_null (str, -1);
    return_val_if_lt (needed, 0, -1);
 
    if (needed <= str->capacity)
        return 0;

    int new_capacity = str->capacity ? str->capacity : INITIAL_CAPACITY;
    while (new_capacity < needed)
        new_capacity *= GROW_FACTOR;

    char *new_data = realloc(str->data, new_capacity);
    if (!new_data)
    {
        log_fatal ("string_ensure_capacity: memory allocation failure");
        return -1;
    }

    str->data = new_data;
    str->capacity = new_capacity;
    return 0;
}

// Create and destroy
String *string_create_from_cstr(const char *data)
{
    if (!data)
    {
        log_fatal ("string_create_from_cstr: argument 'data' is null");
        return NULL;
    }

    int len = strlen(data);
    String *str = string_create_empty(len + 1);
    if (!str)
    {
        log_fatal ("string_create_from_cstr: out of memory");
        return NULL;
    }
    if (string_set_cstr(str, data) != 0)
    {
        string_destroy(str);
        log_fatal ("string_create_from_cstr: set failed");
        return NULL;
    }
    return str;
}

String *string_create_empty(int capacity)
{
    if (capacity < 0)
    {
        log_fatal ("string_create_empty: argument 'capacity' is negative");
        return NULL;
    }
    String *str = malloc(sizeof(String));
    if (!str) {
        log_fatal ("string_create_empty: error: out of memory");
        return NULL;
    }
    str->data = NULL;
    str->length = 0;
    str->capacity = 0;

    if (capacity > 0 && string_ensure_capacity(str, capacity) != 0)
    {
        free (str);
        log_fatal ("string_create_empty: out of memory");
        return NULL;
    }

    if (str->capacity > 0)
    {
        str->data[0] = '\0';
    }
    return str;
}

String *string_create_from(String *other)
{
    return_val_if_null (other, NULL);
    return string_create_from_cstr(string_data(other));
}

void string_destroy(String *str)
{
    if (str)
    {
        log_debug("string_destroy: freeing string %p = %s", str, string_data(str));        
        free(str->data);
        free(str);
    }
}

// Accessors
const char *string_data(const String *str)
{
    return_val_if_null (str, NULL);
    return str->data ? str->data : "";
}

int string_length(const String *str)
{
    return_val_if_null (str, 0);
    return str->length;
}

int string_capacity(const String *str)
{
    return_val_if_null (str, 0);
    return str->capacity;
}

int string_is_empty(const String *str)
{
    return_val_if_null (str, 0);
    return str->length == 0;
}

// Modification
int string_append_cstr(String *str, const char *data)
{
    return_val_if_null (str, -1);
    return_val_if_null (data, -1);
    int append_len = strlen(data);
    int new_len = str->length + append_len + 1;

    if (string_ensure_capacity(str, new_len) != 0)
    {
        log_fatal ("string_append_cstr: out of memory");
        return -1;
    }

    memcpy(str->data + str->length, data, append_len);
    str->length += append_len;
    str->data[str->length] = '\0';
    return 0;
}

int string_append_ascii_char(String *str, char c)
{
    return_val_if_null (str, -1);
    return_val_if_lt(c, 0, -1);
    return_val_if_ge(c, 128, -1);
    const char cstr[2] = {c, 0};
    return string_append_cstr(str, cstr);
}

int string_append(String *str, const String *other)
{
    return_val_if_null (str, -1);
    return_val_if_null (other, -1);
    return string_append_cstr(str, string_data(other));
}

int string_clear(String *str)
{
    return_val_if_null(str, -1);
    str->length = 0;
    if (str->data)
        str->data[0] = '\0';
    return 0;
}

int string_set_cstr(String *str, const char *data)
{
    return_val_if_null(str, -1);
    return_val_if_null(data, -1);
    int new_len = strlen(data) + 1;

    if (string_ensure_capacity(str, new_len) != 0)
    {
        log_fatal ("string_set_cstr: out of memory");
        return -1;
    }

    memcpy(str->data, data, new_len);
    str->length = new_len - 1;
    return 0;
}

int string_resize(String *str, int new_capacity)
{
    return_val_if_null(str, -1);
    return_val_if_lt(new_capacity, 0, -1);
    if (new_capacity < str->length + 1)
        new_capacity = str->length + 1;

    if (string_ensure_capacity(str, new_capacity) != 0)
    {
        log_fatal ("string_resize: out of memory");
        return -1;
    }

    str->capacity = new_capacity;
    str->data[str->length] = '\0';
    return 0;
}

// Operations
String *string_substring(const String *str, int start, int length)
{
    return_val_if_null(str, string_create_empty(0));
    return_val_if_lt(start, 0, string_create_empty(0));
    return_val_if_lt(length, 0, string_create_empty(0));
    return_val_if_ge(start, str->length, string_create_empty(0));

    if (start + length > str->length)
        length = str->length - start;

    String *result = string_create_empty(length + 1);
    if (!result)
    {
        log_fatal ("string_substring: out of memory");
        return NULL;
    }

    memcpy(result->data, str->data + start, length);
    result->data[length] = '\0';
    result->length = length;
    return result;
}

int string_compare(const String *str1, const String *str2)
{
    if (!str1 || !str2)
        return str1 == str2 ? 0 : str1 ? 1
                                       : -1;
    return strcmp(string_data(str1), string_data(str2));
}

int string_compare_cstr(const String *str, const char *data)
{
    return_val_if_null(str, data ? 1 : 0);
    return_val_if_null (data, -1);
    return strcmp(string_data(str), data);
}

int string_find_cstr(const String *str, const char *substr, int *pos)
{
    return_val_if_null(str, -1);
    return_val_if_null(substr, -1);
    return_val_if_null(pos, -1);

    char *found = strstr(str->data, substr);
    if (!found)
        return -1;
    *pos = found - str->data;
    return 0;
}

int string_replace_cstr(String *str, const char *find, const char *replace, int max_replacements)
{
    return_val_if_null (str, -1);
    return_val_if_null (find, -1);
    return_val_if_null (replace, -1);
    return_val_if_eq (find[0], '\0', -1);

    size_t find_len = strlen(find);
    String *result = string_create_empty(str->length + 1);
    if (!result) {
        log_fatal ("string_replace_cstr: out of memory");
        return -1;
    }

    char *current = str->data;
    int replacements = 0;

    while (*current && (max_replacements == 0 || replacements < max_replacements))
    {
        char *next = strstr(current, find);
        if (!next)
        {
            if (string_append_cstr(result, current) != 0) {
                string_destroy (result);
                log_fatal ("string_replace_cstr: append failed");
                return -1;
            }
            break;
        }

        // Append up to the match
        int prefix_len = next - current;
        char *prefix = malloc(prefix_len + 1);
        if (!prefix)
        {
            string_destroy(result);
            log_fatal ("string_replace_cstr: out of memory");
            return -1;
        }
        memcpy(prefix, current, prefix_len);
        prefix[prefix_len] = '\0';
        if (string_append_cstr(result, prefix) != 0) {
            free(prefix);
            string_destroy(result);
            log_fatal("string_replace_cstr: append failed");
            return -1;
        }
        free(prefix);
        if (string_append_cstr(result, replace) != 0) {
            string_destroy(result);
            log_fatal("string_replace_cstr: append failed");
            return -1;
        }
        current = next + find_len;
        replacements++;
    }

    // Set result back to str
    int ret = string_set_cstr(str, string_data(result));
    string_destroy(result);
    return ret;
}

// UTF-8 specific
static int is_utf8_continuation_byte(uint8_t c)
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

int string_utf8_length(const String *str)
{
    return_val_if_null(str, 0);
    if (!str->data)
        return 0;

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

int string_is_valid_utf8(const String *str)
{
    return_val_if_null(str, 1);
    if (!str->data)
        return 1; // Empty is valid

    for (int i = 0; i < str->length;)
    {
        uint8_t c = (uint8_t)str->data[i];
        int size = utf8_char_size(c);
        if (size == 0 || i + size > str->length)
            return 0;

        // Check continuation bytes
        for (int j = 1; j < size; j++)
        {
            if (!is_utf8_continuation_byte((uint8_t)str->data[i + j]))
                return 0;
        }

        i += size;
    }
    return 1;
}

int string_utf8_char_at(const String *str, int char_index, char *buffer, int buffer_size)
{
    return_val_if_null (str, -1);
    return_val_if_null (buffer, -1);
    return_val_if_lt (buffer_size, 5, -1); // Need space for max 4-byte char + null

    int byte_pos = 0;
    int current_char = 0;

    while (byte_pos < str->length)
    {
        if (current_char == char_index)
        {
            int size = utf8_char_size((uint8_t)str->data[byte_pos]);
            if (size == 0 || byte_pos + size > str->length || size > buffer_size - 1)
                return -1;

            memcpy(buffer, str->data + byte_pos, size);
            buffer[size] = '\0';
            return 0;
        }

        int size = utf8_char_size((uint8_t)str->data[byte_pos]);
        if (size == 0 || byte_pos + size > str->length)
            return -1;

        byte_pos += size;
        current_char++;
    }

    return -1; // Index out of range
}

#include "string_t.h"
#include "logging.h"
#include "xalloc.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Helpers
static void string_ensure_capacity(string_t *str, int needed)
{
    Expects_not_null(str);
    Expects(needed >= 0);
    
    // Since this is used in creation, handle nulls and zeros.

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

    if (str->data == NULL)
    {
        str->data = xcalloc(new_capacity, 1);
        str->capacity = new_capacity;
        return;
    }

    char *new_data = xrealloc(str->data, new_capacity);
    str->data = new_data;
    str->capacity = new_capacity;
}


// Constructors
string_t *string_create(void)
{
    string_t *str = xcalloc(1, sizeof(string_t));

    str->data = xcalloc(INITIAL_CAPACITY, 1);
    str->capacity = INITIAL_CAPACITY;
    str->length = 0;

    return str;
}

string_t *string_create_from_n_chars(size_t count, char ch)
{
    Expects(count <= INT_MAX);

    string_t *str = xcalloc(1, sizeof(string_t));
    int needed_capacity = (int)(count + 1);
    string_ensure_capacity(str, needed_capacity);

    for (size_t i = 0; i < count; i++)
    {
        str->data[i] = ch;
    }
    str->data[count] = '\0';
    str->length = (int)count;

    return str;
}

string_t *string_create_from_cstr(const char *data)
{
    Expects_not_null(data);
    int len = strlen(data);
    return string_create_from_cstr_len(data, len);
}

string_t *string_create_from_cstr_len(const char *data, int len)
{
    Expects_not_null(data);
    Expects(len >= 0);

    string_t *str = xcalloc(1, sizeof(string_t));
    int needed_capacity = len + 1;
    string_ensure_capacity(str, needed_capacity);

    memcpy(str->data, data, len);
    str->data[len] = '\0';
    str->length = len;

    return str;
}

string_t *string_create_from(const string_t *other)
{
    Expects_not_null(other);
    Expects(other->data != NULL);
    Expects(other->length >= 0);

    return string_create_from_range(other, 0, other->length);
}

string_t *string_create_from_range(const string_t *str, int start, int end)
{
    Expects_not_null(str);
    Expects(start >= 0);
    Expects(end >= start);
    Expects(end <= str->length);

    int length = end - start;
    string_t *result = xcalloc(1, sizeof(string_t));
    int needed_capacity = length + 1;
    string_ensure_capacity(result, needed_capacity);

    memcpy(result->data, str->data + start, length);
    result->data[length] = '\0';
    result->length = length;
    return result;
}

// Destructors
void string_destroy(string_t **str)
{
    Expects_not_null(str);
    string_t *s = *str;
    Expects_not_null(s);
    Expects(s->length >= 0);
    Expects(s->capacity >= 0);
    Expects(s->data != NULL);

    xfree(s->data);
    xfree(s);
    *str = NULL;
}

char *string_release(string_t **str)
{
    Expects_not_null(str);
    string_t *s = *str;
    Expects_not_null(s);
    Expects(s->length >= 0);
    Expects(s->capacity >= 0);
    Expects(s->data != NULL);

    char *result = s->data;
    xfree(s);
    *str = NULL;
    return result;
}

// Setters
void string_set(string_t *str, const string_t *str2)
{
    Expects_not_null(str);
    Expects_not_null(str2);
    Expects(str2->length >= 0);

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

    // Free existing data in str
    xfree(str->data);

    // Move data from str2 to str
    str->data = str2->data;
    str->length = str2->length;
    str->capacity = str2->capacity;

    // Invalidate str2
    str2->data = NULL;
    str2->length = 0;
    str2->capacity = 0;
    string_ensure_capacity(str2, INITIAL_CAPACITY);
}

// Move the contents of str2 into str and then destroy str2
void string_consume(string_t *str, string_t **str2)
{
    Expects_not_null(str);
    Expects_not_null(str2);
    Expects_not_null(*str2);
    
    string_move(str, *str2);
    string_destroy(str2);
}

void string_set_cstr(string_t *str, const char *cstr)
{
    Expects_not_null(str);
    Expects_not_null(cstr);
    int len = strlen(cstr);

    int needed_capacity = len + 1;
    string_ensure_capacity(str, needed_capacity);

    memcpy(str->data, cstr, len);
    str->length = len;
    str->data[str->length] = '\0';
}

void string_set_char(string_t *str, char ch)
{
    Expects_not_null(str);

    int needed_capacity = 1 + 1;
    string_ensure_capacity(str, needed_capacity);

    str->data[0] = ch;
    str->length = 1;
    str->data[str->length] = '\0';
}

void string_set_data(string_t *str, const char *data, int n)
{
    Expects_not_null(str);
    Expects_not_null(data);
    Expects(n >= 0);

    int needed_capacity = n + 1;
    string_ensure_capacity(str, needed_capacity);

    memcpy(str->data, data, n);
    str->length = n;
    str->data[str->length] = '\0';
}

void string_set_n_chars(string_t *str, size_t count, char ch)
{
    Expects_not_null(str);

    int needed_capacity = (int)(count + 1);
    string_ensure_capacity(str, needed_capacity);

    for (size_t i = 0; i < count; i++)
    {
        str->data[i] = ch;
    }
    str->length = (int)count;
    str->data[str->length] = '\0';
}

void string_set_substring(string_t *str, const string_t *str2, int begin2, int end2)
{
    Expects_not_null(str);
    Expects_not_null(str2);
    Expects(begin2 >= 0);
    Expects(end2 >= begin2);
    Expects(end2 <= str2->length);

    int substring_length = end2 - begin2;
    int needed_capacity = substring_length + 1;
    string_ensure_capacity(str, needed_capacity);

    memcpy(str->data, str2->data + begin2, substring_length);
    str->length = substring_length;
    str->data[str->length] = '\0';
}

// Accessors
char string_at(const string_t *str, int index)
{
    Expects_not_null(str);
    Expects(index >= 0);
    Expects(index < str->length);

    return str->data[index];
}

char string_front(const string_t *str)
{
    Expects_not_null(str);
    Expects(str->length > 0);

    return str->data[0];
}

char string_back(const string_t *str)
{
    Expects_not_null(str);
    Expects(str->length > 0);

    return str->data[str->length - 1];
}

char *string_data(string_t *str)
{
    Expects_not_null(str);
    Expects(str->length >= 0);
    Expects(str->capacity >= 0);
    Expects(str->data != NULL);

    return str->data;
}

char *string_data_at(string_t *str, int pos)
{
    Expects_not_null(str);
    Expects(pos >= 0);
    Expects(pos <= str->length); // allow pointing to null terminator
    Expects(str->data != NULL);

    return str->data + pos;
}

const char *string_cstr(const string_t *str)
{
    Expects_not_null(str);
    Expects(str->length >= 0);
    Expects(str->capacity >= 0);
    Expects(str->data != NULL);

    return str->data;
}

// Capacity
bool string_empty(const string_t *str)
{
    Expects_not_null(str);

    return str->length == 0;
}

int string_size(const string_t *str)
{
    Expects_not_null(str);

    return str->length;
}

int string_length(const string_t *str)
{
    Expects_not_null(str);

    return str->length;
}

int string_max_size(const string_t *str)
{
    Expects_not_null(str);

    return INT_MAX - 1; // Reserve space for null terminator
}

void string_reserve(string_t *str, int new_cap)
{
    Expects_not_null(str);
    Expects(new_cap >= 0);

    if (new_cap + 1 > str->capacity)
    {
        string_ensure_capacity(str, new_cap + 1);
    }
}

int string_capacity(const string_t *str)
{
    Expects_not_null(str);

    return str->capacity;
}

void string_shrink_to_fit(string_t *str)
{
    Expects_not_null(str);

    if (str->capacity > str->length + 1)
    {
        int new_capacity = str->length + 1;
        char *new_data = xrealloc(str->data, new_capacity);
        str->data = new_data;
        str->capacity = new_capacity;
    }
}

// Modifiers
void string_clear(string_t *str)
{
    Expects_not_null(str);

    str->length = 0;
    if (str->data != NULL && str->capacity > 0)
    {
        str->data[0] = '\0';
    }
}

#if 0
void string_insert(string_t *str, int pos, const string_t *other);
void string_insert_n_chars(string_t *str, int pos, size_t count, char ch);
void string_insert_cstr(string_t *str, int pos, const char *cstr);
void string_insert_data(string_t *str, int pos, const char *data, int len);
void string_erase(string_t *str, int pos, int len);
#endif

void string_push_back(string_t *str, char ch)
{
    Expects_not_null(str);

    int needed_capacity = str->length + 1 + 1;
    string_ensure_capacity(str, needed_capacity);

    str->data[str->length] = ch;
    str->length += 1;
    str->data[str->length] = '\0';
}

void string_pop_back(string_t *str)
{
    Expects_not_null(str);
    Expects(str->length > 0);

    str->length -= 1;
    str->data[str->length] = '\0';
}

void string_append(string_t *str, const string_t *other)
{
    Expects_not_null(str);
    Expects_not_null(other);
    Expects(other->length >= 0);

    int needed_capacity = str->length + other->length + 1;
    string_ensure_capacity(str, needed_capacity);

    memcpy(str->data + str->length, other->data, other->length);
    str->length += other->length;
    str->data[str->length] = '\0';
}

void string_append_substring(string_t *str, const string_t *other, int begin, int end)
{
    Expects_not_null(str);
    Expects_not_null(other);
    Expects(begin >= 0);
    Expects(end >= begin);
    Expects(end <= other->length);

    int substring_length = end - begin;
    int needed_capacity = str->length + substring_length + 1;
    string_ensure_capacity(str, needed_capacity);

    memcpy(str->data + str->length, other->data + begin, substring_length);
    str->length += substring_length;
    str->data[str->length] = '\0';
}

void string_append_cstr(string_t *str, const char *cstr)
{
    Expects_not_null(str);
    Expects_not_null(cstr);
    int cstr_len = strlen(cstr);

    int needed_capacity = str->length + cstr_len + 1;
    string_ensure_capacity(str, needed_capacity);

    memcpy(str->data + str->length, cstr, cstr_len);
    str->length += cstr_len;
    str->data[str->length] = '\0';
}

void string_append_n_chars(string_t *str, size_t count, char ch)
{
    Expects_not_null(str);
    Expects(count <= INT_MAX);

    int needed_capacity = str->length + (int)count + 1;
    string_ensure_capacity(str, needed_capacity);

    for (size_t i = 0; i < count; i++)
    {
        str->data[str->length + i] = ch;
    }
    str->length += (int)count;
    str->data[str->length] = '\0';
}

void string_append_data(string_t *str, const char *data, int len)
{
    Expects_not_null(str);
    Expects_not_null(data);
    Expects(len >= 0);

    int needed_capacity = str->length + len + 1;
    string_ensure_capacity(str, needed_capacity);

    memcpy(str->data + str->length, data, len);
    str->length += len;
    str->data[str->length] = '\0';
}

void string_append_char(string_t *str, char c)
{
    Expects_not_null(str);

    int needed_capacity = str->length + 1 + 1;
    string_ensure_capacity(str, needed_capacity);

    str->data[str->length] = c;
    str->length += 1;
    str->data[str->length] = '\0';
}

#if 0
void string_replace(string_t *str, int pos, int len, const string_t *other);
void string_replace_substring(string_t *str, int pos, int len, const string_t *other, int begin2, int end2);
void string_replace_cstr(string_t *str, int pos, int len, const char *cstr);
void string_replace_n_chars(string_t *str, int pos, int len, size_t count, char ch);
void string_replace_data(string_t *str, int pos, int len, const char *data, int data_len);
void string_copy_to_cstr(const string_t *str, char *dest, int count);
void string_copy_to_cstr_at(const string_t *str, int pos, char *dest, int count);
#endif

void string_resize(string_t *str, int new_size)
{
    Expects_not_null(str);
    Expects(new_size >= 0);

    int needed_capacity = new_size + 1;
    string_ensure_capacity(str, needed_capacity);

    if (new_size > str->length)
    {
        // Initialize new characters to null bytes
        memset(str->data + str->length, 0, new_size - str->length);
    }
    str->length = new_size;
    str->data[str->length] = '\0';
}

void string_resize_with_char(string_t *str, int new_size, char ch)
{
    Expects_not_null(str);
    Expects(new_size >= 0);

    int needed_capacity = new_size + 1;
    string_ensure_capacity(str, needed_capacity);

    if (new_size > str->length)
    {
        // Initialize new characters to ch
        for (int i = str->length; i < new_size; i++)
        {
            str->data[i] = ch;
        }
    }
    str->length = new_size;
    str->data[str->length] = '\0';
}

int string_find(const string_t *str, const string_t *substr)
{
    return string_find_at(str, substr, 0);
}

int string_find_at(const string_t *str, const string_t *substr, int pos)
{
    Expects_not_null(str);
    Expects_not_null(substr);
    Expects(pos >= 0);
    Expects(pos <= str->length);

    if (substr->length == 0)
        return pos; // empty substring found at pos

    for (int i = pos; i <= str->length - substr->length; i++)
    {
        if (memcmp(str->data + i, substr->data, substr->length) == 0)
        {
            return i;
        }
    }
    return -1; // not found
}

int string_find_cstr(const string_t *str, const char *substr)
{
    Expects_not_null(str);
    Expects_not_null(substr);

    int substr_len = strlen(substr);
    if (substr_len == 0)
        return 0; // empty substring found at position 0

    for (int i = 0; i <= str->length - substr_len; i++)
    {
        if (memcmp(str->data + i, substr, substr_len) == 0)
        {
            return i;
        }
    }
    return -1; // not found
}

int string_find_cstr_at(const string_t *str, const char *substr, int pos)
{
    Expects_not_null(str);
    Expects_not_null(substr);
    Expects(pos >= 0);
    Expects(pos <= str->length);

    int substr_len = strlen(substr);
    if (substr_len == 0)
        return pos; // empty substring found at pos

    for (int i = pos; i <= str->length - substr_len; i++)
    {
        if (memcmp(str->data + i, substr, substr_len) == 0)
        {
            return i;
        }
    }
    return -1; // not found
}

#if 0
int string_rfind(const string_t *str, const string_t *substr);
int string_rfind_at(const string_t *str, const string_t *substr, int pos);
int string_rfind_cstr(const string_t *str, const char *substr);
int string_rfind_cstr_at(const string_t *str, const char *substr, int pos);
int string_find_first_of(const string_t *str, const string_t *chars);
int string_find_first_of_at(const string_t *str, const string_t *chars, int pos);
int string_find_first_of_cstr(const string_t *str, const char *chars);
int string_find_first_of_cstr_at(const string_t *str, const char *chars, int pos);
int string_find_first_of_predicate(const string_t *str, bool (*predicate)(char));
int string_find_first_of_predicate_at(const string_t *str, bool (*predicate)(char), int pos);
int string_find_first_not_of(const string_t *str, const string_t *chars);
int string_find_first_not_of_at(const string_t *str, const string_t *chars, int pos);
int string_find_first_not_of_cstr(const string_t *str, const char *chars);
#endif

int string_find_first_not_of_cstr_at(const string_t *str, const char *chars, int pos)
{
    Expects_not_null(str);
    Expects_not_null(chars);
    Expects(pos >= 0);
    Expects(pos <= str->length);

    for (int i = pos; i < str->length; i++)
    {
        if (strchr(chars, str->data[i]) == NULL)
        {
            return i;
        }
    }
    return -1; // not found
}

int string_find_first_not_of_predicate(const string_t *str, bool (*predicate)(char))
{
    return string_find_first_not_of_predicate_at(str, predicate, 0);
}

int string_find_first_not_of_predicate_at(const string_t *str, bool (*predicate)(char), int pos)
{
    Expects_not_null(str);
    Expects_not_null(predicate);
    Expects(pos >= 0);
    Expects(pos <= str->length);

    for (int i = pos; i < str->length; i++)
    {
        if (!predicate(str->data[i]))
        {
            return i;
        }
    }
    return -1; // not found
}

#if 0
int string_find_last_of(const string_t *str, const string_t *chars);
int string_find_last_of_at(const string_t *str, const string_t *chars, int pos);
int string_find_last_of_cstr(const string_t *str, const char *chars);
int string_find_last_of_cstr_at(const string_t *str, const char *chars, int pos);
int string_find_last_not_of(const string_t *str, const string_t *chars);
int string_find_last_not_of_at(const string_t *str, const string_t *chars, int pos);
int string_find_last_not_of_cstr(const string_t *str, const char *chars);
int string_find_last_not_of_cstr_at(const string_t *str, const char *chars, int pos);
#endif

int string_compare(const string_t *str1, const string_t *str2)
{
    Expects_not_null(str1);
    Expects_not_null(str2);

    return strcmp(str1->data, str2->data);
}

int string_compare_at(const string_t *str1, int pos1, const string_t *str2, int pos2)
{
    Expects_not_null(str1);
    Expects_not_null(str2);
    Expects(pos1 >= 0);
    Expects(pos1 <= str1->length);
    Expects(pos2 >= 0);
    Expects(pos2 <= str2->length);

    const char *data1 = str1->data + pos1;
    const char *data2 = str2->data + pos2;

    return strcmp(data1, data2);
}

int string_compare_cstr(const string_t *str, const char *cstr)
{
    Expects_not_null(str);
    Expects_not_null(cstr);

    return strcmp(str->data, cstr);
}

#if 0
int string_compare_cstr_at(const string_t *str, int pos1, const char *cstr, int pos2);
int string_compare_substring(const string_t *str1, int pos1, const string_t *str2, int begin2, int end2);
int string_compare_cstr_substring(const string_t *str, int pos, const char *cstr, int begin2, int end2);
bool string_starts_with(const string_t *str, const string_t *prefix);
bool string_starts_with_cstr(const string_t *str, const char *prefix);
bool string_ends_with(const string_t *str, const string_t *suffix);
bool string_ends_with_cstr (const string_t *str, const char *suffix);
bool string_contains(const string_t *str, const string_t *substr);
bool string_contains_cstr(const string_t *str, const char *substr);
#endif

string_t *string_substring(const string_t *str, int begin, int end)
{
    Expects_not_null(str);
    Expects(begin >= 0);
    Expects(end >= begin);
    Expects(end <= str->length);

    return string_create_from_range(str, begin, end);
}


#if 0
bool string_eq(const string_t *str1, const string_t *str2);
bool string_lt(const string_t *str1, const string_t *str2);
bool string_le(const string_t *str1, const string_t *str2);
bool string_gt(const string_t *str1, const string_t *str2);
bool string_ge(const string_t *str1, const string_t *str2);
int string_cmp(const string_t *str1, const string_t *str2);
#endif

// Formatted I/O
void string_printf(string_t *str, const char *format, ...)
{
    Expects_not_null(str);
    Expects_not_null(format);

    va_list args;
    va_start(args, format);
    string_vprintf(str, format, args);
    va_end(args);
}

void string_vprintf(string_t *str, const char *format, va_list args)
{
    Expects_not_null(str);
    Expects_not_null(format);

    // First, determine required length
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    string_ensure_capacity(str, str->length + needed + 1);

    int written = vsnprintf(&str->data[str->length], str->capacity - str->length, format, args);
    str->length += written;
}

// string_t I/O functions
// void string_getline(string_t *str, FILE *stream);
// void string_getline_delim(string_t *str, int delim, FILE *stream);

// Numeric conversions
int string_atoi_at(const string_t *str, int pos, int *endpos)
{
    Expects_not_null(str);
    Expects(pos >= 0);
    Expects(pos <= str->length);

    char *end_ptr = NULL;
    int value = (int)strtol(&str->data[pos], &end_ptr, 10);

    if (endpos != NULL)
    {
        *endpos = (int)(end_ptr - &str->data[0]);
    }

    return value;
}

int string_atoi(const string_t *str)
{
    Expects_not_null(str);
    return string_atoi_at(str, 0, NULL);
}

long string_atol_at(const string_t *str, int pos, int *endpos)
{
    Expects_not_null(str);
    Expects(pos >= 0);
    Expects(pos <= str->length);

    char *end_ptr = NULL;
    long value = strtol(&str->data[pos], &end_ptr, 10);

    if (endpos != NULL)
    {
        *endpos = (int)(end_ptr - &str->data[0]);
    }

    return value;
}

long string_atol(const string_t *str)
{
    Expects_not_null(str);
    return string_atol_at(str, 0, NULL);
}

// long long string_atoll_at(const string_t *str, int pos, int *endpos);
// long long string_atoll(const string_t *str);
// double string_atof_at(const string_t *str, int pos, int *endpos);
// double string_atof(const string_t *str);

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

// This is the 32-bit Fowler–Noll–Vo hash function
// as described in Wikipedia.
uint32_t string_hash(const string_t *str)
{
    Expects_not_null(str);
    uint32_t h = 0x811c9dc5;  // FNV-1a prime
    for (int i = 0; i < str->length; i++)
    {
        h ^= (uint32_t)str->data[i];
        h *= 0x01000193;      // FNV-1a magic constant
    }
    return h ? h : 1;         // avoid zero (optional)
}

// ============================================================================
// string_t List Functions
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
        string_destroy(&list->strings[i]);
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

void string_list_append(string_list_t *list, const string_t *str)
{
    Expects_not_null(list);
    Expects_not_null(str);
    
    string_t *cloned = string_create_from(str);
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
    
    string_destroy(&list->strings[index]);
    list->strings[index] = str;
}

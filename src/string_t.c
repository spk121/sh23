#include "string_t.h"
#include "logging.h"
#include "xalloc.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Helpers

/**
 * Normalize a half-open substring range [begin, end) for a string of length `len`.
 *
 * Rules:
 *   - end == -1 → treat as len
 *   - begin < 0 → clamp to 0
 *   - end < 0 → clamp to 0
 *   - begin > len → clamp to len
 *   - end > len → clamp to len
 *   - If end <= begin → empty range (begin = end)
 *
 * Returns the normalized begin/end through output parameters.
 */
static inline void clamp_range(int len, int begin, int end, int *out_begin, int *out_end)
{
    // Default end to full length
    if (end == -1)
        end = len;

    // Clamp begin
    if (begin < 0)
        begin = 0;
    if (begin > len)
        begin = len;

    // Clamp end
    if (end < 0)
        end = 0;
    if (end > len)
        end = len;

    // Empty or inverted -> collapse to empty
    if (end <= begin)
    {
        *out_begin = *out_end = begin;
        return;
    }

    *out_begin = begin;
    *out_end = end;
}

static void string_ensure_capacity(string_t *str, int needed)
{
    return_if_null(str);
    return_if_lt(needed, 0);

    // Handle shrink case first
    if (str->capacity > REDUCE_THRESHOLD && needed <= REDUCE_THRESHOLD)
    {
        int new_capacity = REDUCE_THRESHOLD;

        if (str->data == NULL)
        {
            str->data = xcalloc(new_capacity, 1);
            str->capacity = new_capacity;
            return;
        }

        char *new_data = xrealloc(str->data, new_capacity);
        str->data = new_data;

        // Ensure null termination if shrinking below current length
        if (str->length >= new_capacity)
        {
            str->length = new_capacity - 1;
            str->data[str->length] = '\0';
        }

        str->capacity = new_capacity;
        return;
    }

    // Normal growth logic
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

string_t *string_create_from_n_chars(int count, char ch)
{
    return_val_if_lt(count, 0, NULL);

    string_t *str = xcalloc(1, sizeof(string_t));
    int needed_capacity = count + 1;
    string_ensure_capacity(str, needed_capacity);

    for (int i = 0; i < count; i++)
    {
        str->data[i] = ch;
    }
    str->data[count] = '\0';
    str->length = count;

    return str;
}

string_t *string_create_from_cstr(const char *data)
{
    if (data == NULL)
        return string_create();
    size_t len = strlen(data);
    return_val_if_gt(len, INT_MAX, NULL);
    return string_create_from_cstr_len(data, (int)len);
}

string_t *string_create_from_cstr_len(const char *data, int len)
{
    if (data == NULL)
        return string_create();
    if (len < 0)
        len = (int)strlen(data);

    string_t *str = xcalloc(1, sizeof(string_t));
    int needed_capacity = len + 1;
    string_ensure_capacity(str, needed_capacity);

    return_val_if_null(str->data, NULL);

    memcpy(str->data, data, len);
    str->data[len] = '\0';
    str->length = len;

    return str;
}

string_t *string_create_from(const string_t *other)
{
    if (other == NULL)
        return string_create();
    if (other->length == 0)
        return string_create();

    return string_create_from_range(other, 0, other->length);
}

/**
 * Creates a new string containing the substring of str from [start, end).
 *
 * - start < 0  → clamped to 0
 * - start > length → results in empty string
 * - end == -1  → treated as str->length
 * - end < start (after clamping) → empty string
 * - end > length → clamped to length
 *
 * Returns a new string_t or an empty string on any NULL/invalid input.
 */
string_t *string_create_from_range(const string_t *str, int start, int end)
{
    // Handle NULL or empty source string gracefully
    if (str == NULL || str->data == NULL || str->length == 0)
    {
        return string_create(); // empty string
    }

    int b, e;
    clamp_range(str->length, start, end, &b, &e);

    if (b == e)
        return string_create(); // empty
    int length = e - b;

    // Allocate new string
    string_t *result = xcalloc(1, sizeof(string_t));
    if (result == NULL)
    {
        return string_create(); // fallback to empty on allocation failure
    }

    int needed_capacity = length + 1;
    string_ensure_capacity(result, needed_capacity);
    if (result->data == NULL)
    {
        xfree(result);
        return string_create(); // fallback on failure
    }

    // Copy the substring
    memcpy(result->data, str->data + b, length);
    result->data[length] = '\0';
    result->length = length;

    return result;
}

// Destructors
void string_destroy(string_t **str)
{
    return_if_null(str);
    return_if_null(*str);
    xfree((*str)->data);
    xfree(*str);
    *str = NULL;
}

char *string_release(string_t **str)
{
    return_val_if_null(str, NULL);
    char *result = NULL;
    if (*str && (*str)->data)
    {
        result = (*str)->data;
        (*str)->data = NULL;
        (*str)->length = 0;
        (*str)->capacity = 0;
    }
    xfree(*str);
    *str = NULL;
    return result;
}

// Setters
void string_set(string_t *str, const string_t *str2)
{
    return_if_null(str);
    return_if_null(str2);
    if (str2 == str)
        return;
    if (str2->length == 0)
    {
        str->length = 0;
        if (str->data != NULL && str->capacity > 0)
            str->data[0] = '\0';
        return;
    }

    int needed_capacity = str2->length + 1;
    string_ensure_capacity(str, needed_capacity);
    return_if_null(str->data);

    memcpy(str->data, str2->data, str2->length);
    str->length = str2->length;
    str->data[str->length] = '\0';
}

void string_move(string_t *str, string_t *str2)
{
    return_if_null(str);
    return_if_null(str2);

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
}

// Move the contents of str2 into str and then destroy str2
void string_consume(string_t *str, string_t **str2)
{
    return_if_null(str);
    return_if_null(str2);
    return_if_null(*str2);

    string_move(str, *str2);
    string_destroy(str2);
}

void string_set_cstr(string_t *str, const char *cstr)
{
    return_if_null(str);
    return_if_null(cstr);
    size_t slen = strlen(cstr);
    return_if_gt(slen, INT_MAX);
    int len = (int)slen;

    int needed_capacity = len + 1;
    string_ensure_capacity(str, needed_capacity);
    return_if_null(str->data);
    memcpy(str->data, cstr, len);
    str->length = len;
    str->data[str->length] = '\0';
}

void string_set_char(string_t *str, char ch)
{
    return_if_null(str);

    int needed_capacity = 1 + 1;
    string_ensure_capacity(str, needed_capacity);
    return_if_null(str->data);
    str->data[0] = ch;
    str->length = 1;
    str->data[str->length] = '\0';
}

void string_set_data(string_t *str, const char *data, int n)
{
    return_if_null(str);
    return_if_null(data);

    if (n == -1)
    {
        size_t slen = strlen(data);
        if (slen > INT_MAX - 1)
            slen = INT_MAX - 1;
        n = (int)slen;
    }
    if (n <= 0)
    {
        str->length = 0;
        if (str->data != NULL && str->capacity > 0)
            str->data[0] = '\0';
        return;
    }
    if (n > INT_MAX - 1)
        n = INT_MAX - 1;
    int needed_capacity = n + 1;
    string_ensure_capacity(str, needed_capacity);
    return_if_null(str->data);
    memcpy(str->data, data, n);
    str->data[n] = '\0';
    str->length = n;
}

void string_set_n_chars(string_t *str, int count, char ch)
{
    return_if_null(str);
    if (count < 0)
        count = 0;
    if (count > INT_MAX - 1)
        count = INT_MAX - 1;
    int needed_capacity = count + 1;
    string_ensure_capacity(str, needed_capacity);
    return_if_null(str->data);

    for (int i = 0; i < count; i++)
        str->data[i] = ch;
    str->length = count;
    str->data[str->length] = '\0';
}

// Set str to the substring of str2 from [begin2, end2)
void string_set_substring(string_t *str, const string_t *str2, int begin2, int end2)
{
    return_if_null(str);
    return_if_null(str2);

    int b, e;
    clamp_range(str2->length, begin2, end2, &b, &e);

    if (b == e)
    {
        str->length = 0;
        if (str->data && str->capacity > 0)
            str->data[0] = '\0';
        return;
    }

    int substring_length = e - b;
    int needed_capacity = substring_length + 1;
    string_ensure_capacity(str, needed_capacity);
    return_if_null(str->data);
    return_if_null(str2->data);
    if (substring_length > 0)
        memcpy(str->data, str2->data + b, substring_length);
    str->length = substring_length;
    str->data[str->length] = '\0';
}

// Accessors
char string_at(const string_t *str, int index)
{
    return_val_if_null(str, '\0');
    return_val_if_null(str->data, '\0');
    return_val_if_lt(index, 0, '\0');
    return_val_if_ge(index, str->length, '\0');
    return str->data[index];
}

char string_front(const string_t *str)
{
    return_val_if_null(str, '\0');
    return_val_if_null(str->data, '\0');
    return_val_if_le(str->length, 0, '\0');

    return str->data[0];
}

char string_back(const string_t *str)
{
    return_val_if_null(str, '\0');
    return_val_if_null(str->data, '\0');
    return_val_if_le(str->length, 0, '\0');

    return str->data[str->length - 1];
}

char *string_data(string_t *str)
{
    return_val_if_null(str, NULL);
    return_val_if_null(str->data, NULL);

    return str->data;
}

char *string_data_at(string_t *str, int pos)
{
    return_val_if_null(str, NULL);
    return_val_if_null(str->data, NULL);
    return_val_if_lt(pos, 0, NULL);
    return_val_if_gt(pos, str->length, NULL);

    return str->data + pos;
}

const char *string_cstr(const string_t *str)
{
    return_val_if_null(str, NULL);
    return_val_if_null(str->data, NULL);

    return str->data;
}

// Capacity
bool string_empty(const string_t *str)
{
    return_val_if_null(str, true);

    return str->length == 0;
}

int string_size(const string_t *str)
{
    return_val_if_null(str, 0);

    return str->length;
}

int string_length(const string_t *str)
{
    return_val_if_null(str, 0);

    return str->length;
}

int string_max_size(const string_t *str)
{
    return INT_MAX - 1; // Reserve space for null terminator
}

void string_reserve(string_t *str, int new_cap)
{
    return_if_null(str);
    return_if_lt(new_cap, 0);

    if (new_cap == INT_MAX)
        new_cap = INT_MAX - 1;
    if (new_cap + 1 > str->capacity)
        string_ensure_capacity(str, new_cap + 1);
}

int string_capacity(const string_t *str)
{
    return_val_if_null(str, 0);

    return str->capacity;
}

void string_shrink_to_fit(string_t *str)
{
    return_if_null(str);

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
    return_if_null(str);

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
    return_if_null(str);
    return_if_ge(str->length, INT_MAX - 2);

    int needed_capacity = str->length + 1 + 1;
    string_ensure_capacity(str, needed_capacity);
    return_if_null(str->data);

    str->data[str->length] = ch;
    str->length += 1;
    str->data[str->length] = '\0';
}

void string_pop_back(string_t *str)
{
    return_if_null(str);
    return_if_le(str->length, 0);

    str->length --;
    str->data[str->length] = '\0';
}

void string_append(string_t *str, const string_t *other)
{
    return_if_null(str);
    return_if_null(other);
    return_if_null(other->data);
    return_if_lt(other->length, 0);

    int other_length = other->length;
    // Check that combined length does not exceed INT_MAX - 1
    if (other->length > INT_MAX - 1 - str->length)
    {
        log_error("%s: appending would exceed maximum string size", __func__);
        other_length = INT_MAX - 1 - str->length;
    }
    int needed_capacity = str->length + other_length + 1;
    string_ensure_capacity(str, needed_capacity);
    return_if_null(str->data);

    memcpy(str->data + str->length, other->data, other_length);
    str->length += other_length;
    str->data[str->length] = '\0';
}

// Append substring of other from [begin, end)
void string_append_substring(string_t *str, const string_t *other, int begin, int end)
{
    return_if_null(str);
    return_if_null(other);
    return_if_null(other->data);

    int b, e;
    clamp_range(other->length, begin, end, &b, &e);

    if (b == e)
        return; // nothing to append

    int substring_length = e - b;
    int needed_capacity = str->length + substring_length + 1;
    string_ensure_capacity(str, needed_capacity);
    return_if_null(str->data);

    if (substring_length > 0)
        memcpy(str->data + str->length, other->data + b, substring_length);
    str->length += substring_length;
    str->data[str->length] = '\0';
}

void string_append_cstr(string_t *str, const char *cstr)
{
    return_if_null(str);
    return_if_null(cstr);
    size_t cstr_len = strlen(cstr);
    if (cstr_len == 0)
        return;
    if (cstr_len > INT_MAX - 1 - str->length)
    {
        log_error("%s: appending would exceed maximum string size", __func__);
        cstr_len = INT_MAX - 1 - str->length;
    }

    int needed_capacity = str->length + cstr_len + 1;
    string_ensure_capacity(str, needed_capacity);
    return_if_null(str->data);
    memcpy(str->data + str->length, cstr, cstr_len);
    str->length += (int)cstr_len;
    str->data[str->length] = '\0';
}

void string_append_n_chars(string_t *str, int count, char ch)
{
    return_if_null(str);
    if (count < 0)
        return;
    if (count > INT_MAX - 1 - str->length)
    {
        log_error("%s: appending would exceed maximum string size", __func__);
        count = INT_MAX - 1 - str->length;
    }

    int needed_capacity = str->length + count + 1;
    string_ensure_capacity(str, needed_capacity);
    return_if_null(str->data);

    for (int i = 0; i < count; i++)
    {
        str->data[str->length + i] = ch;
    }
    str->length += count;
    str->data[str->length] = '\0';
}

void string_append_data(string_t *str, const char *data, int len)
{
    return_if_null(str);
    if (data == NULL || len == 0)
        return;

    /* Determine length if len == -1 */
    if (len == -1)
    {
        size_t data_len = strlen(data);
        if (data_len > (size_t)INT_MAX - 1 - str->length)
        {
            log_error("%s: appending would exceed maximum string size", __func__);
            len = INT_MAX - 1 - str->length;
        }
        else
        {
            len = (int)data_len;
        }
    }
    else if (len < 0)
    {
        len = 0;
    }

    /* Truncate if needed to avoid overflow */
    if (len > INT_MAX - 1 - str->length)
    {
        log_error("%s: appending would exceed maximum string size", __func__);
        len = INT_MAX - 1 - str->length;
    }

    if (len == 0)
        return;

    int needed = str->length + len + 1;
    string_ensure_capacity(str, needed);
    return_if_null(str->data);

    memcpy(str->data + str->length, data, len);
    str->length += len;
    str->data[str->length] = '\0';
}


void string_append_char(string_t *str, char c)
{
    return_if_null(str);
    return_if_ge(str->length, INT_MAX - 2);

    int needed_capacity = str->length + 1 + 1;
    string_ensure_capacity(str, needed_capacity);
    return_if_null(str->data);

    str->data[str->length] = c;
    str->length++;
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
    return_if_null(str);
    return_if_lt(new_size, 0);

    int needed = new_size + 1;
    string_ensure_capacity(str, needed);
    return_if_null(str->data);

    if (new_size > str->length)
    {
        memset(str->data + str->length, 0, new_size - str->length);
    }

    str->length = new_size;
    str->data[new_size] = '\0';
}

void string_resize_with_char(string_t *str, int new_size, char ch)
{
    return_if_null(str);
    return_if_lt(new_size, 0);

    int needed = new_size + 1;
    string_ensure_capacity(str, needed);
    return_if_null(str->data);

    if (new_size > str->length)
    {
        memset(str->data + str->length, ch, new_size - str->length);
    }

    str->length = new_size;
    str->data[new_size] = '\0';
}

int string_find(const string_t *str, const string_t *substr)
{
    return string_find_at(str, substr, 0);
}

// Returns the index of the first occurrence of substr in str starting from pos, or -1 if not found
int string_find_at(const string_t *str, const string_t *substr, int pos)
{
    return_val_if_null(str, -1);
    return_val_if_null(substr, -1);
    return_val_if_lt(pos, 0, -1);
    return_val_if_gt(pos, str->length, -1);

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
    return_val_if_null(str, -1);
    return_val_if_null(substr, -1);

    size_t slen = strlen(substr);
    if (slen == 0)
        return 0; // empty substr at 0
    if (slen > (size_t)str->length)
        return -1; // too long

    int len = (int)slen;
    for (int i = 0; i <= str->length - len; i++)
    {
        if (memcmp(str->data + i, substr, len) == 0)
            return i;
    }
    return -1;
}

/**
 * Finds the first occurrence of substr in str starting from pos.
 *
 * Returns the index (>= 0) if found, or -1 if not found.
 *
 * Behavior:
 * - If substr is NULL or empty (""), returns pos if pos is valid (0 <= pos <= str->length),
 *   otherwise -1.
 * - If pos < 0, it is clamped to 0.
 * - If pos > str->length, returns -1.
 * - Otherwise, performs a normal substring search starting at the clamped pos.
 */
int string_find_cstr_at(const string_t *str, const char *substr, int pos)
{
    return_val_if_null(str, -1);
    return_val_if_null(substr, (pos < 0 ? 0 : (pos > str->length ? -1 : pos)));

    size_t sublen = strlen(substr);

    // Empty substring: found at any valid position, including exactly at the end
    if (sublen == 0)
    {
        if (pos < 0)
        {
            pos = 0; // treat negative as start
        }
        if ((size_t)pos > (size_t)str->length)
        {
            return -1;
        }
        return pos;
    }

    // Clamp starting position
    if (pos < 0)
    {
        pos = 0;
    }
    if ((size_t)pos > (size_t)str->length)
    {
        return -1;
    }

    // Not enough room left for non-empty substring
    if ((size_t)pos + sublen > (size_t)str->length)
    {
        return -1;
    }

    // Standard substring search using memcmp
    const char *haystack = str->data;
    const char *end = haystack + str->length - sublen + 1;

    for (const char *p = haystack + pos; p < end; ++p)
    {
        if (memcmp(p, substr, sublen) == 0)
        {
            return (int)(p - haystack);
        }
    }

    return -1;
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
#endif
int string_find_first_not_of_cstr(const string_t *str, const char *chars)
{
    return string_find_first_not_of_cstr_at(str, chars, 0);
}

int string_find_first_not_of_cstr_at(const string_t *str, const char *chars, int pos)
{
    return_val_if_null(str, -1);
    return_val_if_null(chars, -1);

    if (strlen(chars) == 0)
        return (pos < str->length) ? pos : -1;

    // Clamp pos
    if (pos < 0)
        pos = 0;
    if (pos >= str->length)
        return -1;

    const char *start = str->data + pos;
    const char *end = str->data + str->length;

    for (const char *p = start; p < end; ++p)
    {
        if (strchr(chars, *p) == NULL) // not found in chars
            return (int)(p - str->data);
    }
    return -1;
}

int string_find_first_not_of_predicate(const string_t *str, bool (*predicate)(char))
{
    return string_find_first_not_of_predicate_at(str, predicate, 0);
}

int string_find_first_not_of_predicate_at(const string_t *str, bool (*predicate)(char), int pos)
{
    return_val_if_null(str, -1);
    return_val_if_null(predicate, -1);

    // Clamp pos
    if (pos < 0)
        pos = 0;
    if (pos >= str->length)
        return -1;

    const char *start = str->data + pos;
    const char *end = str->data + str->length;

    for (const char *p = start; p < end; ++p)
    {
        if (!predicate(*p)) // predicate returns false → char does NOT satisfy it
            return (int)(p - str->data);
    }
    return -1;
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
    char *buf1, *buf2;
    if (str1 == NULL || str1->data == NULL)
        buf1 = "";
    else
        buf1 = str1->data;
    if (str2 == NULL || str2->data == NULL)
        buf2 = "";
    else
        buf2 = str2->data;

    return strcmp(buf1, buf2);
}

int string_compare_at(const string_t *str1, int pos1, const string_t *str2, int pos2)
{
    // Can't even guess what the correct behavior is if either string is NULL.
    Expects_not_null(str1);
    Expects_not_null(str2);
    Expects_not_null(str1->data);
    Expects_not_null(str2->data);

    // Clamp negative positions
    if (pos1 < 0)
        pos1 = 0;
    if (pos2 < 0)
        pos2 = 0;

    const char *p1 = str1->data + pos1;
    const char *p2 = str2->data + pos2;
    const char *end1 = str1->data + str1->length;
    const char *end2 = str2->data + str2->length;

    while (p1 < end1 && p2 < end2)
    {
        if (*p1 != *p2)
            return (unsigned char)*p1 - (unsigned char)*p2;
        ++p1;
        ++p2;
    }

    // One or both reached end
    if ((p1 == end1) && (p2 == end2))
        return 0; // both substrings ended together
    else if (p1 == end1)
        return -1; // str1 substring shorter
    else
        return 1; // str2 substring shorter
}

int string_compare_cstr(const string_t *str, const char *cstr)
{
    const char *buf1;
    const char *buf2;
    if (str == NULL || str->data == NULL)
        buf1 = "";
    else
        buf1 = str->data;
    if (cstr == NULL)
        buf2 = "";
    else
        buf2 = cstr;
    return strcmp(buf1, buf2);
}

int string_compare_cstr_at(const string_t *str, int pos1, const char *cstr, int pos2)
{
    /* Treat NULL str or missing data as empty string */
    const char *buf1 = "";
    size_t len1 = 0;
    if (str != NULL && str->data != NULL)
    {
        if (pos1 < 0)
            pos1 = 0;
        if (pos1 < str->length)
        {
            buf1 = str->data + pos1;
            len1 = str->length - pos1;
        }
        // else: pos1 >= length → empty
    }

    /* Treat NULL cstr as empty string; clamp and bound pos2 */
    const char *buf2 = "";
    size_t len2 = 0;
    if (cstr != NULL)
    {
        if (pos2 < 0)
            pos2 = 0;

        // Find actual length of cstr
        size_t full_len = strlen(cstr);

        if ((size_t)pos2 < full_len)
        {
            buf2 = cstr + pos2;
            len2 = full_len - pos2;
        }
        // else: pos2 >= length → treat as empty substring
    }

    /* Manual bounded comparison – never relies on undefined pointers */
    size_t i = 0;
    while (i < len1 && i < len2)
    {
        if ((unsigned char)buf1[i] != (unsigned char)buf2[i])
            return (unsigned char)buf1[i] - (unsigned char)buf2[i];
        ++i;
    }

    /* One string is prefix of the other */
    if (len1 < len2)
        return -1;
    if (len1 > len2)
        return 1;
    return 0;
}

int string_compare_substring(const string_t *str1, int begin1, const string_t *str2, int begin2,
                             int end2)
{
    // No idea what the fallback behavior should be if either string is NULL.
    Expects_not_null(str1);
    Expects_not_null(str2);

    // Clamp begin1
    if (begin1 < 0)
        begin1 = 0;

    // Compute effective range for str1: from begin1 to end of string
    size_t len1 = (begin1 < str1->length) ? str1->length - begin1 : 0;
    const char *p1 = (len1 > 0) ? str1->data + begin1 : "";

    // Handle str2 substring [begin2, end2)
    if (begin2 < 0)
        begin2 = 0;
    if (end2 == -1)
        end2 = str2->length;
    if (end2 < begin2)
        end2 = begin2; // ensure non-negative length

    size_t len2 = 0;
    const char *p2 = "";
    if (begin2 < str2->length)
    {
        int effective_end = (end2 > str2->length) ? str2->length : end2;
        len2 = (size_t)(effective_end - begin2);
        p2 = str2->data + begin2;
    }

    // Use strncmp – it safely compares up to the minimum length
    int cmp = strncmp(p1, p2, len1 < len2 ? len1 : len2);

    if (cmp != 0)
        return cmp; // difference found within common prefix

    // Common prefix equal → shorter string is less
    if (len1 < len2)
        return -1;
    if (len1 > len2)
        return 1;
    return 0;
}

#if 0
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
    return_if_null(str);
    const char *fmt;
    if (format == NULL)
        fmt = "(null)";
    else
        fmt = format;

    va_list args;
    va_start(args, fmt);
    string_vprintf(str, fmt, args);
    va_end(args);
}

void string_vprintf(string_t *str, const char *format, va_list args)
{
    return_if_null(str);
    const char *fmt;
    if (format == NULL)
        fmt = "(null)";
    else
        fmt = format;

    // First, determine required length
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    if (needed < 0)
    {
        log_error("%s: vsnprintf failed to compute required length", __func__);
        return;
    }
    string_ensure_capacity(str, str->length + needed + 1);
    return_if_null(str->data);

    int written = vsnprintf(&str->data[str->length], str->capacity - str->length, fmt, args);
    if (written < 0)
    {
        log_error("%s: vsnprintf failed to write formatted string", __func__);
        return;
    }
    str->length += written;
}

// string_t I/O functions
// void string_getline(string_t *str, FILE *stream);
// void string_getline_delim(string_t *str, int delim, FILE *stream);

// Numeric conversions
int string_atoi_at(const string_t *str, int pos, int *endpos)
{
    return_val_if_null(str, 0);
    if (pos < 0)
        pos = 0;
    if (pos > str->length)
        pos = str->length;

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
    return_val_if_null(str, 0);
    return string_atoi_at(str, 0, NULL);
}

long string_atol_at(const string_t *str, int pos, int *endpos)
{
    return_val_if_null(str, 0);
    if (pos < 0)
        pos = 0;
    if (pos > str->length)
        pos = str->length;
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
    return_val_if_null(str, 0);
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
    if (str == NULL || str->length == 0)
        return 0x811c9dc5; // FNV-1a prime

    uint32_t h = 0x811c9dc5; // FNV-1a prime
    for (int i = 0; i < str->length; i++)
    {
        h ^= (uint32_t)str->data[i];
        h *= 0x01000193; // FNV-1a magic constant
    }
    return h ? h : 1; // avoid zero (optional)
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


void string_list_destroy(string_list_t **list)
{
    if (list == NULL || *list == NULL)
        return;
    for (int i = 0; i < (*list)->size; i++)
    {
        string_destroy(&(*list)->strings[i]);
    }

    xfree((*list)->strings);
    xfree(*list);
    *list = NULL;
}

void string_list_move_push_back(string_list_t *list, string_t *str)
{
    return_if_null(list);
    return_if_null(str);

    if (list->size >= list->capacity)
    {
        list->capacity *= GROW_FACTOR;
        // Defensive, and should not be needed
        if (list->capacity <= list->size)
        {
            list->capacity = list->size + 1;
        }
        // list->strings should not be null, but, if it is, realloc will behave like malloc
        list->strings = xrealloc(list->strings, list->capacity * sizeof(string_t *));
    }

    list->strings[list->size++] = str;
}

void string_list_push_back(string_list_t *list, const string_t *str)
{
    return_if_null(list);
    return_if_null(str);

    string_t *cloned = string_create_from(str);
    string_list_move_push_back(list, cloned);
}

int string_list_size(const string_list_t *list)
{
    return_val_if_null(list, 0);
    return list->size;
}

const string_t *string_list_at(const string_list_t *list, int index)
{
    return_val_if_null(list, NULL);
    return_val_if_lt(index, 0, NULL);
    return_val_if_gt(index, list->size, NULL);
    if (index == list->size)
        return NULL; // out of bounds, but not an error
    return list->strings[index];
}

void string_list_assign(string_list_t *list, int index, const string_t *str)
{
    return_if_null(list);
    return_if_null(str);
    return_if_lt(index, 0);
    return_if_ge(index, list->size);

    string_set(list->strings[index], str);
}

#ifndef STRING_T_H
#define STRING_T_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/*
 * NOTE ON ARGUMENTS:
 * For all functions that take string_t *as their initial argument, the argument must not be NULL.
 * It will abort if it is.
 *
 * For all functions that take string_list_t*as their initial argument, the argument must not be NULL.
 * It will abort if it is.
 *
 * Other pointer arguments must not be NULL unless documented as such.
 *
 * NOTE ON BINARY DATA:
 * This string class does not attempt to handle true binary data. For all functions that take data
 * buffers as arguments, the data should not contain null bytes.
 * If it does, the null byte will be treated as the end of the data and may
 * cause truncation.
 *
 */


/*
 * A dynamic string type with small string optimization.
 */
#define STRING_T_INITIAL_CAPACITY 16
static const int STRING_INITIAL_CAPACITY = STRING_T_INITIAL_CAPACITY;
static const int STRING_GROW_FACTOR = 2;
// When resizing down a string, reduce capacity if there's more than this many unused bytes
static const int STRING_REDUCE_THRESHOLD = 512;

typedef struct string_t
{
    int length;
    int capacity;
    char *data;
    char inline_data[STRING_T_INITIAL_CAPACITY]; // Small string optimization. Length must be same as INITIAL_CAPACITY.
} string_t;

/*
 * Internal helper functions for capacity normalization.
 * static inline bool string_is_inline(const string_t *str);
 * static inline bool string_is_heap_allocated(const string_t *str);
 * static inline void string_normalize_capacity(string_t *str, int new_capacity);
 */

#if 0
// FIXME: Move this to the string_t.c file when done.
static inline bool string_is_inline(const string_t *str) {
    return str->capacity <= STRING_INITIAL_CAPACITY;
}

static inline bool string_is_heap_allocated(const string_t *str) {
    return str->capacity > STRING_INITIAL_CAPACITY;
}

static inline void string_normalize_capacity(string_t *str, int new_capacity)
{
    if (string_is_heap_allocated(str) && new_capacity <= STRING_INITIAL_CAPACITY)
    {
        // Move to inline storage
        int new_length =
            str->length < STRING_INITIAL_CAPACITY ? str->length : STRING_INITIAL_CAPACITY - 1;
        str->length = new_length;
        str->capacity = STRING_INITIAL_CAPACITY;
        memcpy(str->inline_data, str->data, new_length);
        str->inline_data[new_length] = '\0';
        xfree(str->data);
        str->data = str->inline_data;
    }
    else if (string_is_heap_allocated(str) && new_capacity > STRING_INITIAL_CAPACITY)
    {
        // Resize heap allocation
        char *new_data = (char *)xmalloc(new_capacity * sizeof(char));
        memcpy(new_data, str->data, str->length);
        new_data[str->length] = '\0';
        xfree(str->data);
        str->data = new_data;
        str->capacity = new_capacity;
    }
    else if (string_is_inline(str) && new_capacity > STRING_INITIAL_CAPACITY)
    {
        // Move to heap allocation
        char *new_data = (char *)xmalloc(new_capacity * sizeof(char));
        memcpy(new_data, str->inline_data, str->length);
        new_data[str->length] = '\0';
        str->data = new_data;
        str->capacity = new_capacity;
    } // else: already inline with sufficient capacity
}
#endif

// Constructors
/*
 * Creates an empty string.
 */
string_t *string_create(void);
/*
 * Creates a string with 'count' instances of the character 'ch'.
 * If count is zero or negative, creates an empty string.
 */
string_t *string_create_from_n_chars(int count, char ch);
/*
 * Creates a string from a null-terminated C string.
 * It copies the data.
 * If 'data' is NULL, creates an empty string.
 */
string_t *string_create_from_cstr(const char *data);
/*
 * Creates a string from a data buffer of length 'len'.
 * It copies the data.
 * The data buffer need not be null-terminated.
 * If 'data' is NULL or 'len' is zero or negative, creates an empty string.
 */
string_t *string_create_from_cstr_len(const char *data, int len);
/*
 * Clones the string 'other'.
 */

string_t *string_create_from_cstr_list(const char **strv, const char *separator);

string_t *string_create_from(const string_t *other);
/*
 * Creates a string from the substring of 'str' from 'start' (inclusive) to 'end' (exclusive).
 * If end is -1, it is treated as the length of str.
 * If end > than the length of str, it is clamped to the length.
 * if start < 0, it is clamped to 0.
 * If end <= start, the substring is empty.
 */
string_t *string_create_from_range(const string_t *str, int start, int end);

// Destructors

/*
 * Destroys the string and frees its memory.
 * Sets the pointer to NULL.
 */
void string_destroy(string_t **str);

/*
 * Returns the internal C-string data and releases ownership of it.
 * If the string was used with small string optimization, a new heap allocation is made.
 * Caller frees.
 */
char *string_release(string_t **str);

// string_t *string_vcreate(const char *format, va_list args);
// string_t *string_create_from_format(const char *format, ...);

// Setters

/*
 * Sets str's contents to be a copy of str2.
 */
void string_set(string_t *str, const string_t *str2);
/*
 * If str2 is heap-allocated, str1 takes ownership of its contents.
 * If str2 is not heap-allocated, str1 makes a copy of its contents.
 * After this operation, str2 is left in an empty state.
 */
void string_move(string_t *str, string_t *str2);
/*
 * Sets str's contents to be that of str2, taking ownership of str2's data.
 * After this operation, str2 is freed and set to NULL.
 */
void string_consume(string_t *str, string_t **str2);
/*
 * Sets str's contents to be a copy of the null-terminated C string cstr.
 */
void string_set_cstr(string_t *str, const char *cstr);
/*
 * Sets str's contents to be the single character ch.
 * If ch is '\0', the string becomes empty.
 */
void string_set_char(string_t *str, char ch);
/*
 * Sets str's contents to be a copy of the data buffer of length n.
 * The 'data' buffer need not be null-terminated.
 * If 'n' is zero or negative, the string becomes empty.
 * If 'data' is NULL, the string becomes empty.
 *
 * N.B.: If data contains null bytes, only the data up to the first null byte will be used for
 * replacement.
 */
void string_set_data(string_t *str, const char *data, int n);
/*
 * Sets str's contents to be the 'count' instances of character 'ch'.
 * If 'count' is zero or negative, the string becomes empty.
 * If 'ch' is '\0', the string becomes empty.
 */
void string_set_n_chars(string_t *str, int count, char ch);
/*
 * Sets str's contents to be a copy of the substring of str2 from begin2 (inclusive) to end2
 * (exclusive).
 * If end2 is -1, it is treated as the length of str2.
 * If end2 > than the length of str2, it is clamped to the length.
 * if begin2 < 0, it is clamped to 0.
 * If end2 <= begin2, the substring is empty.
 * If str2 is NULL or empty, the string becomes empty.
 */
void string_set_substring(string_t *str, const string_t *str2, int begin2, int end2);

// Accessors
/*
 * Returns the character at the given index.
 * If index is out of bounds, returns '\0'.
 */
char string_at(const string_t *str, int index);
/*
 * Returns the first character of the string.
 * If the string is empty, returns '\0'.
 */
char string_front(const string_t *str);
/*
 * Returns the last character of the string.
 * If the string is empty, returns '\0'.
 */
char string_back(const string_t *str);
/*
 * Returns a pointer to the internal mutable character data.
 * It is up to the caller to not be stupid and not modify beyond the string's length.
 */
char *string_data(string_t *x);
/*
 * Returns a pointer to the internal mutable character data beginning at position pos.
 * It is up to the caller to not be stupid and not modify beyond the string's length.
 * If pos is out of bounds, returns NULL.
 */
char *string_data_at(string_t *str, int pos);
/*
 * Returns a pointer to the internal constant character data.
 */
const char *string_cstr(const string_t *str);

// Capacity
/*
 * Returns true if the string is empty, false otherwise.
 */
bool string_empty(const string_t *str);
/*
 * Returns the length of the string, not including the null terminator.
 */
int string_length(const string_t *str);
/*
 * An alias for string_length(). string_length() is preferred.
 */
static inline int string_size(const string_t *str) {
    return string_length(str);
}

// int string_max_size(const string_t *str);

/*
 * Increases the capacity of the string to at least new_cap, without changing its length.
 * The new capacity includes space for the null terminator, so to reserve space for N characters,
 * new_cap should be at least N + 1.
 * If new_cap is less than or equal to the current capacity, does nothing.
 */
void string_reserve(string_t *str, int new_cap);
/*
 * Returns the currently allocated capacity of the string, including space for the null terminator.
 * So the maximum number of characters that can be stored without reallocation is
 * capacity - 1.
 */
int string_capacity(const string_t *str);
/*
 * Reduces the capacity of the string to fit its current length plus null terminator.
 */
void string_shrink_to_fit(string_t *str);

// Modifiers

/*
 * Clears the string to be empty. Does not necessarily reduce capacity.
 */
void string_clear(string_t *str);
/*
 * Inserts the contents of 'other' into 'str' at position 'pos'.
 * The position pos will clamped to 0 and the string length.
 * If pos == the string length, this is a simple append.
 * If 'other' is NULL or empty, does nothing.
 */
void string_insert(string_t *str, int pos, const string_t *other);
/*
 * Inserts n instances of the character 'ch' into 'str' at position 'pos'.
 * The position pos will clamped to 0 and the string length.
 * If pos == the string length, this is a simple append.
 * If count is zero or negative, does nothing.
 * If 'ch' is '\0', this truncates the string at position 'pos'.
 */
void string_insert_n_chars(string_t *str, int pos, int count, char ch);
/*
 * Inserts the null-terminated C string 'cstr' into 'str' at position 'pos'.
 * The position pos will clamped to 0 and the string length.
 * If pos == the string length, this is a simple append.
 * If cstr is NULL or empty, does nothing.
 */
void string_insert_cstr(string_t *str, int pos, const char *cstr);
/*
 * Inserts the data buffer of length 'len' into 'str' at position 'pos'.
 * The data buffer need not be null-terminated.
 * If 'data' is NULL, does nothing.
 * If 'len' is zero or negative, does nothing.
 * The position pos will clamped to 0 and the string length.
 * If pos == the string length, it will append.
 * If 'data' is NULL, does nothing.
 *
 * N.B.: If data contains null bytes, the string will be truncated at the first null byte.
 */
void string_insert_data(string_t *str, int pos, const char *data, int len);
/*
 * Erases 'len' characters from 'str' starting at position 'pos'.
 * The position 'pos' is clamped to 0 and the string length.
 * If 'pos' is equal to the string length, does nothing.
 * If 'len' is zero or negative, does nothing.
 * If pos + len exceeds the string length, it erases up to the end of the string.
 */
void string_erase(string_t *str, int pos, int len);
/*
 * Appends the character 'ch' to the end of the string.
 * If 'ch' is '\0', does nothing.
 */
void string_push_back(string_t *str, char ch);
/*
 * Removes and returns the last character of the string.
 * If the string is empty, returns '\0'.
 */
char string_pop_back(string_t *str);
/*
 * Appends the contents of 'other' to the end of 'str'.
 * If 'other' is NULL or empty, does nothing.
 */
void string_append(string_t *str, const string_t *other);
/*
 * Appends the substring of 'other' from 'begin' (inclusive) to 'end' (exclusive) to the end of
 * 'str'.
 * If 'other' is NULL or empty, does nothing.
 * If 'end' is -1, it is treated as the length of other.
 * 'end' is clamped to 0 and the length of 'other'.
 * 'begin' is clamped to 0 and the length of 'other'.
 * If end <= begin, nothing is appended.
 */
void string_append_substring(string_t *str, const string_t *other, int begin, int end);
/*
 * Appends the null-terminated C string 'cstr' to the end of 'str'.
 * If 'cstr' is NULL or empty, does nothing.
 */
void string_append_cstr(string_t *str, const char *cstr);

/*
 * Appends the Unicode code point 'cp' as UTF-8 to the end of 'str'.
 * Returns the number of bytes written (1–4), or 0 if the code point is invalid.
 */
int string_append_utf8(string_t *str, uint32_t cp);

/*
 * Appends 'count' instances of the character 'ch' to the end of 'str'.
 * If 'count' is zero or negative, does nothing.
 * If 'ch' is '\0', does nothing.
 */
void string_append_n_chars(string_t *str, int count, char ch);

/*
 * Appends the data buffer of length 'len' to the end of 'str'.
 * The data buffer need not be null-terminated.
 * If 'data' is NULL, does nothing.
 * If 'len' is zero or negative, does nothing.
 *
 * N.B.: If 'data' contains null bytes, the string will be truncated at the first null byte.
 */
void string_append_data(string_t *str, const char *data, int len);

/*
 * An alias for string_push_back(). Appends the character 'c' to the end of 'str'.
 */
static inline void string_append_char(string_t *str, char c) {
    string_push_back(str, c);
}

/*
 * Replaces 'len' characters in 'str' starting at position 'pos' with the contents of 'other'.
 * The position 'pos' is clamped to 0 and the string length.
 * If 'pos' is equal to the string length, does nothing.
 * If 'len' is zero or negative, does nothing.
 * If pos + len exceeds the string length, it replaces up to the end of the string.
 * If 'other' is NULL or empty, this effectively erases the specified range.
 */
void string_replace(string_t *str, int pos, int len, const string_t *other);

/*
 * Replaces 'len' characters in 'str' starting at position 'pos' with the substring of 'other'
 * from 'begin2' (inclusive) to 'end2' (exclusive).
 * The position 'pos' is clamped to 0 and the string length.
 * If 'pos' is equal to the string length, does nothing.
 * If 'len' is zero or negative, does nothing.
 * If pos + len exceeds the string length, it replaces up to the end of the string.
 * If 'other' is NULL or empty, this effectively erases the specified range.
 * If 'end2' is -1, it is treated as the length of other.
 * 'end2' is clamped to 0 and the length of 'other'.
 * 'begin2' is clamped to 0 and the length of 'other'.
 * If end2 <= begin2, this effectively erases the specified range.
 */
void string_replace_substring(string_t *str, int pos, int len, const string_t *other, int begin2,
                              int end2);
/*
 * Replaces 'len' characters in 'str' starting at position 'pos' with the null-terminated C
 * string 'cstr'.
 * The position 'pos' is clamped to 0 and the string length.
 * If 'pos' is equal to the string length, does nothing.
 * If 'len' is zero or negative, does nothing.
 * If pos + len exceeds the string length, it replaces up to the end of the string.
 * If 'cstr' is NULL or empty, this effectively erases the specified range.
 */
void string_replace_cstr(string_t *str, int pos, int len, const char *cstr);
/*
 * Replaces 'len' characters in 'str' starting at position 'pos' with 'count' instances of the
 * character 'ch'.
 * The position 'pos' is clamped to 0 and the string length.
 * If 'pos' is equal to the string length, does nothing.
 * If 'len' is zero or negative, does nothing.
 * If pos + len exceeds the string length, it replaces up to the end of the string.
 * If 'count' is zero or negative, this effectively erases the specified range.
 * If 'ch' is '\0', does nothing.
 */
void string_replace_n_chars(string_t *str, int pos, int len, int count, char ch);
/*
 * Replaces 'len' characters in 'str' starting at position 'pos' with the data buffer of length
 * 'data_len'.
 * The position 'pos' is clamped to 0 and the string length.
 * If 'pos' is equal to the string length, does nothing.
 * If 'len' is zero or negative, does nothing.
 * If pos + len exceeds the string length, it replaces up to the end of the string.
 * If 'data' is NULL, does nothing.
 * If 'data_len' is zero or negative, this effectively erases the specified range.
 *
 * N.B.: If 'data' contains null bytes, the string will be truncated at the first null byte during
 * replacement.
 */
void string_replace_data(string_t *str, int pos, int len, const char *data, int data_len);
/*
 * Copies up to 'count' characters from 'str' into the buffer 'dest'.
 * The destination buffer must be at least 'count' bytes long.
 * If 'count' is greater than the string length, only the string length is copied.
 * The copied data is null-terminated if there is space in the destination buffer.
 */
void string_copy_to_cstr(const string_t *str, char *dest, int count);
/*
 * Copies up to 'count' characters from 'str' starting at position 'pos' into the buffer
 * 'dest'.
 * The destination buffer must be at least 'count' bytes long.
 * The position 'pos' is clamped to 0 and the string length.
 * If 'pos' equals the string length, nothing is copied.
 * If 'count' is greater than the number of characters from 'pos' to the end of the string,
 * only that many characters are copied.
 * The copied data is null-terminated if there is space in the destination buffer.
 */
void string_copy_to_cstr_at(const string_t *str, int pos, char *dest, int count);
/*
 * Resizes the string to 'new_size'.
 * If 'new_size' is less than zero, sets the size to zero.
 * If 'new_size' is less than the current length, the string is truncated.
 * If 'new_size' is greater than the current length, nothing happens.
 */
void string_resize(string_t *str, int new_size);
/*
 * Resizes the string to 'new_size'.
 * If 'new_size' is less than zero, sets the size to zero.
 * If 'new_size' is less than the current length, the string is truncated.
 * If 'new_size' is greater than the current length, the new characters are filled with 'ch'.
 */
void string_resize_with_char(string_t *str, int new_size, char ch);


// String operations
/*
 * Finds the first occurrence of 'substr' in 'str'.
 * Returns the index of the first occurrence, or -1 if not found.
 * If 'substr' is NULL or empty, returns 0.
 */
int string_find(const string_t *str, const string_t *substr);
/*
 * Finds the first occurrence of 'substr' in 'str' starting from position 'pos'.
 * The position 'pos' is clamped to 0 and the string length.
 * Returns the index of the first occurrence, or -1 if not found.
 * If 'substr' is NULL or empty, returns 'pos'.
 */
int string_find_at(const string_t *str, const string_t *substr, int pos);
/*
 * Finds the first occurrence of the null-terminated C string 'substr' in 'str'.
 * Returns the index of the first occurrence, or -1 if not found.
 * If 'substr' is NULL or empty, returns 0.
 */
int string_find_cstr(const string_t *str, const char *substr);
/*
 * Finds the first occurrence of the null-terminated C string 'substr' in 'str' starting from
 * position 'pos'.
 * The position 'pos' is clamped to 0 and the string length.
 * Returns the index of the first occurrence, or -1 if not found.
 * If 'substr' is NULL or empty, returns 'pos'.
 */
int string_find_cstr_at(const string_t *str, const char *substr, int pos);
/*
 * Finds the last occurrence of 'substr' in 'str'.
 * Returns the index of the last occurrence, or -1 if not found.
 * If 'substr' is NULL or empty, returns the length of 'str'.
 */
int string_rfind(const string_t *str, const string_t *substr);
/*
 * Finds the last occurrence of 'substr' in 'str' starting from position 'pos'.
 * The position 'pos' is clamped to 0 and the string length.
 * Returns the index of the last occurrence, or -1 if not found.
 * If 'substr' is NULL or empty, returns 'pos'.
 */
int string_rfind_at(const string_t *str, const string_t *substr, int pos);
/*
 * Finds the last occurrence of the null-terminated C string 'substr' in 'str'.
 * Returns the index of the last occurrence, or -1 if not found.
 * If 'substr' is NULL or empty, returns the length of 'str'.
 */
int string_rfind_cstr(const string_t *str, const char *substr);
/*
 * Finds the last occurrence of the null-terminated C string 'substr' in 'str' starting from
 * position 'pos'.
 * The position 'pos' is clamped to 0 and the string length.
 * Returns the index of the last occurrence, or -1 if not found.
 * If 'substr' is NULL or empty, returns 'pos'.
 */
int string_rfind_cstr_at(const string_t *str, const char *substr, int pos);
/*
 * Finds the first occurence of one of the characters in 'chars' in 'str'.
 * Returns the index of the first occurrence, or -1 if not found.
 * If 'chars' is NULL or empty, returns -1.
 */
int string_find_first_of(const string_t *str, const string_t *chars);
/*
 * Finds the first occurence of one of the characters in 'chars' in 'str' starting from position
 * 'pos'.
 * The position 'pos' is clamped to 0 and the string length.
 * Returns the index of the first occurrence, or -1 if not found.
 * If 'chars' is NULL or empty, returns -1.
 */
int string_find_first_of_at(const string_t *str, const string_t *chars, int pos);
/*
 * Finds the first occurence of one of the characters in 'chars' in 'str'.
 * Returns the index of the first occurrence, or -1 if not found.
 * If 'chars' is NULL or empty, returns -1.
 */
int string_find_first_of_cstr(const string_t *str, const char *chars);
/*
 * Finds the first occurence of one of the characters in 'chars' in 'str' starting from position
 * 'pos'.
 * The position 'pos' is clamped to 0 and the string length.
 * Returns the index of the first occurrence, or -1 if not found.
 * If 'chars' is NULL or empty, returns -1.
 */
int string_find_first_of_cstr_at(const string_t *str, const char *chars, int pos);
/*
 * Finds the first character in 'str' that satisfies the given predicate function.
 * The predicate function should return true for characters to be matched.
 * Returns the index of the first occurrence, or -1 if not found.
 * If 'predicate' is NULL, returns -1.
 */
int string_find_first_of_predicate(const string_t *str, bool (*predicate)(char));
/*
 * Finds the first character in 'str' starting from position 'pos' that satisfies the given
 * predicate function.
 * The position 'pos' is clamped to 0 and the string length.
 * The predicate function should return true for characters to be matched.
 * Returns the index of the first occurrence, or -1 if not found.
 * If 'predicate' is NULL, returns -1.
 */
int string_find_first_of_predicate_at(const string_t *str, bool (*predicate)(char), int pos);
/*
 * Finds the first occurence of one a character in 'str' that is not one of the charcters in 'chars'.
 * If all characters are in 'chars', returns -1.
 * If 'chars' is NULL or empty, returns 0.
 */
int string_find_first_not_of(const string_t *str, const string_t *chars);
/*
 * Finds the first occurence of one a character in 'str' starting from position 'pos' that is not
 * one of the charcters in 'chars'.
 * The position 'pos' is clamped to 0 and the string length.
 * If all characters from 'pos' onward are in 'chars', returns -1.
 * If 'chars' is NULL or empty, returns 'pos'.
 */
int string_find_first_not_of_at(const string_t *str, const string_t *chars, int pos);
/*
 * Finds the first occurence of one a character in 'str' that is not one of the charcters in
 * 'chars'.
 * If all characters are in 'chars', returns -1.
 * If 'chars' is NULL or empty, returns 0.
 */
int string_find_first_not_of_cstr(const string_t *str, const char *chars);
/*
 * Finds the first occurence of one a character in 'str' starting from position 'pos' that is not
 * one of the charcters in 'chars'.
 * The position 'pos' is clamped to 0 and the string length.
 * If all characters from 'pos' onward are in 'chars', returns -1.
 * If 'chars' is NULL or empty, returns 'pos'.
 */
int string_find_first_not_of_cstr_at(const string_t *str, const char *chars, int pos);
/*
 * Finds the first character in 'str' that does not satisfy the given predicate function.
 * The predicate function should return true for characters to be excluded.
 * Returns the index of the first occurrence, or -1 if not found.
 * If 'predicate' is NULL, returns 0.
 */
int string_find_first_not_of_predicate(const string_t *str, bool (*predicate)(char));
/*
 * Finds the first character in 'str' starting from position 'pos' that does not satisfy the
 * given predicate function.
 * The position 'pos' is clamped to 0 and the string length.
 * The predicate function should return true for characters to be excluded.
 * Returns the index of the first occurrence, or -1 if not found.
 * If 'predicate' is NULL, returns 'pos'.
 */
int string_find_first_not_of_predicate_at(const string_t *str, bool (*predicate)(char), int pos);
/*
 * Finds the last occurence of one of the characters in 'chars' in 'str'.
 * Returns the index of the last occurrence, or -1 if not found.
 * If 'chars' is NULL or empty, returns -1.
 */
int string_find_last_of(const string_t *str, const string_t *chars);
int string_find_last_of_at(const string_t *str, const string_t *chars, int pos);
int string_find_last_of_cstr(const string_t *str, const char *chars);
int string_find_last_of_cstr_at(const string_t *str, const char *chars, int pos);
int string_find_last_not_of(const string_t *str, const string_t *chars);
int string_find_last_not_of_at(const string_t *str, const string_t *chars, int pos);
int string_find_last_not_of_cstr(const string_t *str, const char *chars);
int string_find_last_not_of_cstr_at(const string_t *str, const char *chars, int pos);

int string_compare(const string_t *str1, const string_t *str2);
int string_compare_at(const string_t *str1, int pos1, const string_t *str2, int pos2);
int string_compare_cstr(const string_t *str, const char *cstr);
int string_compare_cstr_at(const string_t *str, int pos1, const char *cstr, int pos2);
int string_compare_substring(const string_t *str1, int begin1, int end1, const string_t *str2, int begin2,
                             int end2);
int string_compare_cstr_substring(const string_t *str, int begin1, int end1, const char *cstr, int begin2,
                                  int end2);
bool string_starts_with(const string_t *str, const string_t *prefix);
bool string_starts_with_cstr(const string_t *str, const char *prefix);
bool string_ends_with(const string_t *str, const string_t *suffix);
bool string_ends_with_cstr(const string_t *str, const char *suffix);
/*
 * Returns true if 'substr' is found within 'str', false otherwise.
 * If 'substr' is NULL or empty, returns true.
 */
bool string_contains(const string_t *str, const string_t *substr);
bool string_contains_cstr(const string_t *str, const char *substr);

/*
 * Returns a new string that is the substring of 'str' from 'begin' (inclusive) to 'end'
 * (exclusive).
 * If 'end' is -1, it is treated as the length of str.
 * 'end' is clamped to 0 and the length of 'str'.
 * 'begin' is clamped to 0 and the length of 'str'.
 * If 'end' <= 'begin', the substring is empty.
 */
string_t *string_substring(const string_t *str, int begin, int end);

// Comparison operators

/*
 * Returns if the contents of 'str1' and 'str2' are equal.
 * If both strings are empty, returns true.
 */
bool string_eq(const string_t *str1, const string_t *str2);

bool string_eq_cstr(const string_t *str, const char *cstr);

    /*
 * Returns if the contents of 'str1' and 'str2' are not equal.
 */
bool string_ne(const string_t *str1, const string_t *str2);
/*
 * Returns if 'str1' is less than 'str2' lexicographically assuming a raw byte comparison.
 * The locales are not considered.
 */
bool string_lt(const string_t *str1, const string_t *str2);
bool string_le(const string_t *str1, const string_t *str2);
bool string_gt(const string_t *str1, const string_t *str2);
bool string_ge(const string_t *str1, const string_t *str2);
/*
 * Compares 'str1' and 'str2' lexicographically assuming a raw byte comparison.
 * The locales are not considered.
 * Returns a negative value if str1 < str2, zero if str1 == str2, and a positive value if
 * str1 > str2.
 */
int string_cmp(const string_t *str1, const string_t *str2);

// Formatted I/O
/*
 * Sets the string to the formatted output.
 * Uses printf-style formatting.
 * If 'format' is NULL, the string is set to empty.
 */
void string_printf(string_t *str, const char *format, ...);
void string_vprintf(string_t *str, const char *format, va_list args);

// string_scanf_result_t string_scanf(const string_t *str, const char *format, ...);
// string_vscanf_result_t string_vscanf(const string_t *str, const char *format,
//                                        va_list args);

/*
 * Reads a line from the given stream into the string, resizing as necessary.
 * If end-of-file is reached before any characters are read, the string is left unchanged.
 * If 'stream' is NULL, 'str' is set to empty.
 */
void string_getline(string_t *str, FILE *stream);
/*
 * Reads characters from the given stream into the string until the delimiter character is
 * encountered or end-of-file is reached, resizing as necessary.
 * The delimiter character is not included in the string.
 * If end-of-file is reached before any characters are read, the string is left unchanged.
 * If 'stream' is NULL, does nothing.
 * If 'delim' is '\0', reads until end-of-file.
 */
void string_getline_delim(string_t *str, char delim, FILE *stream);

// Conversion functions
/*
 * Searches 'str' starting at the beginning for the longest possible substring that
 * is a valid integer representation and then converts the substring to an integer.
 * If the string does not represent a valid integer, returns 0.
 */
int string_atoi(const string_t *str);
/*
 * Searches 'str' starting at position 'pos' for the longest possible substring that
 * is a valid integer representation and then converts the substring to an integer.
 * The position 'pos' is clamped to 0 and the string length.
 * If 'endpos' is not NULL, it is set to the position of the first character after the
 * parsed integer. If no integer could be parsed, it is set to 'pos'.
 * If the substring does not represent a valid integer, returns 0.
 */
int string_atoi_at(const string_t *str, int pos, int *endpos);
long string_atol(const string_t *str);
long string_atol_at(const string_t *str, int pos, int *endpos);
long long string_atoll(const string_t *str);
long long string_atoll_at(const string_t *str, int pos, int *endpos);
double string_atof(const string_t *str);
double string_atof_at(const string_t *str, int pos, int *endpos);
/*
 * Creates a new string representing the integer 'value'.
 */
string_t *string_from_int(int value);
string_t *string_from_long(long value);
/*
 * Creates a new string representing the floating point 'value'.
 * If 'value' is NaN or infinite, it will return the strings "nan", "inf", or "-inf" respectively.
 */
string_t *string_from_double(double value);

// Hash function
/*
 * Computes a hash value for the string.
 */
uint32_t string_hash(const string_t *str);

#endif /* STRING_T_H */

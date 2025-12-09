#ifndef STRING_T_H
#define STRING_T_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct string_t
{
    char *data;   // UTF-8 encoded string
    int length;   // Byte length (excluding null terminator)
    int capacity; // Total allocated bytes (including null terminator)
} string_t;

typedef struct string_list_t {
    string_t **strings;
    int size;
    int capacity;
} string_list_t;

// alias_t for compatibility
typedef string_t String;

static const int INITIAL_CAPACITY = 16;
static const int GROW_FACTOR = 2;
// When resizing down a string, reduce capacity if there's more than this many unused bytes
static const int REDUCE_THRESHOLD = 4096;

// Create and destroy
string_t *string_create_from_cstr(const char *data);
string_t *string_create_from_cstr_len(const char *data, int len);
string_t *string_vcreate(const char *format, va_list args);
string_t *string_create_from_format(const char *format, ...);
// Creates an empty string with an initial allocated capacity of at least 'min_capacity' bytes.
// Or zero for default initial capacity.
string_t *string_create_empty(int min_capacity);
string_t *string_clone(const string_t *other);
string_t *string_create_from_range(const string_t *str, int start, int length);
void string_destroy(string_t *str);

// Accessors
const char *string_data(const string_t *str);
char string_char_at(const string_t *str, int index);
int string_length(const string_t *str);
int string_capacity(const string_t *str);
bool string_is_empty(const string_t *str);

char *string_front(string_t *str);
char string_front_char(const string_t *str);
char *string_back(string_t *str);
char string_back_char(const string_t *str);

// Modification
void string_append_cstr(string_t *str, const char *data);
void string_append_ascii_char(string_t *str, char c);
void string_append(string_t *str, const string_t *other);
void string_clear(string_t *str);
void string_set_cstr(string_t *str, const char *data);
void string_resize(string_t *str, int new_capacity);
void string_drop_front(string_t *str, int n);
void string_drop_back(string_t *str, int n);

// Operations
string_t *string_substring(const string_t *str, int start, int length);
int string_compare(const string_t *str1, const string_t *str2);
int string_compare_cstr(const string_t *str, const char *data);
char *string_find_cstr(const string_t *str, const char *substr);

// Check if a matching cstr is found at the given position
bool string_starts_with_cstr_at(const string_t *str, const char *prefix, int pos);

bool string_contains_glob(const string_t *str);

// UTF-8 specific
int string_utf8_length(const string_t *str);
bool string_is_valid_utf8(const string_t *str);
bool string_utf8_char_at(const string_t *str, int char_index, char *buffer, int buffer_size);

string_list_t *string_list_create(void);
void string_list_destroy(string_list_t *list);
void string_list_take_append(string_list_t *list, string_t *str);
void string_list_clone_append(string_list_t *list, const string_t *str);
int string_list_size(const string_list_t *list);
const string_t *string_list_get(const string_list_t *list, int index);
void string_list_take_replace(string_list_t *list, int index, string_t *str);

#endif

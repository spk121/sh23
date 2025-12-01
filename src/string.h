#ifndef STRING_WITH_A_CAPITAL_S_H
#define STRING_WITH_A_CAPITAL_S_H

#include <stddef.h>

typedef struct String String;

// Create and destroy
String *string_create_from_cstr(const char *data);
String *string_create_empty(int capacity);
String *string_create_from(String *other);
void string_destroy(String *str);

// Accessors
const char *string_data(const String *str);
int string_length(const String *str);
int string_capacity(const String *str);
int string_is_empty(const String *str);

// Modification
int string_append_cstr(String *str, const char *data);
int string_append_ascii_char(String *str, char c);
int string_append(String *str, const String *other);
int string_clear(String *str);
int string_set_cstr(String *str, const char *data);
int string_resize(String *str, int new_capacity);

// Operations
String *string_substring(const String *str, int start, int length);
int string_compare(const String *str1, const String *str2);
int string_compare_cstr(const String *str, const char *data);
int string_find_cstr(const String *str, const char *substr, int *pos);
int string_replace_cstr(String *str, const char *find, const char *replace, int max_replacements);

// UTF-8 specific
int string_utf8_length(const String *str);
int string_is_valid_utf8(const String *str);
int string_utf8_char_at(const String *str, int char_index, char *buffer, int buffer_size);

#endif

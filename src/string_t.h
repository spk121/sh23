#ifndef STRING_T_H
#define STRING_T_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

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


static const int INITIAL_CAPACITY = 16;
static const int GROW_FACTOR = 2;
// When resizing down a string, reduce capacity if there's more than this many unused bytes
static const int REDUCE_THRESHOLD = 4096;

// Constructors
string_t *string_create(void);
string_t *string_create_from_n_chars(size_t count, char ch);
string_t *string_create_from_cstr(const char *data);
string_t *string_create_from_cstr_len(const char *data, int len);
string_t *string_create_from(const string_t *other);
string_t *string_create_from_range(const string_t *str, int start, int end);

// Destructors
void string_destroy(string_t **str);
char *string_release(string_t **str);

#if 0
string_t *string_vcreate(const char *format, va_list args);
string_t *string_create_from_format(const char *format, ...);
#endif

// Setters
void string_set(string_t *str, const string_t *str2);
// Free str contents. Move contents of str2 into str. Empty str2.
void string_move(string_t *str, string_t *str2);
// Free str contents. Move contents of str2 into str. Destroy str2.
void string_consume(string_t *str, string_t **str2);
void string_set_cstr(string_t *str, const char *cstr);
void string_set_char(string_t *str, char ch);
void string_set_data(string_t *str, const char *data, int n);
void string_set_n_chars(string_t *str, size_t count, char ch);
void string_set_substring(string_t *str, const string_t *str2, int begin2, int end2);

// Accessors
char string_at(const string_t *str, int index);
char string_front(const string_t *str);
char string_back(const string_t *str);
char *string_data(string_t *x);
char *string_data_at(string_t *str, int pos);
const char *string_cstr(const string_t *str);

// Capacity
bool string_empty(const string_t *str);
int string_size(const string_t *str);
int string_length(const string_t *str);
int string_max_size(const string_t *str);
void string_reserve(string_t *str, int new_cap);
int string_capacity(const string_t *str);
void string_shrink_to_fit(string_t *str);

// Modifiers
void string_clear(string_t *str);
void string_insert(string_t *str, int pos, const string_t *other);
void string_insert_n_chars(string_t *str, int pos, size_t count, char ch);
void string_insert_cstr(string_t *str, int pos, const char *cstr);
void string_insert_data(string_t *str, int pos, const char *data, int len);
void string_erase(string_t *str, int pos, int len);
void string_push_back(string_t *str, char ch);
void string_pop_back(string_t *str);
void string_append(string_t *str, const string_t *other);
void string_append_substring(string_t *str, const string_t *other, int begin, int end);
void string_append_cstr(string_t *str, const char *cstr);
void string_append_n_chars(string_t *str, size_t count, char ch);
void string_append_data(string_t *str, const char *data, int len);
void string_append_char(string_t *str, char c);
void string_replace(string_t *str, int pos, int len, const string_t *other);
void string_replace_substring(string_t *str, int pos, int len, const string_t *other, int begin2, int end2);
void string_replace_cstr(string_t *str, int pos, int len, const char *cstr);
void string_replace_n_chars(string_t *str, int pos, int len, size_t count, char ch);
void string_replace_data(string_t *str, int pos, int len, const char *data, int data_len);
void string_copy_to_cstr(const string_t *str, char *dest, int count);
void string_copy_to_cstr_at(const string_t *str, int pos, char *dest, int count);
void string_resize(string_t *str, int new_size);
void string_resize_with_char(string_t *str, int new_size, char ch);

int string_find(const string_t *str, const string_t *substr);
int string_find_at(const string_t *str, const string_t *substr, int pos);
int string_find_cstr(const string_t *str, const char *substr);
int string_find_cstr_at(const string_t *str, const char *substr, int pos);
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
int string_find_first_not_of_cstr_at(const string_t *str, const char *chars, int pos);
int string_find_first_not_of_predicate(const string_t *str, bool (*predicate)(char));
int string_find_first_not_of_predicate_at(const string_t *str, bool (*predicate)(char), int pos);
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
int string_compare_substring(const string_t *str1, int pos1, const string_t *str2, int begin2, int end2);
int string_compare_cstr_substring(const string_t *str, int pos, const char *cstr, int begin2, int end2);
bool string_starts_with(const string_t *str, const string_t *prefix);
bool string_starts_with_cstr(const string_t *str, const char *prefix);
bool string_ends_with(const string_t *str, const string_t *suffix);
bool string_ends_with_cstr (const string_t *str, const char *suffix);
bool string_contains(const string_t *str, const string_t *substr);
bool string_contains_cstr(const string_t *str, const char *substr);

string_t *string_substring(const string_t *str, int begin, int end);

bool string_eq(const string_t *str1, const string_t *str2);
bool string_lt(const string_t *str1, const string_t *str2);
bool string_le(const string_t *str1, const string_t *str2);
bool string_gt(const string_t *str1, const string_t *str2);
bool string_ge(const string_t *str1, const string_t *str2);
int string_cmp(const string_t *str1, const string_t *str2);

// Formatted I/O
void string_printf(string_t *str, const char *format, ...);
void string_vprintf(string_t *str, const char *format, va_list args);
#if 0
string_scanf_result_t string_scanf(const string_t *str, const char *format, ...);
string_vscanf_result_t string_vscanf(const string_t *str, const char *format,
                                        va_list args);
#endif
void string_getline(string_t *str, FILE *stream);
void string_getline_delim(string_t *str, int delim, FILE *stream);

int string_atoi(const string_t *str);
int string_atoi_at(const string_t *str, int pos, int *endpos);
long string_atol(const string_t *str);
long string_atol_at(const string_t *str, int pos, int *endpos);
long long string_atoll(const string_t *str);
long long string_atoll_at(const string_t *str, int pos, int *endpos);
double string_atof(const string_t *str);
double string_atof_at(const string_t *str, int pos, int *endpos);
string_t *string_from_int(int value);
string_t *string_from_long(long value);
string_t *string_from_double(double value);

uint32_t string_hash(const string_t *str);

string_list_t *string_list_create(void);
void string_list_destroy(string_list_t **list);
void string_list_move_push_back(string_list_t *list, string_t *str);
void string_list_push_back(string_list_t *list, const string_t *str);
int string_list_size(const string_list_t *list);
const string_t *string_list_at(const string_list_t *list, int index);
void string_list_assign(string_list_t *list, int index, const string_t *str);

#endif

#ifndef STRING_LIST_H
#define STRING_LIST_H

#include <stdint.h>
#include "string_t.h"

/**
 * @file string_list.h
 * @brief Dynamic array of string_t pointers with small list optimization
 */

/* ============================================================================
 * String List Type Definition
 * ============================================================================ */

#define STRING_LIST_T_INITIAL_CAPACITY 4

typedef struct string_list_t
{
    int size;
    int capacity;
    string_t **strings;
    string_t *inline_strings[STRING_LIST_T_INITIAL_CAPACITY]; // Small list optimization
} string_list_t;

/* ============================================================================
 * Constructors and Destructors
 * ============================================================================ */

/**
 * Creates an empty string list.
 *
 * @return New string list instance, or NULL on allocation failure
 */
string_list_t *string_list_create(void);

/**
 * Create a new string list by deep-copying a C string array.
 *
 * @param strv An array of C strings
 * @param len Length of the C array. If -1, strv is a NULL terminated list.
 * @return New string list instance.
 */
string_list_t *string_list_create_from_cstr_array(const char **strv, int len);

/**
 * Create a string list from the system environment variables.
 * Gets the environment from the system (environ on Unix, _environ on Windows).
 *
 * @return New string list instance with environment variables, or NULL on allocation failure.
 */
string_list_t *string_list_create_from_system_env(void);

string_list_t *string_list_create_from(const string_list_t *other);

/**
 * Create a string list by splitting a string using a single character separator.
 *
 * @param str The string to split
 * @param separator Character to split on
 * @return New string list instance with split strings, or NULL on allocation failure
 */
string_list_t *string_list_create_from_string_split_char(const string_t *str, char separator);

/**
 * Create a string list by splitting a string using any of the characters in the separators
 * C-string.
 * @param str The string to split
 * @param separators C-string containing separator characters
 * @return New string list instance with split strings, or NULL on allocation failure
 */
string_list_t *string_list_create_from_string_split_cstr(const string_t *str,
                                                         const char *separators);

/**
 * Create a new string list containing a slice of another list.
 * Creates deep copies of the strings in the specified range [start, end).
 * 
 * @param list Source string list
 * @param start Starting index (inclusive), clamped to [0, size]
 * @param end Ending index (exclusive), clamped to [start, size]. Use -1 for end of list.
 * @return New string list with copied strings from [start, end), or NULL on error
 */
string_list_t *string_list_create_slice(const string_list_t *list, int start, int end);

/**
 * Destroys the string list and frees its memory.
 * Also destroys all strings contained in the list.
 * Sets the pointer to NULL.
 *
 * @param list Pointer to the list pointer (set to NULL after destruction)
 */
void string_list_destroy(string_list_t **list);

/* ============================================================================
 * Capacity
 * ============================================================================ */

/**
 * Returns the number of strings in the list.
 *
 * @param list The string list
 * @return Number of strings in the list
 */
int string_list_size(const string_list_t *list);

/* ============================================================================
 * Element Access
 * ============================================================================ */

/**
 * Returns the string at the given index in the list.
 *
 * @param list The string list
 * @param index Index of the string to retrieve
 * @return Pointer to the string at the given index, or NULL if out of bounds
 */
const string_t *string_list_at(const string_list_t *list, int index);

/* ============================================================================
 * Modifiers
 * ============================================================================ */

/**
 * Appends a copy of the string to the end of the list.
 *
 * @param list The string list
 * @param str String to copy and append (if NULL, does nothing)
 */
void string_list_push_back(string_list_t *list, const string_t *str);

/**
 * Moves the string into the list, taking ownership of it.
 * After this operation, str is set to NULL.
 *
 * @param list The string list
 * @param str Pointer to string pointer to move (if NULL or *str is NULL, does nothing)
 */
void string_list_move_push_back(string_list_t *list, string_t **str);

/**
 * Inserts a copy of the string into the list at the given index.
 * Index is clamped to [0, size]. If index equals size, this is a simple append.
 *
 * @param list The string list
 * @param index Position to insert at
 * @param str String to copy and insert (if NULL, inserts an empty string)
 */
void string_list_insert(string_list_t *list, int index, const string_t *str);

/**
 * Moves the string into the list at the given index, taking ownership of it.
 * Index is clamped to [0, size]. If index equals size, this is a simple append.
 * After this operation, str is set to NULL.
 *
 * @param list The string list
 * @param index Position to insert at
 * @param str Pointer to string pointer to move (if NULL or *str is NULL, does nothing)
 */
void string_list_move_insert(string_list_t *list, int index, string_t **str);

/**
 * Removes the string at the given index from the list and destroys it.
 *
 * @param list The string list
 * @param index Index of the string to remove (if out of bounds, does nothing)
 */
void string_list_erase(string_list_t *list, int index);

/**
 * Clears the list, destroying all strings contained in it.
 *
 * @param list The string list
 */
void string_list_clear(string_list_t *list);

/* ============================================================================
 * Conversion and Utility
 * ============================================================================ */

/**
 *  Returns a null-terminated array of C-strings representing the strings in the list.
 * The array and the C-strings are heap-allocated.
 * The number of strings is returned in out_size.
 * Caller must free the array and each C-string.
 *
 * @param list The string list
 * @param out_size Pointer to receive the number of strings (optional, can be NULL)
 * @return Null-terminated array of C-strings, or NULL on error
 */
char **string_list_to_cstr_array(const string_list_t *list, int *out_size);

/**
 * Returns a null-terminated array of C-strings representing the strings in the list.
 * The array and the C-strings are heap-allocated.
 * The number of strings is returned in out_size.
 * After this operation, the list is destroyed and set to NULL.
 * Caller must free the array and each C-string.
 *
 * @param list Pointer to the list pointer (destroyed after operation)
 * @param out_size Pointer to receive the number of strings (optional, can be NULL)
 * @return Null-terminated array of C-strings, or NULL on error
 */
char **string_list_release_cstr_array(string_list_t **list, int *out_size);

string_t *string_list_join(const string_list_t *list, const char *separator);

/**
 * Returns a new string that is the result of joining all strings in the list
 * with the given separator string.
 * After this operation, the list is destroyed and set to NULL.
 *
 * @param list Pointer to the list pointer (destroyed after operation)
 * @param separator Separator string to place between elements
 * @return Joined string, or NULL on error
 */
string_t *string_list_join_move(string_list_t **list, const char *separator);

#endif /* STRING_LIST_H */

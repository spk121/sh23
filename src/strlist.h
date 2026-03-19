#ifndef STRLIST_H
#define STRLIST_H

#include <stdint.h>

#include "miga/api.h"
#include "miga/string_t.h"

MIGA_EXTERN_C_START

/**
 * @file strlist.h
 * @brief Dynamic array of string_t pointers with small list optimization
 */

/* ============================================================================
 * String List Type Definition
 * ============================================================================ */

#define strlist_T_INITIAL_CAPACITY 4

typedef struct strlist_t
{
    int size;
    int capacity;
    string_t **strings;
    string_t *inline_strings[strlist_T_INITIAL_CAPACITY]; // Small list optimization
} strlist_t;

/* ============================================================================
 * Constructors and Destructors
 * ============================================================================ */

/**
 * Creates an empty string list.
 *
 * @return New string list instance, or NULL on allocation failure
 */
MIGA_API strlist_t *strlist_create(void);

/**
 * Create a new string list by deep-copying a C string array.
 *
 * @param strv An array of C strings
 * @param len Length of the C array. If -1, strv is a NULL terminated list.
 * @return New string list instance.
 */
MIGA_API strlist_t *strlist_create_from_cstr_array(const char **strv, int len);

/**
 * Create a string list from the system environment variables.
 * Gets the environment from the system (environ on Unix, _environ on Windows).
 * In ISO C, always returns an empty string list, since there is no environ
 * variable that can be queried in ISO C.
 *
 * @return New string list instance with environment variables, or NULL on allocation failure.
 */
MIGA_API strlist_t *strlist_create_from_system_env(void);

MIGA_API strlist_t *strlist_create_from(const strlist_t *other);

/**
 * Create a string list by splitting a string using a single character separator.
 *
 * @param str The string to split
 * @param separator Character to split on
 * @return New string list instance with split strings, or NULL on allocation failure
 */
MIGA_API strlist_t *strlist_create_from_string_split_char(const string_t *str, char separator);

/**
 * Create a string list by splitting a string using any of the characters in the separators
 * C-string.
 * @param str The string to split
 * @param separators C-string containing separator characters
 * @return New string list instance with split strings, or NULL on allocation failure
 */
MIGA_API strlist_t *strlist_create_from_string_split_cstr(const string_t *str,
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
MIGA_API strlist_t *strlist_create_slice(const strlist_t *list, int start, int end);

/**
 * Destroys the string list and frees its memory.
 * Also destroys all strings contained in the list.
 * Sets the pointer to NULL.
 *
 * @param list Pointer to the list pointer (set to NULL after destruction)
 */
MIGA_API void strlist_destroy(strlist_t **list);

/* ============================================================================
 * Capacity
 * ============================================================================ */

/**
 * Returns the number of strings in the list.
 *
 * @param list The string list
 * @return Number of strings in the list
 */
MIGA_API int strlist_size(const strlist_t *list);

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
MIGA_API const string_t *strlist_at(const strlist_t *list, int index);

/* ============================================================================
 * Modifiers
 * ============================================================================ */

/**
 * Appends a copy of the string to the end of the list.
 *
 * @param list The string list
 * @param str String to copy and append (if NULL, does nothing)
 */
MIGA_API void strlist_push_back(strlist_t *list, const string_t *str);

/**
 * Moves the string into the list, taking ownership of it.
 * After this operation, str is set to NULL.
 *
 * @param list The string list
 * @param str Pointer to string pointer to move (if NULL or *str is NULL, does nothing)
 */
MIGA_API void strlist_move_push_back(strlist_t *list, string_t **str);

/**
 * Inserts a copy of the string into the list at the given index.
 * Index is clamped to [0, size]. If index equals size, this is a simple append.
 *
 * @param list The string list
 * @param index Position to insert at
 * @param str String to copy and insert (if NULL, inserts an empty string)
 */
MIGA_API void strlist_insert(strlist_t *list, int index, const string_t *str);

/**
 * Moves the string into the list at the given index, taking ownership of it.
 * Index is clamped to [0, size]. If index equals size, this is a simple append.
 * After this operation, str is set to NULL.
 *
 * @param list The string list
 * @param index Position to insert at
 * @param str Pointer to string pointer to move (if NULL or *str is NULL, does nothing)
 */
MIGA_API void strlist_move_insert(strlist_t *list, int index, string_t **str);

/**
 * Removes the string at the given index from the list and destroys it.
 *
 * @param list The string list
 * @param index Index of the string to remove (if out of bounds, does nothing)
 */
MIGA_API void strlist_erase(strlist_t *list, int index);

/**
 * Clears the list, destroying all strings contained in it.
 *
 * @param list The string list
 */
MIGA_API void strlist_clear(strlist_t *list);

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
MIGA_API char **strlist_to_cstr_array(const strlist_t *list, int *out_size);

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
MIGA_API char **strlist_release_cstr_array(strlist_t **list, int *out_size);

MIGA_API string_t *strlist_join(const strlist_t *list, const char *separator);

/**
 * Returns a new string that is the result of joining all strings in the list
 * with the given separator string.
 * After this operation, the list is destroyed and set to NULL.
 *
 * @param list Pointer to the list pointer (destroyed after operation)
 * @param separator Separator string to place between elements
 * @return Joined string, or NULL on error
 */
MIGA_API string_t *strlist_join_move(strlist_t **list, const char *separator);

MIGA_EXTERN_C_END

#endif /* STRLIST_H */

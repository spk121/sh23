/**
 * @file pattern_removal.c
 * @brief Implementation of pattern removal parameter expansions
 *
 * Implements:
 * - ${var#pattern}  - Remove smallest matching prefix
 * - ${var##pattern} - Remove largest matching prefix
 * - ${var%pattern}  - Remove smallest matching suffix
 * - ${var%%pattern} - Remove largest matching suffix
 */

#include <stdlib.h>
#include <string.h>
#include "glob_util.h"
#include "logging.h"
#include "string_t.h"

/**
 * Remove prefix matching pattern (smallest match).
 * Implements ${var#pattern}
 *
 * Algorithm: Try each position from start (0, 1, 2, ...) until we find
 * the first (shortest) prefix that matches the pattern.
 *
 * Example:
 *   value = "path/to/file.txt"
 *   pattern = "*\/"
 *
 *   Try: "" - no match
 *   Try: "p" - no match
 *   ...
 *   Try: "path/" - MATCH! Return "to/file.txt"
 *
 * @param value The variable value
 * @param pattern The glob pattern to match
 * @return Newly allocated string with prefix removed, or copy of value if no match
 */
string_t *remove_prefix_smallest(const string_t *value, const string_t *pattern)
{
    if (value == NULL || pattern == NULL)
        return string_create();

    // Handle empty pattern - no removal
    if (string_empty(pattern))
        return string_create_from(value);

    const char *val_str = string_cstr(value);
    const char *pat_str = string_cstr(pattern);
    int val_len = string_length(value);

    // Try each position from start to end (shortest to longest prefix)
    for (int i = 0; i <= val_len; i++)
    {
        // Extract prefix of length i
        char *prefix = (char *)malloc(i + 1);
        if (prefix == NULL)
        {
            log_error("remove_prefix_smallest: malloc failed");
            return string_create_from(value);
        }
        memcpy(prefix, val_str, i);
        prefix[i] = '\0';

        // Check if pattern matches this prefix
        bool matched = glob_util_match(pat_str, prefix, 0);
        free(prefix);

        if (matched)
        {
            // Match found! Return the suffix (everything after position i)
            log_debug("remove_prefix_smallest: matched at position %d", i);
            return string_create_from_cstr(val_str + i);
        }
    }

    // No match - return original value
    log_debug("remove_prefix_smallest: no match, returning original");
    return string_create_from(value);
}

/**
 * Remove prefix matching pattern (largest match).
 * Implements ${var##pattern}
 *
 * Algorithm: Try each position from end (n, n-1, n-2, ...) until we find
 * the first (longest) prefix that matches the pattern.
 *
 * Example:
 *   value = "path/to/file.txt"
 *   pattern = "*\/"
 *
 *   Try: "path/to/file.txt" - no match
 *   Try: "path/to/file.tx" - no match
 *   ...
 *   Try: "path/to/" - MATCH! Return "file.txt"
 *
 * @param value The variable value
 * @param pattern The glob pattern to match
 * @return Newly allocated string with prefix removed, or copy of value if no match
 */
string_t *remove_prefix_largest(const string_t *value, const string_t *pattern)
{
    if (value == NULL || pattern == NULL)
        return string_create();

    // Handle empty pattern - no removal
    if (string_empty(pattern))
        return string_create_from(value);

    const char *val_str = string_cstr(value);
    const char *pat_str = string_cstr(pattern);
    int val_len = string_length(value);

    // Try each position from end to start (longest to shortest prefix)
    for (int i = val_len; i >= 0; i--)
    {
        // Extract prefix of length i
        char *prefix = (char *)malloc(i + 1);
        if (prefix == NULL)
        {
            log_error("remove_prefix_largest: malloc failed");
            return string_create_from(value);
        }
        memcpy(prefix, val_str, i);
        prefix[i] = '\0';

        // Check if pattern matches this prefix
        bool matched = glob_util_match(pat_str, prefix, 0);
        free(prefix);

        if (matched)
        {
            // Match found! Return the suffix (everything after position i)
            log_debug("remove_prefix_largest: matched at position %d", i);
            return string_create_from_cstr(val_str + i);
        }
    }

    // No match - return original value
    log_debug("remove_prefix_largest: no match, returning original");
    return string_create_from(value);
}

/**
 * Remove suffix matching pattern (smallest match).
 * Implements ${var%pattern}
 *
 * Algorithm: Try each position from end (n, n-1, n-2, ...) until we find
 * the first (shortest) suffix that matches the pattern.
 *
 * Example:
 *   value = "file.tar.gz"
 *   pattern = ".*"
 *
 *   Try: "" - no match
 *   Try: "z" - no match
 *   Try: "gz" - no match
 *   Try: ".gz" - MATCH! Return "file.tar"
 *
 * @param value The variable value
 * @param pattern The glob pattern to match
 * @return Newly allocated string with suffix removed, or copy of value if no match
 */
string_t *remove_suffix_smallest(const string_t *value, const string_t *pattern)
{
    if (value == NULL || pattern == NULL)
        return string_create();

    // Handle empty pattern - no removal
    if (string_empty(pattern))
        return string_create_from(value);

    const char *val_str = string_cstr(value);
    const char *pat_str = string_cstr(pattern);
    int val_len = string_length(value);

    // Try each position from end to start (shortest to longest suffix)
    for (int i = val_len; i >= 0; i--)
    {
        // Extract suffix starting at position i
        const char *suffix = val_str + i;

        // Check if pattern matches this suffix
        bool matched = glob_util_match(pat_str, suffix, 0);

        if (matched)
        {
            // Match found! Return the prefix (everything before position i)
            log_debug("remove_suffix_smallest: matched at position %d", i);
            return string_create_from_cstr_len(val_str, i);
        }
    }

    // No match - return original value
    log_debug("remove_suffix_smallest: no match, returning original");
    return string_create_from(value);
}

/**
 * Remove suffix matching pattern (largest match).
 * Implements ${var%%pattern}
 *
 * Algorithm: Try each position from start (0, 1, 2, ...) until we find
 * the first (longest) suffix that matches the pattern.
 *
 * Example:
 *   value = "file.tar.gz"
 *   pattern = ".*"
 *
 *   Try: "file.tar.gz" - no match
 *   Try: "ile.tar.gz" - no match
 *   ...
 *   Try: ".tar.gz" - MATCH! Return "file"
 *
 * @param value The variable value
 * @param pattern The glob pattern to match
 * @return Newly allocated string with suffix removed, or copy of value if no match
 */
string_t *remove_suffix_largest(const string_t *value, const string_t *pattern)
{
    if (value == NULL || pattern == NULL)
        return string_create();

    // Handle empty pattern - no removal
    if (string_empty(pattern))
        return string_create_from(value);

    const char *val_str = string_cstr(value);
    const char *pat_str = string_cstr(pattern);
    int val_len = string_length(value);

    // Try each position from start to end (longest to shortest suffix)
    for (int i = 0; i <= val_len; i++)
    {
        // Extract suffix starting at position i
        const char *suffix = val_str + i;

        // Check if pattern matches this suffix
        bool matched = glob_util_match(pat_str, suffix, 0);

        if (matched)
        {
            // Match found! Return the prefix (everything before position i)
            log_debug("remove_suffix_largest: matched at position %d", i);
            return string_create_from_cstr_len(val_str, i);
        }
    }

    // No match - return original value
    log_debug("remove_suffix_largest: no match, returning original");
    return string_create_from(value);
}

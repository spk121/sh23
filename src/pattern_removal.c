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

#ifdef POSIX_API
#include <fnmatch.h>
#endif
#include <string.h>
#include <stdlib.h>
#include "string_t.h"
#include "logging.h"

#ifndef POSIX_API
#define FNM_NOMATCH 1

/**
 * Simplified fnmatch implementation for Windows.
 * Supports: *, ?, [...], [!...] character classes
 * Does not support: extended patterns, locale-specific collation
 */
static int fnmatch(const char *pattern, const char *string, int flags)
{
    (void)flags; // Unused in this simple implementation
    
    const char *p = pattern;
    const char *s = string;
    const char *star_pattern = NULL;
    const char *star_string = NULL;
    
    while (*s)
    {
        if (*p == '*')
        {
            // Remember this position for backtracking
            star_pattern = p++;
            star_string = s;
        }
        else if (*p == '?' || *p == *s)
        {
            // '?' matches any character, or exact match
            p++;
            s++;
        }
        else if (*p == '[')
        {
            // Character class matching
            p++; // skip '['
            int negate = 0;
            int matched = 0;
            
            if (*p == '!' || *p == '^')
            {
                negate = 1;
                p++;
            }
            
            while (*p && *p != ']')
            {
                if (p[1] == '-' && p[2] != ']' && p[2] != '\0')
                {
                    // Range: a-z
                    if (*s >= *p && *s <= p[2])
                        matched = 1;
                    p += 3;
                }
                else
                {
                    // Single character
                    if (*s == *p)
                        matched = 1;
                    p++;
                }
            }
            
            if (*p == ']')
                p++; // skip closing ']'
            
            if (matched == negate)
                goto backtrack; // Character class didn't match
            
            s++;
        }
        else
        {
backtrack:
            // Mismatch - try backtracking to the last '*'
            if (star_pattern)
            {
                p = star_pattern + 1;
                s = ++star_string;
            }
            else
            {
                return FNM_NOMATCH;
            }
        }
    }
    
    // Skip trailing stars in pattern
    while (*p == '*')
        p++;
    
    return (*p == '\0') ? 0 : FNM_NOMATCH;
}
#endif

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
        int match_result = fnmatch(pat_str, prefix, 0);
        free(prefix);
        
        if (match_result == 0)
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
        int match_result = fnmatch(pat_str, prefix, 0);
        free(prefix);
        
        if (match_result == 0)
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
        int match_result = fnmatch(pat_str, suffix, 0);
        
        if (match_result == 0)
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
        int match_result = fnmatch(pat_str, suffix, 0);
        
        if (match_result == 0)
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

/**
 * @file pattern_removal.h
 * @brief Pattern removal parameter expansion functions
 */

#ifndef PATTERN_REMOVAL_H
#define PATTERN_REMOVAL_H

#include "string_t.h"

/**
 * Remove smallest matching prefix.
 * Implements \${var#pattern}
 * 
 * Example: \${path#star-slash} where path="a/b/c" -> "b/c"
 * 
 * @param value The variable value
 * @param pattern The glob pattern to match
 * @return Newly allocated string with prefix removed (caller must free)
 */
string_t *remove_prefix_smallest(const string_t *value, const string_t *pattern);

/**
 * Remove largest matching prefix.
 * Implements \${var##pattern}
 * 
 * Example: \${path##star-slash} where path="a/b/c" -> "c"
 * 
 * @param value The variable value
 * @param pattern The glob pattern to match
 * @return Newly allocated string with prefix removed (caller must free)
 */
string_t *remove_prefix_largest(const string_t *value, const string_t *pattern);

/**
 * Remove smallest matching suffix.
 * Implements \${var%pattern}
 * 
 * Example: \${file%dot-star} where file="name.tar.gz" -> "name.tar"
 * 
 * @param value The variable value
 * @param pattern The glob pattern to match
 * @return Newly allocated string with suffix removed (caller must free)
 */
string_t *remove_suffix_smallest(const string_t *value, const string_t *pattern);

/**
 * Remove largest matching suffix.
 * Implements \${var%%pattern}
 * 
 * Example: \${file%%dot-star} where file="name.tar.gz" -> "name"
 * 
 * @param value The variable value
 * @param pattern The glob pattern to match
 * @return Newly allocated string with suffix removed (caller must free)
 */
string_t *remove_suffix_largest(const string_t *value, const string_t *pattern);

#endif /* PATTERN_REMOVAL_H */

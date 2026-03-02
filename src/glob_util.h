#ifndef GLOB_UTIL_H
#define GLOB_UTIL_H

#include <stdbool.h>

#include "string_list.h"
#include "string_t.h"

/* ============================================================================
 * Pattern Matching (fnmatch-like API)
 * ============================================================================ */

/**
 * Pattern matching flags (compatible with POSIX fnmatch flags where applicable).
 */
typedef enum
{
    GLOB_UTIL_NONE = 0,
    GLOB_UTIL_PATHNAME = (1 << 0), /**< Slash must be matched explicitly */
    GLOB_UTIL_PERIOD = (1 << 1),   /**< Leading period must be matched explicitly */
    GLOB_UTIL_NOESCAPE = (1 << 2), /**< Backslash treated as ordinary character */
    GLOB_UTIL_CASEFOLD = (1 << 3)  /**< Case-insensitive matching (extension) */
} glob_util_flags_t;

/**
 * Match a pattern against a string using shell glob patterns.
 *
 * Supported pattern syntax:
 * - *       : Matches zero or more characters
 * - ?       : Matches exactly one character
 * - [abc]   : Matches one character from the set
 * - [a-z]   : Matches one character from the range
 * - [!abc]  : Matches one character NOT in the set
 * - [^abc]  : Same as [!abc] (alternative negation syntax)
 *
 * On POSIX systems, this delegates to the system fnmatch() function.
 * On other platforms, uses a custom implementation with the same semantics.
 *
 * @param pattern The glob pattern to match
 * @param string The string to test against the pattern
 * @param flags Combination of glob_util_flags_t values (bitwise OR)
 * @return true if the pattern matches the string, false otherwise
 *
 * Examples:
 *   glob_util_match("*.txt", "file.txt", 0) -> true
 *   glob_util_match("test?.c", "test1.c", 0) -> true
 *   glob_util_match("[a-z]*", "hello", 0) -> true
 *   glob_util_match("a/b", "a/b", GLOB_UTIL_PATHNAME) -> true
 */
bool glob_util_match(const char *pattern, const char *string, int flags);

/**
 * Match a pattern against a string (string_t version).
 *
 * @param pattern The glob pattern (string_t)
 * @param string The string to test (string_t)
 * @param flags Pattern matching flags
 * @return true if the pattern matches, false otherwise
 */
bool glob_util_match_str(const string_t *pattern, const string_t *string, int flags);

/* ============================================================================
 * Pathname Expansion (glob-like API)
 * ============================================================================ */

/**
 * Expand a glob pattern against the filesystem.
 *
 * Platform behaviors:
 * - POSIX: Uses wordexp() for full expansion including tilde, variables, etc.
 * - UCRT: Uses _findfirst()/_findnext() for basic wildcard matching in current directory
 * - ISO_C: Returns NULL (no filesystem access available)
 *
 * The returned list contains filenames that match the pattern. If no matches
 * are found, returns NULL to signal that the pattern should be kept literal
 * (per POSIX shell behavior).
 *
 * @param pattern The glob pattern (may contain *, ?, [...])
 * @return List of matching paths, or NULL if no matches found
 *         Caller must free the returned list with string_list_destroy()
 *
 * Examples (POSIX):
 *   glob_util_expand_path("*.txt") -> ["file1.txt", "file2.txt"]
 *   glob_util_expand_path("test?.c") -> ["test1.c", "test2.c"]
 *   glob_util_expand_path("no-match-*") -> NULL
 */
string_list_t *glob_util_expand_path(const string_t *pattern);

/**
 * Expand a glob pattern with explicit flags and base directory.
 *
 * This is an extended version of glob_util_expand_path() that allows
 * more control over the expansion behavior.
 *
 * @param pattern The glob pattern
 * @param flags Pattern matching flags (currently unused, reserved for future)
 * @param base_dir Base directory for relative patterns (NULL = current directory)
 * @return List of matching paths, or NULL if no matches
 *
 * Note: Currently base_dir is not implemented and should be passed as NULL.
 * Future enhancement will support searching in specific directories.
 */
string_list_t *glob_util_expand_path_ex(const string_t *pattern, int flags, const char *base_dir);

#endif /* GLOB_UTIL_H */

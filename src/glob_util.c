#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <string.h>

#ifdef POSIX_API
#include <fnmatch.h>
#include <wordexp.h>
#endif

#ifdef UCRT_API
#include <errno.h>
#include <io.h>
#endif

#include "glob_util.h"

#include "logging.h"
#include "string_list.h"
#include "string_t.h"


/* ============================================================================
 * Pattern Matching Implementation
 * ============================================================================ */

#ifdef POSIX_API

bool glob_util_match(const char *pattern, const char *string, int flags)
{
    // Convert our flags to POSIX fnmatch flags
    int fn_flags = 0;

    if (flags & GLOB_UTIL_PATHNAME)
        fn_flags |= FNM_PATHNAME;
    if (flags & GLOB_UTIL_PERIOD)
        fn_flags |= FNM_PERIOD;
    if (flags & GLOB_UTIL_NOESCAPE)
        fn_flags |= FNM_NOESCAPE;

#ifdef FNM_CASEFOLD
    if (flags & GLOB_UTIL_CASEFOLD)
        fn_flags |= FNM_CASEFOLD;
#endif

    return fnmatch(pattern, string, fn_flags) == 0;
}

#else

/**
 * Helper: Compare two characters, optionally case-insensitive.
 * Returns true if characters match (according to flags).
 */
static inline int chars_match(char c1, char c2, int flags)
{
    if (c1 == c2)
        return 1;
    if (flags & GLOB_UTIL_CASEFOLD)
    {
        // Case-insensitive: convert to lowercase and compare
        char lower_c1 = (c1 >= 'A' && c1 <= 'Z') ? (c1 - 'A' + 'a') : c1;
        char lower_c2 = (c2 >= 'A' && c2 <= 'Z') ? (c2 - 'A' + 'a') : c2;
        return lower_c1 == lower_c2;
    }
    return 0;
}

/**
 * Helper: Compare two characters for range matching (case-insensitive if needed).
 * Returns true if c is within the range [start, end].
 */
static inline int char_in_range(char c, char start, char end, int flags)
{
    if (flags & GLOB_UTIL_CASEFOLD)
    {
        // Normalize to lowercase for comparison
        char lower_c = (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
        char lower_start = (start >= 'A' && start <= 'Z') ? (start - 'A' + 'a') : start;
        char lower_end = (end >= 'A' && end <= 'Z') ? (end - 'A' + 'a') : end;
        return lower_c >= lower_start && lower_c <= lower_end;
    }
    return c >= start && c <= end;
}

/**
 * Custom fnmatch implementation for non-POSIX platforms.
 *
 * This is a comprehensive implementation that supports:
 * - Wildcards: * (zero or more), ? (exactly one)
 * - Character classes: [abc], [a-z], [!abc], [^abc]
 * - Backtracking for proper * matching
 * - Flags: GLOB_UTIL_PATHNAME, GLOB_UTIL_PERIOD, GLOB_UTIL_NOESCAPE, GLOB_UTIL_CASEFOLD
 *
 * Based on the implementation from pattern_removal.c, enhanced for
 * full glob semantics.
 */
bool glob_util_match(const char *pattern, const char *string, int flags)
{
    const char *p = pattern;
    const char *s = string;
    const char *star_pattern = NULL;
    const char *star_string = NULL;

    while (*s)
    {
        // Handle escape sequences in pattern (unless GLOB_UTIL_NOESCAPE is set)
        if (!(flags & GLOB_UTIL_NOESCAPE) && *p == '\\' && p[1])
        {
            // Escaped character: treat next char as literal, not special
            p++;
            if (chars_match(*p, *s, flags))
            {
                p++;
                s++;
            }
            else
            {
                goto backtrack;
            }
        }
        else if (*p == '*')
        {
            // Handle GLOB_UTIL_PERIOD: * doesn't match a leading '.'
            // A '.' is "leading" at the start of the string, or after '/' when
            // GLOB_UTIL_PATHNAME is also set.
            if ((flags & GLOB_UTIL_PERIOD) && *s == '.')
            {
                int is_leading = (s == string);
                if (!is_leading && (flags & GLOB_UTIL_PATHNAME) && s > string && s[-1] == '/')
                    is_leading = 1;
                if (is_leading)
                    return false;
            }

            // Handle GLOB_UTIL_PATHNAME: * doesn't match '/'.
            // If s is already at '/', the * can only match zero characters here —
            // but the '/' itself must be matched by a literal '/' in the pattern.
            // Don't record a backtrack point that would let * consume the '/';
            // instead fall through to the literal-match branch which will fail and
            // backtrack to any earlier '*', or return false if there is none.
            if ((flags & GLOB_UTIL_PATHNAME) && *s == '/')
                goto backtrack;

            // Remember this position for backtracking.
            star_pattern = p++;
            star_string = s;
        }
        else if (*p == '?' || chars_match(*p, *s, flags))
        {
            // Handle GLOB_UTIL_PATHNAME flag: ? doesn't match /
            if (*p == '?' && (flags & GLOB_UTIL_PATHNAME) && *s == '/')
            {
                goto backtrack;
            }

            // Handle GLOB_UTIL_PERIOD flag: ? doesn't match leading .
            if (*p == '?' && (flags & GLOB_UTIL_PERIOD) && *s == '.')
            {
                // Check if . is in leading position (start of string or after /)
                int is_leading = (s == string);
                if (!is_leading && (flags & GLOB_UTIL_PATHNAME) && s > string && s[-1] == '/')
                {
                    is_leading = 1;
                }
                if (is_leading)
                {
                    goto backtrack;
                }
            }

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

            // Handle special case: ] immediately after [ is literal ]
            // This is required by POSIX: ] as first character doesn't close the class.
            // This allows [] to match ] and requires another ] to close the class.
            if (*p == ']')
            {
                if (chars_match(*s, ']', flags))
                    matched = 1;
                p++;
            }

            // Parse remaining characters in the class until we find unescaped ]
            while (*p)
            {
                int is_escaped = 0;

                // Handle escape sequences if GLOB_UTIL_NOESCAPE is not set.
                // An escaped character loses any special meaning (-, ], etc.)
                if (!(flags & GLOB_UTIL_NOESCAPE) && *p == '\\' && p[1])
                {
                    is_escaped = 1;
                    p++;
                }

                // Check for end of class: unescaped ]
                // This can't be the first ] (handled above), so it terminates the class
                if (!is_escaped && *p == ']')
                {
                    break;
                }

                // Check for range operator: char1-char2
                if (!is_escaped && p[1] == '-' && p[2] != ']' && p[2] != '\0')
                {
                    // Range found: match if string char is within [p to p[2]]
                    if (char_in_range(*s, *p, p[2], flags))
                        matched = 1;
                    p += 3;
                }
                else
                {
                    // Single character in set (either escaped or regular)
                    if (chars_match(*s, *p, flags))
                        matched = 1;
                    p++;
                }
            }

            if (*p == ']')
                p++; // skip closing ']'

            if (matched == negate)
                goto backtrack; // Character class didn't match

            // Handle GLOB_UTIL_PERIOD: [...] doesn't match a leading '.'
            // A '.' is "leading" at the start of the string, or after '/' when
            // GLOB_UTIL_PATHNAME is also set.
            if ((flags & GLOB_UTIL_PERIOD) && *s == '.')
            {
                int is_leading = (s == string);
                if (!is_leading && (flags & GLOB_UTIL_PATHNAME) && s > string && s[-1] == '/')
                    is_leading = 1;
                if (is_leading)
                    goto backtrack;
            }

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

                // Handle GLOB_UTIL_PERIOD: * doesn't match leading .
                if ((flags & GLOB_UTIL_PERIOD) && *s == '.')
                {
                    // Check if . is in leading position (start of string or after /)
                    int is_leading = (s == string);
                    if (!is_leading && (flags & GLOB_UTIL_PATHNAME) && s > string && s[-1] == '/')
                    {
                        is_leading = 1;
                    }
                    if (is_leading)
                    {
                        return false;
                    }
                }

                // With GLOB_UTIL_PATHNAME, * doesn't match / character.
                // When backtracking, if we hit a /, the * can't match past it, so fail.
                if ((flags & GLOB_UTIL_PATHNAME) && *s == '/')
                {
                    return false;
                }
            }
            else
            {
                return false;
            }
        }
    }

    // Skip trailing stars in pattern
    while (*p == '*')
        p++;

    return (*p == '\0');
}

#endif

bool glob_util_match_str(const string_t *pattern, const string_t *string, int flags)
{
    if (!pattern || !string)
        return false;

    return glob_util_match(string_cstr(pattern), string_cstr(string), flags);
}

/* ============================================================================
 * Pathname Expansion Implementation
 * ============================================================================ */

#ifdef POSIX_API

string_list_t *glob_util_expand_path(const string_t *pattern)
{
    if (!pattern)
        return NULL;

    const char *pattern_str = string_cstr(pattern);
    wordexp_t we;
    int ret;

    /* WRDE_NOCMD: prevent command substitution (security)
     * WRDE_UNDEF: fail on undefined variables (like $foo)
     * No WRDE_SHOWERR � we silence errors about ~nonexistentuser
     */
    ret = wordexp(pattern_str, &we, WRDE_NOCMD | WRDE_UNDEF);

    if (ret != 0 || we.we_wordc == 0)
    {
        /* Possible return values:
         * WRDE_BADCHAR: illegal char like | or ;
         * WRDE_BADVAL: undefined variable reference (with WRDE_UNDEF)
         * WRDE_CMDSUB: command substitution (blocked by WRDE_NOCMD)
         * WRDE_NOSPACE: out of memory
         * WRDE_SYNTAX: shell syntax error
         * Or we_wordc == 0: no matches
         */
        if (ret != 0)
        {
            wordfree(&we); // safe to call even on failure
        }
        return NULL; // treat errors and no matches the same: keep literal
    }

    /* Success and at least one match */
    string_list_t *result = string_list_create();

    for (size_t i = 0; i < we.we_wordc; i++)
    {
        string_t *expanded = string_create_from_cstr(we.we_wordv[i]);

        // Filter out . and .. entries by examining the final path component.
        // name_start is the index just after the last '/', or 0 if no slash present.
        int sep = string_find_last_of_cstr(expanded, "/");
        int name_start = (sep >= 0) ? sep + 1 : 0;

        if (string_compare_cstr_substring(expanded, name_start, -1, ".", 0, -1) == 0 ||
            string_compare_cstr_substring(expanded, name_start, -1, "..", 0, -1) == 0)
        {
            string_destroy(&expanded);
            continue;
        }

        string_list_move_push_back(result, &expanded);
    }

    wordfree(&we);

    // If only . and .. matched, return NULL
    if (string_list_size(result) == 0)
    {
        string_list_destroy(&result);
        return NULL;
    }

    return result;
}

#elifdef UCRT_API

string_list_t *glob_util_expand_path(const string_t *pattern)
{
    if (!pattern)
        return NULL;

    const char *pattern_str = string_cstr(pattern);
    log_debug("glob_util_expand_path: UCRT glob pattern='%s'", pattern_str);

    struct _finddata_t fd;
    intptr_t handle;

    // Attempt to find first matching file
    handle = _findfirst(pattern_str, &fd);
    if (handle == -1L)
    {
        if (errno == ENOENT)
        {
            // No matches found
            log_debug("glob_util_expand_path: no matches found");
            return NULL;
        }
        // Other error (access denied, etc.)
        log_debug("glob_util_expand_path: error: %s", strerror(errno));
        return NULL;
    }

    // Create result list
    string_list_t *result = string_list_create();

    // Extract the directory prefix from the pattern so we can reconstruct full
    // paths.  _findfirst/_findnext only return the bare filename in fd.name,
    // but callers expect paths in the same form as the POSIX wordexp() version
    // (e.g. pattern "src/*.c" -> results like "src/file.c", not "file.c").
    // string_find_last_of_cstr returns the index of the last separator, or -1.
    int sep_idx = string_find_last_of_cstr(pattern, "/\\");
    // dir_prefix is the leading "dir/" portion (inclusive of the separator),
    // or an empty string when sep_idx == -1 (sep_idx + 1 == 0 -> empty range).
    string_t *dir_prefix = string_create_from_range(pattern, 0, sep_idx + 1);

    // Add all matching files
    do
    {
        // Skip . and .. entries
        if (strcmp(fd.name, ".") == 0 || strcmp(fd.name, "..") == 0)
            continue;

        // Reconstruct the full path by appending the bare filename to the prefix.
        string_t *filepath = string_create_from(dir_prefix);
        string_append_cstr(filepath, fd.name);

        string_list_move_push_back(result, &filepath);
        log_debug("glob_util_expand_path: matched '%s'", fd.name);

    } while (_findnext(handle, &fd) == 0);

    string_destroy(&dir_prefix);

    _findclose(handle);

    // If no files were added (only . and .. were found), return NULL
    if (string_list_size(result) == 0)
    {
        log_debug("glob_util_expand_path: only . and .. matched");
        string_list_destroy(&result);
        return NULL;
    }

    log_debug("glob_util_expand_path: returning %d matches", string_list_size(result));
    return result;
}

#else

/* ISO_C: No filesystem access available */
string_list_t *glob_util_expand_path(const string_t *pattern)
{
    (void)pattern;
    log_warn("glob_util_expand_path: No glob implementation available in ISO_C mode");
    return NULL;
}

#endif

string_list_t *glob_util_expand_path_ex(const string_t *pattern, int flags, const char *base_dir)
{
    // For now, ignore flags and base_dir - future enhancement
    (void)flags;
    (void)base_dir;

    if (base_dir != NULL)
    {
        log_warn("glob_util_expand_path_ex: base_dir parameter not yet implemented");
    }

    return glob_util_expand_path(pattern);
}

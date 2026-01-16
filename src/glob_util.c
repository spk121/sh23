#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "glob_util.h"
#include "logging.h"
#include <string.h>
#include <stdlib.h>

#ifdef POSIX_API
#include <fnmatch.h>
#include <wordexp.h>
#endif

#ifdef UCRT_API
#include <io.h>
#include <errno.h>
#endif

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
 * Custom fnmatch implementation for non-POSIX platforms.
 * 
 * This is a comprehensive implementation that supports:
 * - Wildcards: * (zero or more), ? (exactly one)
 * - Character classes: [abc], [a-z], [!abc], [^abc]
 * - Backtracking for proper * matching
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
        if (*p == '*')
        {
            // Handle GLOB_UTIL_PATHNAME flag: * doesn't match /
            if ((flags & GLOB_UTIL_PATHNAME) && *s == '/')
            {
                // Move past the * and continue
                p++;
                continue;
            }
            
            // Remember this position for backtracking
            star_pattern = p++;
            star_string = s;
        }
        else if (*p == '?' || *p == *s)
        {
            // Handle GLOB_UTIL_PATHNAME flag: ? doesn't match /
            if (*p == '?' && (flags & GLOB_UTIL_PATHNAME) && *s == '/')
            {
                goto backtrack;
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

            // Handle special case: ] as first character in class
            if (*p == ']')
            {
                if (*s == ']')
                    matched = 1;
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
     * No WRDE_SHOWERR — we silence errors about ~nonexistentuser
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
            wordfree(&we);  // safe to call even on failure
        }
        return NULL;  // treat errors and no matches the same: keep literal
    }

    /* Success and at least one match */
    string_list_t *result = string_list_create();

    for (size_t i = 0; i < we.we_wordc; i++)
    {
        // Filter out . and .. entries
        const char *path = we.we_wordv[i];
        const char *name = strrchr(path, '/');
        name = name ? name + 1 : path;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        string_t *expanded = string_create_from_cstr(path);
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

    // Add all matching files
    do
    {
        // Skip . and .. entries
        if (strcmp(fd.name, ".") == 0 || strcmp(fd.name, "..") == 0)
            continue;

        // Add the matched filename to the result list
        string_t *filename = string_create_from_cstr(fd.name);
        string_list_move_push_back(result, &filename);
        log_debug("glob_util_expand_path: matched '%s'", fd.name);

    } while (_findnext(handle, &fd) == 0);

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

string_list_t *glob_util_expand_path_ex(const string_t *pattern, int flags, 
                                        const char *base_dir)
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
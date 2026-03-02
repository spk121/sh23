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
 * Internal Helpers
 * ============================================================================ */

/**
 * Check whether position `s` within `string` is a "leading" position for the
 * purpose of the GLOB_UTIL_PERIOD flag.
 *
 * A position is leading if it is the very start of the string, or (when
 * GLOB_UTIL_PATHNAME is also set) immediately after a '/'.
 */
static inline int is_leading_position(const char *s, const char *string, int flags)
{
    if (s == string)
        return 1;
    if ((flags & GLOB_UTIL_PATHNAME) && s > string && s[-1] == '/')
        return 1;
    return 0;
}

/**
 * Check whether the character at position `s` is a leading dot that must be
 * matched explicitly (i.e. GLOB_UTIL_PERIOD is set, `*s == '.'`, and the
 * position is leading).
 */
static inline int is_protected_dot(const char *s, const char *string, int flags)
{
    return (flags & GLOB_UTIL_PERIOD) && *s == '.' && is_leading_position(s, string, flags);
}

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
        char lower_c1 = (c1 >= 'A' && c1 <= 'Z') ? (char)(c1 - 'A' + 'a') : c1;
        char lower_c2 = (c2 >= 'A' && c2 <= 'Z') ? (char)(c2 - 'A' + 'a') : c2;
        return lower_c1 == lower_c2;
    }
    return 0;
}

/**
 * Helper: Check whether character `c` falls within the range [start, end].
 * Case-insensitive if GLOB_UTIL_CASEFOLD is set.
 */
static inline int char_in_range(char c, char start, char end, int flags)
{
    if (flags & GLOB_UTIL_CASEFOLD)
    {
        // Normalize to lowercase for comparison
        char lower_c = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        char lower_start = (start >= 'A' && start <= 'Z') ? (char)(start - 'A' + 'a') : start;
        char lower_end = (end >= 'A' && end <= 'Z') ? (char)(end - 'A' + 'a') : end;
        return lower_c >= lower_start && lower_c <= lower_end;
    }
    return c >= start && c <= end;
}

/**
 * Helper: Read one (possibly backslash-escaped) character from the pattern.
 *
 * On return, *out contains the literal character and *pp is advanced past it
 * (and past the backslash, if any).  Returns 1 if the character was escaped,
 * 0 otherwise.  If the pattern is exhausted (points to '\0'), returns -1 and
 * *out is undefined.
 */
static inline int read_pattern_char(const char **pp, char *out, int flags)
{
    const char *p = *pp;

    if (*p == '\0')
        return -1;

    if (!(flags & GLOB_UTIL_NOESCAPE) && *p == '\\' && p[1] != '\0')
    {
        p++;         // skip backslash
        *out = *p++; // consume the escaped character
        *pp = p;
        return 1; // was escaped
    }

    *out = *p++;
    *pp = p;
    return 0; // not escaped
}

/**
 * Parse and evaluate a bracket expression ([...]) against character `sc`.
 *
 * On entry, `*pp` must point to the first character *after* the opening '['.
 * On return, `*pp` points to the character after the closing ']', or to the
 * end of the pattern if no closing ']' was found (treating the whole thing as
 * a failed match — POSIX says the result is unspecified, but this is a common
 * behaviour).
 *
 * Returns 1 if `sc` matches the bracket expression, 0 otherwise.
 */
static int match_bracket(const char **pp, char sc, int flags)
{
    const char *p = *pp;
    int negate = 0;
    int matched = 0;

    // Check for negation prefix: [!...] or [^...]
    if (*p == '!' || *p == '^')
    {
        negate = 1;
        p++;
    }

    // POSIX: ']' immediately after '[' (or '[!' / '[^') is a literal ']', not
    // the end of the class.
    if (*p == ']')
    {
        if (chars_match(sc, ']', flags))
            matched = 1;
        p++;
    }

    // Scan the rest of the bracket expression.
    while (*p != '\0')
    {
        char ch;
        int escaped = read_pattern_char(&p, &ch, flags);
        if (escaped < 0)
            break; // hit '\0'

        // Unescaped ']' terminates the class.
        if (!escaped && ch == ']')
        {
            *pp = p; // past the ']'
            return matched != negate;
        }

        // Check for range:  <ch> '-' <end>
        // A '-' is a range operator only when:
        //   - It is not escaped.
        //   - The character after '-' is not ']' and not '\0'.
        // Otherwise '-' is literal.
        if (*p == '-' && p[1] != ']' && p[1] != '\0')
        {
            p++; // skip '-'

            // Read the range endpoint, handling a possible backslash escape.
            char end_ch;
            if (read_pattern_char(&p, &end_ch, flags) < 0)
                break; // hit '\0' unexpectedly

            if (char_in_range(sc, ch, end_ch, flags))
                matched = 1;
        }
        else
        {
            // Single character match.
            if (chars_match(sc, ch, flags))
                matched = 1;
        }
    }

    // If we get here we never found a closing ']'.  POSIX says the result is
    // unspecified; we treat the '[' as a literal that doesn't match.
    *pp = p;
    return 0;
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
 * Conformance notes vs POSIX fnmatch():
 * - Locale-dependent collation is not supported; ranges use byte values.
 * - Named character classes like [:alpha:] are not supported.
 */
bool glob_util_match(const char *pattern, const char *string, int flags)
{
    const char *p = pattern;
    const char *s = string;
    const char *star_pattern = NULL;
    const char *star_string = NULL;

    while (*s)
    {
        // ---- Protected leading dot: only an explicit literal '.' may match.
        // We check this first so that *, ?, and [...] all consistently fail
        // against a leading dot when GLOB_UTIL_PERIOD is active.
        if (is_protected_dot(s, string, flags))
        {
            // Consume any leading '*'s in the pattern — they cannot match the
            // dot, and we must not record them as backtrack points (the dot
            // will still be here if we ever backtrack to this position).
            while (*p == '*')
                p++;

            // The only thing that can match a protected dot is a literal '.'.
            // (An escaped '\.' also counts as literal.)
            if (*p == '.')
            {
                p++;
                s++;
                continue;
            }
            if (!(flags & GLOB_UTIL_NOESCAPE) && *p == '\\' && p[1] == '.')
            {
                p += 2;
                s++;
                continue;
            }
            // Nothing else matches a protected dot.
            return false;
        }

        // ---- Escape sequences (unless GLOB_UTIL_NOESCAPE)
        if (!(flags & GLOB_UTIL_NOESCAPE) && *p == '\\' && p[1])
        {
            p++; // skip backslash
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
        // ---- Wildcard: *
        else if (*p == '*')
        {
            // With GLOB_UTIL_PATHNAME, '*' does not match '/'.
            // If *s is '/', don't record a backtrack point — fall through so
            // that a literal '/' in the pattern can match it.
            if ((flags & GLOB_UTIL_PATHNAME) && *s == '/')
                goto backtrack;

            // Record backtrack point and skip the '*'.
            star_pattern = p++;
            star_string = s;
        }
        // ---- Wildcard: ?
        else if (*p == '?')
        {
            // With GLOB_UTIL_PATHNAME, '?' does not match '/'.
            if ((flags & GLOB_UTIL_PATHNAME) && *s == '/')
                goto backtrack;

            p++;
            s++;
        }
        // ---- Character class: [...]
        else if (*p == '[')
        {
            // With GLOB_UTIL_PATHNAME, bracket expressions do not match '/'.
            if ((flags & GLOB_UTIL_PATHNAME) && *s == '/')
                goto backtrack;

            p++; // skip '['
            if (!match_bracket(&p, *s, flags))
                goto backtrack;

            s++;
        }
        // ---- Literal character match
        else if (chars_match(*p, *s, flags))
        {
            p++;
            s++;
        }
        else
        {
        backtrack:
            // Mismatch — try advancing the most recent '*' by one character.
            if (!star_pattern)
                return false;

            p = star_pattern + 1;
            s = ++star_string;

            // Check whether the new string position is past end-of-string.
            if (*s == '\0')
                break;

            // With GLOB_UTIL_PATHNAME, '*' cannot consume '/'.
            if ((flags & GLOB_UTIL_PATHNAME) && *s == '/')
                return false;

            // Note: we do NOT check for a protected dot here and return false.
            // Instead we let the next iteration of the main loop handle it via
            // the is_protected_dot() check at the top, which will correctly
            // skip over any '*'s and require a literal '.'.  Returning false
            // here was incorrect because the pattern may well have a literal
            // '.' that matches.
        }
    }

    // String exhausted — skip any trailing '*'s in the pattern.
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

#include "lib.h"
#include <stdint.h>
#include <locale.h>
#include <string.h>

static inline uint8_t ascii_tolower(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
}

/* ASCII-only case-insensitive string comparison (full strings) */
int ascii_strcasecmp(const char *s1, const char *s2)
{
    if (s1 == s2)
        return 0;

    while (*s1 && *s2)
    {
        uint8_t c1 = ascii_tolower((uint8_t)*s1++);
        uint8_t c2 = ascii_tolower((uint8_t)*s2++);

        if (c1 != c2)
        {
            return (int)c1 - (int)c2;
        }
    }

    /* One string ended; check the other */
    uint8_t c1 = ascii_tolower((uint8_t)*s1);
    uint8_t c2 = ascii_tolower((uint8_t)*s2);

    return (int)c1 - (int)c2;
}

/* ASCII-only case-insensitive string comparison (up to n bytes) */
int ascii_strncasecmp(const char *s1, const char *s2, int n)
{
    if (n == 0 || s1 == s2)
        return 0;

    while (n-- > 0 && *s1 && *s2)
    {
        uint8_t c1 = ascii_tolower((uint8_t)*s1++);
        uint8_t c2 = ascii_tolower((uint8_t)*s2++);
        if (c1 != c2)
        {
            return (int)c1 - (int)c2;
        }
    }

    if (n <= 0)
        return 0; // n exhausted without mismatch

    /* If n still > 0, compare next chars (or '\0' if end reached) */
    uint8_t c1 = ascii_tolower((uint8_t)*s1);
    uint8_t c2 = ascii_tolower((uint8_t)*s2);

    return (int)c1 - (int)c2;
}

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L || defined(_MSC_VER)
/* Securely zero out memory to prevent compiler optimization */
void memset_explicit(void *dest, int ch, size_t count)
{
    if (count == 0) {
        return;
    }
    volatile unsigned char *p = (volatile unsigned char *)dest;
    unsigned char val = (unsigned char)ch;  /* Truncate to byte as per memset behavior */
    for (size_t i = 0; i < count; ++i) {
        p[i] = val;
    }
}
#endif

/* ============================================================================
 * Locale support
 * ============================================================================
 */

/**
 * Initialize locale for the shell
 *
 * Sets locale from environment. This respects LC_ALL, LC_COLLATE, LANG, etc.
 * according to standard locale precedence rules.
 *
 * On all platforms (POSIX, UCRT, ISO C), setlocale(LC_ALL, "") reads locale
 * settings from the environment.
 */
void lib_setlocale(void)
{
    /* Set locale from environment variables (LC_ALL, LC_COLLATE, LANG, etc.) */
    setlocale(LC_ALL, "");
}

/**
 * Locale-aware string comparison using LC_COLLATE
 *
 * Uses strcoll() which is available on all three APIs (POSIX, UCRT, ISO C).
 * The comparison respects the current LC_COLLATE locale setting.
 *
 * Note: On Windows (UCRT), strcoll() may not handle multibyte UTF-8 characters
 * properly. This is a known limitation of the Windows C runtime. For proper
 * UTF-8 support on Windows, one would need to convert to wide characters and
 * use wcscoll(), but that adds significant complexity.
 */
int lib_strcoll(const char *s1, const char *s2)
{
    /* strcoll() is available on all three APIs: POSIX, UCRT, and ISO C */
    return strcoll(s1, s2);
}

/* ============================================================================
 * Quoting support for POSIX shell 'set' builtin
 * ============================================================================
 */

/**
 * Check if a character needs to be backslash-escaped
 */
static bool needs_backslash_escape(char c)
{
    switch (c) {
        case '|': case '&': case ';': case '<': case '>': case '(':
        case ')': case '$': case '`': case '\\': case '"': case '\'':
        case ' ': case '\t': case '\n': case '*': case '?': case '[':
        case ']': case '#': case '~': case '{': case '}':
            return true;
        default:
            return false;
    }
}

/**
 * Quote using backslash escaping
 * Returns a new string_t with the quoted value
 */
static string_t *quote_backslash(const string_t *value)
{
    string_t *quoted = string_create();

    const char *data = string_cstr(value);
    int len = string_length(value);

    for (int i = 0; i < len; i++) {
        char c = data[i];
        if (needs_backslash_escape(c)) {
            string_append_char(quoted, '\\');
        }
        string_append_char(quoted, c);
    }

    return quoted;
}

/**
 * Check if value can be safely single-quoted
 * (must not contain single quotes)
 */
static bool can_single_quote(const string_t *value)
{
    return string_find_cstr(value, "'") == -1;
}

/**
 * Quote using single quotes (no escaping needed inside)
 * Returns a new string_t with the quoted value: 'value'
 */
static string_t *quote_single(const string_t *value)
{
    string_t *quoted = string_create();
    string_append_char(quoted, '\'');
    string_append(quoted, value);
    string_append_char(quoted, '\'');
    return quoted;
}

/**
 * Check if value can be safely double-quoted
 * (must not contain unescaped double quotes)
 */
static bool can_double_quote(const string_t *value)
{
    return string_find_cstr(value, "\"") == -1;
}

/**
 * Quote using double quotes
 * Backslash-escape $, `, and \ within double quotes
 * Returns a new string_t with the quoted value: "value"
 */
static string_t *quote_double(const string_t *value)
{
    string_t *quoted = string_create();
    string_append_char(quoted, '"');

    const char *data = string_cstr(value);
    int len = string_length(value);

    for (int i = 0; i < len; i++) {
        char c = data[i];
        if (c == '$' || c == '`' || c == '\\') {
            string_append_char(quoted, '\\');
        }
        string_append_char(quoted, c);
    }

    string_append_char(quoted, '"');
    return quoted;
}

/**
 * Quote a shell variable for the 'set' builtin
 *
 * Creates a string representation suitable for the POSIX shell 'set' builtin.
 * Tries three different quoting strategies and returns the shortest result.
 */
string_t *lib_quote(const string_t *key, const string_t *value)
{
    string_t *result = string_create();

    // Start with key=
    string_append(result, key);
    string_append_char(result, '=');

    // Generate the three quoting methods
    string_t *repr1 = quote_backslash(value);  // Backslash quoting
    string_t *repr2 = NULL;                     // Double quoting (if possible)
    string_t *repr3 = NULL;                     // Single quoting (if possible)

    if (can_double_quote(value)) {
        repr2 = quote_double(value);
    }

    if (can_single_quote(value)) {
        repr3 = quote_single(value);
    }

    // Find the shortest representation
    string_t *shortest = repr1;
    int shortest_len = string_length(repr1);

    if (repr2 != NULL) {
        int len2 = string_length(repr2);
        if (len2 < shortest_len) {
            shortest = repr2;
            shortest_len = len2;
        }
    }

    if (repr3 != NULL) {
        int len3 = string_length(repr3);
        if (len3 < shortest_len) {
            shortest = repr3;
            shortest_len = len3;
        }
    }

    // Append the shortest representation to result
    string_append(result, shortest);

    // Clean up temporary strings
    string_destroy(&repr1);
    if (repr2 != NULL) {
        string_destroy(&repr2);
    }
    if (repr3 != NULL) {
        string_destroy(&repr3);
    }

    return result;
}


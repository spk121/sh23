#ifndef LIB_OF_LEFTOVER_JUNK_H
#define LIB_OF_LEFTOVER_JUNK_H

#include "string_t.h"

#if (defined (__STDC_VERSION__) && __STDC_VERSION__ >= 202311L)
// C23 has nullptr support
// However, UCRT claims to be C23 but does not define nullptr
  #if _MSC_VER >= 1900
    typedef void *nullptr_t;
    #define nullptr ((nullptr_t)0)
  #else
    #include <stddef.h>
  #endif
#elif defined(__cplusplus)
// C++ has nullptr support
#elif defined(__GNUC__)
// GCC had nullptr support before C23, so we can't rely on the __STDC_VERSION__ check
  #include <stddef.h>
  #ifndef _GCC_NULLPTR_T
    typedef void * nullptr_t;
    #define nullptr ((nullptr_t)0)
  #endif
#else
// For non-GCC C libraries claiming pre-C23 __STDC_VERSION__, define nullptr as ((void *)0)
  typedef void * nullptr_t;
  #define nullptr ((nullptr_t)0)
#endif

/* ASCII-only case-insensitive string comparison for ISO C */
int ascii_strcasecmp(const char *s1, const char *s2);
int ascii_strncasecmp(const char *s1, const char *s2, int n);

/* ============================================================================
 * Locale support
 * ============================================================================
 * These functions provide portable locale handling across POSIX, UCRT, and ISO C.
 */

/**
 * Initialize locale for the shell
 *
 * Sets the locale from the environment (LC_ALL, LC_COLLATE, LANG, etc.)
 * This should be called early in main() after arena initialization.
 */
void lib_setlocale(void);

/**
 * Locale-aware string comparison using LC_COLLATE
 *
 * Compares two strings according to the current locale's collation rules.
 *
 * @param s1 First string to compare
 * @param s2 Second string to compare
 * @return <0 if s1 < s2, 0 if s1 == s2, >0 if s1 > s2 (in collation order)
 *
 * Note: On Windows (UCRT), this function may not properly handle multibyte
 * UTF-8 characters. For full UTF-8 support on Windows, consider converting
 * to wide characters and using wcscoll().
 */
int lib_strcoll(const char *s1, const char *s2);

/* ============================================================================
 * Quoting support for POSIX shell 'set' builtin
 * ============================================================================
 * Quotes a value string in three different ways and returns the shortest
 * representation in the form key=quoted_value.
 *
 * Representation 1 - Backslash quoting: Escapes special characters
 * Representation 2 - Double quoting: Wraps in "..." with selective escaping
 * Representation 3 - Single quoting: Wraps in '...' (no escaping possible)
 */

/**
 * Quote a shell variable for the 'set' builtin
 *
 * Creates a string representation suitable for the POSIX shell 'set' builtin.
 * Tries three different quoting strategies and returns the shortest result.
 *
 * @param key The variable name (never requires quoting)
 * @param value The variable value (requires quoting based on content)
 * @return A newly allocated string_t in the form "key=quoted_value"
 *         The shortest of three quoting representations is chosen.
 *         Caller is responsible for freeing the returned string with string_destroy().
 */
string_t *lib_quote(const string_t *key, const string_t *value);



/* POSIX-style file descriptor constants for UCRT (Windows) */
#ifdef UCRT_API
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif
#endif

#endif

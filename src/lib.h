#ifndef LIB_OF_LEFTOVER_JUNK_H
#define LIB_OF_LEFTOVER_JUNK_H

#include "string_t.h"

/* lib.h - C23 compatibility layer
 *
 * This module provides ISO C23 compatibility features for non-C23 compilers,
 * and libraries.
 * */

#ifndef __cplusplus
#include <stddef.h>

/* Check for known guard macros that indicate nullptr_t exists */
#if defined(_GCC_NULLPTR_T)
#define LIB_NULLPTR_PROVIDED 1
#endif

/* Check for Clang's indicator (varies by version) */
#if defined(__clang__) && defined(__clang_major__) && __clang_major__ >= 16
#ifndef LIB_NULLPTR_PROVIDED
#define LIB_NULLPTR_PROVIDED 1
#endif
#endif

/* Check for proper C23 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#if !defined(_MSC_VER)
#ifndef LIB_NULLPTR_PROVIDED
#define LIB_NULLPTR_PROVIDED 1
#endif
#endif
#endif

/* Fallback: provide our own */
#ifndef LIB_NULLPTR_PROVIDED
typedef void *nullptr_t;
#define nullptr ((void *)0)
#define LIB_NULLPTR_PROVIDED 1
#endif

#endif /* !__cplusplus */

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L || defined(_MSC_VER)
/* Securely zero out memory to prevent compiler optimization */
void memset_explicit(void *dest, int ch, size_t count);
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

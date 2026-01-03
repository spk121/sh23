#ifndef LIB_OF_LEFTOVER_JUNK_H
#define LIB_OF_LEFTOVER_JUNK_H

#include <stddef.h>
#if (defined (__STDC_VERSION__) && __STDC_VERSION__ < 202311L)
#ifndef _GCC_NULLPTR_T
typedef void * nullptr_t;
#define nullptr ((nullptr_t)0)
#endif
#endif

/* ASCII-only case-insensitive string comparison for ISO C */
int ascii_strcasecmp(const char *s1, const char *s2);
int ascii_strncasecmp(const char *s1, const char *s2, int n);

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

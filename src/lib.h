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

#endif

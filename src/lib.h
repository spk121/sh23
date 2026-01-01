#ifndef LIB_OF_LEFTOVER_JUNK_H
#define LIB_OF_LEFTOVER_JUNK_H

#include <stddef.h>
#if __STDC_VERSION_STDDEF_H__ != 201311L
typedef typeof(NULL) nullptr_t;
#define nullptr (NULL)
#endif

/* ASCII-only case-insensitive string comparison for ISO C */
int ascii_strcasecmp(const char *s1, const char *s2);
int ascii_strncasecmp(const char *s1, const char *s2, int n);

#endif

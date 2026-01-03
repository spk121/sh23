#ifndef LIB_OF_LEFTOVER_JUNK_H
#define LIB_OF_LEFTOVER_JUNK_H

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

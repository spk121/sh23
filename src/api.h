/* api.h - visibility and guard macros for the public API

  No copyright by Michael Gran. Modified 2025,2026.

  This file is part of Miga Shell.

  This sort of boilerplate is extremely common.
  It would be wrong to claim copyright on it.
*/#ifndef MIGA_API_H
#define MIGA_API_H

/*
   The public API macros for use with public header files:
    - put MIGA_EXTERN_C_START just after the #include block of a header file
    - put MIGA_EXTERN_C_END just before the #endif of the header guard
    - put MIGA_API before public function declarations
    - put MIGA_LOCAL before internal function declarations (if any) in the header
    - define MIGA_BUILD_SHARED when compiling the library itself
    - do not define MIGA_BUILD_SHARED when consuming the library
*/

/* These macros control symbol visibility; they describe whether a function
   or variable in a dynamically linked library is visible outside the library
   (exported) or if it is hidden (internal).

   While elsewhere we speak of our three configurations: POSIX, UCRT, and ISO C,
   and we try to abstract over platform differences, the symbol visibility attributes
   are actually platform-specific and don't really fit into that model.
*/

/* MIGA_API controls symbol visibility / dll export-import.
   MIGA_LOCAL can be used to explicitly hide symbols.
*/

#if defined(_WIN32) || defined(__CYGWIN__)
    /* Windows - MSVC & MinGW/GCC on Windows */
    #if defined(__GNUC__) || defined(__clang__)
        /* MinGW / Clang on Windows */
        #define MIGA_EXPORT     __attribute__((dllexport))
        #define MIGA_IMPORT     __attribute__((dllimport))
    #else
        /* Classic MSVC */
        #define MIGA_EXPORT     __declspec(dllexport)
        #define MIGA_IMPORT     __declspec(dllimport)
    #endif
    #define MIGA_LOCAL
#else
    /* GCC / Clang / ICC on Linux, macOS, *BSD, etc. */
    #if __GNUC__ >= 4 || defined(__clang__)
        #define MIGA_EXPORT     __attribute__((visibility("default")))
        #define MIGA_IMPORT
        #define MIGA_LOCAL      __attribute__((visibility("hidden")))
    #else
        /* Very old compiler or unknown platform -> no control */
        #define MIGA_EXPORT
        #define MIGA_IMPORT
        #define MIGA_LOCAL
    #endif
#endif


#if defined(MIGA_BUILD_SHARED)
/* We are building the shared library -> export symbols */
    #define MIGA_API  MIGA_EXPORT
#else
/* We are consuming the shared library -> import symbols (Windows only) */
    #define MIGA_API  MIGA_IMPORT
#endif

/* C++ header guards  */
#ifdef __cplusplus
    #define MIGA_EXTERN_C_START   extern "C" {
    #define MIGA_EXTERN_C_END     }
#else
    #define MIGA_EXTERN_C_START
    #define MIGA_EXTERN_C_END
#endif

#endif /* MIGA_API_H */


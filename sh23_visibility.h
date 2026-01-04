#ifndef SH23_VISIBILITY_H
#define SH23_VISIBILITY_H

/*
 * Symbol visibility control for sh23 libraries
 *
 * This header provides macros to control which symbols are exported from
 * shared libraries and which remain internal. It respects the API_TYPE
 * selection to ensure ISO C mode doesn't use compiler extensions.
 *
 * Usage in headers:
 *
 *   SH23_API         - Public API functions (exported in debug, hidden in release)
 *   SH23_INTERNAL    - Internal functions (always hidden when possible)
 *   SH23_TEST_API    - Functions only needed for testing (exported only with IN_CTEST)
 *
 * Example:
 *
 *   // Public API - always accessible
 *   SH23_API void public_function(void);
 *
 *   // Internal helper - hidden from public interface
 *   SH23_INTERNAL void internal_helper(void);
 *
 *   // Test-only function - only visible when building tests
 *   SH23_TEST_API void test_hook_function(void);
 */

#if defined(ISO_C)
 /*
  * ISO C mode: No compiler-specific attributes allowed
  * All symbols use default visibility (typically public in shared libs)
  * Cannot control DLL exports on Windows without .def files.
  */
#define SH23_API
#define SH23_INTERNAL
#define SH23_TEST_API

#elif defined(_WIN32) || defined(__CYGWIN__)
 /*
  * Windows (UCRT mode): Use __declspec for DLL import/export
  */
#ifdef SH23_DEBUG_BUILD
#define SH23_API __declspec(dllexport)
#else
#define SH23_API
#endif

#define SH23_INTERNAL

#ifdef IN_CTEST
#define SH23_TEST_API __declspec(dllexport)
#else
#define SH23_TEST_API SH23_INTERNAL
#endif

#else
 /*
  * POSIX mode: Use GCC/Clang visibility attributes
  *
  * This provides the best development experience with strict layering
  * validation via --no-undefined when building shared libraries.
  */
#ifdef SH23_DEBUG_BUILD
#define SH23_API __attribute__((visibility("default")))
#else
#define SH23_API
#endif

#define SH23_INTERNAL __attribute__((visibility("hidden")))

#ifdef IN_CTEST
#define SH23_TEST_API __attribute__((visibility("default")))
#else
#define SH23_TEST_API SH23_INTERNAL
#endif

#endif

#endif /* SH23_VISIBILITY_H */

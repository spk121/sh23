#ifndef MY_CTEST_H
#define MY_CTEST_H

#ifdef CTEST
#error "CTEST already defined"
#endif

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

typedef struct CTest CTest;

// Test function signature
typedef void (*CTestFunc)(CTest*);

// Setup/teardown function signature
typedef void (*CTestSetupFunc)(CTest*);

// Test entry
typedef struct {
    const char* name;
    CTestFunc func;
    CTestSetupFunc setup;
    CTestSetupFunc teardown;
    bool xfail;           // expected to fail
	char padding[7];
} CTestEntry;

// Test context (passed to each test)
typedef struct CTest {
    int tests_run;
    int tests_failed;
    const char* current_test;
    void* user_data;      // optional, for fixtures
} CTest;

// === Assertion Macros ===
bool ctest_assert(CTest* ctest, bool condition, const char* file, int line,
          const char* test_name, const char* msg);

#define CTEST_ASSERT(ctest, cond, msg) \
    ctest_assert(ctest, (cond), __FILE__, __LINE__, (ctest)->current_test, (msg))

#if 0
bool ctest_assert_eq_char(CTest* ctest, char a, char b, const char* file, int line, const char* test_name, const char* msg);

#define CTEST_ASSERT_EQ(ctest, a, b, msg) \
    _Generic((a), \
        char: ctest_assert_eq_char, \
        unsigned char: ctest_assert_eq_uchar, \
        int: ctest_assert_eq_int, \
        unsigned int: ctest_assert_eq_uint, \
        long: ctest_assert_eq_long, \
        unsigned long: ctest_assert_eq_ulong, \
        const char*: ctest_assert_eq_str \

    )(ctest, (a), (b), __FILE__, __LINE__, (ctest)->current_test, (msg))
#endif

#define CTEST_ASSERT_EQ(ctest, a, b, msg) \
    CTEST_ASSERT(ctest, (a) == (b), msg " (expected " #a " == " #b ")")

#define CTEST_ASSERT_NE(ctest, a, b, msg) \
    CTEST_ASSERT(ctest, (a) != (b), msg " (expected " #a " != " #b ")")

#define CTEST_ASSERT_TRUE(ctest, cond, msg) \
    CTEST_ASSERT(ctest, (cond), msg " (" #cond ")")

#define CTEST_ASSERT_FALSE(ctest, cond, msg) \
    CTEST_ASSERT(ctest, !(cond), msg " (expected " #cond " to be false)")

#define CTEST_ASSERT_GT(ctest, a, b, msg) \
    CTEST_ASSERT(ctest, (a) > (b), msg " (expected " #a " > " #b ")")

#define CTEST_ASSERT_NULL(ctest, ptr, msg) \
    CTEST_ASSERT(ctest, (ptr) == NULL, msg " (expected " #ptr " to be NULL)")

#define CTEST_ASSERT_NOT_NULL(ctest, ptr, msg) \
    CTEST_ASSERT(ctest, (ptr) != NULL, msg)

#define CTEST_ASSERT_STR_EQ(ctest, s1, s2, msg) \
    CTEST_ASSERT(ctest, strcmp((s1), (s2)) == 0, msg " (expected " #s1 " == " #s2 ")")

// === Test Declaration Macros ===

// Simple test, no setup/teardown
#define CTEST(Name) \
    static void ctest_func_##Name(CTest *ctest); \
    static CTestEntry ctest_entry_##Name = { \
        .name = #Name, \
        .func = ctest_func_##Name, \
        .setup = NULL, \
        .teardown = NULL, \
        .xfail = false \
    }; \
    static void ctest_func_##Name(CTest *ctest)

// Test with setup/teardown
#define CTEST_WITH_FIXTURE(Name, setup_fn, teardown_fn) \
    static void ctest_func_##Name(CTest *ctest); \
    static CTestEntry ctest_entry_##Name = { \
        .name = #Name, \
        .func = ctest_func_##Name, \
        .setup = (setup_fn), \
        .teardown = (teardown_fn), \
        .xfail = false \
    }; \
    static void ctest_func_##Name(CTest *ctest)

// Expected-to-fail test (XFAIL)
#define CTEST_XFAIL(Name) \
    static void ctest_func_##Name(CTest *ctest); \
    static CTestEntry ctest_entry_##Name = { \
        .name = #Name, \
        .func = ctest_func_##Name, \
        .setup = NULL, \
        .teardown = NULL, \
        .xfail = true \
    }; \
    static void ctest_func_##Name(CTest *ctest)

#define CTEST_ENTRY(Name) &ctest_entry_##Name

// === Runner ===
int ctest_run_suite(CTestEntry** suite);  // NULL-terminated array

// Summary of last run
typedef struct {
    int tests_run;
    int unexpected_failures;
} CTestSummary;

CTestSummary ctest_last_summary(void);

#endif // MY_CTEST_H

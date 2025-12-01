#include "ctest.h"
#include <stdio.h>

static CTestSummary last_summary = { 0, 0 };

bool ctest_assert(CTest* ctest, bool condition, const char* file, int line, const char* test_name, const char* msg)
{
    if (!condition) {
        printf("#     FAIL: %s:%d in %s\n#          %s\n",
               file, line, test_name, msg);
        ctest->tests_failed++;
        return false;
    }
    return true;
}

bool ctest_assert_eq_char(CTest* ctest, char a, char b, const char* file, int line, const char* test_name, const char* msg)
{
    if (a != b) {
        printf("#     FAIL: %s:%d in %s\n#          %s\n#          Expected: '%c' (0x%02x)\n#          Actual:   '%c' (0x%02x)\n",
               file, line, test_name, msg, b, (unsigned char)b, a, (unsigned char)a);
        ctest->tests_failed++;
        return false;
    }
    return true;
}

bool ctest_assert_eq_uchar(CTest* ctest, unsigned char a, unsigned char b, const char* file, int line, const char* test_name, const char* msg)
{
    if (a != b) {
        printf("#     FAIL: %s:%d in %s\n#          %s\n#          Expected: 0x%02x\n#          Actual:   0x%02x\n",
               file, line, test_name, msg, b, a);
        ctest->tests_failed++;
        return false;
    }
    return true;
}


int ctest_run_suite(CTestEntry** suite)
{
    CTest ctest = { 0 };
    int test_count = 0;
    int unexpected_failures = 0;

    // Count tests
    for (CTestEntry** p = suite; *p; ++p) test_count++;

    printf("TAP version 14\n");
    printf("1..%d\n", test_count);

    for (CTestEntry** p = suite; *p; ++p) {
        CTestEntry* entry = *p;
        ctest.tests_run++;
        ctest.current_test = entry->name;
        int failed_before = ctest.tests_failed;

        // Run setup
        if (entry->setup) {
            entry->setup(&ctest);
        }

        // Only run test body if setup didn't fail
        if (ctest.tests_failed == failed_before) {
            entry->func(&ctest);
        }

        // Run teardown (always, even if test or setup failed)
        if (entry->teardown) {
            entry->teardown(&ctest);
        }

        bool this_failed = (ctest.tests_failed > failed_before);

        if (entry->xfail) {
            if (this_failed) {
                printf("not ok %d - %s # TODO expected failure\n",
                    ctest.tests_run, entry->name);
            }
            else {
                printf("ok %d - %s # TODO unexpected success\n",
                    ctest.tests_run, entry->name);
                unexpected_failures++;
            }
        }
        else {
            if (this_failed) {
                printf("not ok %d - %s\n", ctest.tests_run, entry->name);
                unexpected_failures++;
            }
            else {
                printf("ok %d - %s\n", ctest.tests_run, entry->name);
            }
        }
    }

    // Final summary
    if (unexpected_failures == 0) {
        printf("# All %d tests passed!\n", ctest.tests_run);
    }
    else {
        printf("# %d test(s) failed unexpectedly\n", unexpected_failures);
    }

    last_summary.tests_run = ctest.tests_run;
    last_summary.unexpected_failures = unexpected_failures;

    return unexpected_failures != 0;
}

CTestSummary ctest_last_summary(void)
{
    return last_summary;
}

#include "ctest.h"
#include "executor.h"
#include "string_t.h"
#include "logging.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Test: Executor Creation and Destruction
 * ============================================================================ */

static void test_executor_create_destroy(CTest *ctest)
{
    executor_t *executor = executor_create();
    
    // Verify executor was created
    CTEST_ASSERT(ctest, executor != NULL, "executor should be created");
    
    // Verify basic fields are initialized
    CTEST_ASSERT(ctest, executor->last_exit_status == 0, "last_exit_status should be 0");
    CTEST_ASSERT(ctest, executor->dry_run == false, "dry_run should be false");
    CTEST_ASSERT(ctest, executor->error_msg != NULL, "error_msg should be initialized");
    CTEST_ASSERT(ctest, executor->variables != NULL, "variables should be initialized");
    CTEST_ASSERT(ctest, executor->positional_params != NULL, "positional_params should be initialized");
    
    // Verify new special variable fields are initialized
    CTEST_ASSERT(ctest, executor->last_background_pid == 0, "last_background_pid should be 0");
#ifdef POSIX_API
    CTEST_ASSERT(ctest, executor->shell_pid > 0, "shell_pid should be set to getpid() on POSIX");
#else
    CTEST_ASSERT(ctest, executor->shell_pid == 0, "shell_pid should be 0 on non-POSIX");
#endif
    CTEST_ASSERT(ctest, executor->last_argument != NULL, "last_argument should be initialized");
    CTEST_ASSERT(ctest, string_length(executor->last_argument) == 0, "last_argument should be empty");
    CTEST_ASSERT(ctest, executor->shell_flags != NULL, "shell_flags should be initialized");
    CTEST_ASSERT(ctest, string_length(executor->shell_flags) == 0, "shell_flags should be empty");
    
    // Clean up
    executor_destroy(&executor);
    CTEST_ASSERT(ctest, executor == NULL, "executor should be NULL after destroy");
}

/* ============================================================================
 * Test: Executor Special Variables
 * ============================================================================ */

static void test_executor_special_variables(CTest *ctest)
{
    executor_t *executor = executor_create();
    
    // Test that we can modify special variable fields
    executor->last_background_pid = 12345;
    CTEST_ASSERT(ctest, executor->last_background_pid == 12345, "last_background_pid should be settable");
    
    // Test string fields
    string_append_cstr(executor->last_argument, "test_arg");
    CTEST_ASSERT(ctest, string_length(executor->last_argument) == 8, "last_argument should have length 8");
    CTEST_ASSERT(ctest, strcmp(string_cstr(executor->last_argument), "test_arg") == 0, "last_argument should contain 'test_arg'");
    
    string_append_cstr(executor->shell_flags, "ix");
    CTEST_ASSERT(ctest, string_length(executor->shell_flags) == 2, "shell_flags should have length 2");
    CTEST_ASSERT(ctest, strcmp(string_cstr(executor->shell_flags), "ix") == 0, "shell_flags should contain 'ix'");
    
    // Clean up (this verifies that string_destroy is called properly)
    executor_destroy(&executor);
    CTEST_ASSERT(ctest, executor == NULL, "executor should be NULL after destroy");
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(void)
{
    // Set log level to suppress debug output during tests
    g_log_threshold = LOG_ERROR;
    
    CTest ctest = {0};
    int previous_failures = 0;
    
    printf("TAP version 14\n");
    printf("1..2\n");
    
    // Run tests
    ctest.current_test = "test_executor_create_destroy";
    test_executor_create_destroy(&ctest);
    if (ctest.tests_failed == previous_failures)
        printf("ok 1 - test_executor_create_destroy\n");
    previous_failures = ctest.tests_failed;
    
    ctest.current_test = "test_executor_special_variables";
    test_executor_special_variables(&ctest);
    if (ctest.tests_failed == previous_failures)
        printf("ok 2 - test_executor_special_variables\n");
    
    // Print summary
    if (ctest.tests_failed == 0)
    {
        printf("# All 2 tests passed!\n");
        return 0;
    }
    else
    {
        printf("# %d test(s) failed unexpectedly\n", ctest.tests_failed);
        return 1;
    }
}

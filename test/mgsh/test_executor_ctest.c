#include <string.h>
#include "ctest.h"
#include "executor.h"
#include "string_t.h"
#include "logging.h"

/* ============================================================================
 * Test: Executor Creation and Destruction
 * ============================================================================ */

CTEST(test_executor_create_destroy)
{
    executor_t *executor = executor_create();
    
    // Verify executor was created
    CTEST_ASSERT_NOT_NULL(ctest, executor, "executor should be created");
    
    // Verify basic fields are initialized
    CTEST_ASSERT_EQ(ctest, executor->last_exit_status, 0, "last_exit_status should be 0");
    CTEST_ASSERT_FALSE(ctest, executor->dry_run, "dry_run should be false");
    CTEST_ASSERT_NOT_NULL(ctest, executor->error_msg, "error_msg should be initialized");
    CTEST_ASSERT_NOT_NULL(ctest, executor->variables, "variables should be initialized");
    CTEST_ASSERT_NOT_NULL(ctest, executor->positional_params, "positional_params should be initialized");
    
    // Verify new special variable fields are initialized
    CTEST_ASSERT_EQ(ctest, executor->last_background_pid, 0, "last_background_pid should be 0");
#ifdef POSIX_API
    CTEST_ASSERT(ctest, executor->shell_pid > 0, "shell_pid should be set to getpid() on POSIX");
#else
    CTEST_ASSERT_EQ(ctest, executor->shell_pid, 0, "shell_pid should be 0 on non-POSIX");
#endif
    CTEST_ASSERT_NOT_NULL(ctest, executor->last_argument, "last_argument should be initialized");
    CTEST_ASSERT_EQ(ctest, string_length(executor->last_argument), 0, "last_argument should be empty");
    CTEST_ASSERT_NOT_NULL(ctest, executor->shell_flags, "shell_flags should be initialized");
    CTEST_ASSERT_EQ(ctest, string_length(executor->shell_flags), 0, "shell_flags should be empty");
    
    // Clean up
    executor_destroy(&executor);
    CTEST_ASSERT_NULL(ctest, executor, "executor should be NULL after destroy");
    
    (void)ctest;
}

/* ============================================================================
 * Test: Executor Special Variables
 * ============================================================================ */

CTEST(test_executor_special_variables)
{
    executor_t *executor = executor_create();
    
    // Test that we can modify special variable fields
    executor->last_background_pid = 12345;
    CTEST_ASSERT_EQ(ctest, executor->last_background_pid, 12345, "last_background_pid should be settable");
    
    // Test string fields
    string_append_cstr(executor->last_argument, "test_arg");
    CTEST_ASSERT_EQ(ctest, string_length(executor->last_argument), 8, "last_argument should have length 8");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(executor->last_argument), "test_arg", "last_argument should contain 'test_arg'");
    
    string_append_cstr(executor->shell_flags, "ix");
    CTEST_ASSERT_EQ(ctest, string_length(executor->shell_flags), 2, "shell_flags should have length 2");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(executor->shell_flags), "ix", "shell_flags should contain 'ix'");
    
    // Clean up (this verifies that string_destroy is called properly)
    executor_destroy(&executor);
    CTEST_ASSERT_NULL(ctest, executor, "executor should be NULL after destroy");
    
    (void)ctest;
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(void)
{
    // Set log level to suppress debug output during tests
    g_log_threshold = LOG_ERROR;
    
    CTestEntry *suite[] = {
        CTEST_ENTRY(test_executor_create_destroy),
        CTEST_ENTRY(test_executor_special_variables),
        NULL
    };
    
    int result = ctest_run_suite(suite);
    
    return result;
}

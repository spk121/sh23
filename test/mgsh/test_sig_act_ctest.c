// ============================================================================
// test_sig_act_ctest.c
// Unit tests for sig_act signal handler archiving
// ============================================================================

#include "ctest.h"
#include "sig_act.h"
#include <signal.h>
#include <stdbool.h>

// Test signal handler flag - set by our test handler
static volatile sig_atomic_t test_handler_called = 0;

// Simple test signal handler
static void test_signal_handler(int signo)
{
    (void)signo;
    test_handler_called = 1;
}

// ============================================================================
// Basic Creation/Destruction Tests (All Platforms)
// ============================================================================

CTEST(test_sig_act_create_and_destroy)
{
    sig_act_store_t *store = sig_act_store_create();
    CTEST_ASSERT_NOT_NULL(ctest, store, "store created");

    sig_act_store_destroy(&store);
}

CTEST(test_sig_act_destroy_null_is_safe)
{
    sig_act_store_destroy(NULL);
    CTEST_ASSERT_TRUE(ctest, true, "destroy null did not crash");
}

CTEST(test_sig_act_is_saved_initially_false)
{
    sig_act_store_t *store = sig_act_store_create();

    CTEST_ASSERT_FALSE(ctest, sig_act_store_is_saved(store, SIGINT), "SIGINT not saved");
    CTEST_ASSERT_FALSE(ctest, sig_act_store_is_saved(store, SIGTERM), "SIGTERM not saved");

    sig_act_store_destroy(&store);
}

CTEST(test_sig_act_is_saved_with_null_store)
{
    CTEST_ASSERT_FALSE(ctest, sig_act_store_is_saved(NULL, SIGINT), "null store returns false");
}

CTEST(test_sig_act_is_saved_with_invalid_signal)
{
    sig_act_store_t *store = sig_act_store_create();

    CTEST_ASSERT_FALSE(ctest, sig_act_store_is_saved(store, -1), "negative signal returns false");
    CTEST_ASSERT_FALSE(ctest, sig_act_store_is_saved(store, 9999), "huge signal returns false");

    sig_act_store_destroy(&store);
}

CTEST(test_sig_act_get_returns_null_for_unsaved)
{
    sig_act_store_t *store = sig_act_store_create();

    const sig_act_t *entry = sig_act_store_get(store, SIGINT);
    CTEST_ASSERT_NULL(ctest, entry, "unsaved signal returns null");

    sig_act_store_destroy(&store);
}

// ============================================================================
// POSIX-Specific Tests
// ============================================================================

#ifdef POSIX_API

CTEST(test_sig_act_posix_set_and_save_basic)
{
    sig_act_store_t *store = sig_act_store_create();

    struct sigaction sa;
    sa.sa_handler = test_signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    int result = sig_act_store_set_and_save(store, SIGUSR2, &sa);
    CTEST_ASSERT_EQ(ctest, result, 0, "set_and_save succeeded");
    CTEST_ASSERT_TRUE(ctest, sig_act_store_is_saved(store, SIGUSR2), "signal marked as saved");

    sig_act_store_destroy(&store);
}

CTEST(test_sig_act_posix_set_and_save_preserves_original)
{
    sig_act_store_t *store = sig_act_store_create();

    struct sigaction sa1;
    sa1.sa_handler = test_signal_handler;
    sa1.sa_flags = 0;
    sigemptyset(&sa1.sa_mask);

    sig_act_store_set_and_save(store, SIGUSR2, &sa1);

    const sig_act_t *entry = sig_act_store_get(store, SIGUSR2);
    CTEST_ASSERT_NOT_NULL(ctest, entry, "saved entry exists");
    CTEST_ASSERT_TRUE(ctest, entry->is_saved, "entry marked as saved");

    void (*original_handler)(int) = entry->original_action.sa_handler;

    struct sigaction sa2;
    sa2.sa_handler = SIG_IGN;
    sa2.sa_flags = 0;
    sigemptyset(&sa2.sa_mask);

    sig_act_store_set_and_save(store, SIGUSR2, &sa2);

    entry = sig_act_store_get(store, SIGUSR2);
    CTEST_ASSERT_NOT_NULL(ctest, entry, "saved entry still exists");
    CTEST_ASSERT_TRUE(ctest, entry->original_action.sa_handler == original_handler,
                      "original handler preserved");

    sig_act_store_destroy(&store);
}

CTEST(test_sig_act_posix_detect_sig_ign)
{
    sig_act_store_t *store = sig_act_store_create();

    struct sigaction ignore;
    ignore.sa_handler = SIG_IGN;
    ignore.sa_flags = 0;
    sigemptyset(&ignore.sa_mask);
    sigaction(SIGUSR2, &ignore, NULL);

    struct sigaction sa;
    sa.sa_handler = test_signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sig_act_store_set_and_save(store, SIGUSR2, &sa);

    CTEST_ASSERT_TRUE(ctest, sig_act_store_was_ignored(store, SIGUSR2), "detected SIG_IGN");

    sig_act_store_destroy(&store);
}

CTEST(test_sig_act_posix_restore_one)
{
    sig_act_store_t *store = sig_act_store_create();

    struct sigaction sa;
    sa.sa_handler = test_signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sig_act_store_set_and_save(store, SIGUSR2, &sa);

    bool result = sig_act_store_restore_one(store, SIGUSR2);
    CTEST_ASSERT_TRUE(ctest, result, "restore_one succeeded");

    sig_act_store_destroy(&store);
}

CTEST(test_sig_act_posix_restore_one_unsaved_fails)
{
    sig_act_store_t *store = sig_act_store_create();

    bool result = sig_act_store_restore_one(store, SIGUSR2);
    CTEST_ASSERT_FALSE(ctest, result, "restore_one failed for unsaved");

    sig_act_store_destroy(&store);
}

CTEST(test_sig_act_posix_restore_all)
{
    sig_act_store_t *store = sig_act_store_create();

    struct sigaction sa;
    sa.sa_handler = test_signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sig_act_store_set_and_save(store, SIGUSR1, &sa);
    sig_act_store_set_and_save(store, SIGUSR2, &sa);

    sig_act_store_restore(store);

    CTEST_ASSERT_TRUE(ctest, true, "restore_all did not crash");

    sig_act_store_destroy(&store);
}

CTEST(test_sig_act_posix_actual_signal_delivery)
{
    sig_act_store_t *store = sig_act_store_create();

    test_handler_called = 0;

    struct sigaction sa;
    sa.sa_handler = test_signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sig_act_store_set_and_save(store, SIGUSR2, &sa);

    raise(SIGUSR2);

    CTEST_ASSERT_EQ(ctest, test_handler_called, 1, "handler was called");

    sig_act_store_restore_one(store, SIGUSR2);

    sig_act_store_destroy(&store);
}

CTEST(test_sig_act_posix_reject_sigkill)
{
    sig_act_store_t *store = sig_act_store_create();

    struct sigaction sa;
    sa.sa_handler = test_signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    int result = sig_act_store_set_and_save(store, SIGKILL, &sa);
    CTEST_ASSERT_EQ(ctest, result, -1, "SIGKILL rejected");
    CTEST_ASSERT_FALSE(ctest, sig_act_store_is_saved(store, SIGKILL), "SIGKILL not saved");

    sig_act_store_destroy(&store);
}

CTEST(test_sig_act_posix_reject_sigstop)
{
    sig_act_store_t *store = sig_act_store_create();

    struct sigaction sa;
    sa.sa_handler = test_signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    int result = sig_act_store_set_and_save(store, SIGSTOP, &sa);
    CTEST_ASSERT_EQ(ctest, result, -1, "SIGSTOP rejected");
    CTEST_ASSERT_FALSE(ctest, sig_act_store_is_saved(store, SIGSTOP), "SIGSTOP not saved");

    sig_act_store_destroy(&store);
}

CTEST(test_sig_act_posix_null_action_fails)
{
    sig_act_store_t *store = sig_act_store_create();

    int result = sig_act_store_set_and_save(store, SIGUSR2, NULL);
    CTEST_ASSERT_EQ(ctest, result, -1, "null action rejected");

    sig_act_store_destroy(&store);
}

#else  // Non-POSIX (UCRT_API or ISO_C)

// ============================================================================
// UCRT/ISO_C-Specific Tests
// ============================================================================

CTEST(test_sig_act_nonposix_set_and_save_basic)
{
    sig_act_store_t *store = sig_act_store_create();

    void (*old_handler)(int) = sig_act_store_set_and_save(store, SIGINT, test_signal_handler);

    CTEST_ASSERT_TRUE(ctest, old_handler != SIG_ERR, "set_and_save succeeded");
    CTEST_ASSERT_TRUE(ctest, sig_act_store_is_saved(store, SIGINT), "signal marked as saved");

    sig_act_store_destroy(&store);
}

CTEST(test_sig_act_nonposix_set_and_save_preserves_original)
{
    sig_act_store_t *store = sig_act_store_create();

    void (*old1)(int) = sig_act_store_set_and_save(store, SIGINT, test_signal_handler);
    CTEST_ASSERT_TRUE(ctest, old1 != SIG_ERR, "first set_and_save succeeded");

    const sig_act_t *entry = sig_act_store_get(store, SIGINT);
    CTEST_ASSERT_NOT_NULL(ctest, entry, "saved entry exists");
    CTEST_ASSERT_TRUE(ctest, entry->is_saved, "entry marked as saved");

    void (*original_handler)(int) = entry->original_handler;

    void (*old2)(int) = sig_act_store_set_and_save(store, SIGINT, SIG_IGN);
    CTEST_ASSERT_TRUE(ctest, old2 != SIG_ERR, "second set_and_save succeeded");

    entry = sig_act_store_get(store, SIGINT);
    CTEST_ASSERT_NOT_NULL(ctest, entry, "saved entry still exists");
    CTEST_ASSERT_TRUE(ctest, entry->original_handler == original_handler,
                      "original handler preserved");

    sig_act_store_destroy(&store);
}

CTEST(test_sig_act_nonposix_detect_sig_ign)
{
    sig_act_store_t *store = sig_act_store_create();

    signal(SIGINT, SIG_IGN);

    void (*old)(int) = sig_act_store_set_and_save(store, SIGINT, test_signal_handler);
    CTEST_ASSERT_TRUE(ctest, old == SIG_IGN, "old handler was SIG_IGN");
    CTEST_ASSERT_TRUE(ctest, sig_act_store_was_ignored(store, SIGINT), "detected SIG_IGN");

    sig_act_store_destroy(&store);
}

CTEST(test_sig_act_nonposix_restore_one)
{
    sig_act_store_t *store = sig_act_store_create();

    sig_act_store_set_and_save(store, SIGINT, test_signal_handler);

    bool result = sig_act_store_restore_one(store, SIGINT);
    CTEST_ASSERT_TRUE(ctest, result, "restore_one succeeded");

    sig_act_store_destroy(&store);
}

CTEST(test_sig_act_nonposix_restore_one_unsaved_fails)
{
    sig_act_store_t *store = sig_act_store_create();

    bool result = sig_act_store_restore_one(store, SIGTERM);
    CTEST_ASSERT_FALSE(ctest, result, "restore_one failed for unsaved");

    sig_act_store_destroy(&store);
}

CTEST(test_sig_act_nonposix_restore_all)
{
    sig_act_store_t *store = sig_act_store_create();

    sig_act_store_set_and_save(store, SIGINT, test_signal_handler);
    sig_act_store_set_and_save(store, SIGTERM, test_signal_handler);

    sig_act_store_restore(store);

    CTEST_ASSERT_TRUE(ctest, true, "restore_all did not crash");

    sig_act_store_destroy(&store);
}

CTEST(test_sig_act_nonposix_null_store_returns_error)
{
    void (*result)(int) = sig_act_store_set_and_save(NULL, SIGINT, test_signal_handler);
    CTEST_ASSERT_TRUE(ctest, result == SIG_ERR, "null store returns SIG_ERR");
}

CTEST(test_sig_act_nonposix_invalid_signal_returns_error)
{
    sig_act_store_t *store = sig_act_store_create();

    void (*result)(int) = sig_act_store_set_and_save(store, -1, test_signal_handler);
    CTEST_ASSERT_TRUE(ctest, result == SIG_ERR, "negative signal returns SIG_ERR");

    result = sig_act_store_set_and_save(store, 9999, test_signal_handler);
    CTEST_ASSERT_TRUE(ctest, result == SIG_ERR, "huge signal returns SIG_ERR");

    sig_act_store_destroy(&store);
}

#endif  // POSIX_API vs non-POSIX

// ============================================================================
// Main - Conditionally build test suite
// ============================================================================

int main(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    CTestEntry *suite[] = {
        // Common tests (all platforms)
        CTEST_ENTRY(test_sig_act_create_and_destroy),
        CTEST_ENTRY(test_sig_act_destroy_null_is_safe),
        CTEST_ENTRY(test_sig_act_is_saved_initially_false),
        CTEST_ENTRY(test_sig_act_is_saved_with_null_store),
        CTEST_ENTRY(test_sig_act_is_saved_with_invalid_signal),
        CTEST_ENTRY(test_sig_act_get_returns_null_for_unsaved),

#ifdef POSIX_API
        // POSIX-specific tests
        CTEST_ENTRY(test_sig_act_posix_set_and_save_basic),
        CTEST_ENTRY(test_sig_act_posix_set_and_save_preserves_original),
        CTEST_ENTRY(test_sig_act_posix_detect_sig_ign),
        CTEST_ENTRY(test_sig_act_posix_restore_one),
        CTEST_ENTRY(test_sig_act_posix_restore_one_unsaved_fails),
        CTEST_ENTRY(test_sig_act_posix_restore_all),
        CTEST_ENTRY(test_sig_act_posix_actual_signal_delivery),
        CTEST_ENTRY(test_sig_act_posix_reject_sigkill),
        CTEST_ENTRY(test_sig_act_posix_reject_sigstop),
        CTEST_ENTRY(test_sig_act_posix_null_action_fails),
#else
        // Non-POSIX tests (UCRT/ISO_C)
        CTEST_ENTRY(test_sig_act_nonposix_set_and_save_basic),
        CTEST_ENTRY(test_sig_act_nonposix_set_and_save_preserves_original),
        CTEST_ENTRY(test_sig_act_nonposix_detect_sig_ign),
        CTEST_ENTRY(test_sig_act_nonposix_restore_one),
        CTEST_ENTRY(test_sig_act_nonposix_restore_one_unsaved_fails),
        CTEST_ENTRY(test_sig_act_nonposix_restore_all),
        CTEST_ENTRY(test_sig_act_nonposix_null_store_returns_error),
        CTEST_ENTRY(test_sig_act_nonposix_invalid_signal_returns_error),
#endif

        NULL  // Terminator
    };

    return ctest_run_suite(suite);
}

// ============================================================================
// sig_act.h
// Signal handler archiving for POSIX shell trap builtin
//
// This module saves and restores signal handlers. The key design principle:
// We ONLY save a handler when we SET a new one (not by querying the system).
// This works portably across POSIX (sigaction), UCRT, and ISO C (signal).
//
// Usage pattern:
// 1. Create a store when creating an executor
// 2. When trap builtin sets a handler, call sig_act_store_set_and_save()
// 3. When executor is destroyed, call sig_act_store_restore() to restore original handlers
// ============================================================================

#ifndef SIG_ACT_H
#define SIG_ACT_H

// #ifdef POSIX_API
#define _POSIX_C_SOURCE 202401L
// #endif

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>

/* Use sigaction only where it is actually available (non-Windows POSIX). */
#if defined(POSIX_API) && !defined(_WIN32) && !defined(__MINGW32__) && !defined(__MSYS__) &&          \
    !defined(_MSC_VER)
/* Most Unix-likes + real POSIX environments */
#define SIG_ACT_USE_SIGACTION
#endif

// Signal disposition tracking for restoration after traps
// Stores the ORIGINAL signal handler before shell modifies it

typedef struct sig_act_t
{
    int signal_number;
    bool is_saved;   // Whether we have a saved handler for this signal
    bool was_ignored; // Whether the original handler was SIG_IGN

#ifdef SIG_ACT_USE_SIGACTION
    struct sigaction original_action; // Original sigaction structure
#else
    void (*original_handler)(int); // Original signal handler (signal() style)
#endif
} sig_act_t;

typedef struct sig_act_store_t
{
    size_t capacity;    // Size of array (max signal number + 1)
    sig_act_t *actions; // Array indexed by signal number
} sig_act_store_t;

// Create a new signal disposition store
// All entries are initially marked as not-saved
sig_act_store_t *sig_act_store_create(void);

// Destroy a signal disposition store and free all resources
void sig_act_store_destroy(sig_act_store_t **store);

// Set a new signal handler AND save the previous one (if not already saved)
// This is the primary way handlers get saved in the store
// Returns: the previous handler (for chaining), or SIG_ERR on error
#ifdef SIG_ACT_USE_SIGACTION
int sig_act_store_set_and_save(sig_act_store_t *store, int signo,
                               const struct sigaction *new_action);
#else
void (*sig_act_store_set_and_save(sig_act_store_t *store, int signo,
                                  void (*new_handler)(int)))(int);
#endif

// Restore ALL saved signal dispositions to their original state
// Only restores signals that were previously saved via set_and_save()
void sig_act_store_restore(const sig_act_store_t *store);

// Restore a single signal to its original disposition (if saved)
// Returns: true if restored, false if not saved or error
bool sig_act_store_restore_one(const sig_act_store_t *store, int signo);

// Query functions

// Check if a signal handler has been saved
bool sig_act_store_is_saved(const sig_act_store_t *store, int signo);

// Check if the original handler was SIG_IGN (signal was ignored)
// Only valid if sig_act_store_is_saved() returns true
bool sig_act_store_was_ignored(const sig_act_store_t *store, int signo);

// Get the original disposition for a specific signal (read-only)
// Returns NULL if not saved
const sig_act_t *sig_act_store_get(const sig_act_store_t *store, int signo);

// Check whether a signal is supported by this store.
// Returns true if signo is valid for this platform or is the EXIT trap (signo == 0).
bool sig_act_store_is_supported(const sig_act_store_t *store, int signo);

#endif // SIG_ACT_H

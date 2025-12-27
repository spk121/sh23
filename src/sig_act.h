// ============================================================================
// sig_act.h
// ============================================================================

#ifndef SIG_ACT_H
#define SIG_ACT_H

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>

// Signal disposition tracking for restoration after traps
// Tracks original signal handlers before shell modifies them

typedef struct sig_act_t
{
    int signal_number;
#ifdef POSIX_API
    struct sigaction original_action; // Original sigaction structure
#else
    void (*original_handler)(int); // Original signal handler (signal() style)
#endif
    bool was_ignored; // Whether signal was originally ignored
} sig_act_t;

typedef struct sig_act_store_t
{
    sig_act_t *actions; // Array indexed by signal number
    size_t capacity;    // Size of array
} sig_act_store_t;

// Create a new signal disposition store
sig_act_store_t *sig_act_store_create(void);

// Destroy a signal disposition store and free all resources
void sig_act_store_destroy(sig_act_store_t *store);

// Create a copy of a signal disposition store
sig_act_store_t *sig_act_store_copy(const sig_act_store_t *store);

// Capture current signal dispositions from the system
sig_act_store_t *sig_act_store_capture(void);

// Restore signal dispositions to their original state
void sig_act_store_restore(const sig_act_store_t *store);

// Get the original disposition for a specific signal
const sig_act_t *sig_act_store_get(const sig_act_store_t *store, int signal_number);

// Check if a signal was originally ignored
bool sig_act_store_was_ignored(const sig_act_store_t *store, int signal_number);

#endif // SIG_ACT_H

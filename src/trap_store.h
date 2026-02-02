// ============================================================================
// trap_store.h
// ============================================================================

#ifndef TRAP_STORE_H
#define TRAP_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <signal.h>
#include "string_t.h"

typedef struct exec_frame_t exec_frame_t;
typedef struct sig_act_store_t sig_act_store_t;

/* Enable full POSIX signal set only where available (non-Windows POSIX builds). */
#if defined(POSIX_API) && !defined(_WIN32) && !defined(__MINGW32__) && !defined(__MSYS__)
#define TRAP_USE_POSIX_SIGNALS 1
#endif

// Signals KILL and STOP are not catchable or ignorable.
// POSIX <signal.h> requires ABRT, ALRM, BUS, CHLD, CONT, FPE, HUP,
//   ILL, INT, KILL, PIPE, QUIT, SEGV, STOP, TERM,
//   TSTP, TTIN, TTOU, USR1, USR2, WINCH
// UCRT defines ABRT, FPE, ILL, INT, SEGV, TERM.
// ISO C only defines ABRT, FPE, ILL, INT, SEGV, TERM.

typedef struct trap_action_t
{
    string_t *action;  // Command string to execute, or NULL for default action
    int signal_number; // Signal number (SIGINT, SIGTERM, etc.)
    bool is_ignored;   // true if trap is set to ignore (trap '' SIGNAL)
    bool is_default;   // true if trap is set to default (trap - SIGNAL)
    char padding[2];
} trap_action_t;

typedef struct trap_store_t
{
    trap_action_t *traps;  // Array of trap actions, indexed by signal number
#ifdef TRAP_USE_UCRT_SIGNALS
    trap_action_t fpe_trap; // Single FPE trap for all exception types (shell uses integer arithmetic)
#endif
    string_t *exit_action; // Action for EXIT trap
    size_t capacity;       // Size of array (typically NSIG or _NSIG)
    bool exit_trap_set;    // Special case: trap on EXIT (signal 0)
    char padding[7];
} trap_store_t;

// Create a new trap store
trap_store_t *trap_store_create(void);

// Destroy a trap store and free all resources
void trap_store_destroy(trap_store_t **store);

// Create a deep copy of a trap store
trap_store_t *trap_store_clone(const trap_store_t *store);

// Get or set the global trap store, which is the one
// that will be used by the signal handlers
trap_store_t *trap_store_get_current(void);
void trap_store_set_current(trap_store_t *store);

// Get or set the global signal action store, which is used to save/restore
// original signal dispositions when traps are installed
void trap_store_set_sig_act_store(sig_act_store_t *store);
sig_act_store_t *trap_store_get_sig_act_store(void);

// Set a trap action for a signal
// action: command string to execute (NULL for default)
// is_ignored: true for trap '' SIGNAL
// is_default: true for trap - SIGNAL
// Returns: true on success, false if OS call failed (e.g., signal() or sigaction() failure)
bool trap_store_set(trap_store_t *store, int signal_number,
                    string_t *action, bool is_ignored, bool is_default);

// Set trap for EXIT (signal 0)
// Returns: true on success (EXIT traps don't install OS handlers, so always true unless precondition fails)
bool trap_store_set_exit(trap_store_t *store, string_t *action,
                         bool is_ignored, bool is_default);

// Get trap action for a signal (returns NULL if no trap set)
const trap_action_t *trap_store_get(const trap_store_t *store, int signal_number);

// Get FPE trap action for UCRT (returns NULL if no FPE trap set)
// Note: Shell arithmetic is performed on 'long' integers, not floating-point,
// so FPE exceptions should be rare or impossible. This function returns a single
// trap action for all FPE exception types (FPE_INTDIV, FPE_FLTDIV, etc.)
// The fpe_code parameter is accepted but currently ignored for future extensibility.
#ifdef TRAP_USE_UCRT_SIGNALS
const trap_action_t *trap_store_get_fpe(const trap_store_t *store, int fpe_code);
#endif

// Get EXIT trap action (returns NULL if no EXIT trap set)
const string_t *trap_store_get_exit(const trap_store_t *store);

// Check if a trap is set for a signal
bool trap_store_is_set(const trap_store_t *store, int signal_number);

// Check if EXIT trap is set
bool trap_store_is_exit_set(const trap_store_t *store);

// Clear a trap (reset to default)
void trap_store_clear(trap_store_t *store, int signal_number);

// Clear EXIT trap
void trap_store_clear_exit(trap_store_t *store);

// Convert signal name (without SIG prefix) to signal number
// Returns -1 if signal name is not recognized
// Examples: "INT" -> SIGINT, "TERM" -> SIGTERM
int trap_signal_name_to_number(const char *name);

// Check if a signal name is completely invalid (not recognized at all)
// Returns true if the name is not a known signal name on any platform
// Examples: "BADNAME" -> true, "INT" -> false, "ALRM" -> false (even if unsupported)
// Precondition: name must not be NULL
bool trap_signal_name_is_invalid(const char *name);

// Check if a signal name is valid but unsupported on current platform
// Returns true if the name is a known signal but not available on this platform
// Examples: "ALRM" -> true on UCRT, "INT" -> false, "BADNAME" -> false
// Precondition: name must not be NULL
bool trap_signal_name_is_unsupported(const char *name);

// Convert signal number to signal name (reverse of trap_signal_name_to_number)
// Returns pointer to static string containing signal name (without SIG prefix)
// Returns "INVALID" if signal number is out of valid range
// Returns "UNSUPPORTED" if signal number is recognized but not available on this platform
// Examples: SIGINT -> "INT", SIGALRM -> "ALRM" (or "UNSUPPORTED" on UCRT), -1 -> "INVALID"
// Note: Returned pointer points to static string storage, do not free
// Precondition: signo must be >= 0 (0 for EXIT, positive for signals)
const char *trap_signal_number_to_name(int signo);

// For each trap in the store, reset non-ignored traps to default action
void trap_store_reset_non_ignored(trap_store_t *store);

// Iterate over all set traps and call callback for each
// The callback is responsible for formatting and displaying trap information
// Callback parameters:
//   signal_number: signal number (use trap_number_to_name for display)
//   trap: const trap_action_t* (action string, is_ignored, is_default flags)
//   context: opaque pointer for callback state (e.g., FILE* for output)
// Signal 0 (EXIT trap) is reported if set with signal_number=0
void trap_store_for_each_set_trap(const trap_store_t *store,
                                   void (*callback)(int signal_number,
                                                    const trap_action_t *trap,
                                                    void *context),
                                   void *context);

void trap_store_run_exit_trap(const trap_store_t *store, exec_frame_t *frame);

#endif // TRAP_STORE_H

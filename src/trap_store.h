// ============================================================================
// trap_store.h
// ============================================================================

#ifndef TRAP_STORE_H
#define TRAP_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <signal.h>
#include "string.h"  // Assuming you have a string_t type

// Signals KILL and STOP are not catchable or ignorable.
// POSIX <signal.h> requires ABRT, ALRM, BUS, CHLD, CONT, FPE, HUP,
//   ILL, INT, KILL, PIPE, QUIT, SEGV, STOP, TERM,
//   TSTP, TTIN, TTOU, USR1, USR2, WINCH
// UCRT defines ABRT, FPE, ILL, INT, SEGV, TERM.
// ISO C only defines ABRT, FPE, ILL, INT, SEGV, TERM.

typedef struct trap_action_t
{
    int signal_number; // Signal number (SIGINT, SIGTERM, etc.)
    string_t *action;  // Command string to execute, or NULL for default action
    bool is_ignored;   // true if trap is set to ignore (trap '' SIGNAL)
    bool is_default;   // true if trap is set to default (trap - SIGNAL)
} trap_action_t;

typedef struct trap_store_t
{
    trap_action_t *traps;  // Array of trap actions, indexed by signal number
    size_t capacity;       // Size of array (typically NSIG or _NSIG)
    bool exit_trap_set;    // Special case: trap on EXIT (signal 0)
    string_t *exit_action; // Action for EXIT trap
} trap_store_t;

// Create a new trap store
trap_store_t *trap_store_create(void);

// Destroy a trap store and free all resources
void trap_store_destroy(trap_store_t *store);

// Create a deep copy of a trap store
trap_store_t *trap_store_copy(const trap_store_t *store);

// Set a trap action for a signal
// action: command string to execute (NULL for default)
// is_ignored: true for trap '' SIGNAL
// is_default: true for trap - SIGNAL
void trap_store_set(trap_store_t *store, int signal_number, 
                    string_t *action, bool is_ignored, bool is_default);

// Set trap for EXIT (signal 0)
void trap_store_set_exit(trap_store_t *store, string_t *action, 
                         bool is_ignored, bool is_default);

// Get trap action for a signal (returns NULL if no trap set)
const trap_action_t *trap_store_get(const trap_store_t *store, int signal_number);

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

#endif // TRAP_STORE_H

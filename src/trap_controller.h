#ifndef TRAP_CONTROLLER_H
#define TRAP_CONTROLLER_H

// trap_controller - Coordinates trap_store and sig_act modules
//
// This module provides a unified interface for managing signal traps and their
// original signal dispositions. It ensures atomicity and consistency between:
// - trap_store: User-defined trap actions and handlers
// - sig_act: Original signal dispositions (saved at startup)
//
// The controller prevents race conditions, ensures cleanup, and provides
// simplified API for shell builtins that need coordinated access.

#include "trap_store.h"
#include "sig_act.h"

// Forward declarations
typedef struct trap_controller_t trap_controller_t;

typedef struct {
    const trap_action_t *user_trap;           // User-defined trap (or NULL if not set)
    const sig_act_t *original_handler;        // Original signal disposition (always present)
} trap_controller_info_t;

// ============================================================================
// Lifecycle Management
// ============================================================================

// Create a trap controller managing both trap_store and sig_act_store
// Both stores must be created and initialized before passing to controller
// The controller does NOT take ownership; caller responsible for cleanup order:
//   1. trap_controller_destroy()
//   2. trap_store_destroy()
//   3. sig_act_store_destroy()
// Precondition: trap_store must not be NULL
// Precondition: sig_act_store must not be NULL
trap_controller_t *trap_controller_create(trap_store_t *trap_store,
                                          sig_act_store_t *sig_act_store);

// Destroy controller and restore all signal handlers to originals
// Does not destroy underlying stores (caller's responsibility)
// Safe to call multiple times (second call is no-op)
// Precondition: controller_ptr must not be NULL
void trap_controller_destroy(trap_controller_t **controller_ptr);

// ============================================================================
// Trap Operations (Coordinated Signal Handling)
// ============================================================================

// Set a signal trap with coordinated handler installation
// - Saves original handler via sig_act
// - Installs trap_handler via signal() or sigaction()
// - Stores user action string in trap_store
// Rejects SIGKILL, SIGSTOP (non-catchable)
// Returns true on success, false if OS call fails
// Precondition: controller must not be NULL
// Precondition: signal_number must be >= 0 and < capacity
// Precondition: action_str must not be NULL (use "" to ignore)
// Note: Currently a stub pending string_t integration with action string conversion
bool trap_controller_set_trap(trap_controller_t *controller,
                              int signal_number,
                              const char *action_str,
                              bool is_ignored);

// Clear a trap and restore original handler
// - Removes user action from trap_store
// - Restores original handler via sig_act
// Precondition: controller must not be NULL
// Precondition: signal_number must be >= 0 and < capacity
// Returns true on success, false on failure
bool trap_controller_clear_trap(trap_controller_t *controller,
                                int signal_number);

// Set EXIT trap (signal 0)
// EXIT traps have no associated signal handler (no OS coordination needed)
// Precondition: controller must not be NULL
// Precondition: action_str must not be NULL
// Returns true on success
// Note: Currently a stub pending string_t integration with action string conversion
bool trap_controller_set_exit_trap(trap_controller_t *controller,
                                   const char *action_str);

// Clear EXIT trap
// Precondition: controller must not be NULL
// Returns true on success
bool trap_controller_clear_exit_trap(trap_controller_t *controller);

// ============================================================================
// Bulk Operations
// ============================================================================

// Reset all traps to their original state
// - Clears all user traps from trap_store
// - Restores all original handlers via sig_act
// - Clears EXIT trap
// Used when shell exits or user runs: trap - [signals...]
// Precondition: controller must not be NULL
void trap_controller_reset_all(trap_controller_t *controller);

// Reset all EXCEPT ignored traps
// Useful for: trap -p (print traps)
// Precondition: controller must not be NULL
void trap_controller_reset_non_ignored(trap_controller_t *controller);

// ============================================================================
// Query Operations
// ============================================================================

// Get combined info about a signal's trap and original handler
// Returns struct with both user_trap and original_handler pointers
// Safe to call; never returns NULL pointers (provides default if not set)
// Precondition: controller must not be NULL
// Precondition: signal_number must be >= 0 and < capacity
trap_controller_info_t trap_controller_get_info(trap_controller_t *controller,
                                                 int signal_number);

// Iterate over all traps with both user and original handler info
// Callback receives signal_number, combined info, and context
// Iterates all signal traps, EXIT trap, and UCRT FPE trap (if set)
// Precondition: controller must not be NULL
// Precondition: callback must not be NULL
void trap_controller_for_each(trap_controller_t *controller,
                              void (*callback)(int signal_number,
                                              trap_controller_info_t info,
                                              void *context),
                              void *context);

// ============================================================================
// Execution
// ============================================================================

// Execute trap action for a signal in signal-safe manner
// - Blocks signals during execution
// - Parses and executes action string via exec_frame
// - Restores signal mask
// Precondition: controller must not be NULL
// Precondition: frame must not be NULL
// Precondition: signal_number must have trap set
// Returns exit status of executed command
int trap_controller_execute_trap(trap_controller_t *controller,
                                 int signal_number,
                                 exec_frame_t *frame);

// Execute EXIT trap
// Called on shell termination to run cleanup commands
// Precondition: controller must not be NULL
// Precondition: frame must not be NULL
// Precondition: EXIT trap must be set
// Returns exit status of executed command
int trap_controller_execute_exit_trap(trap_controller_t *controller,
                                      exec_frame_t *frame);

// ============================================================================
// Validation & Debugging
// ============================================================================

// Check if a signal number is valid and catchable
// Returns false for: negative numbers, SIGKILL, SIGSTOP, out-of-range
// Precondition: signal_number must be >= 0 (can be any value, returns false for invalid)
bool trap_controller_is_valid_signal(int signal_number);

// Validate internal state consistency (debugging)
// Checks that trap_store and sig_act_store are in sync
// Returns true if all traps have corresponding original handlers
// Returns false if inconsistencies detected
// Precondition: controller must not be NULL
bool trap_controller_validate_state(trap_controller_t *controller);

#endif // TRAP_CONTROLLER_H

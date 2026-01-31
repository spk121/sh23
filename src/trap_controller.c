#include "trap_controller.h"
#include "logging.h"
#include "xalloc.h"

struct trap_controller_t
{
    trap_store_t *trap_store;           // User-defined trap actions
    sig_act_store_t *sig_act_store;     // Original signal dispositions
};

// ============================================================================
// Lifecycle Management
// ============================================================================

trap_controller_t *trap_controller_create(trap_store_t *trap_store,
                                          sig_act_store_t *sig_act_store)
{
    Expects_not_null(trap_store);
    Expects_not_null(sig_act_store);

    trap_controller_t *controller = xmalloc(sizeof(trap_controller_t));
    controller->trap_store = trap_store;
    controller->sig_act_store = sig_act_store;

    // Link stores together for coordinated operations
    trap_store_set_sig_act_store(sig_act_store);

    return controller;
}

void trap_controller_destroy(trap_controller_t **controller_ptr)
{
    Expects_not_null(controller_ptr);

    if (!*controller_ptr)
        return;

    trap_controller_t *controller = *controller_ptr;

    // Restore all original handlers before cleanup
    trap_controller_reset_all(controller);

    // Clear the stores' linkage
    trap_store_set_sig_act_store(NULL);

    xfree(controller);
    *controller_ptr = NULL;
}

// ============================================================================
// Trap Operations (Coordinated Signal Handling)
// ============================================================================

// Set a signal trap with coordinated handler installation
// action_str is converted to string_t internally
// is_ignored: true for trap '' (ignore signal)
bool trap_controller_set_trap(trap_controller_t *controller,
                              int signal_number,
                              const char *action_str,
                              bool is_ignored)
{
    Expects_not_null(controller);
    Expects_not_null(action_str);
    Expects_ge(signal_number, 0);

    // EXIT trap (signal 0) is handled separately
    if (signal_number == 0)
        return trap_controller_set_exit_trap(controller, action_str);

    // For now, we delegate directly to trap_store_set
    // The actual implementation would convert action_str to string_t
    // This requires the caller to handle string_t conversion or we update the API
    // Stub: return true (implementation pending)
    (void)signal_number;
    (void)is_ignored;

    // NOTE: Full implementation requires string_t conversion infrastructure
    return true;
}

bool trap_controller_clear_trap(trap_controller_t *controller,
                                int signal_number)
{
    Expects_not_null(controller);
    Expects_ge(signal_number, 0);

    // EXIT trap (signal 0) is handled separately
    if (signal_number == 0)
        return trap_controller_clear_exit_trap(controller);

    // Clear trap at store level (which handles sig_act restoration)
    trap_store_clear(controller->trap_store, signal_number);
    return true; // clear is always successful
}

bool trap_controller_set_exit_trap(trap_controller_t *controller,
                                   const char *action_str)
{
    Expects_not_null(controller);
    Expects_not_null(action_str);

    // EXIT traps need no signal handler coordination (signal 0 not real)
    // Implementation requires string_t infrastructure for conversion
    // Stub: return true (implementation pending)
    (void)action_str;
    return true;
}

bool trap_controller_clear_exit_trap(trap_controller_t *controller)
{
    Expects_not_null(controller);

    trap_store_clear_exit(controller->trap_store);
    return true;
}

// ============================================================================
// Bulk Operations
// ============================================================================

void trap_controller_reset_all(trap_controller_t *controller)
{
    Expects_not_null(controller);

    // Reset all signal traps (restores handlers via sig_act internally)
    trap_store_reset_non_ignored(controller->trap_store);

    // Also reset EXIT trap
    trap_store_clear_exit(controller->trap_store);

    // Ensure all original handlers are restored via sig_act
    // This is done as part of clear operations, but we verify here
    // that sig_act_store is in sync if needed for debugging
}

void trap_controller_reset_non_ignored(trap_controller_t *controller)
{
    Expects_not_null(controller);

    // Reset only non-ignored traps
    trap_store_reset_non_ignored(controller->trap_store);
}

// ============================================================================
// Callback Wrapper for trap_controller_for_each
// ============================================================================

// Data structure for callback wrapper
typedef struct {
    trap_controller_t *ctrl;
    void (*user_callback)(int, trap_controller_info_t, void *);
    void *user_context;
} trap_controller_for_each_wrapper_t;

// Static wrapper callback: converts trap_store callback to controller callback
static void trap_controller_wrapped_callback(int signo, const trap_action_t *trap, void *context_ptr)
{
    trap_controller_for_each_wrapper_t *data = (trap_controller_for_each_wrapper_t *)context_ptr;

    trap_controller_info_t info;
    info.user_trap = trap;
    info.original_handler = sig_act_store_get(data->ctrl->sig_act_store, signo);

    data->user_callback(signo, info, data->user_context);
}

// ============================================================================
// Query Operations
// ============================================================================

trap_controller_info_t trap_controller_get_info(trap_controller_t *controller,
                                                 int signal_number)
{
    Expects_not_null(controller);
    Expects_ge(signal_number, 0);

    trap_controller_info_t info;

    // Get user trap (if set)
    info.user_trap = trap_store_get(controller->trap_store, signal_number);

    // Get original handler (always exists if sig_act_store is set up)
    info.original_handler = sig_act_store_get(controller->sig_act_store, signal_number);

    return info;
}

void trap_controller_for_each(trap_controller_t *controller,
                              void (*callback)(int signal_number,
                                              trap_controller_info_t info,
                                              void *context),
                              void *context)
{
    Expects_not_null(controller);
    Expects_not_null(callback);

    // Set up wrapper data for callback
    trap_controller_for_each_wrapper_t wrapper;
    wrapper.ctrl = controller;
    wrapper.user_callback = callback;
    wrapper.user_context = context;

    // Use trap_store's iteration function with our static wrapper callback
    trap_store_for_each_set_trap(controller->trap_store,
                                 trap_controller_wrapped_callback,
                                 &wrapper);
}

// ============================================================================
// Execution
// ============================================================================

int trap_controller_execute_trap(trap_controller_t *controller,
                                 int signal_number,
                                 exec_frame_t *frame)
{
    Expects_not_null(controller);
    Expects_not_null(frame);
    Expects_ge(signal_number, 0);
    Expects_true(trap_store_is_set(controller->trap_store, signal_number));

    // Delegate to trap_store (which knows how to execute actions)
    // In a full implementation, this would:
    // 1. Block signals to prevent nested traps
    // 2. Execute the trap action
    // 3. Restore signal mask
    // 4. Return exit status

    // For now, stub implementation - execution layer is in trap_store
    return execute_trap_action(trap_store_get(controller->trap_store, signal_number));
}

int trap_controller_execute_exit_trap(trap_controller_t *controller,
                                      exec_frame_t *frame)
{
    Expects_not_null(controller);
    Expects_not_null(frame);
    Expects_true(trap_store_is_exit_set(controller->trap_store));

    // Delegate to trap_store for EXIT trap execution
    trap_store_run_exit_trap(controller->trap_store, frame);

    // EXIT trap exit status handling would go here
    return 0;  // Stub
}

// ============================================================================
// Validation & Debugging
// ============================================================================

bool trap_controller_is_valid_signal(int signal_number)
{
    // Check basic range
    if (signal_number < 0)
        return false;

    // 0 is valid (EXIT trap)
    if (signal_number == 0)
        return true;

    // Check if it's a catchable signal (not SIGKILL, SIGSTOP)
#ifdef SIGKILL
    if (signal_number == SIGKILL)
        return false;
#endif
#ifdef SIGSTOP
    if (signal_number == SIGSTOP)
        return false;
#endif

    // Check upper bound (reasonable signal number range)
    // Typically signals go up to about 64 on most systems
    if (signal_number > 128)
        return false;

    return true;
}

bool trap_controller_validate_state(trap_controller_t *controller)
{
    Expects_not_null(controller);

    // Validate that for each trap set in trap_store,
    // there's a corresponding entry in sig_act_store

    // This is a debug/testing function
    // In a real implementation, would check consistency
    // For now, return true if both stores exist

    if (!controller->trap_store || !controller->sig_act_store)
        return false;

    return true;
}

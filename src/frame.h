#ifndef FRAME_H
#define FRAME_H

/**
 * @file frame.h
 * @brief Public API for frame-level operations in the POSIX shell executor.
 *
 * This header defines the public interface for operating on execution frames.
 * It covers:
 *
 *   - Error handling
 *   - Variables and IFS
 *   - Positional parameters
 *   - Named options
 *   - Shell functions
 *   - Exit status and control flow
 *   - Word / string expansion
 *   - Traps
 *   - Aliases
 *   - Frame-level command execution
 *
 * Top-level executor concerns (lifecycle, job control, builtin registration,
 * execution entry points) are defined in exec.h.
 *
 * @par Naming Convention
 * Functions that accept or return strings are provided in two variants:
 *   - The unsuffixed form uses string_t / strlist_t.
 *   - The @c _cstr suffixed form uses plain C strings (const char *).
 */

#include <stdio.h>

#include "miga/api.h"
#include "miga/migaconf.h"
#include "miga/type_pub.h"
#include "miga/strlist.h"
#include "miga/string_t.h"

MIGA_EXTERN_C_START

/* ============================================================================
 * Error Handling
 * ============================================================================ */

/**
 * Checks if the frame currently has an error message set.
 * Returns true if there is an error message, false otherwise.
 */
MIGA_API bool frame_has_error(const miga_frame_t *frame);

/**
 * Gets the current error message for the frame.
 * Caller should not modify or free the returned string.
 * If there is no error message, this will return an empty string (not NULL).
 */
MIGA_API const string_t *frame_get_error_message(const miga_frame_t *frame);

/**
 * Clears the current error message for the frame, if any.
 * After this call, there will be no error message associated with the frame.
 */
MIGA_API void frame_clear_error(miga_frame_t *frame);

/**
 * Sets the error message for the frame to the given string.
 * The provided string will be copied, so the caller retains ownership of the
 * original string.
 */
MIGA_API void frame_set_error(miga_frame_t *frame, const string_t *error);
MIGA_API void frame_set_error_printf(miga_frame_t *frame, const char *format, ...);

/* ============================================================================
 * Variables
 * ============================================================================ */

/**
 * Returns true if a variable with the given name exists in the variable store
 * associated with the current frame, false otherwise.
 */
MIGA_API bool frame_has_variable(const miga_frame_t *frame, const string_t *name);
MIGA_API bool frame_has_variable_cstr(const miga_frame_t *frame, const char *name);

/**
 * Returns the value of a variable with the given name from the variable store
 * associated with the current frame as a newly allocated string.  If the
 * variable exists but has no value, this will return an empty string (not
 * NULL).  If the variable does not exist, this will return an empty string
 * (not NULL).  Caller frees the returned string.
 */
MIGA_API string_t *frame_get_variable_value(miga_frame_t *frame, const string_t *name);
MIGA_API string_t *frame_get_variable_cstr(miga_frame_t *frame, const char *name);

/**
 * Checks if a variable with the given name is marked as exported or read-only
 * in the variable store associated with the current frame.  Returns true if
 * the variable is exported/read-only, false if it is not exported/read-only
 * or does not exist.
 */
MIGA_API bool frame_variable_is_exported(miga_frame_t *frame, const string_t *name);
MIGA_API bool frame_variable_is_exported_cstr(miga_frame_t *frame, const char *name);
MIGA_API bool frame_variable_is_readonly(miga_frame_t *frame, const string_t *name);
MIGA_API bool frame_variable_is_readonly_cstr(miga_frame_t *frame, const char *name);

/**
 * Returns the value of PS1 or PS2 for the current frame.
 * If the variable is not set, it returns a default value (e.g. "$ " for PS1
 * and "> " for PS2).
 * Caller frees the returned string.
 */
MIGA_API string_t *frame_get_ps1(miga_frame_t *frame);
MIGA_API string_t *frame_get_ps2(miga_frame_t *frame);

/**
 * Sets a variable in the variable store associated with current frame.
 * If the variable does not already exist, it will be created with the given
 * value and will not be marked as exported or read-only.  If the variable
 * already exists and is not read-only, its value will be updated but its
 * exported status will not be changed.  If the variable already exists and
 * is read-only and the new value differs from the previous value, this will
 * return an error code and not update the variable.  Returns
 * MIGA_VAR_STATUS_OK on success, or an error code on failure.
 */
MIGA_API miga_var_status_t frame_set_variable(miga_frame_t *frame, const string_t *name,
                                     const string_t *value);
MIGA_API miga_var_status_t frame_set_variable_cstr(miga_frame_t *frame,
                                                   const char *name, const char *value);

/**
 * Sets a non-temporary variable in the variable store associated with the
 * current frame.  Even if the frame normally acts on a temporary variable
 * store to execute a simple command, this will add the variable as
 * non-temporary.
 */
MIGA_API miga_var_status_t frame_set_persistent_variable(miga_frame_t *frame, const string_t *name,
                                                const string_t *value);
MIGA_API miga_var_status_t frame_set_persistent_variable_cstr(miga_frame_t *frame, const char *name,
                                                     const char *value);

/**
 * Updates the export status of an existing variable in the variable store
 * associated with the current frame.  If the variable does not exist, this
 * will return an error code.  If a variable exists and was not previously
 * exported, but is now being marked as exported, this will also set the
 * variable in the environment if supported by the OS.  If a variable exists
 * and was previously exported, but is now being marked as not exported, this
 * will also unset the variable in the environment if supported by the OS.
 * Read-only variables can still have their export status changed.  Returns
 * MIGA_VAR_STATUS_OK on success, or an error code on failure.
 */
MIGA_API miga_var_status_t frame_set_variable_exported(miga_frame_t *frame, const string_t *name,
                                              bool exported);

/**
 * Exports a variable to the environment.  This is a convenience function
 * that:
 * - Sets or creates the variable with the given value (if value is not NULL)
 * - Marks the variable as exported
 * - Exports it to the system environment (if supported)
 *
 * If value is NULL, only marks an existing variable as exported without
 * changing its value.  If the variable doesn't exist and value is NULL,
 * marks the variable as exported with empty value.
 *
 * Returns MIGA_EXPORT_STATUS_SUCCESS on success, or an error code on failure.
 */
MIGA_API miga_export_status_t frame_export_variable(miga_frame_t *frame, const string_t *name,
                                            const string_t *value);

/**
 * Updates the read-only status of an existing variable in the variable store
 * associated with the current frame.  If the variable does not exist, this
 * will return an error code.  Returns MIGA_VAR_STATUS_OK on success, or
 * an error code on failure.
 */
MIGA_API miga_var_status_t frame_set_variable_readonly(miga_frame_t *frame, const string_t *name,
                                              bool readonly);

/**
 * Removes a variable from the variable store associated with the current
 * frame.  Also, when supported by the OS, this will unset the variable in
 * the environment if it was exported.  If the variable does not exist, this
 * will return an error code.  If the variable exists but is read-only, this
 * will return an error code and not unset the variable.  Returns
 * MIGA_VAR_STATUS_OK on success, or an error code on failure.
 */
MIGA_API miga_var_status_t frame_unset_variable(miga_frame_t *frame, const string_t *name);
MIGA_API miga_var_status_t frame_unset_variable_cstr(miga_frame_t *frame, const char *name);

/**
 * Prints all variables in the variable store associated with the current
 * frame that are marked as exported, in a format suitable for re-input
 * (e.g. "export VAR=value").
 *
 * @param frame   The execution frame.
 * @param output  Output stream to write to (e.g. ctx->stdout_fp).
 */
MIGA_API void frame_print_exported_variables_in_export_format(miga_frame_t *frame, FILE *output);

/**
 * Prints all variables in the variable store associated with the current
 * frame that are marked as read-only, in a format suitable for re-input
 * (e.g. "readonly VAR=value").
 *
 * @param frame   The execution frame.
 * @param output  Output stream to write to (e.g. ctx->stdout_fp).
 */
MIGA_API void frame_print_readonly_variables(miga_frame_t *frame, FILE *output);

/**
 * Prints all variables in the variable store associated with the current
 * frame.  This includes all variables, not just exported ones.
 * If reusable_format is true, the output will be in a format that can be
 * reused as input to the shell (e.g. by using "eval" or sourcing a file).
 * If false, the output may be more human-readable but not necessarily
 * reusable as input.
 *
 * @param frame            The execution frame.
 * @param reusable_format  Whether to use shell-reusable format.
 * @param output           Output stream to write to (e.g. ctx->stdout_fp).
 */
MIGA_API void frame_print_variables(miga_frame_t *frame, bool reusable_format, FILE *output);

/* ── IFS convenience ─────────────────────────────────────────────────────── */

/**
 * Returns the current value of IFS for the given frame.
 * If IFS is not set, returns the POSIX default: space, tab, newline (" \t\n").
 * If IFS is set to the empty string, returns an empty string.
 * Caller frees the returned string.
 *
 * @param frame  The execution frame.
 * @return A newly allocated string containing the IFS value.
 */
MIGA_API string_t *frame_get_ifs(miga_frame_t *frame);
MIGA_API char *frame_get_ifs_cstr(miga_frame_t *frame);

/* ── Working directory ───────────────────────────────────────────────────── */

/**
 * Change the current working directory.
 *
 * This is the intended implementation helper for the cd builtin.  It performs
 * all of the following atomically:
 *   - Calls chdir() (or the platform equivalent) to change the process
 *     working directory.
 *   - Sets the OLDPWD variable to the previous value of PWD.
 *   - Sets the PWD variable to the new directory (canonicalised).
 *
 * If the chdir() call fails, no variables are modified and the function
 * returns false.
 *
 * @note This is distinct from exec_set_working_directory(), which only sets
 *       the initial working directory before execution begins and does not
 *       call chdir() or modify PWD/OLDPWD.
 *
 * @param frame  The execution frame.
 * @param path   The target directory path.
 * @return true on success, false on failure (e.g. directory does not exist,
 *         permission denied).
 */
MIGA_API bool frame_change_directory(miga_frame_t *frame, const string_t *path);
MIGA_API bool frame_change_directory_cstr(miga_frame_t *frame, const char *path);

/* ============================================================================
 * Word and String Expansion
 * ============================================================================ */

/**
 * Expands the given string using the variable store and other context of the
 * current frame.  This includes variable expansion, command substitution,
 * arithmetic expansion, etc.  The exact expansions performed will depend on
 * the flags provided.  Returns a newly allocated string with the expanded
 * result.  Caller is responsible for freeing the returned string.
 */
MIGA_API string_t *frame_expand_string(miga_frame_t *frame, const string_t *text,
                              miga_expand_flags_t flags);

/* ============================================================================
 * Positional Parameters
 * ============================================================================ */

/**
 * Returns true if the given frame has positional parameters defined
 * (e.g. $0, $1).
 */
MIGA_API bool frame_has_positional_params(const miga_frame_t *frame);

/**
 * Returns the number of positional parameters (e.g. $1, $2, ...)
 * associated with the given frame.  Does not count $0.
 */
MIGA_API int frame_count_positional_params(const miga_frame_t *frame);

/**
 * Shifts the positional parameters in the given frame by the specified number
 * of positions.  For example, if shift_count is 1, then $2 becomes $1,
 * $3 becomes $2, etc.  If shift_count is greater than or equal to the number
 * of positional parameters, all positional parameters will be removed.
 */
MIGA_API void frame_shift_positional_params(miga_frame_t *frame, int shift_count);

/**
 * Replaces the current positional parameters in the given frame with the new
 * list of positional parameters.  The new_params list will be copied, so the
 * caller retains ownership of the original list and its strings.
 */
MIGA_API void frame_replace_positional_params(miga_frame_t *frame, const strlist_t *new_params);

/**
 * Sets the value of $0 for the given frame.  The new_arg0 string will be
 * copied, so the caller retains ownership of the original string.
 */
MIGA_API void frame_set_arg0(miga_frame_t *frame, const string_t *new_arg0);
MIGA_API void frame_set_arg0_cstr(miga_frame_t *frame, const char *new_arg0);

/**
 * Returns the value of the positional parameter at the given index for the
 * specified frame.  For example, index 0 corresponds to $0, index 1
 * corresponds to $1, etc.  If the index is greater than or equal to the
 * number of positional parameters, this will return an empty string (not
 * NULL).  Caller frees the returned string.
 */
MIGA_API string_t *frame_get_positional_param(const miga_frame_t *frame, int index);

/**
 * Returns a list of all positional parameters for the given frame.
 * The returned list and its strings will be newly allocated, so the caller is responsible for
 * freeing them.
 */
MIGA_API strlist_t *frame_get_all_positional_params(const miga_frame_t *frame);

/* ============================================================================
 * Named Options
 * ============================================================================ */

/**
 * Returns true if the given frame has an option with the specified name,
 * false otherwise.  Note that it is possible for a frame to have an option
 * with the given name that is currently set to false, in which case this
 * function will still return true.  This function only checks for the
 * existence of the option, not its value.
 */
MIGA_API bool frame_has_named_option(const miga_frame_t *frame, const string_t *option_name);
MIGA_API bool frame_has_named_option_cstr(const miga_frame_t *frame, const char *option_name);

/**
 * Returns the value of the named option with the specified name for the given
 * frame.  If the option exists, this may return true or false depending on
 * the current value of the option.  If the option does not exist, this will
 * return false.
 */
MIGA_API bool frame_get_named_option(const miga_frame_t *frame, const string_t *option_name);
MIGA_API bool frame_get_named_option_cstr(const miga_frame_t *frame, const char *option_name);

/**
 * Sets the value of the named option with the specified name for the given
 * frame.  If the option does not already exist, it will be created with the
 * given value.  If the option already exists, its value will be updated to
 * the new value.  If plus_prefix is true, then a plus sign (+) will be used
 * in front of the option name when printing it in error messages or when
 * printing options, to indicate that this option is set using a plus sign
 * (e.g. "set +o option_name").
 */
MIGA_API bool frame_set_named_option(miga_frame_t *frame, const string_t *option_name, bool value,
                            bool plus_prefix);
MIGA_API bool frame_set_named_option_cstr(miga_frame_t *frame, const char *option_name, bool value,
                                 bool plus_prefix);

/* ============================================================================
 * Shell Functions
 * ============================================================================ */

MIGA_API bool frame_has_function(const miga_frame_t *frame, const string_t *name);

MIGA_API miga_func_status_t frame_get_function(miga_frame_t *frame, const string_t *name,
                                      string_t **out_body);
MIGA_API miga_func_status_t frame_get_function_cstr(miga_frame_t *frame, const char *name, char **out_body);

MIGA_API miga_func_status_t frame_set_function(miga_frame_t *frame, const string_t *name,
                                      const string_t *value);
MIGA_API miga_func_status_t frame_set_function_cstr(miga_frame_t *frame, const char *name,
                                           const char *value);

MIGA_API miga_func_status_t frame_unset_function(miga_frame_t *frame, const string_t *name);
MIGA_API miga_func_status_t frame_unset_function_cstr(miga_frame_t *frame, const char *name);

/**
 * Call a shell function by name with the given arguments.
 *
 * Creates a new execution frame for the function call with the given
 * positional parameters.  The function body is parsed and executed in that
 * frame.
 *
 * @param frame  The current execution frame.
 * @param name   The function name.
 * @param args   The argument list (args[0] is conventionally the function name).
 * @return MIGA_EXEC_STATUS_OK on success, MIGA_EXEC_STATUS_ERROR on error.
 */
MIGA_API miga_exec_status_t frame_call_function(miga_frame_t *frame, const string_t *name,
                                  const strlist_t *args);

/* ============================================================================
 * Exit Status
 * ============================================================================ */

MIGA_API int frame_get_last_exit_status(const miga_frame_t *frame);
MIGA_API void frame_set_last_exit_status(miga_frame_t *frame, int status);

/* ============================================================================
 * Control Flow
 * ============================================================================ */

/**
 * Find the nearest ancestor frame that is a return target (function or dot
 * script).  Returns NULL if no return target exists (return is invalid).
 *
 * @param frame The current frame
 * @return The target frame for return, or NULL if return is not valid
 */
MIGA_API miga_frame_t *frame_find_return_target(miga_frame_t *frame);

/**
 * Sets whether this frame is supposed to return, break, or continue.
 * If flow is MIGA_FRAME_FLOW_RETURN, then this frame is supposed to return from
 * a function.  If flow is MIGA_FRAME_FLOW_BREAK, then this frame is supposed to
 * break out of a loop.  If flow is MIGA_FRAME_FLOW_CONTINUE, then this frame is
 * supposed to continue to the next iteration of a loop.  The depth parameter
 * specifies how many nested loops this control flow applies to.  For example,
 * if flow is MIGA_FRAME_FLOW_BREAK and depth is 2, then this frame is supposed to
 * break out of 2 nested loops.
 */
MIGA_API void frame_set_pending_control_flow(miga_frame_t *frame, miga_frame_flow_t flow, int depth);

/* ============================================================================
 * Traps
 * ============================================================================ */

/**
 * Callback type for iterating over set traps.
 * @param signal_number The signal number (0 for EXIT)
 * @param action The trap action string (NULL if trap is ignored)
 * @param is_ignored True if the trap is set to ignore the signal
 * @param context User-provided context pointer
 */
typedef void (*frame_trap_callback_t)(int signal_number, const string_t *action, bool is_ignored,
                                      void *context);

/**
 * Iterate over all set traps and call the callback for each.
 * This includes the EXIT trap (signal 0) if set.
 * @param frame The execution frame
 * @param callback The callback function to call for each trap
 * @param context User-provided context pointer passed to callback
 */
MIGA_API void frame_for_each_set_trap(miga_frame_t *frame, frame_trap_callback_t callback, void *context);

/**
 * Get the trap action for a signal.
 * @param frame The execution frame
 * @param signal_number The signal number
 * @param out_is_ignored If non-NULL, set to true if signal is set to ignore
 * @return The trap action string, or NULL if no trap is set
 */
MIGA_API const string_t *frame_get_trap(miga_frame_t *frame, int signal_number, bool *out_is_ignored);

/**
 * Get the EXIT trap action.
 * @param frame The execution frame
 * @return The EXIT trap action string, or NULL if no EXIT trap is set
 */
MIGA_API const string_t *frame_get_exit_trap(miga_frame_t *frame);

/**
 * Set a trap for a signal.
 * @param frame The execution frame
 * @param signal_number The signal number
 * @param action The trap action string (NULL for reset to default)
 * @param is_ignored True to ignore the signal (action should be NULL)
 * @param is_reset True to reset to default (action should be NULL)
 * @return True on success, false on failure
 */
MIGA_API bool frame_set_trap(miga_frame_t *frame, int signal_number, const string_t *action, bool is_ignored,
                    bool is_reset);

/**
 * Set the EXIT trap.
 * @param frame The execution frame
 * @param action The trap action string (NULL for reset)
 * @param is_ignored True to ignore EXIT (action should be NULL)
 * @param is_reset True to reset to default (action should be NULL)
 * @return True on success, false on failure
 */
MIGA_API bool frame_set_exit_trap(miga_frame_t *frame, const string_t *action, bool is_ignored,
                         bool is_reset);

/**
 * Convert a signal name to its number.
 * Accepts names with or without "SIG" prefix (e.g., "INT", "SIGINT", "EXIT").
 * @param name The signal name
 * @return The signal number, or -1 if not recognized
 */
MIGA_API int frame_trap_name_to_number(const char *name);

/**
 * Convert a signal number to its name (without "SIG" prefix).
 * @param signal_number The signal number (0 for EXIT)
 * @return The signal name (e.g., "INT", "EXIT"), or "INVALID" if not valid
 */
MIGA_API const char *frame_trap_number_to_name(int signal_number);

/**
 * Check if a signal name is valid but unsupported on the current platform.
 * @param name The signal name
 * @return True if the name is valid but unsupported
 */
MIGA_API bool frame_trap_name_is_unsupported(const char *name);

/**
 * Runs any exit traps that are stored in the given trap store, using the
 * context of the given frame.  This should be called when a frame is exiting,
 * to ensure that any traps that were set to run on exit are executed.  The
 * traps will be executed in the order they were added to the trap store.
 */
MIGA_API void frame_run_exit_traps(miga_frame_t *frame);

/* ============================================================================
 * Aliases
 * ============================================================================ */

/**
 * Callback type for iterating over aliases.
 * @param name The alias name
 * @param value The alias value (the replacement text)
 * @param context User-provided context pointer
 */
typedef void (*frame_alias_callback_t)(const string_t *name, const string_t *value, void *context);

/**
 * Check if an alias exists in the frame's alias store.
 * @param frame The execution frame
 * @param name The alias name to check
 * @return True if the alias exists
 */
MIGA_API bool frame_has_alias(const miga_frame_t *frame, const string_t *name);
MIGA_API bool frame_has_alias_cstr(const miga_frame_t *frame, const char *name);

/**
 * Get the value of an alias.
 * @param frame The execution frame
 * @param name The alias name
 * @return The alias value, or NULL if not found. The returned pointer is valid
 *         only until the next mutating operation on the alias store.
 */
MIGA_API const string_t *frame_get_alias(const miga_frame_t *frame, const string_t *name);
MIGA_API const char *frame_get_alias_cstr(const miga_frame_t *frame, const char *name);

/**
 * Set or update an alias.
 * @param frame The execution frame
 * @param name The alias name (will be deep-copied)
 * @param value The alias value (will be deep-copied)
 * @return True on success, false if no alias store available
 */
MIGA_API bool frame_set_alias(miga_frame_t *frame, const string_t *name, const string_t *value);
MIGA_API bool frame_set_alias_cstr(miga_frame_t *frame, const char *name, const char *value);

/**
 * Remove an alias.
 * @param frame The execution frame
 * @param name The alias name to remove
 * @return True if the alias was found and removed, false otherwise
 */
MIGA_API bool frame_remove_alias(miga_frame_t *frame, const string_t *name);
MIGA_API bool frame_remove_alias_cstr(miga_frame_t *frame, const char *name);

/**
 * Get the number of aliases in the frame's alias store.
 * @param frame The execution frame
 * @return The number of aliases, or 0 if no alias store available
 */
MIGA_API int frame_alias_count(const miga_frame_t *frame);

/**
 * Iterate over all aliases and call the callback for each.
 * @param frame The execution frame
 * @param callback The callback function to call for each alias
 * @param context User-provided context pointer passed to callback
 */
MIGA_API void frame_for_each_alias(const miga_frame_t *frame, frame_alias_callback_t callback,
                          void *context);

/**
 * Remove all aliases from the frame's alias store.
 * @param frame The execution frame
 */
MIGA_API void frame_clear_all_aliases(miga_frame_t *frame);

/**
 * Check if a string is a valid alias name.
 * @param name The name to validate
 * @return True if the name is valid for use as an alias
 */
MIGA_API bool frame_alias_name_is_valid(const char *name);

/* ============================================================================
 * Frame-Level Command Execution
 * ============================================================================ */

 /**
 * Execute commands from a string in the context of the given frame.
 * Parses and executes the string as shell commands. When executing, a terminal newline is
 * presumed if not already present.  Returns MIGA_EXEC_STATUS_OK if successful execution,
 * if the string calls 'exit', or if the command is empty.
 * Returns MIGA_EXEC_STATUS_ERROR in other conditions.
 *
 * @param frame The execution frame context
 * @param command The command string to execute
 * @return MIGA_EXEC_STATUS_OK on success, MIGA_EXEC_STATUS_ERROR on error
 */
MIGA_API miga_exec_status_t frame_execute_string(miga_frame_t *frame, const string_t *command);
MIGA_API miga_exec_status_t frame_execute_string_cstr(miga_frame_t *frame, const char *command);

/**
 * Execute an eval command string in the context of the given frame.
 * Creates an EXEC_FRAME_EVAL frame for proper control flow handling
 * (return, break, continue pass through to enclosing contexts).
 *
 * @param frame The execution frame context
 * @param command The command string to execute
 * @return MIGA_EXEC_STATUS_OK on success, MIGA_EXEC_STATUS_ERROR on error
 */
MIGA_API miga_exec_status_t frame_execute_string_as_eval(miga_frame_t *frame, const string_t *command);
MIGA_API miga_exec_status_t frame_execute_string_as_eval_cstr(miga_frame_t *frame, const char *command);

MIGA_EXTERN_C_END

#endif /* FRAME_H */

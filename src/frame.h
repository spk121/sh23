#ifndef FRAME_H
#define FRAME_H

#include <stdio.h>

#include "string_t.h"
#include "string_list.h"
#include "variable_store.h"
#include "func_store.h"

typedef struct exec_frame_t exec_frame_t;
typedef struct trap_store_t trap_store_t;
typedef struct word_token_t word_token_t;
typedef struct ast_node_t ast_node_t;

typedef enum expand_flags_t expand_flags_t;

/**
 * Execution status codes for frame operations.
 */
typedef enum frame_exec_status_t
{
    FRAME_EXEC_OK = 0,       /* Execution succeeded */
    FRAME_EXEC_ERROR = 1,    /* Execution error */
    FRAME_EXEC_NOT_IMPL = 2, /* Feature not implemented */
} frame_exec_status_t;

/**
 * Export variable status codes.
 */
typedef enum frame_export_status_t
{
    FRAME_EXPORT_SUCCESS = 0,        /* Export succeeded */
    FRAME_EXPORT_INVALID_NAME,       /* Invalid variable name */
    FRAME_EXPORT_INVALID_VALUE,      /* Invalid variable value */
    FRAME_EXPORT_READONLY,           /* Variable is readonly */
    FRAME_EXPORT_NOT_SUPPORTED,      /* Export not supported on platform */
    FRAME_EXPORT_SYSTEM_ERROR        /* System error during export */
} frame_export_status_t;

/**
 * Control flow state after executing a frame or command.
 */
typedef enum frame_control_flow_t
{
    FRAME_FLOW_NORMAL,  /* Normal execution */
    FRAME_FLOW_RETURN,  /* 'return' executed */
    FRAME_FLOW_BREAK,   /* 'break' executed */
    FRAME_FLOW_CONTINUE /* 'continue' executed */
} frame_control_flow_t;


// NEW API: error handling

/**
 * Checks if the frame currently has an error message set.
 * Returns true if there is an error message, false otherwise.
 */
bool frame_has_error(const exec_frame_t *frame);

/**
 * Gets the current error message for the frame.
 * Caller should not modify or free the returned string.
 * If there is no error message, this will return an empty string (not NULL).
 */
const string_t *frame_get_error_message(const exec_frame_t *frame);

/**
 * Clears the current error message for the frame, if any.
 * After this call, there will be no error message associated with the frame.
 */
void frame_clear_error(exec_frame_t *frame);

/**
 * Sets the error message for the frame to the given string.
 * The provided string will be copied, so the caller retains ownership of the original string.
 */
void frame_set_error(exec_frame_t *frame, const string_t *error);
void frame_set_error_printf(exec_frame_t *frame, const char *format, ...);

// NEW API: variables and options

/**
 * Returns true if a variable with the given name exists in the variable store associated with the
 * current frame, false otherwise.
 */
bool frame_has_variable(const exec_frame_t *frame, const string_t *name);
bool frame_has_variable_cstr(const exec_frame_t *frame, const char *name);

/**
 * Returns the value of a variable with the given name from the variable store associated with the
 * current frame as a newly allocated string. If the variable exists but has no value, this will
 * return an empty string (not NULL). If the variable does not exist, this will return an empty
 * string (not NULL). Caller frees the returned string.
 */
string_t *frame_get_variable_value(exec_frame_t *frame, const string_t *name);
string_t *frame_get_variable_cstr(exec_frame_t *frame, const char *name);

/**
 * Checks if a variable with the given name is marked as exported or read-only in the variable store
 * associated with the current frame. Returns true if the variable is exported/read-only, false if
 * it is not exported/read-only or does not exist.
 */
bool frame_variable_is_exported(exec_frame_t *frame, const string_t *name);
bool frame_variable_is_exported_cstr(exec_frame_t *frame, const char *name);
bool frame_variable_is_readonly(exec_frame_t *frame, const string_t *name);
bool frame_variable_is_readonly_cstr(exec_frame_t *frame, const char *name);

/**
 * Returns the value of PS1 or PS2 for the current frame.
 * If the variable is not set, it returns a default value (e.g. "$ " for PS1 and "> " for PS2).
 * Caller frees the returned string.
 */
string_t *frame_get_ps1(exec_frame_t *frame);
string_t *frame_get_ps2(exec_frame_t *frame);

/**
 * Sets a variable in the variable store associated with current frame.
 * If the variable does not already exist, it will be created with the given value and will not be
 * marked as exported or read-only. If the variable already exists and is not read-only, its value
 * will be updated but its exported status will not be changed. If the variable already exists and
 * is read-only and the new value differs from the previous value, this will return an error code
 * and not update the variable. Returns VAR_STORE_ERROR_NONE on success, or error code on failure.
 */
var_store_error_t frame_set_variable(exec_frame_t *frame, const string_t *name,
                                     const string_t *value);
var_store_error_t frame_set_variable_cstr(exec_frame_t *frame, const char *name, const char *value);

/**
 * Updates the export status of an existing variable in the variable store associated with the
 * current frame. If the variable does not exist, this will return an error code. If a variable
 * exists and was not previously exported, but is now being marked as exported, this will also set
 * the variable in the environment if supported by the OS. If a variable exists and was previously
 * exported, but is now being marked as not exported, this will also unset the variable in the
 * environment if supported by the OS. Read-only variables can still have their export status
 * changed. Returns VAR_STORE_ERROR_NONE on success, or error code on failure.
 */
var_store_error_t frame_set_variable_exported(exec_frame_t *frame, const string_t *name,
                                              bool exported);

/**
 * Exports a variable to the environment. This is a convenience function that:
 * - Sets or creates the variable with the given value (if value is not NULL)
 * - Marks the variable as exported
 * - Exports it to the system environment (if supported)
 * 
 * If value is NULL, only marks an existing variable as exported without changing its value.
 * If the variable doesn't exist and value is NULL, marks the variable as exported with empty value.
 * 
 * Returns FRAME_EXPORT_SUCCESS on success, or an error code on failure.
 */
frame_export_status_t frame_export_variable(exec_frame_t *frame, const string_t *name,
                                           const string_t *value);

/**
 * Updates the read-only status of an existing variable in the variable store associated with the
 * current frame. If the variable does not exist, this will return an error code. Returns
 * VAR_STORE_ERROR_NONE on success, or error code on failure.
 */
var_store_error_t frame_set_variable_readonly(exec_frame_t *frame, const string_t *name,
                                              bool readonly);

/**
 * Removes a variable from the variable store associated with the current frame.
 * Also, when supported by the OS, this will unset the variable in the environment if it was
 * exported. If the variable does not exist, this will return an error code. If the variable exists
 * but is read-only, this will return an error code and not unset the variable. Returns
 * VAR_STORE_ERROR_NONE on success, or error code on failure.
 */
var_store_error_t frame_unset_variable(exec_frame_t *frame, const string_t *name);
var_store_error_t frame_unset_variable_cstr(exec_frame_t *frame, const char *name);

/**
 * Prints all variables in the variable store associated with the current frame in a format
 * suitable for export (e.g. "export VAR=value").
 * Only variables that are marked as exported will be printed.
 */
void frame_print_exported_variables_in_export_format(exec_frame_t *frame);

/**
 * Prints all variables in the variable store associated with the current frame.
 * This includes all variables, not just exported ones.
 * If reusable_format is true, the output will be in a format that can be reused as input to the
 * shell (e.g. by using "eval" or sourcing a file). If false, the output may be more human-readable
 * but not necessarily reusable as input.
 */
void frame_print_variables(exec_frame_t *frame, bool reusable_format);

// NEW API: word and string expansion

/**
 * Expands the given string using the variable store and other context of the current frame.
 * This includes variable expansion, command substitution, arithmetic expansion, etc.
 * The exact expansions performed will depend on the flags provided.
 * Returns a newly allocated string with the expanded result. Caller is responsible for freeing the
 * returned string.
 */
string_t *frame_expand_string(exec_frame_t *frame, const string_t *text, expand_flags_t flags);

/**
 * Expands the given word token into a list of words using the variable store and other context of
 * the current frame. This includes all expansions that apply to token parts, as well as field splitting
 * and globbing if applicable. The exact expansions performed depend on the token's flags
 * and flags of the word token's individual parts.
 * 
 * Expanding a word token may have side effects on the frame's variable store,
 * such as by parameter expansions that create new variables or update existing ones.
 * 
 * Returns a newly allocated list of strings with the expanded words. Caller is responsible for
 * freeing the returned string list.
 */
string_list_t *frame_expand_word_token(exec_frame_t *frame, const token_t *tok);

// NEW API: positional parameters

/**
 * Returns true if the given frame has positional parameters defined (e.g. $0, $1)
 */
bool frame_has_positional_params(const exec_frame_t *frame);

/**
 * Returns the number of positional parameters (e.g. $0, $1) associated with the given frame.
 */
int frame_count_positional_params(const exec_frame_t *frame);

/**
 * Shifts the positional parameters in the given frame by the specified number of positions.
 * For example, if shift_count is 1, then $1 becomes $0, $2 becomes $1, etc. The last positional
 * parameter will be removed. If shift_count is greater than or equal to the number of positional
 * parameters, all positional parameters will be removed.
 */
void frame_shift_positional_params(exec_frame_t *frame, int shift_count);

/**
 * Replaces the current positional parameters in the given frame with the new list of positional
 * parameters. The new_params list will be copied, so the caller retains ownership of the original
 * list and its strings. Returns true on success, or false on failure (e.g. memory allocation
 * failure).
 */
void frame_replace_positional_params(exec_frame_t *frame, const string_list_t *new_params);

/**
 * Returns the value of the positional parameter at the given index for the specified frame.
 * For example, index 0 corresponds to $0, index 1 corresponds to $1, etc.
 * If the index is greater than or equal to the number of positional parameters, this will return an
 * empty string (not NULL).
 */
string_t *frame_get_positional_param(const exec_frame_t *frame, int index);

/**
 * Returns a list of all positional parameters for the given frame.
 * The returned list and its strings will be newly allocated, so the caller is responsible for
 * freeing them.
 */
string_list_t *frame_get_all_positional_params(const exec_frame_t *frame);

// NEW API: named options

/**
 * Returns true if the given frame has an option with the specified name, false otherwise.
 * Note that it is possible for a frame to have an option with the given name that is currently set
 * to false, in which case this function will still return true. This function only checks for the
 * existence of the option, not its value.
 */
bool frame_has_named_option(const exec_frame_t *frame, const string_t *option_name);
bool frame_has_named_option_cstr(const exec_frame_t *frame, const char *option_name);

/**
 * Returns the value of the named option with the specified name for the given frame.
 * If the option exists, this may return true or false depending on the current value of the option.
 * If the option does not exist, this will return false.
 */
bool frame_get_named_option(const exec_frame_t *frame, const string_t *option_name);
bool frame_get_named_option_cstr(const exec_frame_t *frame, const char *option_name);

/**
 * Sets the value of the named option with the specified name for the given frame.
 * If the option does not already exist, it will be created with the given value.
 * If the option already exists, its value will be updated to the new value.
 * If plus_prefix is true, then a plus sign (+) will be used in front of the option name when
 * printing it in error messages or when printing options, to indicate that this option is set using
 * a plus sign (e.g. "set +o option_name").
 */
bool frame_set_named_option(exec_frame_t *frame, const string_t *option_name, bool value,
                            bool plus_prefix);
bool frame_set_named_option_cstr(exec_frame_t *frame, const char *option_name, bool value,
                                 bool plus_prefix);

// NEW API: functions

bool frame_has_function(const exec_frame_t *frame, const string_t *name);

string_t *frame_get_function(exec_frame_t *frame, const string_t *name);
func_store_error_t frame_get_function_cstr(exec_frame_t *frame, const char *name, string_t **value);
func_store_error_t frame_set_function(exec_frame_t *frame, const string_t *name,
                                      const ast_node_t *value);
func_store_error_t frame_set_function_cstr(exec_frame_t *frame, const char *name,
                                           const char *value);
func_store_error_t frame_unset_function(exec_frame_t *frame, const string_t *name);
func_store_error_t frame_unset_function_cstr(exec_frame_t *frame, const char *name);

// Is this public API? Or is this only for the exec_execute_* function to call?
frame_exec_status_t frame_call_function(exec_frame_t *frame, const string_t *name,
                                        const string_list_t *args);



// NEW API: exit status
int frame_get_last_exit_status(const exec_frame_t *frame);
void frame_set_last_exit_status(exec_frame_t *frame, int status);

// NEW API: control flow

/**
 * Find the nearest ancestor frame that is a return target (function or dot script).
 * Returns NULL if no return target exists (return is invalid).
 * 
 * @param frame The current frame
 * @return The target frame for return, or NULL if return is not valid
 */
exec_frame_t* frame_find_return_target(exec_frame_t* frame);

/**
 * Sets whether this frame is supposed to return, break, or continue.
 * If flow is FRAME_FLOW_RETURN, then this frame is supposed to return from a function.
 * If flow is FRAME_FLOW_BREAK, then this frame is supposed to break out of a loop.
 * If flow is FRAME_FLOW_CONTINUE, then this frame is supposed to continue to the next iteration of a
 * loop. The depth parameter specifies how many nested loops this control flow applies to. For
 * example, if flow is FRAME_FLOW_BREAK and depth is 2, then this frame is supposed to break out of 2
 * nested loops.
 */
void frame_set_pending_control_flow(exec_frame_t *frame, frame_control_flow_t flow, int depth);

// NEW API: traps
/**
 * Runs any exit traps that are stored in the given trap store, using the context of the given
 * frame. This should be called when a frame is exiting, to ensure that any traps that were set to
 * run on exit are executed. The traps will be executed in the order they were added to the trap
 * store.
 */
void frame_run_exit_traps(const trap_store_t *store, exec_frame_t *frame);

// NEW API: background jobs
void frame_reap_background_jobs(exec_frame_t *frame, bool wait_for_completion);
void frame_print_background_jobs(exec_frame_t *frame);

/* ============================================================================
 * Job Control Functions
 * ============================================================================ */

/**
 * Job output format for frame job printing functions.
 */
typedef enum frame_jobs_format_t
{
    FRAME_JOBS_FORMAT_DEFAULT,   /* Default format: [job_id]± state command */
    FRAME_JOBS_FORMAT_LONG,      /* Long format: includes PIDs */
    FRAME_JOBS_FORMAT_PID_ONLY   /* PID only: just the process group leader PID */
} frame_jobs_format_t;

/**
 * Parse a job ID spec from a string.
 * Accepts: %n, %+, %%, %-, plain number n
 * 
 * @param frame   The execution frame
 * @param arg_str The string to parse
 * @return        Job ID on success, -1 on error
 */
int frame_parse_job_id(const exec_frame_t* frame, const string_t* arg_str);

/**
 * Print a specific job by ID.
 * 
 * @param frame   The execution frame
 * @param job_id  The job ID to print
 * @param format  The output format
 * @return        true if job was found and printed, false otherwise
 */
bool frame_print_job_by_id(const exec_frame_t* frame, int job_id, frame_jobs_format_t format);

/**
 * Print all jobs.
 * 
 * @param frame   The execution frame
 * @param format  The output format
 */
void frame_print_all_jobs(const exec_frame_t* frame, frame_jobs_format_t format);

/**
 * Check if the frame has any jobs.
 * 
 * @param frame  The execution frame
 * @return       true if there are jobs, false otherwise
 */
bool frame_has_jobs(const exec_frame_t* frame);

// NEW API: stream execution
/**
 * Execute commands from a stream (file or stdin) in the context of the given frame.
 * Reads lines from the stream, parses them, and executes them in the frame.
 * 
 * @param frame The execution frame context
 * @param fp The file stream to read from
 * @return FRAME_EXEC_OK on success, FRAME_EXEC_ERROR on error
 */
frame_exec_status_t frame_execute_stream(exec_frame_t *frame, FILE *fp);

#endif
#ifndef EXEC_H
#define EXEC_H

/**
 * @file exec.h
 * @brief Public API for the POSIX shell executor (top-level concerns).
 *
 * This header defines the public interface for creating, configuring, and
 * running a shell executor. It covers:
 *
 *   - Executor lifecycle (create / destroy)
 *   - Pre-execution configuration (shell identity, flags, environment, etc.)
 *   - Execution entry points (stream, string, partial string, custom line editor)
 *   - Builtin registration (standard and custom)
 *   - Job control
 *   - Global state queries (exit status, error messages, pipe statuses, PIDs)
 *
 * Frame-level operations (variables, positional parameters, aliases, traps,
 * expansion, control flow) are defined in frame.h.
 *
 * Together, exec.h and frame.h form the complete public API of the execution
 * engine.  No other internal header should be included by library consumers.
 */

#include <stdbool.h>
#include <stdio.h>

/* ── Public utility types (also used by frame.h) ─────────────────────────── */
#include "getopt_string.h"
#include "string_list.h"
#include "string_t.h"

/* ── Platform-specific system headers ────────────────────────────────────── */
#ifdef POSIX_API
#include <sys/resource.h>
#include <sys/types.h>
#endif

/* ============================================================================
 * Opaque Types
 * ============================================================================
 *
 * The concrete definitions of these structures live in exec_internal.h and are
 * not part of the public API.  All access goes through the functions declared
 * in this header and in frame.h.
 */

typedef struct exec_t exec_t;
typedef struct exec_frame_t exec_frame_t;

/* ============================================================================
 * Status / Result Types
 * ============================================================================ */

/**
 * Status codes returned by executor operations.
 */
typedef enum exec_status_t
{
    EXEC_OK = 0,
    EXEC_ERROR = 1,
    EXEC_NOT_IMPL = 2,
    EXEC_OK_INTERNAL_FUNCTION_STORED,
    EXEC_BREAK,            /**< break statement executed */
    EXEC_CONTINUE,         /**< continue statement executed */
    EXEC_RETURN,           /**< return statement executed */
    EXEC_EXIT,             /**< exit statement executed */
    EXEC_INCOMPLETE_INPUT, /**< input ended but command was incomplete */
} exec_status_t;

/**
 * Result of executing a command string at top-level.
 * Bundles the execution status together with the exit code that was produced.
 */
typedef struct exec_result_t
{
    exec_status_t status;
    int exit_code;
} exec_result_t;

/* ============================================================================
 * Standard Exit Codes
 * ============================================================================
 *
 * POSIX-defined exit status values for use by builtins and the executor.
 * A builtin returns one of these (or any value 0–255) from its function.
 */

#define EXEC_EXIT_SUCCESS 0       /**< Successful completion                    */
#define EXEC_EXIT_FAILURE 1       /**< General failure / catchall               */
#define EXEC_EXIT_MISUSE 2        /**< Incorrect usage (bad options / arguments) */
#define EXEC_EXIT_CANNOT_EXEC 126 /**< Command found but not executable         */
#define EXEC_EXIT_NOT_FOUND 127   /**< Command not found                        */

/* ============================================================================
 * Executor Lifecycle
 * ============================================================================ */

/**
 * Create a new executor with default settings.
 *
 * The executor is created in an unconfigured state.  Use the setter functions
 * below to override defaults before calling one of the setup / execute
 * functions.
 *
 * @return A new executor, or NULL on allocation failure.
 */
exec_t *exec_create(void);

/**
 * Destroy an executor and free all associated memory.
 * Safe to call with a pointer to NULL.
 *
 * @param executor  Pointer to the executor pointer; set to NULL on return.
 */
void exec_destroy(exec_t **executor);

/* ============================================================================
 * Pre-Execution Configuration
 * ============================================================================
 *
 * These setters configure the executor before execution begins.  They may only
 * be called before the top frame is initialised.  After that point the setters
 * return false to indicate that the value was not applied.
 *
 * When RC parsing is enabled, values set here may be overridden by the RC
 * files.
 */

/* ── Startup environment ─────────────────────────────────────────────────── */

bool exec_is_args_set(const exec_t *executor);
char *const *exec_get_args(const exec_t *executor, int *argc_out);
bool exec_set_args(exec_t *executor, int argc, char *const *argv);

bool exec_is_envp_set(const exec_t *executor);
char *const *exec_get_envp(const exec_t *executor);
bool exec_set_envp(exec_t *executor, char *const *envp);

/* ── Shell identity ──────────────────────────────────────────────────────── */

bool exec_is_shell_name_set(const exec_t *executor);
const char *exec_get_shell_name(const exec_t *executor);
bool exec_set_shell_name(exec_t *executor, const char *shell_name);

/* ── Shell option flags ──────────────────────────────────────────────────── */

bool exec_get_flag_allexport(const exec_t *executor);
bool exec_set_flag_allexport(exec_t *executor, bool value);

bool exec_get_flag_errexit(const exec_t *executor);
bool exec_set_flag_errexit(exec_t *executor, bool value);

bool exec_get_flag_ignoreeof(const exec_t *executor);
bool exec_set_flag_ignoreeof(exec_t *executor, bool value);

bool exec_get_flag_noclobber(const exec_t *executor);
bool exec_set_flag_noclobber(exec_t *executor, bool value);

bool exec_get_flag_noglob(const exec_t *executor);
bool exec_set_flag_noglob(exec_t *executor, bool value);

bool exec_get_flag_noexec(const exec_t *executor);
bool exec_set_flag_noexec(exec_t *executor, bool value);

bool exec_get_flag_nounset(const exec_t *executor);
bool exec_set_flag_nounset(exec_t *executor, bool value);

bool exec_get_flag_pipefail(const exec_t *executor);
bool exec_set_flag_pipefail(exec_t *executor, bool value);

bool exec_get_flag_verbose(const exec_t *executor);
bool exec_set_flag_verbose(exec_t *executor, bool value);

bool exec_get_flag_vi(const exec_t *executor);
bool exec_set_flag_vi(exec_t *executor, bool value);

bool exec_get_flag_xtrace(const exec_t *executor);
bool exec_set_flag_xtrace(exec_t *executor, bool value);

/* ── Interactive / login mode ────────────────────────────────────────────── */

bool exec_get_is_interactive(const exec_t *executor);
bool exec_set_is_interactive(exec_t *executor, bool is_interactive);

bool exec_get_is_login_shell(const exec_t *executor);
bool exec_set_is_login_shell(exec_t *executor, bool is_login_shell);

/* ── Job control ─────────────────────────────────────────────────────────── */

bool exec_get_job_control_enabled(const exec_t *executor);
bool exec_set_job_control_enabled(exec_t *executor, bool enabled);

/* ── Working directory ───────────────────────────────────────────────────── */

bool exec_is_working_directory_set(const exec_t *executor);
const char *exec_get_working_directory(const exec_t *executor);
bool exec_set_working_directory(exec_t *executor, const char *path);

/* ── File permissions ────────────────────────────────────────────────────── */

bool exec_is_umask_set(const exec_t *executor);
int exec_get_umask(const exec_t *executor);
bool exec_set_umask(exec_t *executor, int mask);

#ifdef POSIX_API
mode_t exec_get_umask_posix(const exec_t *executor);
bool exec_set_umask_posix(exec_t *executor, mode_t mask);

bool exec_is_file_size_limit_set(const exec_t *executor);
rlim_t exec_get_file_size_limit(const exec_t *executor);
bool exec_set_file_size_limit(exec_t *executor, rlim_t limit);
#endif

/* ── Process identity ────────────────────────────────────────────────────── */

bool exec_is_process_group_set(const exec_t *executor);
int exec_get_process_group(const exec_t *executor);
bool exec_set_process_group(exec_t *executor, int pgid);

bool exec_is_shell_pid_set(const exec_t *executor);
int exec_get_shell_pid(const exec_t *executor);
bool exec_set_shell_pid(exec_t *executor, int pid);

bool exec_is_shell_ppid_set(const exec_t *executor);
int exec_get_shell_ppid(const exec_t *executor);
bool exec_set_shell_ppid(exec_t *executor, int ppid);

/* ── RC file control ─────────────────────────────────────────────────────── */

bool exec_get_inhibit_rc_files(const exec_t *executor);
bool exec_set_inhibit_rc_files(exec_t *executor, bool inhibit);

bool exec_is_system_rc_filename_set(const exec_t *executor);
const char *exec_get_system_rc_filename(const exec_t *executor);
bool exec_set_system_rc_filename(exec_t *executor, const char *filename);

bool exec_is_user_rc_filename_set(const exec_t *executor);
const char *exec_get_user_rc_filename(const exec_t *executor);
bool exec_set_user_rc_filename(exec_t *executor, const char *filename);

/* ── Special parameters ──────────────────────────────────────────────────── */

int exec_get_last_exit_status(const exec_t *executor);
bool exec_set_last_exit_status(exec_t *executor, int status);

int exec_get_last_background_pid(const exec_t *executor);
bool exec_set_last_background_pid(exec_t *executor, int pid);

const char *exec_get_last_argument(const exec_t *executor);
bool exec_set_last_argument(exec_t *executor, const char *arg);

/* ============================================================================
 * Frame Access
 * ============================================================================ */

/**
 * Returns true once the top frame has been fully initialised and execution
 * has begun.  Before this point, pre-execution setters are still accepted.
 */
bool exec_is_top_frame_initialized(const exec_t *executor);

/**
 * Get the current execution frame.
 *
 * The current frame is the innermost active frame (e.g. inside a function
 * call or dot-script).  Returns NULL if no execution has begun.
 */
exec_frame_t *exec_get_current_frame(const exec_t *executor);


/* ============================================================================
 * Builtin Registration
 * ============================================================================
 *
 * A builtin receives a context that provides access to both the executor
 * (for global state such as jobs, exit status, PIDs) and the current frame
 * (for variables, positional parameters, traps, etc.), plus the redirected
 * I/O streams for the command invocation.
 *
 * The args list includes argv[0] (the builtin name) as its first element.
 * Use the getopt_string API to parse options from the list.
 *
 * The builtin returns an integer exit status (0 for success).
 */

/**
 * Context passed to every builtin invocation.
 */
typedef struct exec_builtin_context_t
{
    exec_t *executor;    /**< The executor (global state)           */
    exec_frame_t *frame; /**< The current execution frame           */
    FILE *stdin_fp;      /**< stdin for this invocation (may be redirected)  */
    FILE *stdout_fp;     /**< stdout for this invocation (may be redirected) */
    FILE *stderr_fp;     /**< stderr for this invocation (may be redirected) */
} exec_builtin_context_t;

/**
 * Builtin function signature.
 *
 * @param ctx   Builtin context (executor, frame, I/O streams).
 * @param args  Argument list.  The first element is the builtin name.
 * @return      Exit status (0 = success).
 */
typedef int (*exec_builtin_fn_t)(exec_builtin_context_t *ctx, string_list_t *args);

/**
 * Builtin category per POSIX XCU 2.14.
 *
 * The distinction matters for error handling and variable assignment
 * semantics:
 *
 *   SPECIAL builtins (break, continue, return, exit, export, readonly,
 *   set, unset, eval, exec, dot/source, shift, times, trap):
 *     - Variable assignments from the command prefix persist after the
 *       builtin completes.
 *     - A usage or redirection error in a special builtin causes a
 *       non-interactive shell to exit.
 *
 *   REGULAR builtins (cd, read, command, type, etc.):
 *     - Variable assignments from the command prefix are temporary
 *       (scoped to the command).
 *     - Errors do not cause the shell to exit.
 */
typedef enum exec_builtin_category_t
{
    EXEC_BUILTIN_SPECIAL, /**< POSIX special builtin  */
    EXEC_BUILTIN_REGULAR  /**< POSIX regular builtin  */
} exec_builtin_category_t;

/**
 * Register a builtin command.
 *
 * If a builtin with the given name already exists it is replaced.
 *
 * @param executor  The executor.
 * @param name      The command name (e.g. "cd", "export", "myfunc").
 * @param fn        The implementation function.
 * @param category  Whether this is a POSIX special or regular builtin.
 * @return true on success, false on failure (e.g. NULL arguments).
 */
bool exec_register_builtin(exec_t *executor, const char *name, exec_builtin_fn_t fn,
                           exec_builtin_category_t category);

/**
 * Unregister a previously registered builtin command.
 *
 * @return true if the builtin was found and removed, false otherwise.
 */
bool exec_unregister_builtin(exec_t *executor, const char *name);

/**
 * Check whether a builtin with the given name is registered.
 */
bool exec_has_builtin(const exec_t *executor, const char *name);

/**
 * Look up a registered builtin by name.
 *
 * @return The function pointer, or NULL if no builtin is registered under
 *         that name.
 */
exec_builtin_fn_t exec_get_builtin(const exec_t *executor, const char *name);

/**
 * Get the category of a registered builtin.
 *
 * @param executor  The executor.
 * @param name      The builtin name.
 * @param category_out  If non-NULL and the builtin exists, receives the
 *                      category.
 * @return true if the builtin exists, false otherwise.
 */
bool exec_get_builtin_category(const exec_t *executor, const char *name,
                               exec_builtin_category_t *category_out);


/* ============================================================================
 * Execution Setup
 * ============================================================================ */

/**
 * Prepare the executor for interactive use.
 *
 * Initialises the top frame (if not already done), installs signal handlers,
 * and sources RC files according to the current configuration.
 *
 * Even if this function returns EXEC_ERROR (e.g. an RC file failed to
 * source), the executor remains usable.
 *
 * @return EXEC_OK on success, EXEC_ERROR on failure.
 */
exec_status_t exec_setup_interactive(exec_t *executor);

/**
 * Prepare the executor for non-interactive (script / -c) use.
 *
 * Initialises the top frame and sources the system RC file for login shells.
 *
 * @return EXEC_OK on success, EXEC_ERROR on failure.
 */
exec_status_t exec_setup_non_interactive(exec_t *executor);

/*
 * NOTE: If execution begins without calling one of the setup functions above,
 * the executor will perform lazy initialisation but will NOT source any RC
 * files and may skip other platform-specific setup.  Calling a setup function
 * first is recommended.
 */

/* ============================================================================
 * Execution Entry Points
 * ============================================================================ */

/**
 * Execute commands read from a stream.
 *
 * If the executor was set up for interactive mode, this provides the full
 * REPL experience (prompts, signal handling, job notifications).
 *
 * @param executor  The executor.
 * @param fp        Input stream (e.g. stdin, or an open script file).
 * @return Execution status.
 */
exec_status_t exec_execute_stream(exec_t *executor, FILE *fp);

/**
 * Execute commands from a stream, using the given filename for error messages
 * instead of "stdin".
 *
 * Intended for non-interactive use (scripts, sourced files).
 */
exec_status_t exec_execute_stream_named(exec_t *executor, FILE *fp, const char *filename);

/**
 * Execute a complete, self-contained command string at top-level.
 *
 * Intended for `-c` style invocations.  Incomplete input (unclosed quotes,
 * missing keywords) is treated as an error.
 *
 * @param executor  The executor.
 * @param command   The complete command string.
 * @return Result with status and exit code.
 */
exec_result_t exec_execute_command_string(exec_t *executor, const char *command);

/* ── Partial / incremental string execution ──────────────────────────────── */

/**
 * Opaque state for incremental command string execution.
 *
 * Allows a caller implementing a custom REPL to feed input line-by-line,
 * with the executor reporting when more input is needed (e.g. unclosed
 * quotes or compound commands).
 *
 * Zero-initialise before the first call.  The executor populates it with
 * continuation state after each call.
 */
typedef struct exec_partial_state_t exec_partial_state_t;

/**
 * Execute a command string incrementally.
 *
 * @param executor          The executor.
 * @param command           The command text for this chunk.
 * @param filename          Source filename for error messages (may be NULL).
 * @param line_number       Starting line number (0 = not provided).
 * @param partial_state_out Continuation state (caller zero-inits before first
 *                          call; memset to zero to reset).
 * @return EXEC_OK if the command completed, EXEC_INCOMPLETE_INPUT if more
 *         input is needed, or an error / control-flow status.
 */
exec_status_t exec_execute_command_string_partial(exec_t *executor, const char *command,
                                                  const char *filename, size_t line_number,
                                                  exec_partial_state_t *partial_state_out);

/* ── Custom line-editor support ──────────────────────────────────────────── */

/**
 * Status returned by a line-editor callback.
 */
typedef enum line_edit_status_t
{
    LINE_EDIT_OK = 0,            /**< success                                */
    LINE_EDIT_EOF = -1,          /**< EOF / ctrl-D / closed input            */
    LINE_EDIT_ERROR = -2,        /**< fatal I/O or allocation error          */
    LINE_EDIT_INTERRUPT = -3,    /**< SIGINT received                        */
    LINE_EDIT_PREVIOUS = -4,     /**< request previous history entry         */
    LINE_EDIT_NEXT = -5,         /**< request next history entry             */
    LINE_EDIT_CURRENT = -6,      /**< request current history entry          */
    LINE_EDIT_HISTORY_IDX = 1000 /**< values >= 1000 encode a history index */
} line_edit_status_t;

/**
 * Signature of a user-provided line editor function.
 *
 * @param prompt     NUL-terminated prompt string (may be NULL or empty).
 * @param line_out   On entry may be NULL (allocate a new string_t) or
 *                   non-NULL (reuse / replace).  On success must point to a
 *                   valid heap-allocated string_t without trailing newline.
 * @param user_data  Opaque pointer from exec_execute_stream_with_line_editor.
 * @return           A line_edit_status_t value.
 */
typedef line_edit_status_t (*line_editor_fn_t)(const char *prompt, string_t **line_out,
                                               void *user_data);

/**
 * Execute commands from a stream using a custom line editor.
 *
 * Only valid after exec_setup_interactive().
 *
 * @param executor              The executor.
 * @param fp                    Input stream.
 * @param line_editor_fn        Line-reading callback.
 * @param line_editor_user_data Opaque context for the callback.
 * @return Execution status.
 */
exec_status_t exec_execute_stream_with_line_editor(exec_t *executor, FILE *fp,
                                                   line_editor_fn_t line_editor_fn,
                                                   void *line_editor_user_data);

/* ============================================================================
 * Global State Queries
 * ============================================================================ */

/* ── Exit status ─────────────────────────────────────────────────────────── */

int exec_get_exit_status(const exec_t *executor);
void exec_set_exit_status(exec_t *executor, int status);

/**
 * Request that the executor exit.
 *
 * Sets the exit status and marks the executor as exiting.  The next time the
 * executor would normally request more input at top level — whether inside
 * exec_execute_stream(), exec_execute_stream_with_line_editor(), or on the
 * next call to exec_execute_command_string_partial() — it will stop parsing
 * new input and return EXEC_EXIT.
 *
 * This is the mechanism by which the 'exit' builtin communicates its intent
 * to the executor.  The builtin should also set FRAME_FLOW_TOP on its frame
 * to unwind any nested frames before the executor checks this flag.
 *
 * @param executor  The executor.
 * @param status    The exit status to report (becomes $?).
 */
void exec_request_exit(exec_t *executor, int status);

/**
 * Check whether an exit has been requested.
 *
 * @return true if exec_request_exit() has been called and the executor has
 *         not yet returned EXEC_EXIT to its caller.
 */
bool exec_is_exit_requested(const exec_t *executor);

/* ── Error message ───────────────────────────────────────────────────────── */

const char *exec_get_error(const exec_t *executor);
void exec_set_error(exec_t *executor, const char *format, ...);
void exec_clear_error(exec_t *executor);

/* ── Pipe statuses (PIPESTATUS / pipefail) ───────────────────────────────── */

int exec_get_pipe_status_count(const exec_t *executor);
const int *exec_get_pipe_statuses(const exec_t *executor);
void exec_reset_pipe_statuses(exec_t *executor);

/* ── Prompts ─────────────────────────────────────────────────────────────── */

/**
 * Get the raw PS1 value from the variable store.
 * Returns a default if PS1 is not set.  Caller frees.
 */
char *exec_get_ps1(const exec_t *executor);

/**
 * Get PS1 with all prompt expansions applied.  Caller frees.
 */
char *exec_get_rendered_ps1(const exec_t *executor);

/**
 * Get the raw PS2 value.  PS2 is never expanded.  Caller frees.
 */
char *exec_get_ps2(const exec_t *executor);

/* ============================================================================
 * Job Control
 * ============================================================================ */

/**
 * Job state as visible through the public API.
 */
typedef enum exec_job_state_t
{
    EXEC_JOB_RUNNING,
    EXEC_JOB_STOPPED,
    EXEC_JOB_DONE,
    EXEC_JOB_TERMINATED
} exec_job_state_t;

/* ── Reaping ─────────────────────────────────────────────────────────────── */

/**
 * Reap completed background jobs.
 * If @p notify is true, print their status.
 * Only functional in POSIX_API mode; no-op otherwise.
 */
void exec_reap_background_jobs(exec_t *executor, bool notify);

/* ── Enumeration ─────────────────────────────────────────────────────────── */

size_t exec_get_job_count(const exec_t *executor);
size_t exec_get_job_ids(const exec_t *executor, int *job_ids, size_t max_jobs);
int exec_get_current_job_id(const exec_t *executor);
int exec_get_previous_job_id(const exec_t *executor);

/* ── Per-job queries ─────────────────────────────────────────────────────── */

exec_job_state_t exec_job_get_state(const exec_t *executor, int job_id);
const char *exec_job_get_command(const exec_t *executor, int job_id);
bool exec_job_is_background(const exec_t *executor, int job_id);

#ifdef POSIX_API
pid_t exec_job_get_pgid(const exec_t *executor, int job_id);
#else
int exec_job_get_pgid(const exec_t *executor, int job_id);
#endif

/* ── Per-process queries within a job ────────────────────────────────────── */

size_t exec_job_get_process_count(const exec_t *executor, int job_id);
exec_job_state_t exec_job_get_process_state(const exec_t *executor, int job_id, size_t index);
int exec_job_get_process_exit_status(const exec_t *executor, int job_id, size_t index);

#ifdef POSIX_API
pid_t exec_job_get_process_pid(const exec_t *executor, int job_id, size_t index);
#elif defined(UCRT_API)
int exec_job_get_process_pid(const exec_t *executor, int job_id, size_t index);
uintptr_t exec_job_get_process_handle(const exec_t *executor, int job_id, size_t index);
#else
int exec_job_get_process_pid(const exec_t *executor, int job_id, size_t index);
#endif

/* ── Job actions ─────────────────────────────────────────────────────────── */

/**
 * Bring a job to the foreground.
 * If `cmd` is provided and if the function returns 'true' indicating
 * that a job was foregrounded, `cmd` will receive a newly allocated copy of
 * the command string of the 
 * job that is being foregrounded. This necessary because foregrounded jobs
 * are removed from the job list, so their command strings are freed and no
 * longer accessible.
 */
bool exec_job_foreground(exec_t *executor, int job_id, char **cmd);

bool exec_job_background(exec_t *executor, int job_id);
bool exec_job_kill(exec_t *executor, int job_id, int sig);
void exec_print_jobs(const exec_t *executor, FILE *output);

/**
 * Job output format for printing.
 */
typedef enum exec_jobs_format_t
{
    EXEC_JOBS_FORMAT_DEFAULT, /**< [job_id] state command  */
    EXEC_JOBS_FORMAT_LONG,    /**< includes PIDs           */
    EXEC_JOBS_FORMAT_PID_ONLY /**< process group leader PID only */
} exec_jobs_format_t;

/**
 * Parse a job-ID specifier from a string.
 * Accepts: %n, %+, %%, %-, or a plain number.
 *
 * @return Job ID on success, -1 on error.
 */
int exec_parse_job_id(const exec_t *executor, const char *spec);

/**
 * Print a single job.
 *
 * @return true if the job was found and printed.
 */
bool exec_print_job_by_id(const exec_t *executor, int job_id, exec_jobs_format_t format,
                          FILE *output);

/**
 * Print all jobs.
 */
void exec_print_all_jobs(const exec_t *executor, exec_jobs_format_t format, FILE *output);

/**
 * Check whether any jobs exist.
 */
bool exec_has_jobs(const exec_t *executor);

#endif /* EXEC_H */

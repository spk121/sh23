/* exec.h - a POSIX shell executor */
#ifndef MIGA_EXEC_H
#define MIGA_EXEC_H

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

/* Compile-time configuration must come first. */
#include "miga/migaconf.h"

#include <stdbool.h>
#include <stdio.h>
#ifdef MIGA_POSIX_API
#include <sys/resource.h>
#include <sys/types.h>
#endif

#include "miga/api.h"
#include "miga/type_pub.h"
#include "miga/strlist.h"
#include "miga/string_t.h"

MIGA_EXTERN_C_START

/* ============================================================================
 * Executor Lifecycle
 * ============================================================================ */

/**
 * Create a new executor with default settings.
 *
 * The executor is created in an unconfigured state.  Use the setter
 * functions below to override defaults before calling one of the
 * setup / execute functions.
 *
 * @return A new executor, or NULL on allocation failure.
 */
MIGA_API miga_exec_t *exec_create(void);

/**
 * Destroy an executor and free all associated memory.
 * Safe to call with a pointer to NULL.
 *
 * @param executor  Pointer to the executor pointer; set to NULL on return.
 */
MIGA_API void exec_destroy(miga_exec_t **executor);

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

MIGA_API bool exec_is_args_set(const miga_exec_t *executor);
MIGA_API const strlist_t *exec_get_args(const miga_exec_t *executor);
MIGA_API char *const *exec_get_args_cstr(const miga_exec_t *executor,
                                         int *argc_out);
MIGA_API bool exec_set_args(miga_exec_t *executor, const strlist_t *args);
MIGA_API bool exec_set_args_cstr(miga_exec_t *executor, int argc,
                                 char *const *argv);

MIGA_API bool exec_is_envp_set(const miga_exec_t *executor);
MIGA_API const strlist_t *exec_get_envp(const miga_exec_t *executor);
MIGA_API char *const *exec_get_envp_cstr(const miga_exec_t *executor);
MIGA_API bool exec_set_envp(miga_exec_t *executor, const strlist_t *envp);
MIGA_API bool exec_set_envp_cstr(miga_exec_t *executor, char *const *envp);

/* ── Shell identity ──────────────────────────────────────────────────────── */

MIGA_API bool exec_is_shell_name_set(const miga_exec_t *executor);
MIGA_API const string_t *exec_get_shell_name(const miga_exec_t *executor);
MIGA_API const char *exec_get_shell_name_cstr(const miga_exec_t *executor);
MIGA_API bool exec_set_shell_name(miga_exec_t *executor,
                                  const string_t *shell_name);
MIGA_API bool exec_set_shell_name_cstr(miga_exec_t *executor,
                                       const char *shell_name);

/* ── Shell option flags ──────────────────────────────────────────────────── */

MIGA_API bool exec_get_flag_allexport(const miga_exec_t *executor);
MIGA_API bool exec_set_flag_allexport(miga_exec_t *executor, bool value);

MIGA_API bool exec_get_flag_errexit(const miga_exec_t *executor);
MIGA_API bool exec_set_flag_errexit(miga_exec_t *executor, bool value);

MIGA_API bool exec_get_flag_ignoreeof(const miga_exec_t *executor);
MIGA_API bool exec_set_flag_ignoreeof(miga_exec_t *executor, bool value);

MIGA_API bool exec_get_flag_noclobber(const miga_exec_t *executor);
MIGA_API bool exec_set_flag_noclobber(miga_exec_t *executor, bool value);

MIGA_API bool exec_get_flag_noglob(const miga_exec_t *executor);
MIGA_API bool exec_set_flag_noglob(miga_exec_t *executor, bool value);

MIGA_API bool exec_get_flag_noexec(const miga_exec_t *executor);
MIGA_API bool exec_set_flag_noexec(miga_exec_t *executor, bool value);

MIGA_API bool exec_get_flag_nounset(const miga_exec_t *executor);
MIGA_API bool exec_set_flag_nounset(miga_exec_t *executor, bool value);

MIGA_API bool exec_get_flag_pipefail(const miga_exec_t *executor);
MIGA_API bool exec_set_flag_pipefail(miga_exec_t *executor, bool value);

MIGA_API bool exec_get_flag_verbose(const miga_exec_t *executor);
MIGA_API bool exec_set_flag_verbose(miga_exec_t *executor, bool value);

MIGA_API bool exec_get_flag_vi(const miga_exec_t *executor);
MIGA_API bool exec_set_flag_vi(miga_exec_t *executor, bool value);

MIGA_API bool exec_get_flag_xtrace(const miga_exec_t *executor);
MIGA_API bool exec_set_flag_xtrace(miga_exec_t *executor, bool value);

/* ── Interactive / login mode ────────────────────────────────────────────── */

MIGA_API bool exec_get_is_interactive(const miga_exec_t *executor);
MIGA_API bool exec_set_is_interactive(miga_exec_t *executor, bool is_interactive);

MIGA_API bool exec_get_is_login_shell(const miga_exec_t *executor);
MIGA_API bool exec_set_is_login_shell(miga_exec_t *executor, bool is_login_shell);

/* ── Job control ─────────────────────────────────────────────────────────── */

MIGA_API bool exec_get_job_control_enabled(const miga_exec_t *executor);
MIGA_API bool exec_set_job_control_enabled(miga_exec_t *executor, bool enabled);

/* ── Working directory ───────────────────────────────────────────────────── */

MIGA_API bool exec_is_working_directory_set(const miga_exec_t *executor);
MIGA_API const string_t *exec_get_working_directory(const miga_exec_t *executor);
MIGA_API const char *exec_get_working_directory_cstr(const miga_exec_t *executor);
MIGA_API bool exec_set_working_directory(miga_exec_t *executor,
                                         const string_t *path);
MIGA_API bool exec_set_working_directory_cstr(miga_exec_t *executor,
                                              const char *path);

/* ── File permissions ────────────────────────────────────────────────────── */

MIGA_API bool exec_is_umask_set(const miga_exec_t *executor);
MIGA_API int exec_get_umask(const miga_exec_t *executor);
MIGA_API bool exec_set_umask(miga_exec_t *executor, int mask);

/**
 * Format a umask value as a symbolic mode string (e.g. "u=rwx,g=rx,o=rx").
 *
 * The returned string is heap-allocated.  Caller frees.
 *
 * @param mask  The umask value (e.g. 0022).
 * @return A newly allocated string with the symbolic representation.
 */
MIGA_API string_t *exec_format_umask_symbolic(int mask);
MIGA_API char *exec_format_umask_symbolic_cstr(int mask);

#ifdef MIGA_POSIX_API
MIGA_API mode_t exec_get_umask_posix(const miga_exec_t *executor);
MIGA_API bool exec_set_umask_posix(miga_exec_t *executor, mode_t mask);

MIGA_API bool exec_is_file_size_limit_set(const miga_exec_t *executor);
MIGA_API rlim_t exec_get_file_size_limit(const miga_exec_t *executor);
MIGA_API bool exec_set_file_size_limit(miga_exec_t *executor, rlim_t limit);
#endif

/* ── Process identity ────────────────────────────────────────────────────── */

MIGA_API bool exec_is_process_group_set(const miga_exec_t *executor);
MIGA_API int exec_get_process_group(const miga_exec_t *executor);
MIGA_API bool exec_set_process_group(miga_exec_t *executor, int pgid);

MIGA_API bool exec_is_shell_pid_set(const miga_exec_t *executor);
MIGA_API int exec_get_shell_pid(const miga_exec_t *executor);
MIGA_API bool exec_set_shell_pid(miga_exec_t *executor, int pid);

MIGA_API bool exec_is_shell_ppid_set(const miga_exec_t *executor);
MIGA_API int exec_get_shell_ppid(const miga_exec_t *executor);
MIGA_API bool exec_set_shell_ppid(miga_exec_t *executor, int ppid);

/* ── RC file control ─────────────────────────────────────────────────────── */

MIGA_API bool exec_get_inhibit_rc_files(const miga_exec_t *executor);
MIGA_API bool exec_set_inhibit_rc_files(miga_exec_t *executor, bool inhibit);

/**
 * When set, exec_setup_core() will not register the default builtin
 * commands.  The caller is responsible for registering any builtins
 * it needs via exec_register_builtin() before execution begins.
 *
 * Must be called before the top frame is initialized.
 */
MIGA_API bool exec_get_flag_nobuiltins(const miga_exec_t *executor);
MIGA_API bool exec_set_flag_nobuiltins(miga_exec_t *executor, bool value);

MIGA_API bool exec_is_system_rc_filename_set(const miga_exec_t *executor);
MIGA_API const string_t *exec_get_system_rc_filename(const miga_exec_t *executor);
MIGA_API const char *exec_get_system_rc_filename_cstr(const miga_exec_t *executor);
MIGA_API bool exec_set_system_rc_filename(miga_exec_t *executor,
                                          const string_t *filename);
MIGA_API bool exec_set_system_rc_filename_cstr(miga_exec_t *executor,
                                               const char *filename);

MIGA_API bool exec_is_user_rc_filename_set(const miga_exec_t *executor);
MIGA_API const string_t *exec_get_user_rc_filename(const miga_exec_t *executor);
MIGA_API const char *exec_get_user_rc_filename_cstr(const miga_exec_t *executor);
MIGA_API bool exec_set_user_rc_filename(miga_exec_t *executor,
                                        const string_t *filename);
MIGA_API bool exec_set_user_rc_filename_cstr(miga_exec_t *executor,
                                             const char *filename);

/* ── Special parameters ──────────────────────────────────────────────────── */

MIGA_API int exec_get_last_exit_status(const miga_exec_t *executor);
MIGA_API bool exec_set_last_exit_status(miga_exec_t *executor, int status);

MIGA_API int exec_get_last_background_pid(const miga_exec_t *executor);
MIGA_API bool exec_set_last_background_pid(miga_exec_t *executor, int pid);

MIGA_API const string_t *exec_get_last_argument(const miga_exec_t *executor);
MIGA_API const char *exec_get_last_argument_cstr(const miga_exec_t *executor);
MIGA_API bool exec_set_last_argument(miga_exec_t *executor, const string_t *arg);
MIGA_API bool exec_set_last_argument_cstr(miga_exec_t *executor, const char *arg);

/* ============================================================================
 * Frame Access
 * ============================================================================ */

/**
 * Returns true once the top frame has been fully initialised and execution
 * has begun.  Before this point, pre-execution setters are still accepted.
 */
MIGA_API bool exec_is_top_frame_initialized(const miga_exec_t *executor);

/**
 * Get the current execution frame.
 *
 * The current frame is the innermost active frame (e.g. inside a function
 * call or dot-script).  Returns NULL if no execution has begun.
 */
MIGA_API miga_frame_t *exec_get_current_frame(const miga_exec_t *executor);

/* ============================================================================
 * Builtin Registration
 * ============================================================================
 *
 * A builtin receives the current execution frame (which provides access to
 * variables, positional parameters, traps, I/O streams, and the parent
 * executor for global state) and an argument list whose first element is
 * the builtin name.
 *
 * The miga_builtin_fn_t signature and miga_builtin_category_t enum are defined in
 * type_pub.h so they can be shared between the public API and
 * the internal builtin store.
 */

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
MIGA_API bool exec_register_builtin(miga_exec_t *executor, const string_t *name,
                                    miga_builtin_fn_t fn,
                                    miga_builtin_category_t category);
MIGA_API bool exec_register_builtin_cstr(miga_exec_t *executor, const char *name,
                                         miga_builtin_fn_t fn,
                                         miga_builtin_category_t category);

/**
 * Unregister a previously registered builtin command.
 *
 * @return true if the builtin was found and removed, false otherwise.
 */
MIGA_API bool exec_unregister_builtin(miga_exec_t *executor, const string_t *name);
MIGA_API bool exec_unregister_builtin_cstr(miga_exec_t *executor, const char *name);

/**
 * Check whether a builtin with the given name is registered.
 */
MIGA_API bool exec_has_builtin(const miga_exec_t *executor, const string_t *name);
MIGA_API bool exec_has_builtin_cstr(const miga_exec_t *executor, const char *name);

/**
 * Look up a registered builtin by name.
 *
 * @return The function pointer, or NULL if no builtin is registered under
 *         that name.
 */
MIGA_API miga_builtin_fn_t exec_get_builtin(const miga_exec_t *executor,
                                       const string_t *name);
MIGA_API miga_builtin_fn_t exec_get_builtin_cstr(const miga_exec_t *executor,
                                            const char *name);

/**
 * Get the category of a registered builtin.
 *
 * @param executor      The executor.
 * @param name          The builtin name.
 * @param category_out  If non-NULL and the builtin exists, receives the
 *                      category.
 * @return true if the builtin exists, false otherwise.
 */
MIGA_API bool exec_get_builtin_category(const miga_exec_t *executor,
                                        const string_t *name,
                                        miga_builtin_category_t *category_out);
MIGA_API bool exec_get_builtin_category_cstr(const miga_exec_t *executor,
                                             const char *name,
                                             miga_builtin_category_t *category_out);

/* ============================================================================
 * Command Resolution
 * ============================================================================
 *
 * Resolves a command name through the full lookup chain, in the order
 * prescribed by POSIX:
 *
 *   1. Special builtins
 *   2. Shell functions
 *   3. Regular builtins
 *   4. External commands (PATH search)
 *
 * Aliases are not included in the default resolution order because alias
 * expansion happens at the parser level before command lookup.  However,
 * the `type` builtin needs to report aliases, so a separate flag controls
 * whether aliases are checked first.
 *
 * This is the mechanism by which `command -v`, `command -V`, and `type`
 * determine what a command name refers to.
 */

/**
 * The kind of entity a command name resolved to.
 */
typedef enum exec_command_type_t
{
    EXEC_COMMAND_NOT_FOUND,       /**< Name did not resolve to anything          */
    EXEC_COMMAND_ALIAS,           /**< Alias (only when aliases are checked)     */
    EXEC_COMMAND_SPECIAL_BUILTIN, /**< POSIX special builtin                    */
    EXEC_COMMAND_FUNCTION,        /**< Shell function                            */
    EXEC_COMMAND_REGULAR_BUILTIN, /**< POSIX regular builtin                    */
    EXEC_COMMAND_EXTERNAL         /**< External command found on PATH            */
} exec_command_type_t;

/**
 * Result of resolving a command name.
 */
typedef struct exec_command_resolution_t
{
    exec_command_type_t type; /**< What the name resolved to                  */
    string_t *path;           /**< For EXTERNAL: the resolved filesystem path.
                                   For ALIAS: the alias value.
                                   NULL for builtins and functions.
                                   Caller frees.                              */
} exec_command_resolution_t;

/**
 * Resolve a command name through the standard lookup chain.
 *
 * Aliases are not checked unless @p check_aliases is true.  This matches
 * the POSIX distinction: `command -v` does not report aliases, but `type`
 * does.
 *
 * @param executor       The executor.
 * @param name           The command name to resolve.
 * @param check_aliases  If true, check aliases before everything else.
 * @return Resolution result.  Caller must free result.path if non-NULL.
 */
MIGA_API exec_command_resolution_t exec_resolve_command(const miga_exec_t *executor,
                                                        const string_t *name,
                                                        bool check_aliases);
MIGA_API exec_command_resolution_t exec_resolve_command_cstr(const miga_exec_t *executor,
                                                             const char *name,
                                                             bool check_aliases);

/* ============================================================================
 * Execution Setup
 * ============================================================================ */

/**
 * Prepare the executor for interactive use.
 *
 * Initialises the top frame (if not already done), installs signal handlers,
 * and sources RC files according to the current configuration.
 *
 * Even if this function returns MIGA_EXEC_STATUS_ERROR (e.g. an RC file failed to
 * source), the executor remains usable.
 *
 * @return MIGA_EXEC_STATUS_OK on success, MIGA_EXEC_STATUS_ERROR on failure.
 */
MIGA_API miga_exec_status_t exec_setup_interactive(miga_exec_t *executor);

/**
 * Prepare the executor for non-interactive (script / -c) use.
 *
 * Initialises the top frame and sources the system RC file for login shells.
 *
 * @return MIGA_EXEC_STATUS_OK on success, MIGA_EXEC_STATUS_ERROR on failure.
 */
MIGA_API miga_exec_status_t exec_setup_noninteractive(miga_exec_t *executor);

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
MIGA_API miga_exec_status_t exec_execute_stream(miga_exec_t *executor, FILE *fp);

/**
 * Used primarily for the built-in `source` / `.` command to execute a
 * file in the current context.  This is a simplified version of
 * exec_execute_stream() that does not support interactive features.
 * State does not persist across multiple calls to this function, so
 * it is not suitable for interactive use.
 */
MIGA_API miga_exec_status_t exec_execute_stream_once(miga_exec_t *executor, FILE *fp);

/**
 * Execute commands from a stream, using the given filename for error messages
 * instead of "stdin".
 *
 * Intended for non-interactive use (scripts, sourced files).
 */
MIGA_API miga_exec_status_t exec_execute_named_stream(miga_exec_t *executor, FILE *fp,
                                                 const char *filename);

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
MIGA_API miga_exec_result_t exec_execute_command_string(miga_exec_t *executor,
                                                   const char *command);

/* ── Partial / incremental string execution ──────────────────────────────── */

/**
 * Opaque state for incremental command string execution.
 *
 * Allows a caller implementing a custom REPL to feed input line-by-line,
 * with the executor reporting when more input is needed (e.g. unclosed
 * quotes or compound commands).
 *
 * Allocate with exec_create_parse_session() (preferred) or allocate
 * exec_get_parse_session_size() bytes and zero-initialise before the first call.
 * The executor populates it with continuation state after each call.
 */
typedef struct parse_session_t parse_session_t;

/* Create a new parse session.
 * The EXECUTOR argument is used to determine the alias table for alias expansion
 * during parsing. If it is provided, the session will maintain a pointer to the
 * executor's alias state; the executor must not be destroyed when using this session.
 * If EXECUTOR is NULL, the session will create its own alias state that is independent
 * of any executor.
 */
MIGA_API parse_session_t *exec_create_parse_session(miga_exec_t *executor);

/**
 * Return the size of parse_session_t so callers can allocate it
 * without including parse_session.h.
 */
MIGA_API size_t exec_get_parse_session_size(void);

/**
 * Reset a parse session to prepare for the next command.
 *
 * This clears the lexer, accumulated tokens, and resets the tokenizer
 * for the next command, but keeps the session allocated for reuse.
 * The filename and line counter are NOT reset (they keep incrementing).
 * The alias store is NOT reset (aliases persist across commands).
 */
MIGA_API void exec_reset_parse_session(parse_session_t *session);

/**
 * Fully reset a session, including destroying and recreating the tokenizer.
 * and the alias store.
 * If an executor is provided, its alias store will replace the existing one; otherwise the
 * existing alias store will be cleared if owned, or a new one will be created if not owned.
 * The filename and line counter are NOT reset (they keep incrementing).
 *
 * Used after SIGINT or other hard interrupts where any buffered
 * compound-command state in the tokenizer must be discarded.
 *
 * @param session  The session.
 * @param executor Executor whose alias store will replace the existing one (may be NULL).
 */
MIGA_API void exec_hard_reset_parse_session(parse_session_t *session, miga_exec_t *executor);

/* return val may be NULL */
MIGA_API const char *exec_get_parse_session_filename_cstr(const parse_session_t *session);
/* return value of zero indicates line numbers are not being tracked */
MIGA_API size_t exec_get_parse_session_line_number(const parse_session_t *session);
/* 'filename' may be NULL */
MIGA_API void exec_set_parse_session_filename_cstr(parse_session_t *session, const char *filename);
/* set to zero to stop counting line numbers, or set to > 0 to indicate a specific line number. */
MIGA_API void exec_set_parse_session_line_number(parse_session_t *session, size_t line_number);

/**
 * Destroy a parse session allocated with exec_create_parse_session() and set
 * the pointer to NULL.
 */
MIGA_API void exec_destroy_parse_session(parse_session_t **session);

/**
 * Execute a command string incrementally. The 'session' argument
 * maintains state across calls to allow feeding input line-by-line, with the
 * executor reporting when more input is needed (e.g. unclosed quotes or compound commands).
 *
 * The caller should create a session with exec_create_parse_session() before the first call.
 *
 * If a caller provides a line number greater than 0, the executor will use it as the
 * starting line number. In subsequent calls with the same session, the executor will
 * increment the line number stored in the session by the number of lines in the command text
 * whenever a non-zero line number is not provided.
 *
 * If MIGA_EXEC_STATUS_ERROR is returned, the caller can get the error message with exec_get_error()
 * and can get the current filename and line number from the 'session'.
 *
 * @param executor          The executor.
 * @param command           The command text for this chunk.
 * @param filename          Source filename for error messages (may be NULL).
 * @param line_number       Starting line number (0 = not provided).
 * @param session           Parse session (caller creates with
 *                          exec_create_parse_session() before first call).
 * @return MIGA_EXEC_STATUS_OK if the command completed, MIGA_EXEC_STATUS_INCOMPLETE if more
 *         input is needed, or an error / control-flow status.
 */
MIGA_API miga_exec_status_t exec_execute_command_string_partial(miga_exec_t *executor,
                                                           const string_t *command,
                                                           const string_t *filename,
                                                           size_t line_number,
                                                           parse_session_t *session);

MIGA_API miga_exec_status_t exec_execute_command_string_partial_cstr(miga_exec_t *executor,
                                                                const char *command,
                                                                const char *filename,
                                                                size_t line_number,
                                                       parse_session_t *session);

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
typedef line_edit_status_t (*line_editor_fn_t)(const char *prompt,
                                               string_t **line_out,
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
MIGA_API miga_exec_status_t
exec_execute_stream_with_line_editor(miga_exec_t *executor, FILE *fp,
                                     line_editor_fn_t line_editor_fn,
                                     void *line_editor_user_data);

/* ============================================================================
 * Global State Queries
 * ============================================================================ */

/* ── Exit status ─────────────────────────────────────────────────────────── */

MIGA_API int exec_get_exit_status(const miga_exec_t *executor);
MIGA_API void exec_set_exit_status(miga_exec_t *executor, int status);

/**
 * Request that the executor exit.
 *
 * Sets the exit status and marks the executor as exiting.  The next time the
 * executor would normally request more input at top level — whether inside
 * exec_execute_stream(), exec_execute_stream_with_line_editor(), or on the
 * next call to exec_execute_command_string_partial() — it will stop parsing
 * new input and return MIGA_EXEC_STATUS_EXIT.
 *
 * This is the mechanism by which the 'exit' builtin communicates its intent
 * to the executor.  The builtin should also set MIGA_FRAME_FLOW_TOP on its frame
 * to unwind any nested frames before the executor checks this flag.
 *
 * @param executor  The executor.
 * @param status    The exit status to report (becomes $?).
 */
MIGA_API void exec_request_exit(miga_exec_t *executor, int status);

/**
 * Check whether an exit has been requested.
 *
 * @return true if exec_request_exit() has been called and the executor has
 *         not yet returned MIGA_EXEC_STATUS_EXIT to its caller.
 */
MIGA_API bool exec_is_exit_requested(const miga_exec_t *executor);

/* ── Error message ───────────────────────────────────────────────────────── */

MIGA_API const string_t *exec_get_error(const miga_exec_t *executor);
MIGA_API const char *exec_get_error_cstr(const miga_exec_t *executor);

MIGA_API void exec_set_error(miga_exec_t *executor, const string_t *message);
MIGA_API void exec_set_error_cstr(miga_exec_t *executor, const char *message);
MIGA_API void exec_set_error_printf(miga_exec_t *executor, const char *format, ...);
MIGA_API void exec_clear_error(miga_exec_t *executor);

/* ── Pipe statuses (PIPESTATUS / pipefail) ───────────────────────────────── */

MIGA_API int exec_get_pipe_status_count(const miga_exec_t *executor);
MIGA_API const int *exec_get_pipe_statuses(const miga_exec_t *executor);
MIGA_API void exec_reset_pipe_statuses(miga_exec_t *executor);

/* ── Prompts ─────────────────────────────────────────────────────────────── */

/**
 * Get the raw PS1 value from the variable store.
 * Returns a default if PS1 is not set.  Caller frees.
 */
MIGA_API string_t *exec_get_ps1(const miga_exec_t *executor);
MIGA_API char *exec_get_ps1_cstr(const miga_exec_t *executor);

/**
 * Get PS1 with all prompt expansions applied.  Caller frees.
 */
MIGA_API string_t *exec_get_rendered_ps1(const miga_exec_t *executor);
MIGA_API char *exec_get_rendered_ps1_cstr(const miga_exec_t *executor);

/**
 * Get the raw PS2 value.  PS2 is never expanded.  Caller frees.
 */
MIGA_API string_t *exec_get_ps2(const miga_exec_t *executor);
MIGA_API char *exec_get_ps2_cstr(const miga_exec_t *executor);

/* ── Signal notification ─────────────────────────────────────────────────── */

/**
 * Signature of a signal-check callback.
 *
 * Long-running builtins (e.g. read, wait) need a way to detect that a
 * signal such as SIGINT was delivered during their execution.  Rather
 * than exposing the executor's internal signal state, the executor
 * provides a callback the builtin can poll.
 *
 * @param signal_number  The signal that was received.
 * @param user_data      Opaque context provided at registration time.
 */
typedef void (*exec_signal_callback_t)(int signal_number, void *user_data);

/**
 * Register a callback to be invoked when a signal is received during
 * execution.
 *
 * Only one callback may be active at a time.  Registering a new callback
 * replaces the previous one.  Pass NULL to clear the callback.
 *
 * The callback is invoked synchronously from the executor's signal-safe
 * bookkeeping path, so it must be async-signal-safe or the caller must
 * arrange for deferred handling (e.g. setting a flag that the builtin
 * polls).
 *
 * @param executor   The executor.
 * @param callback   The callback function, or NULL to clear.
 * @param user_data  Opaque context passed to the callback.
 */
MIGA_API void exec_set_signal_callback(miga_exec_t *executor,
                                       exec_signal_callback_t callback,
                                       void *user_data);

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
 * Only functional in MIGA_POSIX_API mode; no-op otherwise.
 */
MIGA_API void exec_reap_background_jobs(miga_exec_t *executor, bool notify);

/* ── Enumeration ─────────────────────────────────────────────────────────── */

MIGA_API size_t exec_get_job_count(const miga_exec_t *executor);
MIGA_API size_t exec_get_job_ids(const miga_exec_t *executor, int *job_ids,
                                 size_t max_jobs);
MIGA_API int exec_get_current_job_id(const miga_exec_t *executor);
MIGA_API int exec_get_previous_job_id(const miga_exec_t *executor);

/* ── Per-job queries ─────────────────────────────────────────────────────── */

MIGA_API exec_job_state_t exec_job_get_state(const miga_exec_t *executor,
                                             int job_id);

MIGA_API const string_t *exec_job_get_command(const miga_exec_t *executor,
                                              int job_id);
MIGA_API const char *exec_job_get_command_cstr(const miga_exec_t *executor,
                                               int job_id);

MIGA_API bool exec_job_is_background(const miga_exec_t *executor, int job_id);

#ifdef MIGA_POSIX_API
MIGA_API pid_t exec_job_get_pgid(const miga_exec_t *executor, int job_id);
#else
MIGA_API int exec_job_get_pgid(const miga_exec_t *executor, int job_id);
#endif

/* ── Per-process queries within a job ────────────────────────────────────── */

MIGA_API size_t exec_job_get_process_count(const miga_exec_t *executor, int job_id);
MIGA_API exec_job_state_t exec_job_get_process_state(const miga_exec_t *executor,
                                                     int job_id, size_t index);
MIGA_API int exec_job_get_process_exit_status(const miga_exec_t *executor,
                                              int job_id, size_t index);

#ifdef MIGA_POSIX_API
MIGA_API pid_t exec_job_get_process_pid(const miga_exec_t *executor, int job_id,
                                        size_t index);
#elif defined(MIGA_UCRT_API)
MIGA_API int exec_job_get_process_pid(const miga_exec_t *executor,
                                      int job_id, size_t index);
MIGA_API uintptr_t exec_job_get_process_handle(const miga_exec_t *executor,
                                               int job_id, size_t index);
#else
MIGA_API int exec_job_get_process_pid(const miga_exec_t *executor, int job_id,
                                      size_t index);
#endif

/* ── Job actions ─────────────────────────────────────────────────────────── */

/**
 * Bring a job to the foreground.
 * If @p cmd is provided and if the function returns true indicating
 * that a job was foregrounded, @p cmd will receive a newly allocated copy of
 * the command string of the job that is being foregrounded.  This is
 * necessary because foregrounded jobs are removed from the job list, so their
 * command strings are freed and no longer accessible.
 */
MIGA_API bool exec_job_foreground(miga_exec_t *executor, int job_id,
                                  string_t **cmd);
MIGA_API bool exec_job_foreground_cstr(miga_exec_t *executor, int job_id,
                                       char **cmd);

MIGA_API bool exec_job_background(miga_exec_t *executor, int job_id);
MIGA_API bool exec_job_kill(miga_exec_t *executor, int job_id, int sig);

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
MIGA_API int exec_parse_job_id(const miga_exec_t *executor, const string_t *spec);
MIGA_API int exec_parse_job_id_cstr(const miga_exec_t *executor, const char *spec);

/**
 * Print a single job.
 *
 * @return true if the job was found and printed.
 */
MIGA_API bool exec_print_job_by_id(const miga_exec_t *executor, int job_id,
                                   exec_jobs_format_t format,
                                   FILE *output);

/**
 * Print all jobs with a specified verbosity.
 */
MIGA_API void exec_print_all_jobs(const miga_exec_t *executor,
                                  exec_jobs_format_t format,
                                  FILE *output);

/**
 * Print all jobs in verbose format.  This is a debugging aid
 * that dumps all availabe info.  The output format is not guaranteed to be stable.
 */

MIGA_API void exec_print_jobs_verbose(const miga_exec_t *executor, FILE *output);

/**
 * Check whether any jobs exist.
 */
MIGA_API bool exec_has_jobs(const miga_exec_t *executor);

/* ── Waiting ─────────────────────────────────────────────────────────────── */

/**
 * Wait for a job to complete or be stopped.
 *
 * Blocks until the specified job changes to EXEC_JOB_DONE,
 * EXEC_JOB_TERMINATED, or EXEC_JOB_STOPPED.  If the job is already in
 * one of those states, returns immediately.
 *
 * If a signal interrupt is received while waiting (and a signal callback
 * is registered), the callback is invoked and the function returns -1
 * with the job still running.
 *
 * @param executor  The executor.
 * @param job_id    The job to wait for.
 * @return The exit status of the last process in the job's pipeline,
 *         or -1 if the job was not found or waiting was interrupted.
 */
MIGA_API int exec_wait_for_job(miga_exec_t *executor, int job_id);

/**
 * Wait for a specific process to complete.
 *
 * Blocks until the process with the given PID exits.  The process must
 * belong to a job known to the executor.  If the PID is not found among
 * any active jobs, returns -1 immediately.
 *
 * If a signal interrupt is received while waiting (and a signal callback
 * is registered), the callback is invoked and the function returns -1
 * with the process potentially still running.
 *
 * @param executor  The executor.
 * @param pid       The process ID to wait for.
 * @return The exit status of the process, or -1 if not found or
 *         waiting was interrupted.
 */
MIGA_API int exec_wait_for_pid(miga_exec_t *executor, int pid);

/**
 * Wait for all background jobs to complete.
 *
 * Blocks until every job known to the executor reaches EXEC_JOB_DONE
 * or EXEC_JOB_TERMINATED.  If no jobs are active, returns immediately.
 *
 * If a signal interrupt is received while waiting (and a signal callback
 * is registered), the callback is invoked and the function returns -1
 * with some jobs potentially still running.
 *
 * @param executor  The executor.
 * @return The exit status of the last job that completed, or 0 if there
 *         were no jobs, or -1 if waiting was interrupted.
 */
MIGA_API int exec_wait_for_all(miga_exec_t *executor);

MIGA_EXTERN_C_END

#endif /* MIGA_EXEC_H */

#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "ast.h"
#include "string_t.h"
#include "expander.h"
#include "alias_store.h"
#include "variable_store.h"
#include "positional_params.h"
#include "func_store.h"
#include "sig_act.h"
#include "job_store.h"
#if defined(POSIX_API) || defined(UCRT_API)
#include "fd_table.h"
#endif
#include "trap_store.h"
#include <stdbool.h>


/* ============================================================================
 * Executor Status (return codes)
 * ============================================================================ */

typedef enum
{
    EXEC_OK = 0,        // successful execution
    EXEC_ERROR,         // error during execution
    EXEC_NOT_IMPL,      // feature not yet implemented
    EXEC_OK_INTERNAL_FUNCTION_STORED, // internal: function node moved to store, don't free
} exec_status_t;

/* ============================================================================
 * Executor Context
 * ============================================================================ */

typedef struct
{
    bool allexport; // -a
    bool errexit;   // -e
    bool ignoreeof; // no flag
    bool noclobber; // -C
    bool noglob;    // -f
    bool noexec;    // -n
    bool nounset;   // -u
    bool pipefail;  // no flag
    bool verbose;   // -v
    bool vi;
    bool xtrace; // -x
} exec_opt_flags_t;

/**
 * The exec_t structure maintains the execution state for a shell session,
 * including exit status tracking, error reporting, variables, and special
 * POSIX shell variables.
 */
typedef struct exec_t
{
    struct exec_t *parent; // NULL if top-level, else points to parent environment
    bool is_subshell;          // True if this is a subshell environment
    bool is_interactive;       // Whether shell is interactive
    bool is_login_shell;       // Whether this is a login shell

    // Working directory as set by cd
    string_t *working_directory;   // POSIX getcwd(), UCRT _getcwd(), ISO_C no standard way

    // File creation mask set by umask.
    // These are the permissions that should be masked off when creating new files.
#ifdef POSIX_API
    mode_t file_creation_mask;  // return value of umask()
#elifdef UCRT_API
    int file_creation_mask;     // return value of _umask()
#else
    // ISO_C does not define umask
#endif

    // File size limit as set by ulimit
#ifdef POSIX_API
    rlim_t file_size_limit;     // getrlimit(RLIMIT_FSIZE)
#else
    // UCRT_API and ISO_C_API do  not define file size limits
#endif

    // Current traps set by trap
    trap_store_t *traps;
    // Original signal dispositions (to restore after traps)
    sig_act_t *original_signals;

    // Shell parameters that are set by variable assignment and shell
    // parameters that are set from the environment inherited by the shell
    // when it begins.
    variable_store_t *variables;
    positional_params_t *positional_params; // Derive $@, $*, $1, $2, ...

    // Shell parameters that are special built-ins
    bool last_exit_status_set;
    int last_exit_status;
    bool last_background_pid_set; // $! - PID of last background command
#ifdef POSIX_API
    pid_t last_background_pid;
#else // UCRT_API and ISO_C
    int last_background_pid;
#endif
    bool shell_pid_set;           // $$ - PID of the shell process
#ifdef POSIX_API
    pid_t shell_pid;
#else // UCRT_API and ISO_C
    int shell_pid;
#endif
    bool last_argument_set;       // $_ - Last argument of previous command
    string_t *last_argument;
    string_t *shell_name;         // $0 - Name of the shell or shell script

    // Shell functions
    func_store_t *functions;

    // Options turned on at invocation or by set
    bool opt_flags_set; // $- - Current shell option flags (e.g., "ix" for interactive, xtrace)
    exec_opt_flags_t opt;

    // Background jobs and their associated process IDs, and process IDs of
    // child processes created to execute asynchronous AND-OR lists while job
    // control is disabled; together these process IDs constitute the process
    // IDs "known to this shell environment".
    // Job control
    job_store_t *jobs;        // Background jobs
    bool job_control_enabled; // Whether job control is active
#ifdef POSIX_API
    pid_t pgid;      // Process group ID for job control
#else
    // No PGID on non-POSIX systems
#endif

#if defined(POSIX_API) || defined(UCRT_API)
    // Open file descriptors (for managing redirections)
    fd_table_t *open_fds; // Track which FDs are open and their state
    int next_fd;          // For allocating new FDs in redirections
#else
    // There are no file descriptors in ISO C
#endif

    // Shell aliases
    alias_store_t *aliases;

    // Error reporting
    string_t *error_msg;
} exec_t;

typedef struct
{
    // Start-up environment
    int argc;
    char *const *argv;
    char *const *envp;

    // Flags
    exec_opt_flags_t opt;
} exec_cfg_t;

/* ============================================================================
 * Executor Lifecycle Functions
 * ============================================================================ */

/**
 * Create a new executor.
 */
exec_t *exec_create_from_cfg(exec_cfg_t *cfg);
exec_t *exec_create_from_parent(exec_t *parent);

/**
 * Destroy an executor and free all associated memory.
 * Safe to call with nullptr.
 */
void exec_destroy(exec_t **executor);

/* ============================================================================
 * Execution Functions
 * ============================================================================ */

/**
 * Execute an AST.
 *
 * @param executor The executor context
 * @param root The root AST node to execute
 *
 * @return EXEC_OK on success, EXEC_ERROR on error, EXEC_NOT_IMPL for unsupported node types
 */
exec_status_t exec_execute(exec_t *executor, const ast_node_t *root);

/**
 * Execute a command list.
 */
exec_status_t exec_execute_command_list(exec_t *executor, const ast_node_t *node);

/**
 * Execute an and/or list.
 */
exec_status_t exec_execute_andor_list(exec_t *executor, const ast_node_t *node);

/**
 * Execute a pipeline.
 */
exec_status_t exec_execute_pipeline(exec_t *executor, const ast_node_t *node);

/**
 * Execute a simple command.
 */
exec_status_t exec_execute_simple_command(exec_t *executor, const ast_node_t *node);

/**
 * Execute a redirected command wrapper.
 */
exec_status_t exec_execute_redirected_command(exec_t *executor, const ast_node_t *node);

/**
 * Execute an if clause.
 */
exec_status_t exec_execute_if_clause(exec_t *executor, const ast_node_t *node);

/**
 * Execute a while clause.
 */
exec_status_t exec_execute_while_clause(exec_t *executor, const ast_node_t *node);

/**
 * Execute an until clause.
 */
exec_status_t exec_execute_until_clause(exec_t *executor, const ast_node_t *node);

/**
 * Execute a for clause.
 */
exec_status_t exec_execute_for_clause(exec_t *executor, const ast_node_t *node);

/**
 * Execute a case clause.
 */
exec_status_t exec_execute_case_clause(exec_t *executor, const ast_node_t *node);

/**
 * Execute a subshell.
 */
exec_status_t exec_execute_subshell(exec_t *executor, const ast_node_t *node);

/**
 * Execute a brace group.
 */
exec_status_t exec_execute_brace_group(exec_t *executor, const ast_node_t *node);

/**
 * Execute a function definition.
 */
exec_status_t exec_execute_function_def(exec_t *executor, const ast_node_t *node);

/* ============================================================================
 * Visitor Pattern Support
 * ============================================================================ */

/**
 * Visitor callback function type.
 * Returns true to continue traversal, false to stop.
 */
typedef bool (*ast_visitor_fn)(const ast_node_t *node, void *user_data);

/**
 * Traverse an AST in pre-order, calling the visitor function for each node.
 *
 * @param root The root node to start traversal from
 * @param visitor The visitor function to call for each node
 * @param user_data User data to pass to the visitor function
 *
 * @return true if traversal completed, false if stopped early
 */
bool ast_traverse(const ast_node_t *root, ast_visitor_fn visitor, void *user_data);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Get the last exit status.
 */
int exec_get_exit_status(const exec_t *executor);

/**
 * Set the exit status.
 */
void exec_set_exit_status(exec_t *executor, int status);

/**
 * Get the error message from the last failed operation.
 * Returns NULL if no error.
 */
const char *exec_get_error(const exec_t *executor);

/**
 * Set an error message.
 */
void exec_set_error(exec_t *executor, const char *format, ...);

/**
 * Clear the error state.
 */
void exec_clear_error(exec_t *executor);

/**
 * Enable or disable dry-run mode.
 * In dry-run mode, commands are validated but not executed.
 */
void exec_set_dry_run(exec_t *executor, bool dry_run);

/* ============================================================================
 * Expander Callbacks
 * ============================================================================ */

/**
 * Command substitution callback for the expander.
 * Executes a command and returns its output.
 *
 * @param command The command string to execute
 * @param exec_ctx Pointer to the executor context
 * @param user_data Pointer to optional user context
 * @return The output of the command as a newly allocated string_t (caller must free),
 *         or NULL on error
 */
string_t *exec_command_subst_callback(const string_t *command, void *exec_ctx, void *user_data);

/**
 * Pathname expansion (glob) callback for the expander.
 * Platform behavior:
 * - POSIX_API: uses POSIX glob() for pathname expansion.
 * - UCRT_API: uses _findfirst/_findnext from <io.h> for wildcard matching.
 * - ISO_C: no implementation; returns NULL so the expander preserves the literal.
 *
 * @param pattern The glob pattern to expand
 * @param user_data Pointer to the shell_t context (opaque to this function)
 * @return On success with matches: a newly allocated list of filenames
 *         (caller must free with string_list_destroy). On no matches or error:
 *         returns NULL, signaling the expander to leave the pattern unexpanded.
 */
string_list_t *exec_pathname_expansion_callback(const string_t *pattern, void *user_data);

#endif


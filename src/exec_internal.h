#ifndef EXEC_INTERNAL_H
#define EXEC_INTERNAL_H

#include <stdbool.h>
#ifdef POSIX_API
#include <sys/types.h>
#include <sys/resource.h>
#endif
#include "ast.h"
#include "string_t.h"
#include "trap_store.h"
#include "sig_act.h"
#include "variable_store.h"
#include "positional_params.h"
#include "fd_table.h"
#include "alias_store.h"
#include "func_store.h"
#include "job_store.h"


/* ============================================================================
 * Executor Status (return codes)
 * ============================================================================ */

typedef enum
{
    EXEC_OK = 0,                      // successful execution
    EXEC_ERROR,                       // error during execution
    EXEC_NOT_IMPL,                    // feature not yet implemented
    EXEC_OK_INTERNAL_FUNCTION_STORED, // internal: function node moved to store, don't free
    EXEC_RETURN,                      // internal: 'return' executed
    EXEC_BREAK,                       // internal: 'break' executed
    EXEC_CONTINUE,                    // internal: 'continue' executed
    EXEC_EXIT                         // internal: 'exit' executed
} exec_status_t;

/* ============================================================================
 *
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
    bool is_subshell;      // True if this is a subshell environment
    bool is_interactive;   // Whether shell is interactive
    bool is_login_shell;   // Whether this is a login shell

    // Working directory as set by cd
    string_t *working_directory; // POSIX getcwd(), UCRT _getcwd(), ISO_C no standard way

    // File creation mask set by umask.
    // These are the permissions that should be masked off when creating new files.
#ifdef POSIX_API
    mode_t umask; // return value of umask()
#elifdef UCRT_API
    int umask; // return value of _umask()
#else
    // ISO_C does not have a meaningful umask
    int umask; // dummy placeholder
#endif

    // File size limit as set by ulimit
#ifdef POSIX_API
    rlim_t file_size_limit; // getrlimit(RLIMIT_FSIZE)
#else
    // UCRT_API and ISO_C_API do  not define file size limits
#endif

    // Current traps set by trap
    trap_store_t *traps;
    // Original signal dispositions (to restore after traps)
    sig_act_store_t *original_signals;

    // Shell parameters that are set by variable assignment and shell
    // parameters that are set from the environment inherited by the shell
    // when it begins.
    variable_store_t *variables;
    positional_params_t *positional_params; // Derive $@, $*, $1, $2, ...

    // Command-line arguments passed to the shell at startup
    // $? - Exit status of last command
    int last_exit_status;
    bool last_exit_status_set;

    // $! - PID of last background command
#ifdef POSIX_API
    pid_t last_background_pid;
#else // UCRT_API and ISO_C
    int last_background_pid;
#endif
    bool last_background_pid_set;

    // $$ - PID of the shell process
#ifdef POSIX_API
    pid_t shell_pid;
#else // UCRT_API and ISO_C
    int shell_pid;
#endif
    bool shell_pid_set;

    // $_ - Last argument of previous command
    string_t *last_argument;
    bool last_argument_set;

    // $0 - Name of the shell or shell script
    string_t *shell_name;

    // Shell functions
    func_store_t *functions;

    // $- - Current shell option flags (e.g., "ix" for interactive, xtrace)
    exec_opt_flags_t opt;
    bool opt_flags_set;

#if !defined(POSIX_API) && !defined(UCRT_API)
    // ISO C can't pass environment variables via envp, so before
    // calling system() to execute external commands, we write this
    // environment to a temporary file and set ENV_FILE to its path.
    string_t *env_file_path;
#endif

    // Background jobs and their associated process IDs, and process IDs of
    // child processes created to execute asynchronous AND-OR lists while job
    // control is disabled; together these process IDs constitute the process
    // IDs "known to this shell environment".
    // Job control
    job_store_t *jobs; // Background jobs
#ifdef POSIX_API
    pid_t pgid; // Process group ID for job control
#else
    int dummy_pgid; // Placeholder, no job control in UCRT/ISO C
#endif
    bool job_control_enabled; // Whether job control is active

#if defined(POSIX_API) || defined(UCRT_API)
    // Open file descriptors (for managing redirections)
    fd_table_t *open_fds; // Track which FDs are open and their state
    int next_fd;          // For allocating new FDs in redirections
#else
    // There are no file descriptors in ISO C
    void *dummy_open_fds;
    int dummy_next_fd;
#endif

    // Shell aliases
    alias_store_t *aliases;

    // Error reporting
    string_t *error_msg;

    // For multi-frame returns
    int return_count; // Number of frames to break/continue/return through
} exec_t;

typedef struct saved_fd_t
{
    int fd;        // the FD being redirected
    int backup_fd; // duplicate of original FD
} saved_fd_t;


/* ============================================================================
 * Internal Cross-Module Function Declarations
 * ============================================================================ */

/* Forward declaration of ast_node_t */
struct ast_node_t;

/**
 * Execute an AST node (internal recursion function).
 * Used by exec_control.c and exec_redirect.c to execute child nodes.
 */
exec_status_t exec_execute(exec_t *executor, const struct ast_node_t *node);

/**
 * Set an error message (internal utility).
 * Used by various exec modules to report errors.
 */
void exec_set_error(exec_t *executor, const char *format, ...);

/**
 * Set the exit status (internal utility).
 */
void exec_set_exit_status(exec_t *executor, int status);

#endif /* EXEC_INTERNAL_H */


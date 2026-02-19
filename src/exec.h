#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <stdio.h>
#include "alias_store.h"
#include "ast.h"
// #include "exec_command.h"
#include "exec_expander.h"
#include "exec_frame.h"
#include "positional_params.h"
#include "string_t.h"
#include "string_list.h"
#include "variable_store.h"

#ifdef POSIX_API
#include <sys/types.h>
#include <sys/resource.h>
#endif
#include "func_store.h"
#include "sig_act.h"
#include "job_store.h"
#if defined(POSIX_API) || defined(UCRT_API)
#include "fd_table.h"
#endif
#include "trap_store.h"
#include <stdbool.h>


#ifdef POSIX_API
#define EXEC_SYSTEM_RC_PATH "/etc/mgshrc"
#define EXEC_USER_RC_NAME ".mgshrc"
#define EXEC_RC_IN_XDG_CONFIG_HOME
#elifdef UCRT_API
#define EXEC_USER_RC_NAME "mgshrc"
#define EXEC_RC_IN_LOCAL_APP_DATA
#else
// ISO C cannot rely on any paths existing, or on any filename conventions.
// So fallback to presuming FAT-12 style names being valid (a practical worst case)
// and no directory structure.
#define EXEC_USER_RC_NAME "MGSH.RC"
#define EXEC_RC_IN_CURRENT_DIRECTORY
#endif

/* ============================================================================
 * Executor State (Singleton)
 * ============================================================================ */

struct exec_opt_flags_t
{
    bool allexport; // set -a
    bool errexit;   // set -e
    bool ignoreeof; // set -I
    bool noclobber; // set -C
    bool noglob;    // set -f
    bool noexec;    // set -n
    bool nounset;   // set -u
    bool pipefail;  // set -o pipefail
    bool verbose;   // set -v
    bool vi;        // set -o vi
    bool xtrace;    // set -x
};


#define EXEC_CFG_FALLBACK_ARGV0 "mgsh"
#define EXEC_CFG_FALLBACK_OPT_FLAGS_INIT \
    {                                      \
        .allexport = false,                \
        .errexit = false,                  \
        .ignoreeof = false,                \
        .noclobber = false,                \
        .noglob = false,                   \
        .noexec = false,                   \
        .nounset = false,                  \
        .pipefail = false,                 \
        .verbose = false,                  \
        .vi = false,                       \
        .xtrace = false,                   \
    }

/**
 * Holds override info for initializing an executor.
 * For information not provided,
 * info from POSIX, UCRT, or ISO C API calls will be used,
 * or as a last resort, hardcoded FALLBACK values.
 */
struct exec_cfg_t
{
    /* Start-up Environment */
    bool argv_set;
    int argc; /* May be zero. */
    char *const *argv; /* NULL iff argc is zero. Fallback will be used for argv[0]. */

    bool envp_set;
    char *const *envp; /* If !envp_set, this is NULL and envp will come from getopt(). */

    /* Shell identity overrides */
    bool shell_name_set;
    const char *shell_name; /* Overrides argv[0]-derived shell name */

    bool shell_args_set;
    string_list_t *shell_args; /* Overrides argv[1..] derived args */

    bool env_vars_set;
    string_list_t *env_vars; /* Overrides envp-derived environment list */

    /* Flags */
    bool opt_flags_set;
    struct exec_opt_flags_t opt; /* If !opt_flags_set, fallback is used. */

    bool is_interactive_set;
    bool is_interactive;
    bool is_login_shell_set;
    bool is_login_shell;

    bool job_control_enabled_set;
    bool job_control_enabled;

    /* Working directory override */
    bool working_directory_set;
    const char *working_directory;

    /* File permissions */
    bool umask_set;
#ifdef POSIX_API
    mode_t umask;
    bool file_size_limit_set;
    rlim_t file_size_limit;
#else
    int umask;
#endif

    /* Special parameters */
    bool last_exit_status_set;
    int last_exit_status;
    bool last_background_pid_set;
    int last_background_pid;
    bool last_argument_set;
    const char *last_argument;

    /* Process group / PID overrides */
    bool pgid_set;
#ifdef POSIX_API
    pid_t pgid;
#else
    int pgid;
#endif
    bool pgid_valid_set;
    bool pgid_valid;

    bool shell_pid_set;
#ifdef POSIX_API
    pid_t shell_pid;
#else
    int shell_pid;
#endif
    bool shell_pid_valid_set;
    bool shell_pid_valid;

    bool shell_ppid_set;
#ifdef POSIX_API
    pid_t shell_ppid;
#else
    int shell_ppid;
#endif
    bool shell_ppid_valid_set;
    bool shell_ppid_valid;

    /* RC file state overrides */
    bool rc_loaded_set;
    bool rc_loaded;
    bool rc_files_sourced_set;
    bool rc_files_sourced;
};

/**
 * The exec_t holds global shell state that persists across all frames.
 */
struct exec_t
{
    /* -------------------------------------------------------------------------
     * 1) Singleton executor state (not per-frame)
     * -------------------------------------------------------------------------
     */
    bool shell_pid_valid;
    bool shell_ppid_valid;
#ifdef POSIX_API
    pid_t shell_pid;  /* $$ - PID of the main shell process */
    pid_t shell_ppid; /* $PPID at startup */
#else
    int shell_pid;
    int shell_ppid;
#endif

    bool is_interactive;
    bool is_login_shell;

    bool signals_installed;
    sig_act_store_t *original_signals;
    volatile sig_atomic_t sigint_received;
    volatile sig_atomic_t sigchld_received;
    volatile sig_atomic_t trap_pending[NSIG];

    job_store_t *jobs;
    bool job_control_enabled;

    bool pgid_valid;
#ifdef POSIX_API
    pid_t pgid; /* Shell's process group ID */
#else
    int pgid;
#endif

    /* Pipeline status (pipefail / PIPESTATUS) */
    int *pipe_statuses;
    size_t pipe_status_count;
    size_t pipe_status_capacity;

    /* Error state */
    string_t *error_msg;

    /* -------------------------------------------------------------------------
     * 2) Top-frame initialization data
     * -------------------------------------------------------------------------
     * This data is used to initialize the top frame when it is created lazily.
     * The top frame and current frame remain NULL until first execution.
     */
    int argc;
    char **argv;
    char **envp;

    string_t *shell_name;      /* $0 for top-level (argv[0] or script name) */
    string_list_t *shell_args; /* $@ for top-level (argv[1..argc-1]) */
    string_list_t *env_vars;   /* Environment variables */

    struct exec_opt_flags_t opt;

    bool rc_loaded;
    bool rc_files_sourced;

    /* Top-frame stores (owned until top frame is created) */
    variable_store_t *variables;
    variable_store_t *local_variables; /* Unused for top frame; reserved for parity */
    positional_params_t *positional_params;
    func_store_t *functions;
    alias_store_t *aliases;
    trap_store_t *traps;

    /* Tokenizer for persistent state across interactive commands */
    struct tokenizer_t *tokenizer;
#if defined(POSIX_API) || defined(UCRT_API)
    fd_table_t *open_fds;
    int next_fd;
#endif

    string_t *working_directory;
#ifdef POSIX_API
    mode_t umask;
    rlim_t file_size_limit;
#else
    int umask;
#endif

    int last_exit_status;
    bool last_exit_status_set;
    int last_background_pid;
    bool last_background_pid_set;
    string_t *last_argument;
    bool last_argument_set;

    /* -------------------------------------------------------------------------
     * 3) Frame stack pointers
     * -------------------------------------------------------------------------
     */
    bool top_frame_initialized;
    exec_frame_t *top_frame;
    exec_frame_t *current_frame;
};

typedef struct exec_opt_flags_t exec_opt_flags_t;
typedef struct exec_cfg_t exec_cfg_t;
typedef struct exec_t exec_t;

/* ============================================================================
 * Executor Context
 * ============================================================================ */

typedef enum exec_status_t
{
    EXEC_OK = 0,
    EXEC_ERROR = 1,
    EXEC_NOT_IMPL = 2,
    EXEC_OK_INTERNAL_FUNCTION_STORED,
    EXEC_BREAK,           ///< break statement executed
    EXEC_CONTINUE,        ///< continue statement executed
    EXEC_RETURN,          ///< return statement executed
    EXEC_EXIT,            ///< exit statement executed
} exec_status_t;

/* ============================================================================
 * Executor Configuration Functions
 * ============================================================================ */

void exec_cfg_set_from_shell_options(exec_cfg_t *cfg,
    int argc, char *const *argv, char *const *envp,
    const char *shell_name,
    string_list_t *shell_args,
    string_list_t *env_vars,
    const exec_opt_flags_t *opt_flags,
    bool is_interactive,
    bool is_login_shell,
                                       bool job_control_enabled);

/* ============================================================================
 * Executor Lifecycle Functions
 * ============================================================================ */

/**
 * Create a new executor.
 */
exec_t *exec_create(const struct exec_cfg_t *cfg);

/**
 * Destroy an executor and free all associated memory.
 * Safe to call with nullptr.
 */
void exec_destroy(exec_t **executor);

/* ============================================================================
 * Execution Functions
 * ============================================================================ */

/**
 * Setup the executor for interactive execution, including sourcing rc files.
 */
void exec_setup_interactive_execute(exec_t *executor);

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
 * Execute commands from a stream (file or stdin).
 * Reads lines from the stream, parses them, and executes them.
 *
 * @param executor The executor context
 * @param fp The file stream to read from
 * @return EXEC_OK on success, EXEC_ERROR on error
 */
exec_status_t exec_execute_stream(exec_t *executor, FILE *fp);

/**
 * Execute a complete command string.
 *
 * This function executes a self-contained command string, such as a trap
 * handler action or the argument to eval. Unlike exec_execute_stream(),
 * this function expects the command to be complete and will treat incomplete
 * input (unclosed quotes, missing keywords) as an error.
 *
 * @param frame The execution frame to use
 * @param command The complete command string to execute
 * @return exec_result_t with execution status and exit code
 */
exec_result_t exec_command_string(exec_frame_t *frame, const char *command);

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

// exec_result_t exec_pipeline_orchestrate(exec_frame_t *frame, exec_params_t *params);

/**
 * Execute a simple command.
 */
exec_status_t exec_execute_simple_command(exec_frame_t *frame, const ast_node_t *node);

/**
 * Execute a redirected command wrapper.
 */
exec_status_t exec_execute_redirected_command(exec_frame_t *frame, const ast_node_t *node);

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

/**
 * Check if any background jobs have completed, and if so mark them done
 * in the job store.
 * Only properly functions in POSIX_API mode.
 * In other modes, this is a no-op.
 * If notify is true, print completed jobs to the output.
 */
void exec_reap_background_jobs(exec_t *executor, bool notify);

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
 * Get the PS1 prompt string.
 * Returns the value of the PS1 variable from the variable store,
 * or a default prompt if PS1 is not set.
 *
 * @param executor The executor context
 * @return The PS1 prompt string (never NULL)
 */
const char *exec_get_ps1(const exec_t *executor);

/**
 * Get the PS2 prompt string.
 * Returns the value of the PS2 variable from the variable store,
 * or a default prompt if PS2 is not set.
 *
 * @param executor The executor context
 * @return The PS2 prompt string (never NULL)
 */
const char *exec_get_ps2(const exec_t *executor);

positional_params_t *exec_get_positional_params(const exec_t *executor);
variable_store_t *exec_get_variables(const exec_t *executor);
alias_store_t *exec_get_aliases(const exec_t *executor);
bool exec_is_interactive(const exec_t *executor);
bool exec_is_login_shell(const exec_t *executor);

/* ============================================================================
 * Expander Callbacks
 * ============================================================================ */

/**
 * Command substitution callback for the expander.
 * Executes a command and returns its output.
 *
 * @param command The command string to execute
 * @param userdata Pointer to the executor context
 * @param command The command to execute
 * @return The output of the command as a newly allocated string_t (caller must free),
 *         or NULL on error
 */
string_t *exec_command_subst_callback(void *userdata, const string_t *command);

/**
 * Get positional parameters from executor (for expander).
 */
positional_params_t *exec_get_positional_params(const exec_t *executor);

/**
 * Get variable store from executor (for expander).
 */
variable_store_t *exec_get_variables(const exec_t *executor);

#endif


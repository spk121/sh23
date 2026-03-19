#ifndef EXEC_TYPES_INTERNAL_H
#define EXEC_TYPES_INTERNAL_H

/**
 * @file exec_internal.h
 * @brief Internal definitions for the shell executor.
 *
 * This header contains the concrete struct definitions for miga_exec_t and
 * miga_frame_t, as well as internal-only functions that are used by the
 * executor implementation and must NOT be used by library consumers or
 * builtin implementations.
 *
 * Library consumers should include only exec.h and frame.h.
 */

#ifdef MIGA_POSIX_API
#define _POSIX_C_SOURCE 202405L
#endif

#include <signal.h>
#include <stdbool.h>

/* ── Public API headers (for the types they define) ──────────────────────── */
#include "miga/exec.h"

/* ── Internal module headers ─────────────────────────────────────────────── */
#include "alias_store.h"
#include "ast.h"
#include "builtin_store.h"
#include "exec_frame_policy.h"
#include "fd_table.h"
#include "func_store.h"
#include "job_store.h"
#include "positional_params.h"
#include "sig_act.h"
#include "miga/strlist.h"
#include "miga/string_t.h"
#include "trap_store.h"
#include "variable_store.h"

#ifdef MIGA_POSIX_API
#include <sys/resource.h>
#include <sys/types.h>
#endif

#if defined(MIGA_POSIX_API) || defined(MIGA_UCRT_API)
#include "fd_table.h"
#endif

/* ============================================================================
 * Platform Constants
 * ============================================================================ */

#ifndef NSIG
#ifdef _NSIG
#define NSIG _NSIG
#else
#define NSIG 64
#endif
#endif

#ifdef MIGA_POSIX_API
#define EXEC_SYSTEM_RC_PATH "/etc/migarc"
#define EXEC_USER_RC_NAME ".migarc"
#define EXEC_RC_IN_XDG_CONFIG_HOME
#elif defined(MIGA_UCRT_API)
#define EXEC_USER_RC_NAME "migarc"
#define EXEC_RC_IN_LOCAL_APP_DATA
#else
#define EXEC_USER_RC_NAME "MIGA.RC"
#define EXEC_RC_IN_CURRENT_DIRECTORY
#endif

#define EXEC_CFG_FALLBACK_ARGV0 "miga"

/* ============================================================================
 * Option Flags (concrete definition)
 * ============================================================================ */

struct exec_opt_flags_t
{
    bool allexport; /* set -a */
    bool errexit;   /* set -e */
    bool ignoreeof; /* set -I */
    bool noclobber; /* set -C */
    bool noglob;    /* set -f */
    bool noexec;    /* set -n */
    bool nounset;   /* set -u */
    bool pipefail;  /* set -o pipefail */
    bool verbose;   /* set -v */
    bool vi;        /* set -o vi */
    bool xtrace;    /* set -x */
};

typedef struct exec_opt_flags_t exec_opt_flags_t;

#define EXEC_OPT_FLAGS_INIT                                                                        \
    {                                                                                              \
        .allexport = false,                                                                        \
        .errexit = false,                                                                          \
        .ignoreeof = false,                                                                        \
        .noclobber = false,                                                                        \
        .noglob = false,                                                                           \
        .noexec = false,                                                                           \
        .nounset = false,                                                                          \
        .pipefail = false,                                                                         \
        .verbose = false,                                                                          \
        .vi = false,                                                                               \
        .xtrace = false,                                                                           \
    }

/* ============================================================================
 * Executor State (concrete definition of miga_exec_t)
 * ============================================================================ */

struct miga_exec_t
{
    /* ─── Singleton state ─────────────────────────────────────────────── */

    bool shell_pid_valid;
    bool shell_ppid_valid;
#ifdef MIGA_POSIX_API
    pid_t shell_pid;
    pid_t shell_ppid;
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
    bool job_control_disabled;

    bool pgid_valid;
#ifdef MIGA_POSIX_API
    pid_t pgid;
#else
    int pgid;
#endif

    /* Pipeline status (pipefail / PIPESTATUS) */
    int *pipe_statuses;
    size_t pipe_status_count;
    size_t pipe_status_capacity;

    /* Error state */
    string_t *error_msg;

    /* Exit request (set by exec_request_exit, checked by the main loop) */
    bool exit_requested;

    /* Builtin registry */
    struct builtin_store_t *builtins;

    /* ─── Top-frame initialisation data ───────────────────────────────── */

    int argc;
    char *const *argv;
    char *const *envp;

    string_t *shell_name;
    strlist_t *shell_args;
    strlist_t *env_vars;

    exec_opt_flags_t opt;

    bool rc_loaded;
    bool rc_files_sourced;
    bool inhibit_rc_files;
    bool nobuiltins;
    string_t *system_rc_filename;
    string_t *user_rc_filename;

    /* Top-frame stores (owned until top frame is created) */
    variable_store_t *variables;
    variable_store_t *local_variables;
    positional_params_t *positional_params;
    func_store_t *functions;
    alias_store_t *aliases;
    trap_store_t *traps;

    /* Parse session (persistent across interactive commands) */
    parse_session_t *session;

#if defined(MIGA_POSIX_API) || defined(MIGA_UCRT_API)
    fd_table_t *open_fds;
    int next_fd;
#endif

    string_t *working_directory;
#ifdef MIGA_POSIX_API
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

    /* ─── Frame stack ─────────────────────────────────────────────────── */

    bool top_frame_initialized;
    struct miga_frame_t *top_frame;
    struct miga_frame_t *current_frame;
};

/**
 * Types of redirection operations.
 */

/* redirection_type_t is recycled from the one in ast.h */

/**
 * How the target of a redirection is specified.
 */

/* redir_target_kind_t is recycled from the one in ast.h */

/**
 * Runtime representation of a single redirection.
 */
typedef struct exec_redirection_t
{
    redirection_type_t type;
    int explicit_fd;     /* [n] prefix, or -1 for default */
    bool is_io_location; /* POSIX 2024 {varname} syntax */
    string_t *io_location_varname;
    redir_target_kind_t target_kind;

    union {
        /* REDIR_TARGET_FILE */
        struct
        {
            bool is_expanded;
            string_t *filename; /* Expanded filename */
            token_t *tok;       /* Original parts for expansion (if not yet expanded) */
        } file;

        /* REDIR_TARGET_FD */
        struct
        {
            int fixed_fd;            /* Literal fd number, or -1 */
            string_t *fd_expression; /* If fd comes from expansion */
            token_t *fd_token;       /* Full token for variable-derived FDs */
        } fd;

        /* REDIR_TARGET_HEREDOC */
        struct
        {
            string_t *content;    /* The heredoc content */
            bool needs_expansion; /* false if delimiter was quoted */
        } heredoc;

        /* REDIR_TARGET_IO_LOCATION */
        struct
        {
            string_t *raw_filename; /* For {var}>file */
            int fixed_fd;           /* For {var}>&N */
        } io_location;
    } target;

    int source_line; /* For error messages */
} exec_redirection_t;

/**
 * Dynamic array of runtime redirections.
 */

typedef struct exec_redirections_t
{
    exec_redirection_t *items;
    size_t count;
    size_t capacity;
} exec_redirections_t;

/* ============================================================================
 * Partial Execution State
 * ============================================================================
 *
 * The concrete definition lives in parse_session.h.
 *
 * ============================================================================ */

/* ============================================================================
 * Frame-Level Execution Result
 * ============================================================================ */

/**
 * Result of executing a frame or command.
 *
 * Combines the execution status (miga_exec_status_t) with control flow state
 * (miga_frame_flow_t), the shell exit status ($?), and loop depth
 * for break/continue.
 */
typedef struct exec_frame_execute_result_t
{
    miga_exec_status_t status;

    bool has_exit_status;
    int exit_status; /* valid if has_exit_status is true */

    miga_frame_flow_t flow; /* control flow: normal, break, continue, return, top */
    int flow_depth;            /* for break/continue: how many nested loops */
} exec_frame_execute_result_t;

/* ============================================================================
 * Execution Frame Structure
 * ============================================================================ */

/**
 * An execution frame represents a single execution context in the shell.
 * Frames form a stack, with each frame potentially sharing or owning
 * various pieces of state (variables, file descriptors, traps, etc.)
 * based on its policy.
 */
struct miga_frame_t
{
    /* Frame identity */
    exec_frame_type_t type;
    const exec_frame_policy_t *policy;

    /* Parent frame (NULL for top-level) */
    struct miga_frame_t *parent;

    /* Back-reference to executor. Canonically, frames are owned by the executor. */
    miga_exec_t *executor;

    /* -------------------------------------------------------------------------
     * Scope-dependent storage
     * -------------------------------------------------------------------------
     * Ownership depends on the frame's policy:
     * - EXEC_SCOPE_SHARE: Points to parent's instance, must NOT be freed
     * - EXEC_SCOPE_OWN/COPY: Owned by this frame, must be freed on pop
     */
    variable_store_t *variables;
    variable_store_t *saved_variables;
    variable_store_t *local_variables; /* Only for frames with has_locals=true */
    positional_params_t *positional_params;
    positional_params_t *saved_positional_params; /* For dot script override restore */
    func_store_t *functions;
    alias_store_t *aliases;
    fd_table_t *open_fds;
    trap_store_t *traps;
    exec_opt_flags_t *opt_flags;
    string_t *working_directory;
#ifdef MIGA_POSIX_API
    mode_t *umask;
#else
    int *umask;
#endif

    /* -------------------------------------------------------------------------
     * Frame-local state (always owned by this frame)
     * -------------------------------------------------------------------------
     */
#if !defined(MIGA_POSIX_API) && !defined(MIGA_UCRT_API)
    /* For ISO C redirection support in builtins/functions, we need to track FILE pointers */
    FILE **stdin_fp;
    FILE **stdout_fp;
    FILE **stderr_fp;
#endif
    int loop_depth;       /* 0 if not in loop, else depth of nested loops */
    int last_exit_status; /* $? */
    int last_bg_pid;      /* $! */

    /* Control flow state (set by builtins like return, break, continue) */
    miga_frame_flow_t pending_control_flow;
    int pending_flow_depth; /* For 'break N' / 'continue N' */

    /* Source tracking */
    string_t *source_name; /* $BASH_SOURCE / script name */
    int source_line;       /* $LINENO */
    bool lineno_active;    /* false if user has unset/reset LINENO */

    /* Trap handler state */
    bool in_trap_handler; /* Prevents recursive trap handling */
};

/* ============================================================================
 * Execution Parameters
 * ============================================================================ */

/**
 * Parameters passed when creating/executing a frame.
 * Different fields are used depending on the frame type.
 */
typedef struct exec_params_t
{
    /* Body to execute */
    const ast_node_t *body;

    /* Redirections to apply */
    const exec_redirections_t *redirections;

    /* For functions and dot scripts: arguments to set $1, $2, ... */
    strlist_t *arguments;

    /* For dot scripts: the script path (for $0 and source tracking) */
    string_t *script_path;

    /* For loops */
    const ast_node_t *condition;    /* while/until condition */
    bool until_mode;                /* true for until, false for while */
    strlist_t *iteration_words; /* for loop word list */
    string_t *loop_var_name;        /* for loop variable */

    /* For pipelines (EXEC_FRAME_PIPELINE) */
    ast_node_list_t *pipeline_commands; /* List of commands in pipeline */
    bool pipeline_negated;              /* true for ! pipeline */

    /* For pipeline commands (EXEC_FRAME_PIPELINE_CMD) */
#ifdef MIGA_POSIX_API
    pid_t pipeline_pgid; /* Process group to join (0 = create new) */
#else
    int pipeline_pgid;
#endif
    int stdin_pipe_fd;      /* -1 if not piped, else fd to dup2 to stdin */
    int stdout_pipe_fd;     /* -1 if not piped, else fd to dup2 to stdout */
    int *pipe_fds_to_close; /* Array of all pipe FDs to close */
    int pipe_fds_count;     /* Count of pipe FDs to close */

    /* For background jobs / debugging */
    strlist_t *command_args; /* Original command text for job display */

    /* Source location */
    int source_line;
} exec_params_t;

#endif /* EXEC_TYPES_INTERNAL_H */

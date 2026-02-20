#ifndef EXEC_FRAME_POLICY_H
#define EXEC_FRAME_POLICY_H

#include <stdbool.h>

/* ============================================================================
 * Execution Frame Types
 * ============================================================================
 * Each frame type represents a distinct execution context with its own
 * policies for variable scope, process management, control flow, etc.
 */

typedef enum exec_frame_type_t
{
    EXEC_FRAME_TOP_LEVEL,
    EXEC_FRAME_SUBSHELL,
    EXEC_FRAME_COMMAND_SUBSTITUTION = EXEC_FRAME_SUBSHELL, // Same semantics, stdout captured
    EXEC_FRAME_BRACE_GROUP,
    EXEC_FRAME_FUNCTION,
    EXEC_FRAME_LOOP,
    EXEC_FRAME_TRAP,
    EXEC_FRAME_BACKGROUND_JOB,
    EXEC_FRAME_PIPELINE,     // Pipeline orchestrator (cmd1 | cmd2 | cmd3)
    EXEC_FRAME_PIPELINE_CMD, // Individual command within a pipeline
    EXEC_FRAME_DOT_SCRIPT,
    EXEC_FRAME_EVAL,
    EXEC_FRAME_TYPE_COUNT
} exec_frame_type_t;

/* ============================================================================
 * Scope Policy
 * ============================================================================
 * Defines how a particular type of data (variables, fds, traps, etc.) is
 * handled when entering a new execution frame.
 */

typedef enum exec_scope_t
{
    EXEC_SCOPE_NONE, // N/A, typically because this is top-level
    EXEC_SCOPE_OWN,  // This frame has its own instance, not copied from parent
    EXEC_SCOPE_COPY, // This frame has its own instance, initialized from parent
    EXEC_SCOPE_SHARE // This frame shares parent's instance; changes affect parent
} exec_scope_t;

/* ============================================================================
 * Process Group Policy
 * ============================================================================
 * Defines how this frame interacts with process groups.
 * Process groups enable job control (fg, bg, kill %1, etc.)
 */

typedef enum exec_process_group_t
{
    EXEC_PGROUP_NONE,    // No process group manipulation
    EXEC_PGROUP_START,   // Create new group: setpgid(0, 0) — background jobs
    EXEC_PGROUP_PIPELINE // Pipeline semantics: first cmd starts group, others join
} exec_process_group_t;

/* ============================================================================
 * Positional Parameter Policies
 * ============================================================================
 */

/**
 * Defines how $0 is determined when entering this frame type.
 *
 * Note: POSIX says $0 is "the name of the shell or shell script."
 * Functions do NOT change $0 to the function name (a common misconception).
 * Only dot scripts change $0 to the sourced script's name.
 */
typedef enum exec_arg0_policy_t
{
    EXEC_ARG0_INIT_SHELL_OR_SCRIPT, // Top-level only: argv[0] or script path
    EXEC_ARG0_INHERIT,              // Keep parent's $0 (most frame types)
    EXEC_ARG0_SET_TO_SOURCED_SCRIPT // Dot script: $0 becomes the sourced file's path
} exec_arg0_policy_t;

/**
 * Defines how $1, $2, ... are initialized when this frame has its own
 * positional parameters (i.e., when positional.scope is OWN).
 *
 * When scope is COPY, initialization is implicit (copy from parent).
 * When scope is SHARE, the parent's params are used directly.
 * In both cases, use EXEC_POSITIONAL_INIT_NA.
 */
typedef enum exec_positional_init_t
{
    EXEC_POSITIONAL_INIT_NA,       // Not applicable (scope is COPY or SHARE)
    EXEC_POSITIONAL_INIT_ARGV,     // From shell's command-line arguments (top-level)
    EXEC_POSITIONAL_INIT_CALL_ARGS // From function invocation arguments
} exec_positional_init_t;

/* ============================================================================
 * Control Flow Policies
 * ============================================================================
 */

/**
 * Defines how this frame type interacts with 'return' control flow.
 */
typedef enum exec_return_behavior_t
{
    EXEC_RETURN_DISALLOWED,  // return is invalid here and doesn't propagate (subshell, top-level)
    EXEC_RETURN_TRANSPARENT, // return passes through to enclosing frame (brace group, eval, loop)
    EXEC_RETURN_TARGET       // return is valid and stops here (function, dot script)
} exec_return_behavior_t;

/**
 * Defines how this frame type interacts with 'break' and 'continue' control flow.
 */
typedef enum exec_loop_control_t
{
    EXEC_LOOP_DISALLOWED,  // break/continue invalid, doesn't propagate (subshell, function)
    EXEC_LOOP_TRANSPARENT, // break/continue passes through (brace group, eval)
    EXEC_LOOP_TARGET       // break/continue applies here (loop frames)
} exec_loop_control_t;

/* ============================================================================
 * Execution Frame Policy Structure
 * ============================================================================
 * This structure defines the execution policy for a particular execution frame.
 * It specifies how various aspects of execution (variables, file descriptors,
 * traps, options, etc.) are handled in this frame.
 */

typedef struct exec_frame_policy_t
{

    /* -------------------------------------------------------------------------
     * Process Model
     * -------------------------------------------------------------------------
     * Controls whether this frame forks and how it relates to process groups.
     * POSIX job control requires that pipelines and background jobs run in
     * their own process groups so they can be signaled as a unit (fg, bg, kill %1).
     */
    struct
    {
        bool forks;                  // Whether this frame forks a new process
        exec_process_group_t pgroup; // Process group behavior
        bool is_pipeline_member;     // Part of a pipeline (affects exit status, fd wiring)
    } process;

    /* -------------------------------------------------------------------------
     * Variables
     * -------------------------------------------------------------------------
     * Controls variable visibility and lifetime.
     * - SHARE: See and modify parent's variables
     * - COPY: Get a copy; modifications don't propagate back
     * - OWN: Fresh store (top-level initializes from environment)
     */
    struct
    {
        exec_scope_t scope;
        bool init_from_envp;    // When OWN, populate from environment
        bool copy_exports_only; // When COPY, only exported variables
        bool has_locals;        // Supports 'local' builtin (functions)
    } variables;

    /* -------------------------------------------------------------------------
     * Positional Parameters ($0, $1, $2, ..., $@, $*)
     * -------------------------------------------------------------------------
     * $0 is the shell/script name (never the function name).
     * $1.. are arguments, with scope and initialization varying by frame type.
     * Dot scripts are special: SHARE scope but can temporarily override.
     */
    struct
    {
        exec_scope_t scope;
        exec_arg0_policy_t arg0;     // How $0 is set
        exec_positional_init_t argn; // How $1, $2, ... are initialized
        bool can_override;           // Temporarily replace while sharing (dot scripts)
    } positional;

    /* -------------------------------------------------------------------------
     * File Descriptors
     * -------------------------------------------------------------------------
     * SHARE: Redirections affect parent (dot script, brace group)
     * COPY: Subshells inherit FDs but changes don't propagate back
     */
    struct
    {
        exec_scope_t scope;
    } fds;

    /* -------------------------------------------------------------------------
     * Traps / Signal Handling
     * -------------------------------------------------------------------------
     * - Signals ignored at shell startup remain ignored (process-level invariant)
     * - Subshells reset non-ignored traps to SIG_DFL
     * - EXIT trap runs when shell/subshell exits
     */
    struct
    {
        exec_scope_t scope;
        bool resets_non_ignored; // Reset traps to SIG_DFL on entry (subshells)
        bool exit_trap_runs;     // EXIT trap fires when this frame exits
    } traps;

    /* -------------------------------------------------------------------------
     * Shell Options (set -e, set -x, etc.)
     * -------------------------------------------------------------------------
     * Most options follow the scope rules (SHARE or COPY).
     *
     * errexit (-e) is special: POSIX specifies that errexit is disabled
     * during trap handler execution. In all other contexts, errexit
     * follows normal scope rules and is inherited/shared as expected.
     */
    struct
    {
        exec_scope_t scope;
        bool errexit_enabled; // false only for EXEC_FRAME_TRAP
    } options;

    /* -------------------------------------------------------------------------
     * Working Directory
     * -------------------------------------------------------------------------
     */
    struct
    {
        exec_scope_t scope;
        bool init_from_system; // When OWN, call getcwd()
    } cwd;

    /* -------------------------------------------------------------------------
     * umask
     * -------------------------------------------------------------------------
     */
    struct
    {
        exec_scope_t scope;
        bool init_from_system; // When OWN, query system umask
        bool init_to_0022;     // When OWN, default to 0022
    } umask;

    /* -------------------------------------------------------------------------
     * Functions
     * -------------------------------------------------------------------------
     * Function definitions are typically shared—defining a function inside
     * a brace group or loop makes it available to the parent scope.
     * Subshells get a copy.
     */
    struct
    {
        exec_scope_t scope;
    } functions;

    /* -------------------------------------------------------------------------
     * Aliases
     * -------------------------------------------------------------------------
     * Alias expansion occurs during parsing, but the alias store can be
     * modified at runtime. Typically shared across all non-subshell frames.
     */
    struct
    {
        exec_scope_t scope;
        bool expands; // Whether alias expansion is active
    } aliases;

    /* -------------------------------------------------------------------------
     * Control Flow (return, break, continue)
     * -------------------------------------------------------------------------
     * return: Valid inside functions and dot scripts. Transparent frames
     *         (brace groups, eval) let it pass through to the target.
     * break/continue: Valid inside loops. Transparent frames let it pass
     *         through. Functions and subshells block propagation.
     */
    struct
    {
        exec_return_behavior_t return_behavior;
        exec_loop_control_t loop_control;
        bool is_loop; // This frame represents a loop iteration
    } flow;

    /* -------------------------------------------------------------------------
     * Exit Behavior
     * -------------------------------------------------------------------------
     */
    struct
    {
        bool terminates_process;    // exit() vs pop frame
        bool affects_parent_status; // Sets parent's $?
    } exit;

    /* -------------------------------------------------------------------------
     * Source Tracking ($LINENO, $BASH_SOURCE)
     * -------------------------------------------------------------------------
     */
    struct
    {
        bool tracks_location;
    } source;

    /* -------------------------------------------------------------------------
     * Classification
     * -------------------------------------------------------------------------
     * Convenience flags—technically derivable from other fields, but useful
     * for readability and debugging.
     */
    struct
    {
        bool is_subshell;
        bool is_background;
    } classification;

} exec_frame_policy_t;

/* ============================================================================
 * Policy Table
 * ============================================================================
 * Static policy definitions for each frame type.
 */

static const exec_frame_policy_t EXEC_FRAME_POLICIES[EXEC_FRAME_TYPE_COUNT] = {

    /* =========================================================================
     * EXEC_FRAME_TOP_LEVEL
     * =========================================================================
     * The initial shell frame. Interactive shell or script execution.
     * Owns everything, initializes from environment/argv.
     */
    [EXEC_FRAME_TOP_LEVEL] =
        {
            .process =
                {
                    .forks = false,
                    .pgroup = EXEC_PGROUP_NONE,
                    .is_pipeline_member = false,
                },
            .variables =
                {
                    .scope = EXEC_SCOPE_OWN,
                    .init_from_envp = true,
                    .copy_exports_only = false,
                    .has_locals = false,
                },
            .positional =
                {
                    .scope = EXEC_SCOPE_OWN,
                    .arg0 = EXEC_ARG0_INIT_SHELL_OR_SCRIPT,
                    .argn = EXEC_POSITIONAL_INIT_ARGV, // OWN scope: init from command line
                    .can_override = false,
                },
            .fds =
                {
                    .scope = EXEC_SCOPE_OWN,
                },
            .traps =
                {
                    .scope = EXEC_SCOPE_OWN,
                    .resets_non_ignored = false,
                    .exit_trap_runs = true,
                },
            .options =
                {
                    .scope = EXEC_SCOPE_OWN,
                    .errexit_enabled = true,
                },
            .cwd =
                {
                    .scope = EXEC_SCOPE_OWN,
                    .init_from_system = true,
                },
            .umask =
                {
                    .scope = EXEC_SCOPE_OWN,
                    .init_from_system = true,
                    .init_to_0022 = false,
                },
            .functions =
                {
                    .scope = EXEC_SCOPE_OWN,
                },
            .aliases =
                {
                    .scope = EXEC_SCOPE_OWN,
                    .expands = true,
                },
            .flow =
                {
                    .return_behavior = EXEC_RETURN_DISALLOWED,
                    .loop_control = EXEC_LOOP_DISALLOWED,
                    .is_loop = false,
                },
            .exit =
                {
                    .terminates_process = true,
                    .affects_parent_status = false,
                },
            .source =
                {
                    .tracks_location = true,
                },
            .classification =
                {
                    .is_subshell = false,
                    .is_background = false,
                },
        },

    /* =========================================================================
     * EXEC_FRAME_SUBSHELL
     * =========================================================================
     * Explicit subshell: ( commands )
     * Also used for command substitution $( commands )
     * Forks, copies everything, traps reset.
     */
    [EXEC_FRAME_SUBSHELL] =
        {
            .process =
                {
                    .forks = true,
                    .pgroup = EXEC_PGROUP_NONE,
                    .is_pipeline_member = false,
                },
            .variables =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .init_from_envp = false,
                    .copy_exports_only = false,
                    .has_locals = false,
                },
            .positional =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .arg0 = EXEC_ARG0_INHERIT,
                    .argn = EXEC_POSITIONAL_INIT_NA, // COPY scope: implicit from parent
                    .can_override = false,
                },
            .fds =
                {
                    .scope = EXEC_SCOPE_COPY,
                },
            .traps =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .resets_non_ignored = true,
                    .exit_trap_runs = true,
                },
            .options =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .errexit_enabled = true,
                },
            .cwd =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .init_from_system = false,
                },
            .umask =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .init_from_system = false,
                    .init_to_0022 = false,
                },
            .functions =
                {
                    .scope = EXEC_SCOPE_COPY,
                },
            .aliases =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .expands = true,
                },
            .flow =
                {
                    .return_behavior = EXEC_RETURN_DISALLOWED,
                    .loop_control = EXEC_LOOP_DISALLOWED,
                    .is_loop = false,
                },
            .exit =
                {
                    .terminates_process = true,
                    .affects_parent_status = true,
                },
            .source =
                {
                    .tracks_location = true,
                },
            .classification =
                {
                    .is_subshell = true,
                    .is_background = false,
                },
        },

    /* =========================================================================
     * EXEC_FRAME_BRACE_GROUP
     * =========================================================================
     * Brace group: { commands; }
     * Groups commands in current shell. Shares everything with parent.
     * Redirections on the group affect all commands within.
     */
    [EXEC_FRAME_BRACE_GROUP] =
        {
            .process =
                {
                    .forks = false,
                    .pgroup = EXEC_PGROUP_NONE,
                    .is_pipeline_member = false,
                },
            .variables =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_envp = false,
                    .copy_exports_only = false,
                    .has_locals = false,
                },
            .positional =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .arg0 = EXEC_ARG0_INHERIT,
                    .argn = EXEC_POSITIONAL_INIT_NA, // SHARE scope: using parent's directly
                    .can_override = false,
                },
            .fds =
                {
                    .scope = EXEC_SCOPE_SHARE,
                },
            .traps =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .resets_non_ignored = false,
                    .exit_trap_runs = false,
                },
            .options =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .errexit_enabled = true,
                },
            .cwd =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_system = false,
                },
            .umask =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_system = false,
                    .init_to_0022 = false,
                },
            .functions =
                {
                    .scope = EXEC_SCOPE_SHARE,
                },
            .aliases =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .expands = true,
                },
            .flow =
                {
                    .return_behavior = EXEC_RETURN_TRANSPARENT,
                    .loop_control = EXEC_LOOP_TRANSPARENT,
                    .is_loop = false,
                },
            .exit =
                {
                    .terminates_process = false,
                    .affects_parent_status = true,
                },
            .source =
                {
                    .tracks_location = false,
                },
            .classification =
                {
                    .is_subshell = false,
                    .is_background = false,
                },
        },

    /* =========================================================================
     * EXEC_FRAME_FUNCTION
     * =========================================================================
     * Function invocation: fname() { ... } called as fname arg1 arg2
     * Has own positional params (the arguments), supports local variables.
     * Shares most other state with caller. Is a return target.
     */
    [EXEC_FRAME_FUNCTION] =
        {
            .process =
                {
                    .forks = false,
                    .pgroup = EXEC_PGROUP_NONE,
                    .is_pipeline_member = false,
                },
            .variables =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_envp = false,
                    .copy_exports_only = false,
                    .has_locals = true,
                },
            .positional =
                {
                    .scope = EXEC_SCOPE_OWN,
                    .arg0 = EXEC_ARG0_INHERIT,
                    .argn =
                        EXEC_POSITIONAL_INIT_CALL_ARGS, // OWN scope: init from function arguments
                    .can_override = false,
                },
            .fds =
                {
                    .scope = EXEC_SCOPE_SHARE,
                },
            .traps =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .resets_non_ignored = false,
                    .exit_trap_runs = false,
                },
            .options =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .errexit_enabled = true,
                },
            .cwd =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_system = false,
                },
            .umask =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_system = false,
                    .init_to_0022 = false,
                },
            .functions =
                {
                    .scope = EXEC_SCOPE_SHARE,
                },
            .aliases =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .expands = true,
                },
            .flow =
                {
                    .return_behavior = EXEC_RETURN_TARGET,
                    .loop_control = EXEC_LOOP_DISALLOWED,
                    .is_loop = false,
                },
            .exit =
                {
                    .terminates_process = false,
                    .affects_parent_status = true,
                },
            .source =
                {
                    .tracks_location = true,
                },
            .classification =
                {
                    .is_subshell = false,
                    .is_background = false,
                },
        },

    /* =========================================================================
     * EXEC_FRAME_LOOP
     * =========================================================================
     * Loop constructs: for, while, until
     * Shares everything with parent. break/continue are valid here.
     */
    [EXEC_FRAME_LOOP] =
        {
            .process =
                {
                    .forks = false,
                    .pgroup = EXEC_PGROUP_NONE,
                    .is_pipeline_member = false,
                },
            .variables =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_envp = false,
                    .copy_exports_only = false,
                    .has_locals = false,
                },
            .positional =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .arg0 = EXEC_ARG0_INHERIT,
                    .argn = EXEC_POSITIONAL_INIT_NA, // SHARE scope: using parent's directly
                    .can_override = false,
                },
            .fds =
                {
                    .scope = EXEC_SCOPE_SHARE,
                },
            .traps =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .resets_non_ignored = false,
                    .exit_trap_runs = false,
                },
            .options =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .errexit_enabled = true,
                },
            .cwd =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_system = false,
                },
            .umask =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_system = false,
                    .init_to_0022 = false,
                },
            .functions =
                {
                    .scope = EXEC_SCOPE_SHARE,
                },
            .aliases =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .expands = true,
                },
            .flow =
                {
                    .return_behavior = EXEC_RETURN_TRANSPARENT,
                    .loop_control = EXEC_LOOP_TARGET,
                    .is_loop = true,
                },
            .exit =
                {
                    .terminates_process = false,
                    .affects_parent_status = true,
                },
            .source =
                {
                    .tracks_location = false,
                },
            .classification =
                {
                    .is_subshell = false,
                    .is_background = false,
                },
        },

    /* =========================================================================
     * EXEC_FRAME_TRAP
     * =========================================================================
     * Trap handler execution.
     * Runs in current shell context but errexit is disabled.
     * Recursive trap invocation for same signal is blocked (handled elsewhere).
     */
    [EXEC_FRAME_TRAP] =
        {
            .process =
                {
                    .forks = false,
                    .pgroup = EXEC_PGROUP_NONE,
                    .is_pipeline_member = false,
                },
            .variables =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_envp = false,
                    .copy_exports_only = false,
                    .has_locals = false,
                },
            .positional =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .arg0 = EXEC_ARG0_INHERIT,
                    .argn = EXEC_POSITIONAL_INIT_NA, // SHARE scope: using parent's directly
                    .can_override = false,
                },
            .fds =
                {
                    .scope = EXEC_SCOPE_SHARE,
                },
            .traps =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .resets_non_ignored = false,
                    .exit_trap_runs = false,
                },
            .options =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .errexit_enabled = false,
                },
            .cwd =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_system = false,
                },
            .umask =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_system = false,
                    .init_to_0022 = false,
                },
            .functions =
                {
                    .scope = EXEC_SCOPE_SHARE,
                },
            .aliases =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .expands = true,
                },
            .flow =
                {
                    .return_behavior = EXEC_RETURN_DISALLOWED,
                    .loop_control = EXEC_LOOP_DISALLOWED,
                    .is_loop = false,
                },
            .exit =
                {
                    .terminates_process = false,
                    .affects_parent_status = false,
                },
            .source =
                {
                    .tracks_location = false,
                },
            .classification =
                {
                    .is_subshell = false,
                    .is_background = false,
                },
        },

    /* =========================================================================
     * EXEC_FRAME_BACKGROUND_JOB
     * =========================================================================
     * Asynchronous command: command &
     * Forks, creates its own process group for job control.
     * Traps reset, stdin may be redirected from /dev/null.
     */
    [EXEC_FRAME_BACKGROUND_JOB] =
        {
            .process =
                {
                    .forks = true,
                    .pgroup = EXEC_PGROUP_START,
                    .is_pipeline_member = false,
                },
            .variables =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .init_from_envp = false,
                    .copy_exports_only = false,
                    .has_locals = false,
                },
            .positional =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .arg0 = EXEC_ARG0_INHERIT,
                    .argn = EXEC_POSITIONAL_INIT_NA, // COPY scope: implicit from parent
                    .can_override = false,
                },
            .fds =
                {
                    .scope = EXEC_SCOPE_COPY,
                },
            .traps =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .resets_non_ignored = true,
                    .exit_trap_runs = true,
                },
            .options =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .errexit_enabled = true,
                },
            .cwd =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .init_from_system = false,
                },
            .umask =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .init_from_system = false,
                    .init_to_0022 = false,
                },
            .functions =
                {
                    .scope = EXEC_SCOPE_COPY,
                },
            .aliases =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .expands = true,
                },
            .flow =
                {
                    .return_behavior = EXEC_RETURN_DISALLOWED,
                    .loop_control = EXEC_LOOP_DISALLOWED,
                    .is_loop = false,
                },
            .exit =
                {
                    .terminates_process = true,
                    .affects_parent_status = false,
                },
            .source =
                {
                    .tracks_location = true,
                },
            .classification =
                {
                    .is_subshell = true,
                    .is_background = true,
                },
        },
    /* =========================================================================
     * EXEC_FRAME_PIPELINE
     * =========================================================================
     * Pipeline orchestrator: cmd1 | cmd2 | cmd3
     * Coordinates execution of multiple commands connected by pipes.
     * Does not fork itself; orchestrates child processes for each command.
     * Shares everything with parent (transparent wrapper).
     */
    [EXEC_FRAME_PIPELINE] =
        {
            .process =
                {
                    .forks = false,
                    .pgroup = EXEC_PGROUP_NONE,
                    .is_pipeline_member = false,
                },
            .variables =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_envp = false,
                    .copy_exports_only = false,
                    .has_locals = false,
                },
            .positional =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .arg0 = EXEC_ARG0_INHERIT,
                    .argn = EXEC_POSITIONAL_INIT_NA,
                    .can_override = false,
                },
            .fds =
                {
                    .scope = EXEC_SCOPE_SHARE,
                },
            .traps =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .resets_non_ignored = false,
                    .exit_trap_runs = false,
                },
            .options =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .errexit_enabled = true,
                },
            .cwd =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_system = false,
                },
            .umask =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_system = false,
                    .init_to_0022 = false,
                },
            .functions =
                {
                    .scope = EXEC_SCOPE_SHARE,
                },
            .aliases =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .expands = true,
                },
            .flow =
                {
                    .return_behavior = EXEC_RETURN_TRANSPARENT,
                    .loop_control = EXEC_LOOP_TRANSPARENT,
                    .is_loop = false,
                },
            .exit =
                {
                    .terminates_process = false,
                    .affects_parent_status = true,
                },
            .source =
                {
                    .tracks_location = false,
                },
            .classification =
                {
                    .is_subshell = false,
                    .is_background = false,
                },
        },

    /* =========================================================================
     * EXEC_FRAME_PIPELINE_CMD
     * =========================================================================
     * A command within a pipeline: cmd1 | cmd2 | cmd3
     * Each command (except possibly the last, shell-dependent) runs in a
     * subshell. First command starts process group, others join it.
     */
    [EXEC_FRAME_PIPELINE_CMD] =
        {
            .process =
                {
                    .forks = true,
                    .pgroup = EXEC_PGROUP_PIPELINE,
                    .is_pipeline_member = true,
                },
            .variables =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .init_from_envp = false,
                    .copy_exports_only = false,
                    .has_locals = false,
                },
            .positional =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .arg0 = EXEC_ARG0_INHERIT,
                    .argn = EXEC_POSITIONAL_INIT_NA, // COPY scope: implicit from parent
                    .can_override = false,
                },
            .fds =
                {
                    .scope = EXEC_SCOPE_COPY,
                },
            .traps =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .resets_non_ignored = true,
                    .exit_trap_runs = true,
                },
            .options =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .errexit_enabled = true,
                },
            .cwd =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .init_from_system = false,
                },
            .umask =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .init_from_system = false,
                    .init_to_0022 = false,
                },
            .functions =
                {
                    .scope = EXEC_SCOPE_COPY,
                },
            .aliases =
                {
                    .scope = EXEC_SCOPE_COPY,
                    .expands = true,
                },
            .flow =
                {
                    .return_behavior = EXEC_RETURN_DISALLOWED,
                    .loop_control = EXEC_LOOP_DISALLOWED,
                    .is_loop = false,
                },
            .exit =
                {
                    .terminates_process = true,
                    .affects_parent_status = true,
                },
            .source =
                {
                    .tracks_location = true,
                },
            .classification =
                {
                    .is_subshell = true,
                    .is_background = false,
                },
        },

    /* =========================================================================
     * EXEC_FRAME_DOT_SCRIPT
     * =========================================================================
     * Sourced script: . script.sh [args]  or  source script.sh [args]
     * Runs in current shell. Shares variables (modifications persist).
     * Can temporarily override positional params if args given.
     * Is a return target.
     */
    [EXEC_FRAME_DOT_SCRIPT] =
        {
            .process =
                {
                    .forks = false,
                    .pgroup = EXEC_PGROUP_NONE,
                    .is_pipeline_member = false,
                },
            .variables =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_envp = false,
                    .copy_exports_only = false,
                    .has_locals = false,
                },
            .positional =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .arg0 = EXEC_ARG0_SET_TO_SOURCED_SCRIPT,
                    .argn = EXEC_POSITIONAL_INIT_NA, // SHARE scope: can_override handles
                                                     // replacement
                    .can_override = true,
                },
            .fds =
                {
                    .scope = EXEC_SCOPE_SHARE,
                },
            .traps =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .resets_non_ignored = false,
                    .exit_trap_runs = false,
                },
            .options =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .errexit_enabled = true,
                },
            .cwd =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_system = false,
                },
            .umask =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_system = false,
                    .init_to_0022 = false,
                },
            .functions =
                {
                    .scope = EXEC_SCOPE_SHARE,
                },
            .aliases =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .expands = true,
                },
            .flow =
                {
                    .return_behavior = EXEC_RETURN_TARGET,
                    .loop_control = EXEC_LOOP_DISALLOWED,
                    .is_loop = false,
                },
            .exit =
                {
                    .terminates_process = false,
                    .affects_parent_status = true,
                },
            .source =
                {
                    .tracks_location = true,
                },
            .classification =
                {
                    .is_subshell = false,
                    .is_background = false,
                },
        },

    /* =========================================================================
     * EXEC_FRAME_EVAL
     * =========================================================================
     * eval command: eval "string"
     * Parses and executes string in current shell context.
     * Shares everything with parent. Control flow passes through.
     */
    [EXEC_FRAME_EVAL] =
        {
            .process =
                {
                    .forks = false,
                    .pgroup = EXEC_PGROUP_NONE,
                    .is_pipeline_member = false,
                },
            .variables =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_envp = false,
                    .copy_exports_only = false,
                    .has_locals = false,
                },
            .positional =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .arg0 = EXEC_ARG0_INHERIT,
                    .argn = EXEC_POSITIONAL_INIT_NA, // SHARE scope: using parent's directly
                    .can_override = false,
                },
            .fds =
                {
                    .scope = EXEC_SCOPE_SHARE,
                },
            .traps =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .resets_non_ignored = false,
                    .exit_trap_runs = false,
                },
            .options =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .errexit_enabled = true,
                },
            .cwd =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_system = false,
                },
            .umask =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .init_from_system = false,
                    .init_to_0022 = false,
                },
            .functions =
                {
                    .scope = EXEC_SCOPE_SHARE,
                },
            .aliases =
                {
                    .scope = EXEC_SCOPE_SHARE,
                    .expands = true,
                },
            .flow =
                {
                    .return_behavior = EXEC_RETURN_TRANSPARENT,
                    .loop_control = EXEC_LOOP_TRANSPARENT,
                    .is_loop = false,
                },
            .exit =
                {
                    .terminates_process = false,
                    .affects_parent_status = true,
                },
            .source =
                {
                    .tracks_location = true,
                },
            .classification =
                {
                    .is_subshell = false,
                    .is_background = false,
                },
        },
};

#endif /* EXEC_FRAME_POLICY_H */

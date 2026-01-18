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
 * Redirection Structures
 * ============================================================================ */

/**
 * @brief Types of redirection operations (core operators)
 */
typedef enum
{
    REDIR_READ,        // <      or {var}<
    REDIR_WRITE,       // >      or {var}>
    REDIR_APPEND,      // >>     or {var}>>
    REDIR_READWRITE,   // <>     or {var}<>
    REDIR_WRITE_FORCE, // >|     or {var}>|      (noclobber override)

    REDIR_FD_DUP_IN,  // <&     or {var}<&
    REDIR_FD_DUP_OUT, // >&     or {var}>&

    REDIR_FROM_BUFFER,      // <<     or {var}<<     (heredoc, expand)
    REDIR_FROM_BUFFER_STRIP // <<-    or {var}<<-    (heredoc, strip tabs + expand)
} redirection_type_t;

/**
 * @brief How the target of a redirection is specified
 */
typedef enum
{
    REDIR_TARGET_FILE,       // filename (may need expansion)
    REDIR_TARGET_FD,         // fixed fd number (e.g. <&3) or expanded expression
    REDIR_TARGET_CLOSE,      // >&- or <&-
    REDIR_TARGET_BUFFER,     // heredoc content
    REDIR_TARGET_IO_LOCATION // POSIX 2024: {varname} redirection
} redir_target_kind_t;

/**
 * @brief Runtime representation of a single redirection
 * Used by the executor — decoupled from the AST
 */
typedef struct exec_redirection_t
{
    redirection_type_t type; // The operator: < > >> etc.
    int explicit_fd;         // [n] prefix, or -1 for default (0 for input, 1 for output)

    bool is_io_location; // true → POSIX 2024 {varname} syntax

    string_t *io_location_varname; // Owned — the variable name in {varname} (only meaningful if
                                   // is_io_location)

    redir_target_kind_t target_kind;

    union {
        // ── REDIR_TARGET_FILE ──
        struct
        {
            string_t *raw_filename; // Unexpanded word — always expanded at execution time
            // No "needs_expansion" flag needed — always expand fresh
        } file;

        // ── REDIR_TARGET_FD ── (classic <&3, >&5-11, <&"$fdvar")
        struct
        {
            int fixed_fd;            // If already a literal number after expansion
            string_t *fd_expression; // If it was e.g. "$fdvar" — needs expansion (rare)
        } fd;

        // ── REDIR_TARGET_CLOSE ── (>&-, <&-)
        // No additional data needed

        // ── REDIR_TARGET_BUFFER ── (heredoc)
        struct
        {
            string_t *content;    // Owned — the heredoc payload
            bool needs_expansion; // true  → unquoted delimiter → expand content now
                                  // false → quoted delimiter   → literal content
        } heredoc;

        // ── REDIR_TARGET_IO_LOCATION ── (POSIX 2024 {var}>file.txt etc.)
        struct
        {
            // The target after {varname} — can be file, fd, close
            union {
                string_t *raw_filename; // e.g. file-$HOST.txt — always expand at runtime
                int fixed_fd;
                // close needs no payload
            } payload;
            // No separate needs_expansion for filename — always expanded fresh
        } io_location;
    } target;

    // Optional debug / error reporting info
    int source_line; // Original source line (for error messages)
} exec_redirection_t;

/**
 * @brief Dynamic array of runtime redirections
 * Replaces ast_node_list_t of AST_REDIRECTION nodes
 */
typedef struct exec_redirections_t
{
    exec_redirection_t *items;
    size_t count;
    size_t capacity;
} exec_redirections_t;


/* ============================================================================
 * Execution Frame Structure
 * ============================================================================ */

typedef struct exec_frame_t
{
    exec_frame_type_t type;
    const exec_frame_policy_t *policy;
    struct exec_frame_t *parent;

    /* Scope-dependent Storage
     * If these are EXEC_SCOPE_SHARED: they are shared with the parent frame and must not be freed. */
    variable_store_t *variables;
    positional_params_t *positional_params;
    positional_params_t *saved_positional_params; // for temporary overrides
    func_store_t *functions;
    fd_table_t *open_fds;
    trap_store_t *traps;
    exec_opt_flags_t *opt_flags;
    string_t *working_directory;

    /* Always present */
    int loop_depth;       // 0 if not in loop, else depth of nested loops
    int last_exit_status; // $? last exit status within this frame
    int last_bg_pid;    // $! within this frame

    /* Source Tracking */
    string_t *source_name; // for $BASH_SOURCE
    int source_line;       // for $LINENO

    /* Trap handler state */
    bool in_trap_handler; // prevent recursive trap handling
} exec_frame_t;

typedef struct exec_params_t
{
    exec_frame_type_t frame_type;
    
    /* Optional overrides and inputs */
    string_list_t *arguments; // for dot scripts and eval

    exec_redirections_list_t *redirections;

    /* Loop-specific (only if frame type is loop ) */
    ast_node_t *condition;          // for while/until loops
    bool until_mode;   // true if 'until' loop, false if 'while' loop
    string_list_t *iteration_words; // for 'for' loops
    string_t *loop_var_name; // for 'for' loop
} exec_params_t;

typedef enum exec_control_flow_t
{
    EXEC_FLOW_NORMAL, // normal execution
    EXEC_FLOW_RETURN, // from function
    EXEC_FLOW_BREAK,  // from loop
    EXEC_FLOW_CONTINUE // from loop
} exec_control_flow_t;

typedef struct exec_result_t
{
    int exit_status;
    bool has_exit_status;
    exec_control_flow_t flow; // e.g., return, break, continue
    int flow_depth;           // break 2 == flow depth 2
} exec_result_t;

/* New implementation of exec_t */
typedef struct exec_t
{
    /* Singleton Data */
    alias_store_t *aliases;
    int shell_pid;
    string_t *shell_name;
    int pipe_status;

    // Maybe function store should be here too?

    // line-buffer entry for interactive shells

    // ppid, uid, euid, gid, egid ?


    /* Frames */
    exec_frame_t *top_frame;
};

/* ============================================================================
 * Execution Frame Management
 * ============================================================================ */

exec_frame_t exec_frame_find_return_target(exec_frame_t *frame);
variable_store_t *exec_frame_get_variable_store(exec_frame_t *frame);
fd_entry_t *exec_frame_get_fd_entry(exec_frame_t *frame, int fd);
exec_frame_t *exec_frame_push(exec_frame_t *parent, exec_frame_type_t type);
void exec_frame_pop(exec_frame_t **frame_ptr);

/* The general entry point for "execute something in a frame" */
exec_result_t exec_in_frame(exec_frame_t *frame, exec_params_t *params);

/* Specializations of exec_in_frame */
exec_result_t exec_in_subshell(exec_frame_t *parent, const ast_node_t *node);
exec_result_t exec_in_brace_group(exec_frame_t *parent, const ast_node_t *node);
exec_result_t exec_in_function(exec_frame_t *parent, const ast_node_t *node,
                               const string_list_t *args);
exec_result_t exec_in_loop(exec_frame_t *parent, const ast_node_t *node);
exec_result_t exec_in_trap_handler(exec_frame_t *parent, const ast_node_t *node);


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



#endif /* EXEC_INTERNAL_H */


#include "exec.h"
#include "logging.h"
#include "xalloc.h"
#include "string_t.h"
#include "token.h"
#include "variable_store.h"
#include "func_store.h"
#include "trap_store.h"
#include "sig_act.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef POSIX_API
#include <glob.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#endif
#ifdef UCRT_API
#include <io.h>
#include <errno.h>
#endif

/* ============================================================================
 * Constants and types
 * ============================================================================ */

#define EXECUTOR_ERROR_BUFFER_SIZE 512

#ifdef POSIX_API
typedef struct saved_fd_t
{
    int fd;        // the FD being redirected
    int backup_fd; // duplicate of original FD
} saved_fd_t;
#endif

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

#ifdef POSIX_API
static exec_status_t exec_apply_redirections_posix(exec_t *executor, expander_t *exp,
                                                       const ast_node_list_t *redirs,
                                                       saved_fd_t **out_saved,
                                                       int *out_saved_count);


static void exec_restore_redirections_posix(saved_fd_t *saved, int saved_count);
static exec_status_t exec_execute_pipeline_posix(exec_t *executor, const ast_node_t *node);

#else
static exec_status_t exec_apply_redirections_iso_c(exec_t *executor,
                                                       const ast_node_list_t *redirs);
#endif

/* ============================================================================
 * Executor Lifecycle Functions
 * ============================================================================ */

exec_t *exec_create_from_cfg(exec_cfg_t *cfg)
{
    exec_t *e = xcalloc(1, sizeof(exec_t));

    // ========================================================================
    // Subshell Tracking
    // ========================================================================
    e->parent = NULL;
    e->is_subshell = false;

    // Check if interactive: stdin is a tty
#ifdef POSIX_API
    e->is_interactive = isatty(STDIN_FILENO);
#elifdef UCRT_API
    e->is_interactive = _isatty(_fileno(stdin));
#else
    e->is_interactive = false; // Conservative default for ISO C
#endif

#ifdef POSIX_API
    // Check if login shell: argv[0] starts with '-'
    e->is_login_shell = (cfg->argc > 0 && cfg->argv[0] && cfg->argv[0][0] == '-');
#else
    e->is_login_shell = false; // No standard way in UCRT/ISO C
#endif

    // ========================================================================
    // Working Directory
    // ========================================================================
#ifdef POSIX_API
    char cwd_buffer[PATH_MAX];
    char *cwd = getcwd(cwd_buffer, sizeof(cwd_buffer));
    e->working_directory = cwd ? string_create(cwd) : string_create("/");
#elifdef UCRT_API
    char cwd_buffer[_MAX_PATH];
    char *cwd = _getcwd(cwd_buffer, sizeof(cwd_buffer));
    e->working_directory = cwd ? string_create(cwd) : string_create("C:\\");
#else
    // ISO C has no standard way to get cwd
    e->working_directory = string_create_from_cstr(".");
#endif

    // ========================================================================
    // File Permissions
    // ========================================================================
#ifdef POSIX_API
    // Read current umask by setting and restoring
    mode_t current_mask = umask(0);
    umask(current_mask);
    e->umask = current_mask;

    // Get file size limit
    struct rlimit rlim;
    if (getrlimit(RLIMIT_FSIZE, &rlim) == 0)
    {
        e->file_size_limit = rlim.rlim_cur;
    }
    else
    {
        e->file_size_limit = RLIM_INFINITY;
    }
#elifdef UCRT_API
    int current_mask = _umask(0);
    _umask(current_mask);
    e->umask = current_mask;
    // No file_size_limit on UCRT
#else
    // No umask or file size limits in ISO C
#endif

    // ========================================================================
    // Signal Handling
    // ========================================================================
    e->traps = trap_store_create();
    e->original_signals = sig_act_store_create();

    // ========================================================================
    // Variables & Parameters
    // ========================================================================
    e->variables = variable_store_create();

    // Import environment variables
    if (cfg->envp)
    {
        for (int i = 0; cfg->envp[i] != NULL; i++)
        {
            variable_store_import_from_env(e->variables, cfg->envp[i]);
        }
    }

    // Set standard shell variables
    variable_store_set_cstr(e->variables, "PWD", string_get_cstr(e->working_directory));
    variable_store_set_cstr(e->variables, "SHELL", cfg->argv[0]);

    // Initialize positional parameters from command line
    if (cfg->argc > 1)
    {
        e->positional_params = positional_params_create_from_argv(cfg->argc - 1, (const char **)&(cfg->argv[1]));
    }
    else
    {
        e->positional_params = positional_params_create();
    }

    // ========================================================================
    // Special Parameters
    // ========================================================================
    e->last_exit_status_set = true;
    e->last_exit_status = 0;

    e->last_background_pid_set = false;
    e->last_background_pid = 0;

#ifdef POSIX_API
    e->shell_pid_set = true;
    e->shell_pid = getpid();
#elifdef UCRT_API
    e->shell_pid_set = true;
    e->shell_pid = _getpid();
#else
    e->shell_pid_set = false;
    e->shell_pid = 0; // ISO C has no getpid
#endif

    e->last_argument_set = false;
    e->last_argument = NULL;

    e->shell_name = string_create_from_cstr(cfg->argv[0]);

    // ========================================================================
    // Functions
    // ========================================================================
    e->functions = func_store_create();

    // ========================================================================
    // Shell Options
    // ========================================================================
    e->opt_flags_set = true;
    e->opt = cfg->opt;  // structure value copy

    // ========================================================================
    // Job Control
    // ========================================================================
    e->jobs = job_store_create();
    e->job_control_enabled = e->is_interactive;

#ifdef POSIX_API
    e->pgid = getpgrp();

    // If interactive, set up job control
    if (e->is_interactive)
    {
        // Make sure we're in our own process group
        e->pgid = getpid();
        if (setpgid(0, e->pgid) < 0)
        {
            // Ignore errors - might already be in correct group
        }

        // Take control of the terminal
        tcsetpgrp(STDIN_FILENO, e->pgid);

        // Save terminal settings
        // tcgetattr(STDIN_FILENO, &e->terminal_settings);
    }
#endif

    // ========================================================================
    // File Descriptors
    // ========================================================================
#if defined(POSIX_API) || defined(UCRT_API)
    e->open_fds = fd_table_create();

    // Track standard file descriptors
    fd_table_add(e->open_fds, STDIN_FILENO, NULL, FD_NONE);
    fd_table_add(e->open_fds, STDOUT_FILENO, NULL, FD_NONE);
    fd_table_add(e->open_fds, STDERR_FILENO, NULL, FD_NONE);

    e->next_fd = 3; // First available FD after standard FDs
#endif

    // ========================================================================
    // Aliases
    // ========================================================================
    e->aliases = alias_store_create();

    // TODO: Load aliases from init files (.bashrc, .profile, etc.)

    // ========================================================================
    // Error Reporting
    // ========================================================================
    e->error_msg = NULL;

    return e;
}

// ============================================================================
// EXECUTOR FORK SUBSHELL (Create Subshell Environment)
// ============================================================================

exec_t *exec_create_subshell(exec_t *parent)
{
    Expects_not_null(parent);

    exec_t *e = xcalloc(1, sizeof(exec_t));

    // ========================================================================
    // Subshell Tracking
    // ========================================================================
    e->parent = parent;
    e->is_subshell = true;
    e->is_interactive = parent->is_interactive;
    e->is_login_shell = false; // Subshells are never login shells

    // ========================================================================
    // Working Directory
    // ========================================================================
    e->working_directory = string_create_from(parent->working_directory);

    // ========================================================================
    // File Permissions
    // ========================================================================
#ifdef POSIX_API
    e->umaks = parent->umask;
    e->file_size_limit = parent->file_size_limit;
#elifdef UCRT_API
    e->umask = parent->umask;
#endif

    // ========================================================================
    // Signal Handling
    // ========================================================================
    e->traps = trap_store_copy(parent->traps);

    // Subshells create their own signal disposition tracking
    // (they inherit parent's handlers but track their own changes)
    e->original_signals = sig_act_store_create();

    // ========================================================================
    // Variables & Parameters
    // ========================================================================
    // TODO: Implement variable_store_copy or use alternative approach
    e->variables = variable_store_create();
    // For now, subshells start with empty variable store
    // In future, this should copy or reference parent variables
    e->positional_params = positional_params_copy(parent->positional_params);

    // ========================================================================
    // Special Parameters
    // ========================================================================
    e->last_exit_status_set = parent->last_exit_status_set;
    e->last_exit_status = parent->last_exit_status;

    e->last_background_pid_set = parent->last_background_pid_set;
    e->last_background_pid = parent->last_background_pid;

    // CRITICAL: Shell PID must be queried fresh in subshell!
    e->shell_pid_set = true;
#ifdef POSIX_API
    e->shell_pid = getpid();
#elifdef UCRT_API
    e->shell_pid = _getpid();
#else
    e->shell_pid = parent->shell_pid; // Fallback for ISO C
#endif

    e->last_argument_set = parent->last_argument_set;
    e->last_argument = parent->last_argument ? string_create_from(parent->last_argument) : NULL;

    e->shell_name = string_create_from(parent->shell_name);

    // ========================================================================
    // Functions
    // ========================================================================
    e->functions = func_store_copy(parent->functions);

    // ========================================================================
    // Shell Options
    // ========================================================================
    e->opt_flags_set = true;
    e->opt = parent->opt;  // structure value copy

    // ========================================================================
    // Job Control
    // ========================================================================
    // POSIX: Subshells start with empty job table and job control disabled
    e->jobs = job_store_create();
    e->job_control_enabled = false;

#ifdef POSIX_API
    // Subshell gets its own process group
    e->pgid = getpid();

    // Don't call setpgid here - it will be done after fork in parent
    // if job control is needed
#endif

    // ========================================================================
    // File Descriptors
    // ========================================================================
#if defined(POSIX_API) || defined (UCRT_API)
    // Inherit parent's FDs but create new tracker
    // The actual FDs are inherited at the OS level through fork()
    // TODO: Implement proper FD table copying if needed
    e->open_fds = fd_table_create();

    // Copy parent's next_fd
    e->next_fd = parent->next_fd;
#endif

    // ========================================================================
    // Aliases
    // ========================================================================

    e->aliases = alias_store_copy(parent->aliases);

    // ========================================================================
    // Error Reporting
    // ========================================================================
    e->error_msg = NULL; // Fresh error state

    return e;
}

// ============================================================================
// EXECUTOR CLEANUP
// ============================================================================

void exec_destroy(exec_t **executor_ptr)
{
    if (!executor_ptr || !*executor_ptr)
        return;

    exec_t *executor = *executor_ptr;

    string_destroy(&executor->working_directory);

    trap_store_destroy(&executor->traps);
    sig_act_store_destroy(&executor->original_signals);

    variable_store_destroy(&executor->variables);
    positional_params_destroy(&executor->positional_params);

    if (executor->last_argument)
        string_destroy(&executor->last_argument);
    string_destroy(&executor->shell_name);

    func_store_destroy(&executor->functions);

    job_store_destroy(&executor->jobs);

#if defined(POSIX_API) || defined(UCRT_API)
    fd_table_destroy(&executor->open_fds);
#endif

    alias_store_destroy(&executor->aliases);

    if (executor->error_msg)
        string_destroy(&executor->error_msg);

    xfree(executor);
    *executor_ptr = NULL;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

int exec_get_exit_status(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->last_exit_status;
}

void exec_set_exit_status(exec_t *executor, int status)
{
    Expects_not_null(executor);
    executor->last_exit_status = status;
}

const char *exec_get_error(const exec_t *executor)
{
    Expects_not_null(executor);

    if (string_length(executor->error_msg) == 0)
    {
        return NULL;
    }
    return string_data(executor->error_msg);
}

void exec_set_error(exec_t *executor, const char *format, ...)
{
    Expects_not_null(executor);
    Expects_not_null(format);

    va_list args;
    va_start(args, format);

    char buffer[EXECUTOR_ERROR_BUFFER_SIZE];
    vsnprintf(buffer, sizeof(buffer), format, args);

    va_end(args);

    string_clear(executor->error_msg);
    string_append_cstr(executor->error_msg, buffer);
}

void exec_clear_error(exec_t *executor)
{
    Expects_not_null(executor);
    string_clear(executor->error_msg);
}

void exec_set_dry_run(exec_t *executor, bool dry_run)
{
    Expects_not_null(executor);
#ifdef SH23_EXTENSIONS
    executor->dry_run = dry_run;
#endif
}

/* ============================================================================
 * Execution Functions
 * ============================================================================ */

/**
 * @brief Helper function to populate special shell variables into a variable store
 *
 * Populates POSIX special shell variables ($?, $!, $$, $_, $-) from executor
 * state into the provided variable store.
 *
 * @param store The variable store to populate (must not be NULL)
 * @param ex The executor context containing special variable values (must not be NULL)
 */
static void exec_populate_special_variables(variable_store_t *store, const exec_t *ex)
{
    Expects_not_null(store);
    Expects_not_null(ex);
    uint8_t buf[32];

    // $? - last exit status
    if (ex->last_exit_status_set)
    {
        snprintf(buf, sizeof(buf), "%d", ex->last_exit_status);
        variable_store_add_cstr(store, "?", buf, false, false);
    }

    // $! - last background PID (only if set)
    if (ex->last_background_pid_set)
    {
        snprintf(buf, sizeof(buf), "%d", ex->last_background_pid);
        variable_store_add_cstr(store, "!", buf, false, false);
    }

    // $$ - shell PID
    if (ex->shell_pid_set)
    {
        snprintf(buf, sizeof(buf), "%d", ex->shell_pid);
        variable_store_add_cstr(store, "$", buf, false, false);
    }

    // $_ - last argument
    if (ex->last_argument_set)
    {
        variable_store_add(store, "_", ex->last_argument, false, false);
    }

    // $- - shell flags (construct from opt flags)
    if (ex->opt_flags_set)
    {
        char flags[16];
        int idx = 0;
        
        // Add single-letter flags
        if (ex->opt.allexport) flags[idx++] = 'a';
        if (ex->opt.errexit) flags[idx++] = 'e';
        if (ex->opt.noclobber) flags[idx++] = 'C';
        if (ex->opt.noglob) flags[idx++] = 'f';
        if (ex->opt.noexec) flags[idx++] = 'n';
        if (ex->opt.nounset) flags[idx++] = 'u';
        if (ex->opt.verbose) flags[idx++] = 'v';
        if (ex->opt.xtrace) flags[idx++] = 'x';
        if (ex->is_interactive) flags[idx++] = 'i';
        
        flags[idx] = '\0';
        variable_store_add_cstr(store, "-", flags, false, false);
    }
}

// Copy all variables from parent into dst (values are cloned)
static void variable_store_copy_all(variable_store_t *dst, const variable_store_t *src)
{
    if (!dst || !src || !src->map)
        return;

    for (int32_t i = 0; i < src->map->capacity; i++)
    {
        if (!src->map->entries[i].occupied)
            continue;

        const string_t *name = src->map->entries[i].key;
        const string_t *value = src->map->entries[i].mapped.value;
        bool exported = src->map->entries[i].mapped.exported;
        bool read_only = src->map->entries[i].mapped.read_only;

        variable_store_add(dst, name, value, exported, read_only);
    }
}

/**
 * Build a temporary variable store for a simple command:
 *   - copies all variables from executor->variables
 *   - populates special vars ($?, $!, $$, $_, $-)
 *   - overlays assignment words from the command with expanded RHS
 *
 * Returns NULL on error.
 */
static variable_store_t *exec_build_temp_store_for_simple_command(exec_t *ex,
                                                                      const ast_node_t *node,
                                                                      expander_t *base_exp)
{
    Expects_not_null(ex);
    Expects_not_null(node);
    Expects_eq(node->type, AST_SIMPLE_COMMAND);
    Expects_not_null(base_exp);

    variable_store_t *temp = variable_store_create();
    if (!temp)
        return NULL;

    // 1. Copy parent variables
    variable_store_copy_all(temp, ex->variables);

    // 2. Populate special vars from executor state
    exec_populate_special_variables(temp, ex);

    // 3. Overlay assignment words
    token_list_t *assignments = node->data.simple_command.assignments;
    if (assignments)
    {
        // For assignment RHS expansion we need an expander whose vars are "temp"
        expander_t *assign_exp = expander_create(temp, ex->positional_params);
        if (!assign_exp)
        {
            variable_store_destroy(&temp);
            return NULL;
        }

        // install same system hooks as base_exp
        expander_set_userdata(assign_exp, ex);
        expander_set_getenv(assign_exp, expander_getenv);
        expander_set_tilde_expand(assign_exp, expander_tilde_expand);
        expander_set_glob(assign_exp, exec_pathname_expansion_callback);
        expander_set_command_substitute(assign_exp, exec_command_subst_callback);

        for (int i = 0; i < token_list_size(assignments); i++)
        {
            token_t *tok = token_list_get(assignments, i);
            string_t *value = expander_expand_assignment_value(assign_exp, tok);
            if (!value)
            {
                expander_destroy(&assign_exp);
                variable_store_destroy(&temp);
                return NULL;
            }

            var_store_error_t err =
                variable_store_add(temp, tok->assignment_name, value, /*exported*/ true,
                                   /*read_only*/ false);
            string_destroy(&value);

            if (err != VAR_STORE_ERROR_NONE)
            {
                expander_destroy(&assign_exp);
                variable_store_destroy(&temp);
                return NULL;
            }
        }

        expander_destroy(&assign_exp);
    }

    return temp;
}

__attribute__((unused))
static variable_store_t *exec_prepare_temp_variable_store(exec_t *ex, const ast_node_t *node, expander_t *exp)
{
    Expects_not_null(ex);
    Expects_not_null(node);
    Expects_not_null(exp);

    variable_store_t *temp_store = variable_store_create();

    // ------------------------------------------------------------
    // Add special shell variables from executor state
    // These are added per the Phase 2 requirement to populate
    // special variables in the temp variable store
    // ------------------------------------------------------------
    exec_populate_special_variables(temp_store, ex);

    // ------------------------------------------------------------
    // Extract assignment words from current simple command or function definition
    // ------------------------------------------------------------
    if (node->type == AST_SIMPLE_COMMAND)
    {
        token_list_t *assignments = node->data.simple_command.assignments;
        if (assignments != NULL)
        {
            for (int i = 0; i < token_list_size(assignments); i++)
            {
                token_t *tok = token_list_get(assignments, i);
                string_t *value = expander_expand_assignment_value(exp, tok);
                variable_store_add(temp_store, tok->assignment_name, value, false, false);
                string_destroy(&value);
            }
        }
    }
    else if (node->type == AST_FUNCTION_DEF)
    {
        // Set special variables for function context if needed
        // e.g., $FUNCNAME, $BASH_SOURCE, etc. (not implemented here)

        const string_t *fname = node->data.function_def.name;
        string_t *name = string_create_from_cstr("FUNCNAME");
        variable_store_add(temp_store, name, fname, false, false);
        string_destroy(&name);
    }
    else
    {
        // Other node types can be handled here if needed
    }

    return temp_store;
}

exec_status_t exec_execute(exec_t *executor, const ast_node_t *root)
{
    Expects_not_null(executor);

    if (root == NULL)
    {
        return EXEC_OK;
    }

    exec_clear_error(executor);

    switch (root->type)
    {
    case AST_SIMPLE_COMMAND:
        return exec_execute_simple_command(executor, root);
    case AST_PIPELINE:
        return exec_execute_pipeline(executor, root);
    case AST_AND_OR_LIST:
        return exec_execute_andor_list(executor, root);
    case AST_COMMAND_LIST:
        return exec_execute_command_list(executor, root);
    case AST_SUBSHELL:
        return exec_execute_subshell(executor, root);
    case AST_BRACE_GROUP:
        return exec_execute_brace_group(executor, root);
    case AST_IF_CLAUSE:
        return exec_execute_if_clause(executor, root);
    case AST_WHILE_CLAUSE:
        return exec_execute_while_clause(executor, root);
    case AST_UNTIL_CLAUSE:
        return exec_execute_until_clause(executor, root);
    case AST_FOR_CLAUSE:
        return exec_execute_for_clause(executor, root);
    case AST_CASE_CLAUSE:
        return exec_execute_case_clause(executor, root);
    case AST_FUNCTION_DEF:
        return exec_execute_function_def(executor, root);
    case AST_REDIRECTED_COMMAND:
        return exec_execute_redirected_command(executor, root);
    default:
        exec_set_error(executor, "Unsupported AST node type: %s",
                          ast_node_type_to_string(root->type));
        return EXEC_NOT_IMPL;
    }
}

exec_status_t exec_execute_command_list(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_COMMAND_LIST);

    exec_status_t status = EXEC_OK;

    if (node->data.command_list.items == NULL)
    {
        return EXEC_OK;
    }

    for (int i = 0; i < node->data.command_list.items->size; i++)
    {
        ast_node_t *item = node->data.command_list.items->nodes[i];
        status = exec_execute(executor, item);

        // Handle special case: function definition moved to function store
        if (status == EXEC_OK_INTERNAL_FUNCTION_STORED)
        {
            // The AST node has been moved to the function store.
            // Replace it with a placeholder to prevent double-free.
            ast_node_t *placeholder = ast_node_create_function_stored();
            node->data.command_list.items->nodes[i] = placeholder;
            // Normalize status for upstream
            status = EXEC_OK;
        }

        if (status != EXEC_OK)
        {
            // In a command list, continue execution even if one command fails
            // unless it's a critical error
            continue;
        }

        // Check separator - if background, don't wait
        if (i < ast_node_command_list_separator_count(node))
        {
            cmd_separator_t sep = ast_node_command_list_get_separator(node, i);
            if (sep == LIST_SEP_BACKGROUND)
            {
                // Background execution - in a real shell, fork and don't wait
                // For now, we just note it
            }
        }
    }

    return status;
}

exec_status_t exec_execute_andor_list(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_AND_OR_LIST);

    // Execute left side
    exec_status_t status = exec_execute(executor, node->data.andor_list.left);
    if (status != EXEC_OK)
    {
        return status;
    }

    int left_exit = executor->last_exit_status;

    // Check operator
    if (node->data.andor_list.op == ANDOR_OP_AND)
    {
        // && - execute right only if left succeeded
        if (left_exit == 0)
        {
            status = exec_execute(executor, node->data.andor_list.right);
        }
    }
    else // ANDOR_OP_OR
    {
        // || - execute right only if left failed
        if (left_exit != 0)
        {
            status = exec_execute(executor, node->data.andor_list.right);
        }
    }

    return status;
}


exec_status_t exec_execute_pipeline(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_PIPELINE);

    const ast_node_list_t *cmds = node->data.pipeline.commands;
    bool is_negated = node->data.pipeline.is_negated;

    if (cmds == NULL || ast_node_list_size(cmds) == 0)
    {
        return EXEC_OK;
    }

    int n = ast_node_list_size(cmds);

    /* Single command: no actual pipe needed */
    if (n == 1)
    {
        const ast_node_t *only = ast_node_list_get(cmds, 0);
        exec_status_t st = exec_execute(executor, only);

        if (st == EXEC_OK && is_negated)
        {
            int s = exec_get_exit_status(executor);
            exec_set_exit_status(executor, s == 0 ? 1 : 0);
        }

        return st;
    }

#ifdef POSIX_API
    return exec_execute_pipeline_posix(executor, node);
#elifdef UCRT_API
    exec_set_error(executor, "Pipelines are not yet supported in UCRT_API mode");
    return EXEC_NOT_IMPL;
#else
    exec_set_error(executor, "Pipelines are not supported in ISO_C_API mode");
    return EXEC_ERROR;
#endif
}

#ifdef POSIX_API
static exec_status_t exec_execute_pipeline_posix(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_PIPELINE);

    const ast_node_list_t *cmds = node->data.pipeline.commands;
    bool is_negated = node->data.pipeline.is_negated;
    int n = ast_node_list_size(cmds);

    if (n <= 0)
        return EXEC_OK;

    /* Allocate pipes: n-1 pipes => 2*(n-1) fds */
    int num_pipes = n - 1;
    int(*pipes)[2] = NULL;

    if (num_pipes > 0)
    {
        pipes = xcalloc((size_t)num_pipes, sizeof(int[2]));
        for (int i = 0; i < num_pipes; i++)
        {
            if (pipe(pipes[i]) < 0)
            {
                exec_set_error(executor, "pipe() failed");
                for (int j = 0; j < i; j++)
                {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
                xfree(pipes);
                return EXEC_ERROR;
            }
        }
    }

    pid_t *pids = xcalloc((size_t)n, sizeof(pid_t));

    /* Fork children */
    for (int i = 0; i < n; i++)
    {
        const ast_node_t *cmd = ast_node_list_get(cmds, i);

        // Continue to next pipeline command.
        pid_t pid = fork();
        if (pid < 0)
        {
            exec_set_error(executor, "fork() failed");

            /* Close all pipes */
            for (int k = 0; k < num_pipes; k++)
            {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }
            xfree(pipes);
            xfree(pids);
            return EXEC_ERROR;
        }

        if (pid == 0)
        {
            /* ---------------- CHILD PROCESS ---------------- */

            /* Connect stdin if not first command */
            if (i > 0)
            {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0)
                    _exit(127);
            }

            /* Connect stdout if not last command */
            if (i < n - 1)
            {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0)
                    _exit(127);
            }

            /* Close all pipe fds in child */
            for (int k = 0; k < num_pipes; k++)
            {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }

            /*
             * Now execute this command as a simple command in the child.
             * For now, we only support simple commands in pipelines.
             * You can extend this later to handle AST_REDIRECTED_COMMAND,
             * subshells, etc., with a more generic child helper.
             */
            switch (cmd->type)
            {
            case AST_SIMPLE_COMMAND:
                exec_run_simple_command_child(executor, cmd);
                break;
            case AST_REDIRECTED_COMMAND:
                exec_run_redirected_command_child(executor, cmd);
                break;
            case AST_SUBSHELL:
                exec_run_subshell_child(executor, cmd);
                break;
            case AST_BRACE_GROUP:
                exec_run_brace_group_child(executor, cmd);
                break;
            case AST_FUNCTION_DEF:
                exec_run_function_def_child(executor, cmd);
                break;
            default:
                _exit(127);
            }

            /* Not reached */
            _exit(127);
        }

        /* ---------------- PARENT PROCESS ---------------- */
        pids[i] = pid;
    }

    /* Parent: close all pipe fds */
    for (int k = 0; k < num_pipes; k++)
    {
        close(pipes[k][0]);
        close(pipes[k][1]);
    }
    xfree(pipes);

    /* Wait for all children; record status of last in pipeline */
    int last_status = 0;
    for (int i = 0; i < n; i++)
    {
        int status = 0;
        if (waitpid(pids[i], &status, 0) < 0)
        {
            /* Ignore wait errors for now */
            continue;
        }

        if (i == n - 1)
        {
            int code = 0;
            if (WIFEXITED(status))
                code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                code = 128 + WTERMSIG(status);
            else
                code = 127;

            last_status = code;
        }
    }

    xfree(pids);

    /* Apply negation if needed */
    if (is_negated)
        last_status = (last_status == 0) ? 1 : 0;

    exec_set_exit_status(executor, last_status);
    return EXEC_OK;
}
#endif /* POSIX_API */


/* ============================================================================
 * Execute a simple command
 * ============================================================================ */
exec_status_t exec_execute_simple_command(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_SIMPLE_COMMAND);

#ifdef SH23_EXTENSIONS
    if (executor->dry_run)
    {
        // In dry-run mode we only validate – no execution, no side effects
        exec_set_exit_status(executor, 0);
        return EXEC_OK;
    }
#endif

    exec_status_t status = EXEC_OK;

    const token_list_t *word_tokens = node->data.simple_command.words;
    const token_list_t *assign_tokens = node->data.simple_command.assignments;
    const ast_node_list_t *redirs = node->data.simple_command.redirections;

    bool has_words = (word_tokens && token_list_size(word_tokens) > 0);

    /* ============================================================
     * 0. Create a base expander on the shell's persistent vars
     *    (used for building the temp store and, in the assignment-
     *     only case, for assignment RHS expansion).
     * ============================================================ */
    expander_t *base_exp = expander_create(executor->variables, executor->positional_params);
    if (!base_exp)
    {
        exec_set_error(executor, "failed to create expander");
        return EXEC_ERROR;
    }

    expander_set_userdata(base_exp, executor);
    expander_set_getenv(base_exp, expander_getenv);
    expander_set_tilde_expand(base_exp, expander_tilde_expand);
    expander_set_glob(base_exp, exec_pathname_expansion_callback);
    expander_set_command_substitute(base_exp, exec_command_subst_callback);

    /* ============================================================
     * 1. Assignment-only command: VAR=VAL VAR2=VAL2
     *    - no command name
     *    - semantics: update the shell's own variable store
     * ============================================================ */
    if (!has_words)
    {
        if (assign_tokens)
        {
            for (int i = 0; i < token_list_size(assign_tokens); i++)
            {
                token_t *tok = token_list_get(assign_tokens, i);
                string_t *value = expander_expand_assignment_value(base_exp, tok);
                if (!value)
                {
                    exec_set_error(executor, "assignment expansion failed");
                    status = EXEC_ERROR;
                    goto out_base_exp;
                }

                var_store_error_t err =
                    variable_store_add(executor->variables, tok->assignment_name, value,
                                       /*exported*/ false,
                                       /*read_only*/ false);
                string_destroy(&value);

                if (err != VAR_STORE_ERROR_NONE)
                {
                    exec_set_error(executor, "cannot assign variable (error %d)", err);
                    status = EXEC_ERROR;
                    goto out_base_exp;
                }
            }
        }

        exec_set_exit_status(executor, 0);
        status = EXEC_OK;
        goto out_base_exp;
    }

    /* ============================================================
     * 2. Command with leading assignments: VAR=VAL cmd args...
     *
     *    Build a temporary variable store that:
     *      - copies executor->variables
     *      - populates special vars
     *      - overlays assignment words
     *
     *    All expansions for this command use this temp store.
     * ============================================================ */
    variable_store_t *temp_vars =
        exec_build_temp_store_for_simple_command(executor, node, base_exp);
    if (!temp_vars)
    {
        exec_set_error(executor, "failed to build temporary variable store");
        status = EXEC_ERROR;
        goto out_base_exp;
    }

    expander_t *exp = expander_create(temp_vars, executor->positional_params);
    if (!exp)
    {
        exec_set_error(executor, "failed to create expander");
        variable_store_destroy(&temp_vars);
        status = EXEC_ERROR;
        goto out_base_exp;
    }

    expander_set_userdata(exp, executor);
    expander_set_getenv(exp, expander_getenv);
    expander_set_tilde_expand(exp, expander_tilde_expand);
    expander_set_glob(exp, exec_pathname_expansion_callback);
    expander_set_command_substitute(exp, exec_command_subst_callback);

    /* ============================================================
     * 3. Expand command words with full word expansion
     * ============================================================ */
    string_list_t *expanded_words = expander_expand_words(exp, word_tokens);
    if (!expanded_words || string_list_size(expanded_words) == 0)
    {
        // Empty after expansion: POSIX makes this a successful no-op
        exec_set_exit_status(executor, 0);
        if (expanded_words)
            string_list_destroy(&expanded_words);
        status = EXEC_OK;
        goto out_exp_temp;
    }

    const string_t *cmd_name_str = string_list_at(expanded_words, 0);
    const char *cmd_name = string_cstr(cmd_name_str);

    /* ============================================================
     * 4. Apply redirections
     * ============================================================ */
#ifdef POSIX_API
    saved_fd_t *saved_fds = NULL;
    int saved_count = 0;

    status = exec_apply_redirections_posix(executor, exp, redirs, &saved_fds, &saved_count);
    if (status != EXEC_OK)
    {
        string_list_destroy(&expanded_words);
        goto out_exp_temp;
    }
#elif defined(UCRT_API)
    status = exec_apply_redirections_ucrt_c(executor, redirs);
    if (status != EXEC_OK)
    {
        string_list_destroy(&expanded_words);
        goto out_exp_temp;
    }
#else
    if (redirs && ast_node_list_size(redirs) > 0)
    {
        exec_set_error(executor, "redirections not supported in ISO_C_API mode");
        string_list_destroy(&expanded_words);
        status = EXEC_ERROR;
        goto out_exp_temp;
    }
#endif

    /* ============================================================
     * 5. Execute builtin or external command
     * ============================================================ */
    int cmd_exit_status = 0;

    // Very minimal builtin set; extend as needed.
    bool is_builtin = (strcmp(cmd_name, ":") == 0 || strcmp(cmd_name, "exit") == 0);

    if (is_builtin)
    {
        if (strcmp(cmd_name, ":") == 0)
        {
            cmd_exit_status = 0; // no-op
        }
        else if (strcmp(cmd_name, "exit") == 0)
        {
            // For now, just set status; actual shell exit handling is elsewhere.
            // Optional: parse argument as exit status.
            cmd_exit_status = 0;
        }
    }
    else
    {
#ifdef POSIX_API
        // Build argv[]
        int argc = string_list_size(expanded_words);
        char **argv = xcalloc((size_t)argc + 1, sizeof(char *));
        for (int i = 0; i < argc; i++)
        {
            argv[i] = xstrdup(string_cstr(string_list_at(expanded_words, i)));
        }
        argv[argc] = NULL;

        // Build envp from temp_vars + (non-overridden) parent vars
        char *const *envp = variable_store_update_envp_with_parent(temp_vars, executor->variables);

        pid_t pid = fork();
        if (pid == -1)
        {
            exec_set_error(executor, "fork failed");
            cmd_exit_status = 127;
        }
        else if (pid == 0)
        {
            // Child
            execve(cmd_name, argv, envp);
            // If we reach here execve failed; try PATH search via execvp as fallback
            execvp(cmd_name, argv);
            perror(cmd_name);
            _exit(127);
        }
        else
        {
            // Parent
            int wstatus = 0;
            if (waitpid(pid, &wstatus, 0) < 0)
            {
                cmd_exit_status = 127;
            }
            else if (WIFEXITED(wstatus))
            {
                cmd_exit_status = WEXITSTATUS(wstatus);
            }
            else if (WIFSIGNALED(wstatus))
            {
                cmd_exit_status = 128 + WTERMSIG(wstatus);
            }
            else
            {
                cmd_exit_status = 127;
            }
        }

        // Free argv
        for (int i = 0; argv[i]; i++)
            xfree(argv[i]);
        xfree(argv);

#elif defined(UCRT_API)
        // TODO: build argv + _spawnve/_spawnvpe with envp from temp_vars
        exec_set_error(executor, "UCRT simple command execution not yet implemented");
        cmd_exit_status = 127;
#else
        // ISO_C: fall back to system(), but temp_vars can't influence env
        string_t *cmdline = string_create();
        for (int i = 0; i < string_list_size(expanded_words); i++)
        {
            if (i > 0)
                string_append_cstr(cmdline, " ");
            string_append(cmdline, string_list_at(expanded_words, i));
        }
        int rc = system(string_cstr(cmdline));
        string_destroy(&cmdline);
        if (rc == -1)
        {
            exec_set_error(executor, "system() failed");
            cmd_exit_status = 127;
        }
        else
        {
            cmd_exit_status = rc;
        }
#endif
    }

    /* ============================================================
     * 6. Update special variables and exit status
     * ============================================================ */
    exec_set_exit_status(executor, cmd_exit_status);

    // Update $_ with last argument, if any
    if (string_list_size(expanded_words) > 1)
    {
        const string_t *last_arg =
            string_list_at(expanded_words, string_list_size(expanded_words) - 1);
        string_clear(executor->last_argument);
        string_append(executor->last_argument, last_arg);
        executor->last_argument_set = true;
    }

    string_list_destroy(&expanded_words);

#ifdef POSIX_API
    // Restore redirections
    if (redirs && saved_fds)
    {
        exec_restore_redirections_posix(saved_fds, saved_count);
        xfree(saved_fds);
    }
#endif

    status = EXEC_OK;

out_exp_temp:
    expander_destroy(&exp);
    variable_store_destroy(&temp_vars);

out_base_exp:
    expander_destroy(&base_exp);
    return status;
}

/**
 * Run a simple command in a pipeline child.
 *
 * This function:
 *   - builds a temporary variable store (parent vars + specials + assignments)
 *   - expands words and redirections
 *   - applies redirections
 *   - builds argv/envp
 *   - execs the command
 *
 * It NEVER returns. It always calls _exit().
 *
 * It MUST NOT modify executor->variables or other parent state.
 */
__attribute__((unused))
static void exec_run_simple_command_child(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_SIMPLE_COMMAND);

    const token_list_t *word_tokens = node->data.simple_command.words;
    const token_list_t *assign_tokens = node->data.simple_command.assignments;
    const ast_node_list_t *redirs = node->data.simple_command.redirections;

    /* ============================================================
     * 1. Create base expander on parent variables
     * ============================================================ */
    expander_t *base_exp = expander_create(executor->variables, executor->positional_params);
    if (!base_exp)
    {
#ifdef POSIX_API
        _exit(127);
#elifdef UCRT_API
        _exit(127);
#else
        return;
#endif
    }

    expander_set_userdata(base_exp, executor);
    expander_set_getenv(base_exp, expander_getenv);
    expander_set_tilde_expand(base_exp, expander_tilde_expand);
    expander_set_glob(base_exp, exec_pathname_expansion_callback);
    expander_set_command_substitute(base_exp, exec_command_subst_callback);

    /* ============================================================
     * 2. Build temporary variable store for this command
     * ============================================================ */
    variable_store_t *temp_vars =
        exec_build_temp_store_for_simple_command(executor, node, base_exp);
    if (!temp_vars)
    {
#ifdef POSIX_API
        _exit(127);
#elifdef UCRT_API
        _exit(127);
#else
        return;
#endif
    }

    /* ============================================================
     * 3. Create expander using temp_vars
     * ============================================================ */
    expander_t *exp = expander_create(temp_vars, executor->positional_params);
    if (!exp)
    {
#ifdef POSIX_API
        _exit(127);
#elifdef UCRT_API
        _exit(127);
#else
        return;
#endif
    }

    expander_set_userdata(exp, executor);
    expander_set_getenv(exp, expander_getenv);
    expander_set_tilde_expand(exp, expander_tilde_expand);
    expander_set_glob(exp, exec_pathname_expansion_callback);
    expander_set_command_substitute(exp, exec_command_subst_callback);

    /* ============================================================
     * 4. Expand command words
     * ============================================================ */
    string_list_t *expanded_words = expander_expand_words(exp, word_tokens);
    if (!expanded_words || string_list_size(expanded_words) == 0)
    {
        // Empty command → exit 0
#ifdef POSIX_API
        _exit(0);
#elifdef UCRT_API
        _exit(0);
#else
        return;
#endif
    }

    const string_t *cmd_name_str = string_list_at(expanded_words, 0);
    const char *cmd_name = string_cstr(cmd_name_str);

    /* ============================================================
     * 5. Apply redirections
     * ============================================================ */
#ifdef POSIX_API
    saved_fd_t *saved_fds = NULL;
    int saved_count = 0;

    exec_status_t st =
        exec_apply_redirections_posix(executor, exp, redirs, &saved_fds, &saved_count);
    if (st != EXEC_OK)
        _exit(127);
#else
    if (redirs && ast_node_list_size(redirs) > 0)
    {
        // FIXME: implement redirections for UCRT_API and ISO_C_API
        // for pipeline children.
        _exit(127);
    }
#endif
    /* ============================================================
     * 6. Build argv and envp
     * ============================================================ */
    int argc = string_list_size(expanded_words);
    char **argv = xcalloc((size_t)argc + 1, sizeof(char *));
    for (int i = 0; i < argc; i++)
        argv[i] = xstrdup(string_cstr(string_list_at(expanded_words, i)));
    argv[argc] = NULL;

    // Build environment from temp_vars only (it already contains parent vars)
    char *const *envp = variable_store_update_envp(temp_vars);

    /* ============================================================
     * 7. Exec the command
     * ============================================================ */
#ifdef POSIX_API
    execve(cmd_name, argv, envp);

    // If execve fails, try PATH search
    execvp(cmd_name, argv);
#elifdef UCRT_API
    execve(cmd_name, argv, envp);

    // If execve fails, try PATH search
    execvp(cmd_name, argv);
#else
    // FIXME: implement exec for ISO_C_API
#endif
    // If we reach here, exec failed
    perror(cmd_name);

    // Cleanup before exit (not strictly necessary in child, but polite)
    for (int i = 0; argv[i]; i++)
        xfree(argv[i]);
    xfree(argv);

    _exit(127);
}

#ifdef POSIX_API
/**
 * Run a redirected command in a pipeline child.
 *
 * This:
 *   - expands and applies this node's redirections
 *   - executes the wrapped command in the child
 *
 * It NEVER returns; it always calls _exit().
 */
static void exec_run_redirected_command_child(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_REDIRECTED_COMMAND);

    const ast_node_t *inner = node->data.redirected_command.command;
    const ast_node_list_t *redirs = node->data.redirected_command.redirections;

    /* ------------------------------------------------------------
     * 1. Create an expander for redirection targets.
     *    We use the shell's persistent variables here.
     *    (If you later want redirects to see temp env, you could
     *     pass a different store, but for now this matches your
     *     top-level path.)
     * ------------------------------------------------------------ */
    expander_t *exp = expander_create(executor->variables, executor->positional_params);
    if (!exp)
        _exit(127);

    expander_set_userdata(exp, executor);
    expander_set_getenv(exp, expander_getenv);
    expander_set_tilde_expand(exp, expander_tilde_expand);
    expander_set_glob(exp, exec_pathname_expansion_callback);
    expander_set_command_substitute(exp, exec_command_subst_callback);

    saved_fd_t *saved_fds = NULL;
    int saved_count = 0;

    exec_status_t st =
        exec_apply_redirections_posix(executor, exp, redirs, &saved_fds, &saved_count);
    if (st != EXEC_OK)
    {
        // Redirection failure: in a child, just exit with failure.
        _exit(127);
    }

    /* ------------------------------------------------------------
     * 2. Execute the wrapped command in the child.
     *    We dispatch only the kinds we know how to run in a child.
     * ------------------------------------------------------------ */
    switch (inner->type)
    {
    case AST_SIMPLE_COMMAND:
        exec_run_simple_command_child(executor, inner);
        break;

    case AST_REDIRECTED_COMMAND:
        // Nested redirected-command: recurse in child space.
        exec_run_redirected_command_child(executor, inner);
        break;

    case AST_SUBSHELL:
        // You can later introduce exec_run_subshell_child().
        // For now, treat as not supported in pipeline children.
        _exit(127);
        break;

    case AST_BRACE_GROUP:
        // Likewise, could be a direct execute-in-child helper later.
        _exit(127);
        break;

    default:
        // Other node types in a pipeline child not yet supported.
        _exit(127);
    }

    /* Not reached: inner helpers do _exit() */
    _exit(127);
}
#endif /* POSIX_API */

#ifdef POSIX_API
/**
 * Run a subshell in a pipeline child.
 *
 * This:
 *   - creates a child executor (copy of parent state)
 *   - executes the subshell body
 *   - exits with the subshell's exit status
 *
 * It NEVER returns; it always calls _exit().
 */
static void exec_run_subshell_child(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_SUBSHELL);

    const ast_node_t *body = node->data.compound.body;

    /* ------------------------------------------------------------
     * 1. Create a child executor
     * ------------------------------------------------------------ */
    exec_t *child = exec_create();

    /* Copy variable store */
    variable_store_destroy(&child->variables);
    child->variables = variable_store_create();
    variable_store_copy_all(child->variables, executor->variables);

    /* Copy positional parameters */
    positional_params_destroy(&child->positional_params);
    child->positional_params = positional_params_copy(executor->positional_params);

    /* Copy special variables */
    child->last_exit_status_set = executor->last_exit_status_set;
    child->last_exit_status = executor->last_exit_status;

    child->last_background_pid_set = executor->last_background_pid_set;
    child->last_background_pid = executor->last_background_pid;

    child->shell_pid_set = executor->shell_pid_set;
    child->shell_pid = executor->shell_pid;

    child->last_argument_set = executor->last_argument_set;
    string_destroy(&child->last_argument);
    child->last_argument = string_create_from(executor->last_argument);

    child->shell_flags_set = executor->shell_flags_set;
    string_destroy(&child->shell_flags);
    child->shell_flags = string_create_from(executor->shell_flags);

    /* ------------------------------------------------------------
     * 2. Execute the subshell body
     * ------------------------------------------------------------ */
    exec_status_t st = exec_execute(child, body);

    int exit_code = 0;
    if (st == EXEC_OK)
        exit_code = child->last_exit_status;
    else
        exit_code = 127;

    exec_destroy(&child);
    _exit(exit_code);
}
#endif /* POSIX_API */

#ifdef POSIX_API
/**
 * Run a brace group in a pipeline child.
 *
 * This:
 *   - creates a child executor (copy of parent state)
 *   - executes the brace group's body
 *   - exits with the body's exit status
 *
 * It NEVER returns; it always calls _exit().
 */
static void exec_run_brace_group_child(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_BRACE_GROUP);

    const ast_node_t *body = node->data.compound.body;

    /* ------------------------------------------------------------
     * 1. Create a child executor
     * ------------------------------------------------------------ */
    exec_t *child = exec_create();

    /* Copy variable store */
    variable_store_destroy(&child->variables);
    child->variables = variable_store_create();
    variable_store_copy_all(child->variables, executor->variables);

    /* Copy positional parameters */
    positional_params_destroy(&child->positional_params);
    child->positional_params = positional_params_copy(executor->positional_params);

    /* Copy special variables */
    child->last_exit_status_set = executor->last_exit_status_set;
    child->last_exit_status = executor->last_exit_status;

    child->last_background_pid_set = executor->last_background_pid_set;
    child->last_background_pid = executor->last_background_pid;

    child->shell_pid_set = executor->shell_pid_set;
    child->shell_pid = executor->shell_pid;

    child->last_argument_set = executor->last_argument_set;
    string_destroy(&child->last_argument);
    child->last_argument = string_create_from(executor->last_argument);

    child->shell_flags_set = executor->shell_flags_set;
    string_destroy(&child->shell_flags);
    child->shell_flags = string_create_from(executor->shell_flags);

    /* ------------------------------------------------------------
     * 2. Execute the brace group body
     * ------------------------------------------------------------ */
    exec_status_t st = exec_execute(child, body);

    int exit_code = 0;
    if (st == EXEC_OK)
        exit_code = child->last_exit_status;
    else
        exit_code = 127;

    exec_destroy(&child);
    _exit(exit_code);
}
#endif /* POSIX_API */

exec_status_t exec_execute_redirected_command(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_REDIRECTED_COMMAND);

    const ast_node_t *inner = node->data.redirected_command.command;
    const ast_node_list_t *redirs = node->data.redirected_command.redirections;

#ifdef SH23_EXTENSIONS
    if (executor->dry_run)
    {
        int redir_count = node->data.redirected_command.redirections == NULL
                              ? 0
                              : ast_node_list_size(node->data.redirected_command.redirections);
        printf("[DRY RUN] Redirected command (%d redirection%s)\n", redir_count,
               (redir_count == 1) ? "" : "s");
        return EXEC_OK;
    }
#endif

    // Create an expander on the shell's persistent variables
    expander_t *exp = expander_create(executor->variables, executor->positional_params);
    if (!exp)
    {
        exec_set_error(executor, "failed to create expander");
        return EXEC_ERROR;
    }

    expander_set_userdata(exp, executor);
    expander_set_getenv(exp, expander_getenv);
    expander_set_tilde_expand(exp, expander_tilde_expand);
    expander_set_glob(exp, exec_pathname_expansion_callback);
    expander_set_command_substitute(exp, exec_command_subst_callback);

#ifdef POSIX_API
    saved_fd_t *saved_fds = NULL;
    int saved_count = 0;

    exec_status_t st =
        exec_apply_redirections_posix(executor, exp, redirs, &saved_fds, &saved_count);
    if (st != EXEC_OK)
    {
        expander_destroy(&exp);
        return st;
    }
#else
    exec_status_t st = exec_apply_redirections_iso_c(executor, redirs);
    if (st != EXEC_OK)
    {
        expander_destroy(&exp);
        return st;
    }
#endif

    // Execute the wrapped command
    st = exec_execute(executor, inner);

#ifdef POSIX_API
    // Restore redirections
    if (saved_fds)
    {
        exec_restore_redirections_posix(saved_fds, saved_count);
        xfree(saved_fds);
    }
#endif

    expander_destroy(&exp);
    return st;
}


#ifdef POSIX_API
static exec_status_t exec_apply_redirections_posix(exec_t *executor, expander_t *exp,
                                                       const ast_node_list_t *redirs,
                                                       saved_fd_t **out_saved, int *out_saved_count)
{
    Expects_not_null(executor);
    Expects_not_null(exp);
    Expects_not_null(redirs);
    Expects_not_null(out_saved);
    Expects_not_null(out_saved_count);

    int count = ast_node_list_size(redirs);
    saved_fd_t *saved = xcalloc(count, sizeof(saved_fd_t));
    if (!saved)
    {
        exec_set_error(executor, "Out of memory");
        return EXEC_ERROR;
    }

    int saved_i = 0;

    for (int i = 0; i < count; i++)
    {
        const ast_node_t *r = ast_node_list_get(redirs, i);
        Expects_eq(r->type, AST_REDIRECTION);

        int fd = (r->data.redirection.io_number >= 0 ? r->data.redirection.io_number
                  : (r->data.redirection.redir_type == REDIR_INPUT ||
                     r->data.redirection.redir_type == REDIR_HEREDOC ||
                     r->data.redirection.redir_type == REDIR_HEREDOC_STRIP)
                      ? 0
                      : 1);

        // Save original FD
        int backup = dup(fd);
        if (backup < 0)
        {
            exec_set_error(executor, "dup() failed");
            xfree(saved);
            return EXEC_ERROR;
        }

        saved[saved_i].fd = fd;
        saved[saved_i].backup_fd = backup;
        saved_i++;

        redir_operand_kind_t opk = r->data.redirection.operand;

        switch (opk)
        {

        case REDIR_OPERAND_FILENAME: {
            string_t *fname_str = expander_expand_redirection_target(exp, r->data.redirection.target);
            const char *fname = string_cstr(fname_str);
            int flags = 0;
            mode_t mode = 0666;

            switch (r->data.redirection.redir_type)
            {
            case REDIR_INPUT:
                flags = O_RDONLY;
                break;
            case REDIR_OUTPUT:
                flags = O_WRONLY | O_CREAT | O_TRUNC;
                break;
            case REDIR_APPEND:
                flags = O_WRONLY | O_CREAT | O_APPEND;
                break;
            case REDIR_READWRITE:
                flags = O_RDWR | O_CREAT;
                break;
            case REDIR_CLOBBER:
                flags = O_WRONLY | O_CREAT | O_TRUNC;
                break;
            default:
                exec_set_error(executor, "Invalid filename redirection");
                string_destroy(&fname_str);
                xfree(saved);
                return EXEC_ERROR;
            }

            int newfd = open(fname, flags, mode);
            if (newfd < 0)
            {
                exec_set_error(executor, "Failed to open '%s'", fname);
                string_destroy(&fname_str);
                xfree(saved);
                return EXEC_ERROR;
            }

            if (dup2(newfd, fd) < 0)
            {
                exec_set_error(executor, "dup2() failed");
                string_destroy(&fname_str);
                close(newfd);
                xfree(saved);
                return EXEC_ERROR;
            }

            close(newfd);
            string_destroy(&fname_str);
            break;
        }

        case REDIR_OPERAND_FD: {
            string_t *fd_str = expander_expand_redirection_target(exp, r->data.redirection.target);
            const char *lex = string_cstr(fd_str);
            int src = atoi(lex);

            if (dup2(src, fd) < 0)
            {
                exec_set_error(executor, "dup2(%d,%d) failed", src, fd);
                string_destroy(&fd_str);
                xfree(saved);
                return EXEC_ERROR;
            }
            string_destroy(&fd_str);
            break;
        }

        case REDIR_OPERAND_CLOSE: {
            close(fd);
            break;
        }

        case REDIR_OPERAND_HEREDOC: {
            int pipefd[2];
            if (pipe(pipefd) < 0)
            {
                exec_set_error(executor, "pipe() failed");
                xfree(saved);
                return EXEC_ERROR;
            }

            const char *content = r->data.redirection.heredoc_content
                                      ? string_cstr(r->data.redirection.heredoc_content)
                                      : "";

            write(pipefd[1], content, strlen(content));
            close(pipefd[1]);

            if (dup2(pipefd[0], fd) < 0)
            {
                exec_set_error(executor, "dup2() failed for heredoc");
                close(pipefd[0]);
                xfree(saved);
                return EXEC_ERROR;
            }

            close(pipefd[0]);
            break;
        }

        default:
            exec_set_error(executor, "Unknown redirection operand");
            xfree(saved);
            return EXEC_ERROR;
        }
    }

    *out_saved = saved;
    *out_saved_count = saved_i;
    return EXEC_OK;
}

static void exec_restore_redirections_posix(saved_fd_t *saved, int saved_count)
{
    for (int i = 0; i < saved_count; i++)
    {
        dup2(saved[i].backup_fd, saved[i].fd);
        close(saved[i].backup_fd);
    }
}

#elifdef UCRT_API
static exec_status_t exec_apply_redirections_ucrt_c(exec_t *executor,
                                                         const ast_node_list_t *redirs)
{
    (void)redirs;
    exec_set_error(executor, "Redirections are not yet supported in UCRT_API mode");
    return EXEC_NOT_IMPL;
}
#else
static exec_status_t exec_apply_redirections_iso_c(exec_t *executor,
                                                       const ast_node_list_t *redirs)
{
    (void)redirs;

    exec_set_error(executor, "Redirections are not supported in ISO_C_API mode");

    return EXEC_ERROR;
}
#endif

exec_status_t exec_execute_if_clause(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_IF_CLAUSE);

    // Execute condition
    exec_status_t status = exec_execute(executor, node->data.if_clause.condition);
    if (status != EXEC_OK)
    {
        return status;
    }

    // Check condition result
    if (executor->last_exit_status == 0)
    {
        // Condition succeeded - execute then body
        return exec_execute(executor, node->data.if_clause.then_body);
    }

    // Try elif clauses
    if (node->data.if_clause.elif_list != NULL)
    {
        for (int i = 0; i < node->data.if_clause.elif_list->size; i++)
        {
            ast_node_t *elif_node = node->data.if_clause.elif_list->nodes[i];

            // Execute elif condition
            status = exec_execute(executor, elif_node->data.if_clause.condition);
            if (status != EXEC_OK)
            {
                return status;
            }

            if (executor->last_exit_status == 0)
            {
                // Elif condition succeeded
                return exec_execute(executor, elif_node->data.if_clause.then_body);
            }
        }
    }

    // Execute else body if present
    if (node->data.if_clause.else_body != NULL)
    {
        return exec_execute(executor, node->data.if_clause.else_body);
    }

    return EXEC_OK;
}

exec_status_t exec_execute_while_clause(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_WHILE_CLAUSE);

    exec_status_t status = EXEC_OK;

    while (true)
    {
        // Execute condition
        status = exec_execute(executor, node->data.loop_clause.condition);
        if (status != EXEC_OK)
        {
            break;
        }

        // Check condition result
        if (executor->last_exit_status != 0)
        {
            // Condition failed - exit loop
            break;
        }

        // Execute body
        status = exec_execute(executor, node->data.loop_clause.body);
        if (status != EXEC_OK)
        {
            break;
        }
    }

    return status;
}

exec_status_t exec_execute_until_clause(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_UNTIL_CLAUSE);

    exec_status_t status = EXEC_OK;

    while (true)
    {
        // Execute condition
        status = exec_execute(executor, node->data.loop_clause.condition);
        if (status != EXEC_OK)
        {
            break;
        }

        // Check condition result (inverted compared to while)
        if (executor->last_exit_status == 0)
        {
            // Condition succeeded - exit loop
            break;
        }

        // Execute body
        status = exec_execute(executor, node->data.loop_clause.body);
        if (status != EXEC_OK)
        {
            break;
        }
    }

    return status;
}

exec_status_t exec_execute_for_clause(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_FOR_CLAUSE);

    // For now, not fully implemented
    // Would need to:
    // 1. Expand word list
    // 2. For each word, set variable and execute body
    exec_set_error(executor, "For loop execution not yet implemented");
    return EXEC_NOT_IMPL;
}

exec_status_t exec_execute_case_clause(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_CASE_CLAUSE);

    // For now, not fully implemented
    // Would need to:
    // 1. Expand the word to match
    // 2. For each case item, check if pattern matches
    // 3. Execute matching case body
    exec_set_error(executor, "Case statement execution not yet implemented");
    return EXEC_NOT_IMPL;
}

exec_status_t exec_execute_subshell(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_SUBSHELL);

    const ast_node_t *body = node->data.compound.body;

#ifdef POSIX_API
    pid_t pid = fork();
    if (pid < 0)
    {
        exec_set_error(executor, "fork() failed for subshell");
        return EXEC_ERROR;
    }

    if (pid == 0)
    {
        /* ---------------- CHILD PROCESS ---------------- */

        /* Create a child executor */
        exec_t *child = exec_create();

        /* Copy variable store */
        variable_store_destroy(&child->variables);
        child->variables = variable_store_create();
        variable_store_copy_all(child->variables, executor->variables);

        /* Copy positional parameters */
        positional_params_destroy(&child->positional_params);
        child->positional_params = positional_params_copy(executor->positional_params);

        /* Copy special variables */
        child->last_exit_status_set = executor->last_exit_status_set;
        child->last_exit_status = executor->last_exit_status;

        child->last_background_pid_set = executor->last_background_pid_set;
        child->last_background_pid = executor->last_background_pid;

        child->shell_pid_set = executor->shell_pid_set;
        child->shell_pid = executor->shell_pid;

        child->last_argument_set = executor->last_argument_set;
        string_destroy(&child->last_argument);
        child->last_argument = string_create_from(executor->last_argument);

        child->shell_flags_set = executor->shell_flags_set;
        string_destroy(&child->shell_flags);
        child->shell_flags = string_create_from(executor->shell_flags);

        /* Execute the body */
        exec_status_t st = exec_execute(child, body);

        int exit_code = 0;
        if (st == EXEC_OK)
            exit_code = child->last_exit_status;
        else
            exit_code = 127;

        exec_destroy(&child);
        _exit(exit_code);
    }

    /* ---------------- PARENT PROCESS ---------------- */

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
    {
        exec_set_error(executor, "waitpid() failed for subshell");
        return EXEC_ERROR;
    }

    int exit_code = 0;
    if (WIFEXITED(status))
        exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        exit_code = 128 + WTERMSIG(status);
    else
        exit_code = 127;

    exec_set_exit_status(executor, exit_code);
    return EXEC_OK;

#else
    exec_set_error(executor, "subshells not supported in this build");
    return EXEC_NOT_IMPL;
#endif
}

exec_status_t exec_execute_brace_group(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_BRACE_GROUP);

    const ast_node_t *body = node->data.compound.body;

    // Create an expander for redirection targets
    expander_t *exp = expander_create(executor->variables, executor->positional_params);
    if (!exp)
    {
        exec_set_error(executor, "failed to create expander");
        return EXEC_ERROR;
    }

    expander_set_userdata(exp, executor);
    expander_set_getenv(exp, expander_getenv);
    expander_set_tilde_expand(exp, expander_tilde_expand);
    expander_set_glob(exp, exec_pathname_expansion_callback);
    expander_set_command_substitute(exp, exec_command_subst_callback);

#ifdef POSIX_API
    saved_fd_t *saved_fds = NULL;
    int saved_count = 0;

    exec_status_t st = exec_apply_redirections_posix(
        executor, exp, node->data.redirected_command.redirections, &saved_fds, &saved_count);
    if (st != EXEC_OK)
    {
        expander_destroy(&exp);
        return st;
    }
#else
    exec_status_t st =
        exec_apply_redirections_iso_c(executor, node->data.redirected_command.redirections);
    if (st != EXEC_OK)
    {
        expander_destroy(&exp);
        return st;
    }
#endif

    // Execute the body in the current shell (no fork)
    st = exec_execute(executor, body);

#ifdef POSIX_API
    // Restore redirections
    if (saved_fds)
    {
        exec_restore_redirections_posix(saved_fds, saved_count);
        xfree(saved_fds);
    }
#endif

    expander_destroy(&exp);
    return st;
}


exec_status_t exec_execute_function_def(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_FUNCTION_DEF);

    const string_t *name = node->data.function_def.name;

    if (!executor->functions)
    {
        exec_set_error(executor, "function store is not initialized");
        exec_set_exit_status(executor, 1);
        return EXEC_ERROR;
    }

    func_store_error_t err = func_store_add(executor->functions, name, node);

    if (err != FUNC_STORE_ERROR_NONE)
    {
        switch (err)
        {
        case FUNC_STORE_ERROR_EMPTY_NAME:
            exec_set_error(executor, "empty function name");
            exec_set_exit_status(executor, 1);
            break;

        case FUNC_STORE_ERROR_NAME_TOO_LONG:
            exec_set_error(executor, "function name too long");
            exec_set_exit_status(executor, 1);
            break;

        case FUNC_STORE_ERROR_NAME_INVALID_CHARACTER:
        case FUNC_STORE_ERROR_NAME_STARTS_WITH_DIGIT:
            exec_set_error(executor, "invalid function name");
            exec_set_exit_status(executor, 1);
            break;

        case FUNC_STORE_ERROR_STORAGE_FAILURE:
            exec_set_error(executor, "failed to store function definition");
            exec_set_exit_status(executor, 1);
            break;

        case FUNC_STORE_ERROR_NOT_FOUND:
        default:
            // NOT_FOUND shouldn't really occur on add, but handle generically
            exec_set_error(executor, "internal function store error");
            exec_set_exit_status(executor, 1);
            break;
        }

        return EXEC_ERROR;
    }

    // Successful definition: status 0
    // Return special status to indicate the node has been moved (ownership transferred)
    exec_clear_error(executor);
    exec_set_exit_status(executor, 0);
    return EXEC_OK_INTERNAL_FUNCTION_STORED;
}


/* ============================================================================
 * Visitor Pattern Support
 * ============================================================================ */

static bool ast_traverse_helper(const ast_node_t *node, ast_visitor_fn visitor, void *user_data)
{
    if (node == NULL)
    {
        return true;
    }

    // Call visitor for this node
    if (!visitor(node, user_data))
    {
        return false;
    }

    // Recursively traverse children
    switch (node->type)
    {
    case AST_SIMPLE_COMMAND:
        // No child nodes to traverse (tokens are leaves)
        break;

    case AST_PIPELINE:
        if (node->data.pipeline.commands != NULL)
        {
            for (int i = 0; i < node->data.pipeline.commands->size; i++)
            {
                if (!ast_traverse_helper(node->data.pipeline.commands->nodes[i], visitor, user_data))
                {
                    return false;
                }
            }
        }
        break;

    case AST_AND_OR_LIST:
        if (!ast_traverse_helper(node->data.andor_list.left, visitor, user_data))
        {
            return false;
        }
        if (!ast_traverse_helper(node->data.andor_list.right, visitor, user_data))
        {
            return false;
        }
        break;

    case AST_COMMAND_LIST:
        if (node->data.command_list.items != NULL)
        {
            for (int i = 0; i < node->data.command_list.items->size; i++)
            {
                if (!ast_traverse_helper(node->data.command_list.items->nodes[i], visitor, user_data))
                {
                    return false;
                }
            }
        }
        break;

    case AST_SUBSHELL:
    case AST_BRACE_GROUP:
        if (!ast_traverse_helper(node->data.compound.body, visitor, user_data))
        {
            return false;
        }
        break;

    case AST_IF_CLAUSE:
        if (!ast_traverse_helper(node->data.if_clause.condition, visitor, user_data))
        {
            return false;
        }
        if (!ast_traverse_helper(node->data.if_clause.then_body, visitor, user_data))
        {
            return false;
        }
        if (node->data.if_clause.elif_list != NULL)
        {
            for (int i = 0; i < node->data.if_clause.elif_list->size; i++)
            {
                if (!ast_traverse_helper(node->data.if_clause.elif_list->nodes[i], visitor, user_data))
                {
                    return false;
                }
            }
        }
        if (!ast_traverse_helper(node->data.if_clause.else_body, visitor, user_data))
        {
            return false;
        }
        break;

    case AST_WHILE_CLAUSE:
    case AST_UNTIL_CLAUSE:
        if (!ast_traverse_helper(node->data.loop_clause.condition, visitor, user_data))
        {
            return false;
        }
        if (!ast_traverse_helper(node->data.loop_clause.body, visitor, user_data))
        {
            return false;
        }
        break;

    case AST_FOR_CLAUSE:
        if (!ast_traverse_helper(node->data.for_clause.body, visitor, user_data))
        {
            return false;
        }
        break;

    case AST_CASE_CLAUSE:
        if (node->data.case_clause.case_items != NULL)
        {
            for (int i = 0; i < node->data.case_clause.case_items->size; i++)
            {
                if (!ast_traverse_helper(node->data.case_clause.case_items->nodes[i], visitor, user_data))
                {
                    return false;
                }
            }
        }
        break;

    case AST_CASE_ITEM:
        if (!ast_traverse_helper(node->data.case_item.body, visitor, user_data))
        {
            return false;
        }
        break;

    case AST_FUNCTION_DEF:
        if (!ast_traverse_helper(node->data.function_def.body, visitor, user_data))
        {
            return false;
        }
        if (node->data.function_def.redirections != NULL)
        {
            for (int i = 0; i < node->data.function_def.redirections->size; i++)
            {
                if (!ast_traverse_helper(node->data.function_def.redirections->nodes[i], visitor, user_data))
                {
                    return false;
                }
            }
        }
        break;

    case AST_REDIRECTED_COMMAND:
        if (!ast_traverse_helper(node->data.redirected_command.command, visitor, user_data))
        {
            return false;
        }
        if (node->data.redirected_command.redirections != NULL)
        {
            for (int i = 0; i < node->data.redirected_command.redirections->size; i++)
            {
                if (!ast_traverse_helper(node->data.redirected_command.redirections->nodes[i], visitor, user_data))
                {
                    return false;
                }
            }
        }
        break;

    default:
        break;
    }

    return true;
}

bool ast_traverse(const ast_node_t *root, ast_visitor_fn visitor, void *user_data)
{
    Expects_not_null(visitor);

    return ast_traverse_helper(root, visitor, user_data);
}

/* ============================================================================
 * Expander Callbacks
 * ============================================================================ */

// Record the exit status observed during command substitution. The POSIX rule
// that a simple command with no command name (e.g., pure assignments containing
// substitutions) inherits the last substitution's status is enforced by the
// caller; here we merely stash the status on the executor for later use.
static void exec_record_subst_status(exec_t *executor, int raw_status)
{
    if (executor == NULL)
    {
        return;
    }

#ifdef POSIX_API
    int status = raw_status;
    if (WIFEXITED(raw_status))
    {
        status = WEXITSTATUS(raw_status);
    }
    else if (WIFSIGNALED(raw_status))
    {
        status = 128 + WTERMSIG(raw_status);
    }
#else
    int status = raw_status;
#endif

    exec_set_exit_status(executor, status);
}

/**
 * Command substitution callback for the expander.
 * For now, this is a stub that returns empty output.
 * In a full implementation, this would parse and execute the command.
 */
string_t *exec_command_subst_callback(void *userdata, const string_t *command)
{
#ifdef POSIX_API
    exec_t *executor = (exec_t *)userdata;
    const char *cmd = string_cstr(command);
    if (cmd == NULL || *cmd == '\0')
    {
        exec_record_subst_status(executor, 0);
        return string_create();
    }

    FILE *pipe = popen(cmd, "r");
    if (pipe == NULL)
    {
        log_error("exec_command_subst_callback: popen failed for '%s'", cmd);
        exec_record_subst_status(executor, 1);
        return string_create();
    }

    string_t *output = string_create();
    char buffer[256];

    while (fgets(buffer, sizeof(buffer), pipe) != NULL)
    {
        string_append_cstr(output, buffer);
    }

    int exit_code = pclose(pipe);
    if (exit_code != 0)
    {
        log_debug("exec_command_subst_callback: child exited with code %d for '%s'", exit_code, cmd);
    }
    exec_record_subst_status(executor, exit_code);

    // Trim trailing newlines/carriage returns to approximate shell command substitution behavior
    while (string_length(output) > 0)
    {
        char last = string_back(output);
        if (last == '\n' || last == '\r')
        {
            string_pop_back(output);
        }
        else
        {
            break;
        }
    }

    return output;
#elifdef UCRT_API
    (void)user_data;  // unused for now

    exec_t *executor = (exec_t *)exec_ctx;
    const char *cmd = string_cstr(command);
    if (cmd == NULL || *cmd == '\0')
    {
        exec_record_subst_status(executor, 0);
        return string_create();
    }

    FILE *pipe = _popen(cmd, "r");
    if (pipe == NULL)
    {
        log_error("exec_command_subst_callback: _popen failed for '%s'", cmd);
        exec_record_subst_status(executor, 1);
        return string_create();
    }

    string_t *output = string_create();
    char buffer[256];

    while (fgets(buffer, sizeof(buffer), pipe) != NULL)
    {
        string_append_cstr(output, buffer);
    }

    int exit_code = _pclose(pipe);
    if (exit_code != 0)
    {
        log_debug("exec_command_subst_callback: child exited with code %d for '%s'", exit_code, cmd);
    }
    exec_record_subst_status(executor, exit_code);

    // Trim trailing newlines/carriage returns to approximate shell command substitution behavior
    while (string_length(output) > 0)
    {
        char last = string_back(output);
        if (last == '\n' || last == '\r')
        {
            string_pop_back(output);
        }
        else
        {
            break;
        }
    }

    return output;
#else
    // There is no portable way to do command substitution in ISO_C.
    // You could run a shell process via system(), but, without capturing output.
    (void)command;    // unused
    exec_record_subst_status((exec_t *)userdata, 0);
    return string_create();
#endif
}

/**
 * Pathname expansion (glob) callback for the expander.
 * Platform behavior:
 * - POSIX_API: uses POSIX glob() to expand patterns against the filesystem.
 * - UCRT_API: uses _findfirst/_findnext from <io.h> to expand Windows-style
 *   wildcard patterns in a single directory.
 * - ISO_C (default): no glob implementation; returns NULL so the expander
 *   preserves the literal pattern.
 *
 * Return semantics:
 * - On success with one or more matches: returns a newly allocated
 *   string_list_t containing each matched path (caller must destroy).
 * - On no matches or on error: returns NULL, signaling the expander to keep
 *   the original pattern literal per POSIX behavior.
 */
string_list_t *exec_pathname_expansion_callback(void *user_data, const string_t *pattern)
{
#ifdef POSIX_API
    (void)user_data;  // unused

    const char *pattern_str = string_data(pattern);
    glob_t glob_result;

    // Perform glob matching
    // GLOB_NOCHECK: If no matches, return the pattern itself
    // GLOB_TILDE: Expand ~ for home directory
    int ret = glob(pattern_str, GLOB_TILDE, NULL, &glob_result);

    if (ret != 0) {
        // On error or no matches (when not using GLOB_NOCHECK), return NULL
        if (ret == GLOB_NOMATCH) {
            return NULL;
        }
        // GLOB_NOSPACE or GLOB_ABORTED
        return NULL;
    }

    // No matches found
    if (glob_result.gl_pathc == 0) {
        globfree(&glob_result);
        return NULL;
    }

    // Create result list
    string_list_t *result = string_list_create();

    // Add all matched paths
    for (size_t i = 0; i < glob_result.gl_pathc; i++) {
        string_t *path = string_create_from_cstr(glob_result.gl_pathv[i]);
        string_list_move_push_back(result, path);
    }

    globfree(&glob_result);
    return result;

#elifdef UCRT_API
    (void)user_data;  // unused

    const char *pattern_str = string_data(pattern);
    log_debug("exec_pathname_expansion_callback: UCRT glob pattern='%s'", pattern_str);
    struct _finddata_t fd;
    intptr_t handle;

    // Attempt to find first matching file
    handle = _findfirst(pattern_str, &fd);
    if (handle == -1L) {
        if (errno == ENOENT) {
            // No matches found
            return NULL;
        }
        // Other error (access denied, etc.)
        return NULL;
    }

    // Create result list
    string_list_t *result = string_list_create();

    // Add all matching files
    do {
        // Skip . and .. entries
        if (strcmp(fd.name, ".") == 0 || strcmp(fd.name, "..") == 0)
            continue;

        // Add the matched filename to the result list
        string_t *filename = string_create_from_cstr(fd.name);
        string_list_move_push_back(result, filename);

    } while (_findnext(handle, &fd) == 0);

    _findclose(handle);

    // If no files were added (only . and .. were found), return NULL
    if (string_list_size(result) == 0) {
        string_list_destroy(&result);
        return NULL;
    }

    return result;

#else
    /* In ISO_C environments, no glob implementation is available */
    (void)user_data;  // unused
    string_list_t *result = string_list_create();
    string_list_push_back(result, pattern);
    log_warn("exec_pathname_expansion_callback: No glob implementation available");
    return result;
#endif
}

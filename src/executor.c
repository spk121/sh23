#include "executor.h"
#include "logging.h"
#include "xalloc.h"
#include "string_t.h"
#include "token.h"
#include "variable_store.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef POSIX_API
#include <glob.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
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
static exec_status_t executor_apply_redirections_posix(executor_t *executor,
                                                       const ast_node_list_t *redirs,
                                                       saved_fd_t **out_saved,
                                                       int *out_saved_count);

static void executor_restore_redirections_posix(saved_fd_t *saved, int saved_count);
static exec_status_t executor_execute_pipeline_posix(executor_t *executor, const ast_node_t *node);

#else
static exec_status_t executor_apply_redirections_iso_c(executor_t *executor,
                                                       const ast_node_list_t *redirs);
#endif

/* ============================================================================
 * Executor Lifecycle Functions
 * ============================================================================ */

executor_t *executor_create(void)
{
    executor_t *executor = (executor_t *)xcalloc(1, sizeof(executor_t));
    executor->error_msg = string_create();
    executor->last_exit_status = 0;
    executor->dry_run = false;
    
    // Initialize persistent stores
    executor->variables = variable_store_create();
    executor->positional_params = positional_params_stack_create();
    
    // Initialize special variable fields
    executor->last_background_pid = 0;
#ifdef POSIX_API
    executor->shell_pid = (int)getpid();
#else
    executor->shell_pid = 0;
#endif
    executor->last_argument = string_create();
    executor->shell_flags = string_create();
    
    return executor;
}

void executor_destroy(executor_t **executor)
{
    if (!executor) return;
    executor_t *e = *executor;
    
    if (e == NULL)
        return;

    if (e->error_msg != NULL)
    {
        string_destroy(&e->error_msg);
    }
    
    if (e->variables != NULL)
    {
        variable_store_destroy(&e->variables);
    }
    
    if (e->positional_params != NULL)
    {
        positional_params_stack_destroy(&e->positional_params);
    }
    
    // Clean up special variable fields
    if (e->last_argument != NULL)
    {
        string_destroy(&e->last_argument);
    }
    
    if (e->shell_flags != NULL)
    {
        string_destroy(&e->shell_flags);
    }

    xfree(e);
    *executor = NULL;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

int executor_get_exit_status(const executor_t *executor)
{
    Expects_not_null(executor);
    return executor->last_exit_status;
}

void executor_set_exit_status(executor_t *executor, int status)
{
    Expects_not_null(executor);
    executor->last_exit_status = status;
}

const char *executor_get_error(const executor_t *executor)
{
    Expects_not_null(executor);

    if (string_length(executor->error_msg) == 0)
    {
        return NULL;
    }
    return string_data(executor->error_msg);
}

void executor_set_error(executor_t *executor, const char *format, ...)
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

void executor_clear_error(executor_t *executor)
{
    Expects_not_null(executor);
    string_clear(executor->error_msg);
}

void executor_set_dry_run(executor_t *executor, bool dry_run)
{
    Expects_not_null(executor);
    executor->dry_run = dry_run;
}

/* ============================================================================
 * Execution Functions
 * ============================================================================ */

/**
 * Prepare a temporary variable store for expansion context.
 * 
 * This function creates a temporary variable store populated with:
 * 1. Special POSIX shell variables ($?, $!, $$, $_, $-)
 * 2. Assignment words from the current command (VAR=value)
 * 3. Function-specific variables if in function context
 * 
 * The temporary store is used by the expander to resolve variables
 * before falling back to the persistent variable store.
 * 
 * @param ex   Executor context containing special variable state
 * @param node AST node being executed (can be NULL for non-command contexts)
 * @return A newly created variable_store_t (caller must destroy)
 */
static variable_store_t *executor_prepare_temp_variable_store(executor_t *ex, const ast_node_t *node)
{
    Expects_not_null(ex);

    variable_store_t *temp_store = variable_store_create();
    
    /* ============================================================================
     * Add special POSIX shell variables
     * ============================================================================ */
    
    // $? - Last exit status (always available)
    string_t *exit_str = string_from_int(ex->last_exit_status);
    variable_store_add_cstr(temp_store, "?", string_cstr(exit_str), false, true);
    string_destroy(&exit_str);
    
#ifdef POSIX_API
    // $$ - Shell PID (POSIX only)
    string_t *pid_str = string_from_int(ex->shell_pid);
    variable_store_add_cstr(temp_store, "$", string_cstr(pid_str), false, true);
    string_destroy(&pid_str);
#endif
    
    // $! - Last background process PID (if available)
    if (ex->last_background_pid > 0)
    {
        string_t *bg_str = string_from_int(ex->last_background_pid);
        variable_store_add_cstr(temp_store, "!", string_cstr(bg_str), false, true);
        string_destroy(&bg_str);
    }
    
    // $_ - Last argument of previous command (if available)
    if (string_length(ex->last_argument) > 0)
    {
        variable_store_add_cstr(temp_store, "_", string_cstr(ex->last_argument), false, true);
    }
    
    // $- - Current shell option flags (if available)
    if (string_length(ex->shell_flags) > 0)
    {
        variable_store_add_cstr(temp_store, "-", string_cstr(ex->shell_flags), false, true);
    }
    
    /* ============================================================================
     * Add context-specific variables
     * ============================================================================ */
    
    // Extract assignment words from current simple command or function definition
    if (node && node->type == AST_SIMPLE_COMMAND)
    {
        token_list_t *assignments = node->data.simple_command.assignments;
        if (assignments != NULL)
        {
            for (int i = 0; i < token_list_size(assignments); i++)
            {
                token_t *tok = token_list_get(assignments, i);
                variable_store_add(temp_store, tok->assignment_name, tok->assignment_value, false,
                                   false);
            }
        }
    }
    else if (node && node->type == AST_FUNCTION_DEF)
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

/* ============================================================================
 * Expander Callback Functions
 * ============================================================================ */

/**
 * getenv callback for the expander.
 * 
 * This is called by the expander when it doesn't find a variable in its
 * variable stores. We delegate to the system's getenv() to check environment
 * variables.
 * 
 * @param userdata Pointer to executor_t (not used in this implementation)
 * @param name     Variable name to look up
 * @return The value of the environment variable, or NULL if not found
 */
static const char *executor_getenv_callback(void *userdata, const char *name)
{
    (void)userdata; // executor context not needed for basic getenv
    return getenv(name);
}

/**
 * Tilde expansion callback for the expander.
 * 
 * This callback handles tilde expansion (~, ~/path, ~user/path).
 * For now, we delegate to a default implementation in the expander module.
 * 
 * @param userdata Pointer to executor_t
 * @param input    String containing tilde expression
 * @return Expanded string (caller must destroy), or NULL on error
 */
static string_t *executor_tilde_expand_callback(void *userdata, const string_t *input)
{
    (void)userdata; // Not needed for basic tilde expansion
    
#ifdef POSIX_API
    // On POSIX systems, we can expand ~ to HOME and ~user using getpwnam
    const char *str = string_cstr(input);
    
    if (str[0] != '~')
    {
        // No tilde, return copy
        return string_create_from(input);
    }
    
    // Find end of username (slash or end of string)
    const char *slash = strchr(str, '/');
    int username_len = slash ? (int)(slash - str - 1) : (int)strlen(str) - 1;
    
    const char *home = NULL;
    
    if (username_len == 0)
    {
        // ~ or ~/path - use HOME environment variable
        home = getenv("HOME");
    }
    else
    {
        // ~user or ~user/path - look up user's home directory
        char username[256];
        if (username_len >= (int)sizeof(username))
        {
            username_len = (int)sizeof(username) - 1;
        }
        strncpy(username, str + 1, username_len);
        username[username_len] = '\0';
        
        struct passwd *pw = getpwnam(username);
        if (pw)
        {
            home = pw->pw_dir;
        }
    }
    
    if (!home)
    {
        // Cannot expand, return original
        return string_create_from(input);
    }
    
    // Build expanded path
    string_t *result = string_create_from_cstr(home);
    if (slash)
    {
        string_append_cstr(result, slash);
    }
    
    return result;
#else
    // On non-POSIX systems, try HOME environment variable for ~
    const char *str = string_cstr(input);
    
    if (str[0] != '~')
    {
        return string_create_from(input);
    }
    
    // Only support ~ or ~/path (not ~user)
    if (str[1] != '\0' && str[1] != '/')
    {
        // ~user not supported on non-POSIX
        return string_create_from(input);
    }
    
    const char *home = getenv("HOME");
    if (!home)
    {
        return string_create_from(input);
    }
    
    string_t *result = string_create_from_cstr(home);
    if (str[1] == '/')
    {
        string_append_cstr(result, str + 1);
    }
    
    return result;
#endif
}

/**
 * Create and configure an expander for the executor.
 * 
 * This function creates a new expander instance and wires up all the necessary
 * system callbacks. The expander is configured with:
 * - Persistent variable store from the executor
 * - Positional parameters stack from the executor
 * - System callbacks for getenv, tilde expansion, globbing, and command substitution
 * - The executor itself as userdata for callbacks
 * 
 * The caller is responsible for destroying the expander when done.
 * 
 * @param ex Executor context
 * @return A newly created and configured expander_t (caller must destroy)
 */
static expander_t *executor_create_expander(executor_t *ex)
{
    Expects_not_null(ex);
    
    // Create expander with persistent stores from executor
    expander_t *exp = expander_create(ex->variables, ex->positional_params);
    if (!exp)
    {
        return NULL;
    }
    
    // Wire up all system callbacks
    expander_set_getenv(exp, executor_getenv_callback);
    expander_set_tilde_expand(exp, executor_tilde_expand_callback);
    expander_set_glob(exp, executor_pathname_expansion_callback);
    expander_set_command_substitute(exp, executor_command_subst_callback);
    
    // Pass executor as userdata for callbacks
    expander_set_userdata(exp, ex);
    
    return exp;
}

exec_status_t executor_execute(executor_t *executor, const ast_node_t *root)
{
    Expects_not_null(executor);

    if (root == NULL)
    {
        return EXEC_OK;
    }

    executor_clear_error(executor);

    switch (root->type)
    {
    case AST_SIMPLE_COMMAND:
        return executor_execute_simple_command(executor, root);
    case AST_PIPELINE:
        return executor_execute_pipeline(executor, root);
    case AST_AND_OR_LIST:
        return executor_execute_andor_list(executor, root);
    case AST_COMMAND_LIST:
        return executor_execute_command_list(executor, root);
    case AST_SUBSHELL:
        return executor_execute_subshell(executor, root);
    case AST_BRACE_GROUP:
        return executor_execute_brace_group(executor, root);
    case AST_IF_CLAUSE:
        return executor_execute_if_clause(executor, root);
    case AST_WHILE_CLAUSE:
        return executor_execute_while_clause(executor, root);
    case AST_UNTIL_CLAUSE:
        return executor_execute_until_clause(executor, root);
    case AST_FOR_CLAUSE:
        return executor_execute_for_clause(executor, root);
    case AST_CASE_CLAUSE:
        return executor_execute_case_clause(executor, root);
    case AST_FUNCTION_DEF:
        return executor_execute_function_def(executor, root);
    case AST_REDIRECTED_COMMAND:
        return executor_execute_redirected_command(executor, root);
    default:
        executor_set_error(executor, "Unsupported AST node type: %s",
                          ast_node_type_to_string(root->type));
        return EXEC_NOT_IMPL;
    }
}

exec_status_t executor_execute_command_list(executor_t *executor, const ast_node_t *node)
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
        status = executor_execute(executor, item);

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

exec_status_t executor_execute_andor_list(executor_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_AND_OR_LIST);

    // Execute left side
    exec_status_t status = executor_execute(executor, node->data.andor_list.left);
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
            status = executor_execute(executor, node->data.andor_list.right);
        }
    }
    else // ANDOR_OP_OR
    {
        // || - execute right only if left failed
        if (left_exit != 0)
        {
            status = executor_execute(executor, node->data.andor_list.right);
        }
    }

    return status;
}

exec_status_t executor_execute_pipeline(executor_t *executor, const ast_node_t *node)
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

    /* Single command: no actual pipe needed in any mode */
    if (n == 1)
    {
        const ast_node_t *only = ast_node_list_get(cmds, 0);
        exec_status_t st = executor_execute(executor, only);

        if (st == EXEC_OK && is_negated)
        {
            int s = executor_get_exit_status(executor);
            executor_set_exit_status(executor, s == 0 ? 1 : 0);
        }

        return st;
    }

#ifdef POSIX_API
    /* Real pipeline implementation on POSIX */
    return executor_execute_pipeline_posix(executor, node);
#elifdef UCRT_API
    executor_set_error(executor, "Pipelines are not yet supported in UCRT_API mode");
    return EXEC_NOT_IMPL;
#else
    /* No portable way to implement pipelines with system() */
    executor_set_error(executor, "Pipelines are not supported in ISO_C_API mode");
    return EXEC_ERROR;
#endif
}

#ifdef POSIX_API
static exec_status_t executor_execute_pipeline_posix(executor_t *executor, const ast_node_t *node)
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
                executor_set_error(executor, "pipe() failed");
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

        pid_t pid = fork();
        if (pid < 0)
        {
            executor_set_error(executor, "fork() failed");
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
            /* Child */

            /* Connect stdin if not first command */
            if (i > 0)
            {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0)
                {
                    _exit(127);
                }
            }

            /* Connect stdout if not last command */
            if (i < n - 1)
            {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0)
                {
                    _exit(127);
                }
            }

            /* Close all pipe fds in child */
            for (int k = 0; k < num_pipes; k++)
            {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }

            /* Execute command in child context */
            exec_status_t st = executor_execute(executor, cmd);

            int exit_code = 0;
            if (st == EXEC_OK)
                exit_code = executor_get_exit_status(executor);
            else
                exit_code = 127; /* generic failure */

            _exit(exit_code);
        }

        /* Parent */
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
            /* Normalize to shell exit status convention */
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

    executor_set_exit_status(executor, last_status);
    return EXEC_OK;
}
#endif /* POSIX_API */


exec_status_t executor_execute_simple_command(executor_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_SIMPLE_COMMAND);

    // ------------------------------------------------------------
    // DRY RUN MODE
    // ------------------------------------------------------------
    if (executor->dry_run)
    {
        printf("[DRY RUN] Simple command: ");
        if (node->data.simple_command.words)
        {
            for (int i = 0; i < token_list_size(node->data.simple_command.words); i++)
            {
                token_t *tok = token_list_get(node->data.simple_command.words, i);
                string_t *tok_str = token_to_string(tok);
                printf("%s ", string_data(tok_str));
                string_destroy(&tok_str);
            }
        }
        printf("\n");
        executor->last_exit_status = 0;
        return EXEC_OK;
    }

    // ------------------------------------------------------------
    // Detect assignment-only simple commands
    // ------------------------------------------------------------
    bool has_words =
        (node->data.simple_command.words && token_list_size(node->data.simple_command.words) > 0);

    // FIXME: implement executor_simple_command_has_assignments()
    //bool has_assignments = executor_simple_command_has_assignments(node);
    bool has_assignments =
        (node->data.simple_command.assignments &&
                            token_list_size(node->data.simple_command.assignments) > 0);

    if (!has_words && has_assignments)
    {
        // Apply assignments to current environment
        // FIXME: implement executor_apply_assignments()
        // executor_apply_assignments(executor, node);
        executor->last_exit_status = 0;
        return EXEC_OK;
    }

    if (!has_words)
    {
        // No words, no assignments → nothing to do
        executor->last_exit_status = 0;
        return EXEC_OK;
    }

    // ------------------------------------------------------------
    // Prepare temporary variable store (for assignment words and special vars)
    // ------------------------------------------------------------
    variable_store_t *tmpvars = executor_prepare_temp_variable_store(executor, node);

    // ------------------------------------------------------------
    // Create and configure expander with system callbacks
    // ------------------------------------------------------------
    expander_t *exp = executor_create_expander(executor);
    if (!exp)
    {
        variable_store_destroy(&tmpvars);
        executor_set_error(executor, "Failed to create expander");
        return EXEC_ERROR;
    }

    // ------------------------------------------------------------
    // Expand command words
    // ------------------------------------------------------------
    string_list_t *expanded_words = expander_expand_words(exp, node->data.simple_command.words);

    if (!expanded_words || string_list_size(expanded_words) == 0)
    {
        // Expansion produced nothing → command disappears
        expander_destroy(&exp);
        variable_store_destroy(&tmpvars);
        string_list_destroy(expanded_words);
        executor->last_exit_status = 0;
        return EXEC_OK;
    }

    // First expanded word is the command name
    string_t *cmd_name = string_list_at(expanded_words, 0);

    // ------------------------------------------------------------
    // Expand redirections
    // ------------------------------------------------------------
    const ast_node_list_t *redirs = node->data.simple_command.redirections;

    for (int i = 0; i < ast_node_list_size(redirs); i++)
    {
        const ast_node_t *redir = ast_node_list_get(redirs, i);

        string_t *target = expander_expand_redirection_target(exp, redir->data.redirection.target);

        // TODO: apply redirection using expanded target
        string_destroy(&target);
    }

    // ------------------------------------------------------------
    // Execute the command
    // ------------------------------------------------------------

    // FIXME: implement executor_run_command()
    // exec_status_t status = executor_run_command(executor, expanded_words);
    exec_status_t status = EXEC_NOT_IMPL;

    // ------------------------------------------------------------
    // Cleanup
    // ------------------------------------------------------------
    string_list_destroy(expanded_words);
    expander_destroy(&exp);
    variable_store_destroy(&tmpvars);

    return status;
}

#if defined(POSIX_API)
static exec_status_t executor_execute_simple_command_posix(executor_t *executor,
                                                           const ast_node_t *node)
{
    const token_list_t *words = node->data.simple_command.words;
    const token_list_t *assigns = node->data.simple_command.assignments;
    const ast_node_list_t *redirs = node->data.simple_command.redirections;

    bool has_words = (words && token_list_size(words) > 0);

    /* ============================================================
     * 1. Assignment-only command (no command name)
     * ============================================================ */
    if (!has_words)
    {
        /* These modify the shell's own variable store */
        executor_apply_assignments_to_store(executor, assigns);
        executor_set_exit_status(executor, 0);
        return EXEC_OK;
    }

    /* ============================================================
     * 2. Build temporary variable store for VAR=val cmd
     * ============================================================ */
    variable_store_t *tmp_vars = variable_store_create();

    /* Expand and insert assignment words into tmp_vars */
    executor_apply_assignments_to_store((executor_t *)&(executor_t){.variables = tmp_vars},
                                        assigns);

    /* ============================================================
     * 3. Build envp = exported(parent) + all(tmp_vars)
     * ============================================================ */
    char *const *envp = variable_store_update_envp_with_parent(tmp_vars, executor->variables);

    /* ============================================================
     * 4. Apply redirections
     * ============================================================ */
    saved_fd_t *saved = NULL;
    int saved_count = 0;

    exec_status_t st = executor_apply_redirections_posix(executor, redirs, &saved, &saved_count);

    if (st != EXEC_OK)
    {
        variable_store_destroy(&tmp_vars);
        return st;
    }

    /* ============================================================
     * 5. Fork and exec
     * ============================================================ */
    pid_t pid = fork();
    if (pid < 0)
    {
        executor_set_error(executor, "fork() failed");
        executor_restore_redirections_posix(saved, saved_count);
        free(saved);
        variable_store_destroy(&tmp_vars);
        return EXEC_ERROR;
    }

    if (pid == 0)
    {
        /* ---------------- CHILD PROCESS ---------------- */

        /* Build argv[] */
        int argc = token_list_size(words);
        char **argv = xcalloc(argc + 1, sizeof(char *));
        for (int i = 0; i < argc; i++)
            argv[i] = (char *)token_lexeme(token_list_get(words, i));
        argv[argc] = NULL;

        /* Execute command with merged environment */
        execve(argv[0], argv, envp);

        /* If execve returns, it's an error */
        _exit(127);
    }

    /* ---------------- PARENT PROCESS ---------------- */

    int status = 0;
    waitpid(pid, &status, 0);

    executor_restore_redirections_posix(saved, saved_count);
    free(saved);

    variable_store_destroy(&tmp_vars);

    /* ============================================================
     * 6. Normalize exit status
     * ============================================================ */
    int exit_code = 0;
    if (WIFEXITED(status))
        exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        exit_code = 128 + WTERMSIG(status);
    else
        exit_code = 127;

    executor_set_exit_status(executor, exit_code);
    return EXEC_OK;
}
#else
static exec_status_t executor_execute_simple_command_iso_c(executor_t *executor,
                                                           const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_SIMPLE_COMMAND);

    const token_list_t *words = node->data.simple_command.words;
    const token_list_t *assigns = node->data.simple_command.assignments;
    const ast_node_list_t *redirs = node->data.simple_command.redirections;

    /* ISO C: we cannot support redirections at all */
    if (redirs && ast_node_list_size(redirs) > 0)
    {
        executor_set_error(executor, "Redirections are not supported in ISO_C_API mode");
        return EXEC_ERROR;
    }

    bool has_words = (words && token_list_size(words) > 0);

    /* ============================================================
     * 1. Assignment-only simple command: FOO=1
     *    -> update shell variable store, do not run a command
     * ============================================================ */
    if (!has_words)
    {
        /* These persist in the shell's variable namespace */
        executor_apply_assignments_to_store(executor, assigns);
        executor_set_exit_status(executor, 0);
        return EXEC_OK;
    }

    /* ============================================================
     * 2. Command with temporary assignments: FOO=1 cmd ...
     *    We must:
     *      - create a temporary variable store
     *      - populate it with the assignment words
     *      - conceptually use (tmp + executor->variables) for expansions
     *      - BUT we cannot pass envp to system(), so this only affects
     *        the shell language (expansion) if/when wired.
     * ============================================================ */

    variable_store_t *tmp_vars = variable_store_create();

    /*
     * For now, we reuse executor_apply_assignments_to_store, but we
     * need it to target tmp_vars instead of executor->variables.
     * The clean way is to factor it to accept a variable_store_t*,
     * but as a temporary hack you might adapt it. Here we inline a
     * simple "assignments -> tmp_vars" equivalent.
     */
    if (assigns && token_list_size(assigns) > 0)
    {
        for (int i = 0; i < token_list_size(assigns); i++)
        {
            token_t *tok = token_list_get(assigns, i);
            const char *lex = token_lexeme(tok);
            if (!lex)
                continue;

            const char *eq = strchr(lex, '=');
            if (!eq || eq == lex)
            {
                log_warn("executor (ISO_C): invalid assignment word '%s'", lex);
                continue;
            }

            string_t *name = string_create_from_range(lex, eq, -1);
            string_t *value = string_create_from_cstr(eq + 1);

            /* Temporary vars are always considered "environment-visible"
             * in a logical sense, but ISO_C cannot actually pass envp.
             * Export flag here is mostly conceptual for future use.
             */
            variable_store_add(tmp_vars, name, value, true /*exported logically*/,
                               false /*read_only*/);

            string_destroy(&name);
            string_destroy(&value);
        }
    }

    /*
     * At this point, the *ideal* behavior is:
     *  - expansions see tmp_vars + executor->variables
     * But since the expander isn't wired through here yet, we just
     * build a literal command string from the words.
     */

    string_t *cmd = string_create();
    int word_count = token_list_size(words);
    for (int i = 0; i < word_count; i++)
    {
        if (i > 0)
            string_append_cstr(cmd, " ");

        const char *lex = token_lexeme(token_list_get(words, i));
        if (lex)
            string_append_cstr(cmd, lex);
    }

    /* Execute via system() — no control over child environment */
    int rc = system(string_data(cmd));
    string_destroy(&cmd);
    variable_store_destroy(&tmp_vars);

    if (rc == -1)
    {
        executor_set_error(executor, "system() failed");
        return EXEC_ERROR;
    }

    /* ISO C gives us no standard way to decode rc beyond 0/non-0.
     * We'll just store the raw return code here.
     */
    executor_set_exit_status(executor, rc);
    return EXEC_OK;
}
#endif


exec_status_t executor_execute_redirected_command(executor_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_REDIRECTED_COMMAND);

    if (executor->dry_run)
    {
        int redir_count = node->data.redirected_command.redirections == NULL
                              ? 0
                              : ast_node_list_size(node->data.redirected_command.redirections);
        printf("[DRY RUN] Redirected command (%d redirection%s)\n", redir_count,
               (redir_count == 1) ? "" : "s");
        return EXEC_OK;
    }

    const ast_node_list_t *redirs = node->data.redirected_command.redirections;
    const ast_node_t *command = node->data.redirected_command.command;

    if (redirs == NULL || ast_node_list_size(redirs) == 0)
    {
        // No redirections — just execute the command
        return executor_execute(executor, command);
    }

#ifdef POSIX_API
    saved_fd_t *saved = NULL;
    int saved_count = 0;

    exec_status_t st = executor_apply_redirections_posix(executor, redirs, &saved, &saved_count);

    if (st != EXEC_OK)
    {
        return st;
    }

    // Execute wrapped command
    st = executor_execute(executor, command);

    // Restore original FDs
    executor_restore_redirections_posix(saved, saved_count);
    free(saved);

    return st;
#elifdef UCRT_API
    // TODO: Windows implementation
    executor_set_error(executor, "Redirections not yet implemented on UCRT_API");
    return EXEC_NOT_IMPL;
#else
    return executor_apply_redirections_iso_c(executor, redirs);
#endif
}

#ifdef POSIX_API
static exec_status_t executor_apply_redirections_posix(executor_t *executor,
                                                       const ast_node_list_t *redirs,
                                                       saved_fd_t **out_saved, int *out_saved_count)
{
    Expects_not_null(executor);
    Expects_not_null(redirs);
    Expects_not_null(out_saved);
    Expects_not_null(out_saved_count);

    int count = ast_node_list_size(redirs);
    saved_fd_t *saved = calloc(count, sizeof(saved_fd_t));
    if (!saved)
    {
        executor_set_error(executor, "Out of memory");
        return EXEC_ERROR;
    }

    int saved_i = 0;

    for (int i = 0; i < count; i++)
    {
        const ast_node_t *r = redirs->nodes[i];
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
            executor_set_error(executor, "dup() failed");
            free(saved);
            return EXEC_ERROR;
        }

        saved[saved_i].fd = fd;
        saved[saved_i].backup_fd = backup;
        saved_i++;

        redir_operand_kind_t opk = r->data.redirection.operand;

        switch (opk)
        {

        case REDIR_OPERAND_FILENAME: {
            // Get the filename from the token
            // TODO: Use expander_expand_redirection_target for proper expansion
            string_t *fname_str = token_get_all_text(r->data.redirection.target);
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
                executor_set_error(executor, "Invalid filename redirection");
                string_destroy(&fname_str);
                free(saved);
                return EXEC_ERROR;
            }

            int newfd = open(fname, flags, mode);
            if (newfd < 0)
            {
                executor_set_error(executor, "Failed to open '%s'", fname);
                string_destroy(&fname_str);
                free(saved);
                return EXEC_ERROR;
            }

            if (dup2(newfd, fd) < 0)
            {
                executor_set_error(executor, "dup2() failed");
                string_destroy(&fname_str);
                close(newfd);
                free(saved);
                return EXEC_ERROR;
            }

            close(newfd);
            string_destroy(&fname_str);
            break;
        }

        case REDIR_OPERAND_FD: {
            // Get the FD number from the token
            // TODO: Use expander_expand_redirection_target for proper expansion
            string_t *fd_str = token_get_all_text(r->data.redirection.target);
            const char *lex = string_cstr(fd_str);
            int src = atoi(lex);

            if (dup2(src, fd) < 0)
            {
                executor_set_error(executor, "dup2(%d,%d) failed", src, fd);
                string_destroy(&fd_str);
                free(saved);
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
                executor_set_error(executor, "pipe() failed");
                free(saved);
                return EXEC_ERROR;
            }

            const char *content = r->data.redirection.heredoc_content
                                      ? string_cstr(r->data.redirection.heredoc_content)
                                      : "";

            write(pipefd[1], content, strlen(content));
            close(pipefd[1]);

            if (dup2(pipefd[0], fd) < 0)
            {
                executor_set_error(executor, "dup2() failed for heredoc");
                close(pipefd[0]);
                free(saved);
                return EXEC_ERROR;
            }

            close(pipefd[0]);
            break;
        }

        default:
            executor_set_error(executor, "Unknown redirection operand");
            free(saved);
            return EXEC_ERROR;
        }
    }

    *out_saved = saved;
    *out_saved_count = saved_i;
    return EXEC_OK;
}

static void executor_restore_redirections_posix(saved_fd_t *saved, int saved_count)
{
    for (int i = 0; i < saved_count; i++)
    {
        dup2(saved[i].backup_fd, saved[i].fd);
        close(saved[i].backup_fd);
    }
}

#elifdef UCRT_API
static exec_status_t executor_apply_redirections_ucrt_c(executor_t *executor,
                                                         const ast_node_list_t *redirs)
{
    (void)redirs;
    executor_set_error(executor, "Redirections are not yet supported in UCRT_API mode");
    return EXEC_NOT_IMPL;
}
#else
static exec_status_t executor_apply_redirections_iso_c(executor_t *executor,
                                                       const ast_node_list_t *redirs)
{
    (void)redirs;

    executor_set_error(executor, "Redirections are not supported in ISO_C_API mode");

    return EXEC_ERROR;
}
#endif

exec_status_t executor_execute_if_clause(executor_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_IF_CLAUSE);

    // Execute condition
    exec_status_t status = executor_execute(executor, node->data.if_clause.condition);
    if (status != EXEC_OK)
    {
        return status;
    }

    // Check condition result
    if (executor->last_exit_status == 0)
    {
        // Condition succeeded - execute then body
        return executor_execute(executor, node->data.if_clause.then_body);
    }

    // Try elif clauses
    if (node->data.if_clause.elif_list != NULL)
    {
        for (int i = 0; i < node->data.if_clause.elif_list->size; i++)
        {
            ast_node_t *elif_node = node->data.if_clause.elif_list->nodes[i];
            
            // Execute elif condition
            status = executor_execute(executor, elif_node->data.if_clause.condition);
            if (status != EXEC_OK)
            {
                return status;
            }

            if (executor->last_exit_status == 0)
            {
                // Elif condition succeeded
                return executor_execute(executor, elif_node->data.if_clause.then_body);
            }
        }
    }

    // Execute else body if present
    if (node->data.if_clause.else_body != NULL)
    {
        return executor_execute(executor, node->data.if_clause.else_body);
    }

    return EXEC_OK;
}

exec_status_t executor_execute_while_clause(executor_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_WHILE_CLAUSE);

    exec_status_t status = EXEC_OK;

    while (true)
    {
        // Execute condition
        status = executor_execute(executor, node->data.loop_clause.condition);
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
        status = executor_execute(executor, node->data.loop_clause.body);
        if (status != EXEC_OK)
        {
            break;
        }
    }

    return status;
}

exec_status_t executor_execute_until_clause(executor_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_UNTIL_CLAUSE);

    exec_status_t status = EXEC_OK;

    while (true)
    {
        // Execute condition
        status = executor_execute(executor, node->data.loop_clause.condition);
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
        status = executor_execute(executor, node->data.loop_clause.body);
        if (status != EXEC_OK)
        {
            break;
        }
    }

    return status;
}

exec_status_t executor_execute_for_clause(executor_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_FOR_CLAUSE);

    // For now, not fully implemented
    // Would need to:
    // 1. Expand word list
    // 2. For each word, set variable and execute body
    executor_set_error(executor, "For loop execution not yet implemented");
    return EXEC_NOT_IMPL;
}

exec_status_t executor_execute_case_clause(executor_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_CASE_CLAUSE);

    // For now, not fully implemented
    // Would need to:
    // 1. Expand the word to match
    // 2. For each case item, check if pattern matches
    // 3. Execute matching case body
    executor_set_error(executor, "Case statement execution not yet implemented");
    return EXEC_NOT_IMPL;
}

exec_status_t executor_execute_subshell(executor_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_SUBSHELL);

    // Real implementation would fork and execute in child process
    // For now, just execute in current context
    return executor_execute(executor, node->data.compound.body);
}

exec_status_t executor_execute_brace_group(executor_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_BRACE_GROUP);

    // Execute in current context (no subshell)
    return executor_execute(executor, node->data.compound.body);
}

exec_status_t executor_execute_function_def(executor_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_FUNCTION_DEF);

    // For now, function definitions are not executed - they would be stored
    // in the shell environment for later invocation
    // A real implementation would:
    // 1. Store the function name and body in a function table
    // 2. Return EXEC_OK to indicate successful definition
    // For now, just mark as not implemented
    executor_set_error(executor, "Function definition execution not yet implemented");
    return EXEC_NOT_IMPL;
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
static void executor_record_subst_status(executor_t *executor, int raw_status)
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

    executor_set_exit_status(executor, status);
}

/**
 * Command substitution callback for the expander.
 * For now, this is a stub that returns empty output.
 * In a full implementation, this would parse and execute the command.
 */
string_t *executor_command_subst_callback(const string_t *command, void *executor_ctx, void *user_data)
{
#ifdef POSIX_API
    (void)user_data;  // unused for now

    executor_t *executor = (executor_t *)executor_ctx;
    const char *cmd = string_cstr(command);
    if (cmd == NULL || *cmd == '\0')
    {
        executor_record_subst_status(executor, 0);
        return string_create();
    }

    FILE *pipe = popen(cmd, "r");
    if (pipe == NULL)
    {
        log_error("executor_command_subst_callback: popen failed for '%s'", cmd);
        executor_record_subst_status(executor, 1);
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
        log_debug("executor_command_subst_callback: child exited with code %d for '%s'", exit_code, cmd);
    }
    executor_record_subst_status(executor, exit_code);

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

    executor_t *executor = (executor_t *)executor_ctx;
    const char *cmd = string_cstr(command);
    if (cmd == NULL || *cmd == '\0')
    {
        executor_record_subst_status(executor, 0);
        return string_create();
    }

    FILE *pipe = _popen(cmd, "r");
    if (pipe == NULL)
    {
        log_error("executor_command_subst_callback: _popen failed for '%s'", cmd);
        executor_record_subst_status(executor, 1);
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
        log_debug("executor_command_subst_callback: child exited with code %d for '%s'", exit_code, cmd);
    }
    executor_record_subst_status(executor, exit_code);

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
    executor_record_subst_status((executor_t *)executor_ctx, 0);
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
string_list_t *executor_pathname_expansion_callback(const string_t *pattern, void *user_data)
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
    log_debug("executor_pathname_expansion_callback: UCRT glob pattern='%s'", pattern_str);
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
    log_warn("executor_pathname_expansion_callback: No glob implementation available");
    return result;
#endif
}

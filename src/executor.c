#include "executor.h"
#include "logging.h"
#include "xalloc.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef POSIX_API
#include <glob.h>
#endif
#ifdef UCRT_API
#include <io.h>
#include <errno.h>
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define EXECUTOR_ERROR_BUFFER_SIZE 512

/* ============================================================================
 * Executor Lifecycle Functions
 * ============================================================================ */

executor_t *executor_create(void)
{
    executor_t *executor = (executor_t *)xcalloc(1, sizeof(executor_t));
    executor->error_msg = string_create();
    executor->last_exit_status = 0;
    executor->dry_run = false;
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

    // For now, execute commands sequentially without actual piping
    // A real implementation would set up pipes between commands
    exec_status_t status = EXEC_OK;

    if (node->data.pipeline.commands == NULL)
    {
        return EXEC_OK;
    }

    for (int i = 0; i < node->data.pipeline.commands->size; i++)
    {
        ast_node_t *cmd = node->data.pipeline.commands->nodes[i];
        status = executor_execute(executor, cmd);

        if (status != EXEC_OK)
        {
            break;
        }
    }

    // Handle negation
    if (node->data.pipeline.is_negated && status == EXEC_OK)
    {
        // Invert exit status
        if (executor->last_exit_status == 0)
        {
            executor->last_exit_status = 1;
        }
        else
        {
            executor->last_exit_status = 0;
        }
    }

    return status;
}

exec_status_t executor_execute_simple_command(executor_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_SIMPLE_COMMAND);

    // For now, just validate and print what would be executed
    if (executor->dry_run)
    {
        printf("[DRY RUN] Simple command: ");
        if (node->data.simple_command.words != NULL)
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

    // Real execution would:
    // 1. Expand words (parameter expansion, command substitution, etc.)
    // 2. Apply redirections
    // 3. Execute the command (builtin or external)
    // 4. Restore redirections
    // For now, we just mark as not implemented for actual execution
    executor_set_error(executor, "Actual command execution not yet implemented");
    return EXEC_NOT_IMPL;
}

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
        printf("[DRY RUN] Redirected command (%d redirection%s)\n",
               redir_count,
               (redir_count == 1) ? "" : "s");
    }

    // TODO: apply redirections before executing the wrapped command
    return executor_execute(executor, node->data.redirected_command.command);
}

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

/**
 * Command substitution callback for the expander.
 * For now, this is a stub that returns empty output.
 * In a full implementation, this would parse and execute the command.
 */
string_t *executor_command_subst_callback(const string_t *command, void *user_data)
{
#ifdef POSIX_API
    (void)user_data;  // unused for now

    const char *cmd = string_cstr(command);
    if (cmd == NULL || *cmd == '\0')
    {
        return string_create();
    }

    FILE *pipe = popen(cmd, "r");
    if (pipe == NULL)
    {
        log_error("executor_command_subst_callback: popen failed for '%s'", cmd);
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

    const char *cmd = string_cstr(command);
    if (cmd == NULL || *cmd == '\0')
    {
        return string_create();
    }

    FILE *pipe = _popen(cmd, "r");
    if (pipe == NULL)
    {
        log_error("executor_command_subst_callback: _popen failed for '%s'", cmd);
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
    (void)user_data;  // unused for now
    
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
    (void)pattern;    // unused
    (void)user_data;  // unused
    log_warn("executor_pathname_expansion_callback: No glob implementation available");
    return (string_list_t*)NULL;
#endif
}

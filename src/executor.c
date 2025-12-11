#include "executor.h"
#include "logging.h"
#include "xalloc.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        if (i < node->data.command_list.separator_count)
        {
            list_separator_t sep = node->data.command_list.separators[i];
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

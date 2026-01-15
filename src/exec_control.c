#include "ast.h"
#include "exec_control.h"
#include "exec_internal.h"
#include "logging.h"

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

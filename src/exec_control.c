#include "ast.h"
#include "exec_control.h"
#include "exec_expander.h"
#include "exec_internal.h"
#include "logging.h"
#include "positional_params.h"
#include "string_t.h"
#include "token.h"
#include "variable_store.h"
#include "glob_util.h"

#ifdef POSIX_API
#include <fnmatch.h>
#endif

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/* ============================================================================
 * If/Elif/Else Execution
 * ============================================================================ */

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

/* ============================================================================
 * While Loop Execution
 * ============================================================================ */

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
            // Control flow or error in condition
            if (status == EXEC_BREAK || status == EXEC_CONTINUE)
            {
                // Break/continue in condition is unusual but valid
                // POSIX: break in condition should break the loop
                status = EXEC_OK;
            }
            break;
        }

        // Check condition result
        if (executor->last_exit_status != 0)
        {
            // Condition failed - exit loop normally
            status = EXEC_OK;
            break;
        }

        // Execute body
        status = exec_execute(executor, node->data.loop_clause.body);
        
        if (status == EXEC_BREAK)
        {
            // Break out of loop
            status = EXEC_OK;
            break;
        }
        else if (status == EXEC_CONTINUE)
        {
            // Continue to next iteration
            status = EXEC_OK;
            continue;
        }
        else if (status != EXEC_OK)
        {
            // Error, return, or exit
            break;
        }
    }

    return status;
}

/* ============================================================================
 * Until Loop Execution
 * ============================================================================ */

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
            // Control flow or error in condition
            if (status == EXEC_BREAK || status == EXEC_CONTINUE)
            {
                status = EXEC_OK;
            }
            break;
        }

        // Check condition result (inverted compared to while)
        if (executor->last_exit_status == 0)
        {
            // Condition succeeded - exit loop normally
            status = EXEC_OK;
            break;
        }

        // Execute body
        status = exec_execute(executor, node->data.loop_clause.body);
        
        if (status == EXEC_BREAK)
        {
            status = EXEC_OK;
            break;
        }
        else if (status == EXEC_CONTINUE)
        {
            status = EXEC_OK;
            continue;
        }
        else if (status != EXEC_OK)
        {
            break;
        }
    }

    return status;
}

/* ============================================================================
 * For Loop Execution
 * ============================================================================ */

exec_status_t exec_execute_for_clause(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_FOR_CLAUSE);

    const string_t *var_name = node->data.for_clause.variable;
    const token_list_t *word_tokens = node->data.for_clause.words;
    const ast_node_t *body = node->data.for_clause.body;

    if (!var_name)
    {
        exec_set_error(executor, "for loop missing variable name");
        return EXEC_ERROR;
    }

    string_list_t *word_list = NULL;

    // Expand word list (or use positional parameters if omitted)
    if (word_tokens && token_list_size(word_tokens) > 0)
    {
        // Expand the word list
        word_list = exec_expand_words(executor, word_tokens);
    }
    else
    {
        // No word list: use positional parameters "$@"
        word_list = string_list_create();
        if (executor->positional_params)
        {
            int count = positional_params_count(executor->positional_params);
            for (int i = 1; i <= count; i++)
            {
                const string_t *param = positional_params_get(executor->positional_params, i);
                if (param)
                {
                    string_t *param_str = string_create_from(param);
                    string_list_move_push_back(word_list, &param_str);
                }
            }
        }
    }

    // Execute loop body for each word
    exec_status_t status = EXEC_OK;
    int word_count = string_list_size(word_list);

    for (int i = 0; i < word_count; i++)
    {
        const string_t *word = string_list_at(word_list, i);

        // Set loop variable
        var_store_error_t err = variable_store_add(executor->variables, var_name, word,
                                                    false, false);
        if (err != VAR_STORE_ERROR_NONE)
        {
            exec_set_error(executor, "failed to set for loop variable");
            status = EXEC_ERROR;
            break;
        }

        // Execute body
        if (body)
        {
            status = exec_execute(executor, body);

            if (status == EXEC_BREAK)
            {
                // Break out of loop
                status = EXEC_OK;
                break;
            }
            else if (status == EXEC_CONTINUE)
            {
                // Continue to next iteration
                status = EXEC_OK;
                continue;
            }
            else if (status != EXEC_OK)
            {
                // Error, return, or exit - propagate up
                break;
            }
        }
    }

    string_list_destroy(&word_list);
    return status;
}

/* ============================================================================
 * Case Statement Execution
 * ============================================================================ */

exec_status_t exec_execute_case_clause(exec_t *executor, const ast_node_t *node)
{
    Expects_not_null(executor);
    Expects_not_null(node);
    Expects_eq(node->type, AST_CASE_CLAUSE);

    const token_t *word_token = node->data.case_clause.word;
    const ast_node_list_t *case_items = node->data.case_clause.case_items;

    if (!word_token)
    {
        exec_set_error(executor, "case statement missing word to match");
        return EXEC_ERROR;
    }

    // Expand the word to match
    string_list_t *word_list = exec_expand_word(executor, word_token);
    string_t *expanded_word = string_list_join_move(&word_list, " ");
    if (!expanded_word)
    {
        exec_set_error(executor, "failed to expand case word");
        return EXEC_ERROR;
    }

    const char *word_str = string_cstr(expanded_word);
    exec_status_t status = EXEC_OK;
    bool matched = false;

    // Try each case item
    if (case_items)
    {
        for (int i = 0; i < case_items->size && !matched; i++)
        {
            const ast_node_t *case_item = case_items->nodes[i];
            
            if (case_item->type != AST_CASE_ITEM)
            {
                continue;
            }

            const token_list_t *patterns = case_item->data.case_item.patterns;
            
            // Check each pattern in this case item
            if (patterns)
            {
                for (int j = 0; j < token_list_size(patterns); j++)
                {
                    token_t *pattern_token = token_list_get(patterns, j);
                    
                    // Expand the pattern (patterns can contain variables, etc.)
                    string_list_t *pattern_list =
                        exec_expand_word(executor, pattern_token);
                    string_t *expanded_pattern = string_list_join_move(&pattern_list, " ");
                    if (!expanded_pattern)
                    {
                        continue;
                    }

                    const char *pattern_str = string_cstr(expanded_pattern);

                    // Match pattern against word
#ifdef POSIX_API
                    int match_result = fnmatch(pattern_str, word_str, 0);
                    bool pattern_matches = (match_result == 0);
#else
                    bool pattern_matches = glob_util_match(pattern_str, word_str, 0);
#endif

                    string_destroy(&expanded_pattern);

                    if (pattern_matches)
                    {
                        matched = true;
                        
                        // Execute the case item body
                        const ast_node_t *body = case_item->data.case_item.body;
                        if (body)
                        {
                            status = exec_execute(executor, body);
                        }
                        break;
                    }
                }
            }
        }
    }

    string_destroy(&expanded_word);

    // If no match found, case statement succeeds with exit status 0
    if (!matched)
    {
        exec_set_exit_status(executor, 0);
    }

    return status;
}

#include "ast.h"
#include "logging.h"
#include "xalloc.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

#define INITIAL_LIST_CAPACITY 8

/* ============================================================================
 * AST Node Lifecycle Functions
 * ============================================================================ */

ast_node_t *ast_node_create(ast_node_type_t type)
{
    ast_node_t *node = (ast_node_t *)xcalloc(1, sizeof(ast_node_t));
    node->type = type;
    return node;
}

void ast_node_destroy(ast_node_t *node)
{
    if (node == NULL)
        return;

    switch (node->type)
    {
    case AST_SIMPLE_COMMAND:
        if (node->data.simple_command.words != NULL)
        {
            // Don't destroy tokens - they're owned by the parser's token list
            token_list_clear_without_destroy(node->data.simple_command.words);
            xfree(node->data.simple_command.words->tokens);
            xfree(node->data.simple_command.words);
        }
        if (node->data.simple_command.redirections != NULL)
        {
            ast_node_list_destroy(node->data.simple_command.redirections);
        }
        if (node->data.simple_command.assignments != NULL)
        {
            // Don't destroy tokens - they're owned by the parser's token list
            token_list_clear_without_destroy(node->data.simple_command.assignments);
            xfree(node->data.simple_command.assignments->tokens);
            xfree(node->data.simple_command.assignments);
        }
        break;

    case AST_PIPELINE:
        if (node->data.pipeline.commands != NULL)
        {
            ast_node_list_destroy(node->data.pipeline.commands);
        }
        break;

    case AST_AND_OR_LIST:
        ast_node_destroy(node->data.andor_list.left);
        ast_node_destroy(node->data.andor_list.right);
        break;

    case AST_COMMAND_LIST:
        if (node->data.command_list.items != NULL)
        {
            ast_node_list_destroy(node->data.command_list.items);
        }
        if (node->data.command_list.separators != NULL)
        {
            xfree(node->data.command_list.separators);
        }
        break;

    case AST_SUBSHELL:
    case AST_BRACE_GROUP:
        ast_node_destroy(node->data.compound.body);
        break;

    case AST_IF_CLAUSE:
        ast_node_destroy(node->data.if_clause.condition);
        ast_node_destroy(node->data.if_clause.then_body);
        if (node->data.if_clause.elif_list != NULL)
        {
            ast_node_list_destroy(node->data.if_clause.elif_list);
        }
        ast_node_destroy(node->data.if_clause.else_body);
        break;

    case AST_WHILE_CLAUSE:
    case AST_UNTIL_CLAUSE:
        ast_node_destroy(node->data.while_clause.condition);
        ast_node_destroy(node->data.while_clause.body);
        break;

    case AST_FOR_CLAUSE:
        if (node->data.for_clause.variable != NULL)
        {
            string_destroy(node->data.for_clause.variable);
        }
        if (node->data.for_clause.words != NULL)
        {
            // Don't destroy tokens - they're owned by the parser's token list
            token_list_clear_without_destroy(node->data.for_clause.words);
            xfree(node->data.for_clause.words->tokens);
            xfree(node->data.for_clause.words);
        }
        ast_node_destroy(node->data.for_clause.body);
        break;

    case AST_CASE_CLAUSE:
        if (node->data.case_clause.word != NULL)
        {
            // Don't destroy token - it's owned by the parser's token list
            node->data.case_clause.word = NULL;
        }
        if (node->data.case_clause.case_items != NULL)
        {
            ast_node_list_destroy(node->data.case_clause.case_items);
        }
        break;

    case AST_CASE_ITEM:
        if (node->data.case_item.patterns != NULL)
        {
            // Don't destroy tokens - they're owned by the parser's token list
            token_list_clear_without_destroy(node->data.case_item.patterns);
            xfree(node->data.case_item.patterns->tokens);
            xfree(node->data.case_item.patterns);
        }
        ast_node_destroy(node->data.case_item.body);
        break;

    case AST_REDIRECTION:
        if (node->data.redirection.target != NULL)
        {
            // Don't destroy token - it's owned by the parser's token list
            node->data.redirection.target = NULL;
        }
        if (node->data.redirection.heredoc_content != NULL)
        {
            string_destroy(node->data.redirection.heredoc_content);
        }
        break;

    case AST_WORD:
        if (node->data.word.token != NULL)
        {
            // Don't destroy token - it's owned by the parser's token list
            node->data.word.token = NULL;
        }
        break;

    default:
        break;
    }

    xfree(node);
}

/* ============================================================================
 * AST Node Accessors
 * ============================================================================ */

ast_node_type_t ast_node_get_type(const ast_node_t *node)
{
    Expects_not_null(node);
    return node->type;
}

void ast_node_set_location(ast_node_t *node, int first_line, int first_column,
                          int last_line, int last_column)
{
    Expects_not_null(node);
    node->first_line = first_line;
    node->first_column = first_column;
    node->last_line = last_line;
    node->last_column = last_column;
}

/* ============================================================================
 * AST Node Creation Helpers
 * ============================================================================ */

ast_node_t *ast_create_simple_command(token_list_t *words,
                                     ast_node_list_t *redirections,
                                     token_list_t *assignments)
{
    ast_node_t *node = ast_node_create(AST_SIMPLE_COMMAND);
    node->data.simple_command.words = words;
    node->data.simple_command.redirections = redirections;
    node->data.simple_command.assignments = assignments;
    return node;
}

ast_node_t *ast_create_pipeline(ast_node_list_t *commands, bool is_negated)
{
    ast_node_t *node = ast_node_create(AST_PIPELINE);
    node->data.pipeline.commands = commands;
    node->data.pipeline.is_negated = is_negated;
    return node;
}

ast_node_t *ast_create_andor_list(ast_node_t *left, ast_node_t *right,
                                 andor_operator_t op)
{
    ast_node_t *node = ast_node_create(AST_AND_OR_LIST);
    node->data.andor_list.left = left;
    node->data.andor_list.right = right;
    node->data.andor_list.op = op;
    return node;
}

ast_node_t *ast_create_command_list(void)
{
    ast_node_t *node = ast_node_create(AST_COMMAND_LIST);
    node->data.command_list.items = ast_node_list_create();
    node->data.command_list.separators = NULL;
    node->data.command_list.separator_count = 0;
    return node;
}

ast_node_t *ast_create_subshell(ast_node_t *body)
{
    ast_node_t *node = ast_node_create(AST_SUBSHELL);
    node->data.compound.body = body;
    return node;
}

ast_node_t *ast_create_brace_group(ast_node_t *body)
{
    ast_node_t *node = ast_node_create(AST_BRACE_GROUP);
    node->data.compound.body = body;
    return node;
}

ast_node_t *ast_create_if_clause(ast_node_t *condition, ast_node_t *then_body)
{
    ast_node_t *node = ast_node_create(AST_IF_CLAUSE);
    node->data.if_clause.condition = condition;
    node->data.if_clause.then_body = then_body;
    node->data.if_clause.elif_list = NULL;
    node->data.if_clause.else_body = NULL;
    return node;
}

ast_node_t *ast_create_while_clause(ast_node_t *condition, ast_node_t *body)
{
    ast_node_t *node = ast_node_create(AST_WHILE_CLAUSE);
    node->data.while_clause.condition = condition;
    node->data.while_clause.body = body;
    return node;
}

ast_node_t *ast_create_until_clause(ast_node_t *condition, ast_node_t *body)
{
    ast_node_t *node = ast_node_create(AST_UNTIL_CLAUSE);
    node->data.while_clause.condition = condition;
    node->data.while_clause.body = body;
    return node;
}

ast_node_t *ast_create_for_clause(const string_t *variable, token_list_t *words,
                                 ast_node_t *body)
{
    ast_node_t *node = ast_node_create(AST_FOR_CLAUSE);
    node->data.for_clause.variable = string_clone(variable);
    node->data.for_clause.words = words;
    node->data.for_clause.body = body;
    return node;
}

ast_node_t *ast_create_case_clause(token_t *word)
{
    ast_node_t *node = ast_node_create(AST_CASE_CLAUSE);
    node->data.case_clause.word = word;
    node->data.case_clause.case_items = ast_node_list_create();
    return node;
}

ast_node_t *ast_create_case_item(token_list_t *patterns, ast_node_t *body)
{
    ast_node_t *node = ast_node_create(AST_CASE_ITEM);
    node->data.case_item.patterns = patterns;
    node->data.case_item.body = body;
    return node;
}

ast_node_t *ast_create_redirection(redirection_type_t redir_type, int io_number,
                                  token_t *target)
{
    ast_node_t *node = ast_node_create(AST_REDIRECTION);
    node->data.redirection.redir_type = redir_type;
    node->data.redirection.io_number = io_number;
    node->data.redirection.target = target;
    node->data.redirection.heredoc_content = NULL;
    return node;
}

ast_node_t *ast_create_word(token_t *token)
{
    ast_node_t *node = ast_node_create(AST_WORD);
    node->data.word.token = token;
    return node;
}

/* ============================================================================
 * AST Node List Functions
 * ============================================================================ */

ast_node_list_t *ast_node_list_create(void)
{
    ast_node_list_t *list = (ast_node_list_t *)xcalloc(1, sizeof(ast_node_list_t));
    list->nodes = (ast_node_t **)xcalloc(INITIAL_LIST_CAPACITY, sizeof(ast_node_t *));
    list->size = 0;
    list->capacity = INITIAL_LIST_CAPACITY;
    return list;
}

void ast_node_list_destroy(ast_node_list_t *list)
{
    if (list == NULL)
        return;

    for (int i = 0; i < list->size; i++)
    {
        ast_node_destroy(list->nodes[i]);
    }

    xfree(list->nodes);
    xfree(list);
}

int ast_node_list_append(ast_node_list_t *list, ast_node_t *node)
{
    Expects_not_null(list);
    Expects_not_null(node);

    if (list->size >= list->capacity)
    {
        int new_capacity = list->capacity * 2;
        ast_node_t **new_nodes = (ast_node_t **)xrealloc(list->nodes,
                                                         new_capacity * sizeof(ast_node_t *));
        list->nodes = new_nodes;
        list->capacity = new_capacity;
    }

    list->nodes[list->size++] = node;
    return 0;
}

int ast_node_list_size(const ast_node_list_t *list)
{
    Expects_not_null(list);
    return list->size;
}

ast_node_t *ast_node_list_get(const ast_node_list_t *list, int index)
{
    Expects_not_null(list);
    Expects_lt(index, list->size);
    return list->nodes[index];
}

/* ============================================================================
 * AST Utility Functions
 * ============================================================================ */

const char *ast_node_type_to_string(ast_node_type_t type)
{
    switch (type)
    {
    case AST_SIMPLE_COMMAND:
        return "SIMPLE_COMMAND";
    case AST_PIPELINE:
        return "PIPELINE";
    case AST_AND_OR_LIST:
        return "AND_OR_LIST";
    case AST_COMMAND_LIST:
        return "COMMAND_LIST";
    case AST_SUBSHELL:
        return "SUBSHELL";
    case AST_BRACE_GROUP:
        return "BRACE_GROUP";
    case AST_IF_CLAUSE:
        return "IF_CLAUSE";
    case AST_WHILE_CLAUSE:
        return "WHILE_CLAUSE";
    case AST_UNTIL_CLAUSE:
        return "UNTIL_CLAUSE";
    case AST_FOR_CLAUSE:
        return "FOR_CLAUSE";
    case AST_CASE_CLAUSE:
        return "CASE_CLAUSE";
    case AST_REDIRECTION:
        return "REDIRECTION";
    case AST_WORD:
        return "WORD";
    case AST_CASE_ITEM:
        return "CASE_ITEM";
    default:
        return "UNKNOWN";
    }
}

static void ast_node_to_string_helper(const ast_node_t *node, string_t *result,
                                     int indent_level)
{
    if (node == NULL)
    {
        return;
    }

    // Add indentation
    for (int i = 0; i < indent_level; i++)
    {
        string_append_cstr(result, "  ");
    }

    string_append_cstr(result, ast_node_type_to_string(node->type));
    string_append_cstr(result, "\n");

    // Recursively print children based on node type
    switch (node->type)
    {
    case AST_SIMPLE_COMMAND:
        if (node->data.simple_command.words != NULL)
        {
            for (int i = 0; i < indent_level + 1; i++)
                string_append_cstr(result, "  ");
            string_append_cstr(result, "words: ");
            string_t *words_str = token_list_to_string(node->data.simple_command.words);
            string_append(result, words_str);
            string_destroy(words_str);
            string_append_cstr(result, "\n");
        }
        break;

    case AST_PIPELINE:
        if (node->data.pipeline.is_negated)
        {
            for (int i = 0; i < indent_level + 1; i++)
                string_append_cstr(result, "  ");
            string_append_cstr(result, "negated: true\n");
        }
        if (node->data.pipeline.commands != NULL)
        {
            for (int i = 0; i < node->data.pipeline.commands->size; i++)
            {
                ast_node_to_string_helper(node->data.pipeline.commands->nodes[i],
                                        result, indent_level + 1);
            }
        }
        break;

    case AST_AND_OR_LIST:
        for (int i = 0; i < indent_level + 1; i++)
            string_append_cstr(result, "  ");
        string_append_cstr(result, "op: ");
        string_append_cstr(result, node->data.andor_list.op == ANDOR_OP_AND ? "&&" : "||");
        string_append_cstr(result, "\n");
        ast_node_to_string_helper(node->data.andor_list.left, result, indent_level + 1);
        ast_node_to_string_helper(node->data.andor_list.right, result, indent_level + 1);
        break;

    case AST_COMMAND_LIST:
        if (node->data.command_list.items != NULL)
        {
            for (int i = 0; i < node->data.command_list.items->size; i++)
            {
                ast_node_to_string_helper(node->data.command_list.items->nodes[i],
                                        result, indent_level + 1);
            }
        }
        break;

    case AST_SUBSHELL:
    case AST_BRACE_GROUP:
        ast_node_to_string_helper(node->data.compound.body, result, indent_level + 1);
        break;

    case AST_IF_CLAUSE:
        for (int i = 0; i < indent_level + 1; i++)
            string_append_cstr(result, "  ");
        string_append_cstr(result, "condition:\n");
        ast_node_to_string_helper(node->data.if_clause.condition, result, indent_level + 2);
        for (int i = 0; i < indent_level + 1; i++)
            string_append_cstr(result, "  ");
        string_append_cstr(result, "then:\n");
        ast_node_to_string_helper(node->data.if_clause.then_body, result, indent_level + 2);
        if (node->data.if_clause.else_body != NULL)
        {
            for (int i = 0; i < indent_level + 1; i++)
                string_append_cstr(result, "  ");
            string_append_cstr(result, "else:\n");
            ast_node_to_string_helper(node->data.if_clause.else_body, result, indent_level + 2);
        }
        break;

    case AST_WHILE_CLAUSE:
    case AST_UNTIL_CLAUSE:
        for (int i = 0; i < indent_level + 1; i++)
            string_append_cstr(result, "  ");
        string_append_cstr(result, "condition:\n");
        ast_node_to_string_helper(node->data.while_clause.condition, result, indent_level + 2);
        for (int i = 0; i < indent_level + 1; i++)
            string_append_cstr(result, "  ");
        string_append_cstr(result, "body:\n");
        ast_node_to_string_helper(node->data.while_clause.body, result, indent_level + 2);
        break;

    case AST_FOR_CLAUSE:
        if (node->data.for_clause.variable != NULL)
        {
            for (int i = 0; i < indent_level + 1; i++)
                string_append_cstr(result, "  ");
            string_append_cstr(result, "variable: ");
            string_append(result, node->data.for_clause.variable);
            string_append_cstr(result, "\n");
        }
        ast_node_to_string_helper(node->data.for_clause.body, result, indent_level + 1);
        break;

    default:
        break;
    }
}

string_t *ast_node_to_string(const ast_node_t *node)
{
    string_t *result = string_create_empty(256);
    if (node == NULL)
    {
        string_append_cstr(result, "(null)");
        return result;
    }

    ast_node_to_string_helper(node, result, 0);
    return result;
}

string_t *ast_tree_to_string(const ast_node_t *root)
{
    return ast_node_to_string(root);
}

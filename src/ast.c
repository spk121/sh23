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

void ast_node_destroy(ast_node_t **node)
{
    if (!node) return;
    ast_node_t *n = *node;
    
    if (n == NULL)
        return;

    switch (n->type)
    {
    case AST_SIMPLE_COMMAND:
        if (n->data.simple_command.words != NULL)
        {
            // AST owns these tokens - destroy them
            token_list_destroy(&n->data.simple_command.words);
        }
        if (n->data.simple_command.redirections != NULL)
        {
            ast_node_list_destroy(&n->data.simple_command.redirections);
        }
        if (n->data.simple_command.assignments != NULL)
        {
            // AST owns these tokens - destroy them
            token_list_destroy(&n->data.simple_command.assignments);
        }
        break;

    case AST_PIPELINE:
        if (n->data.pipeline.commands != NULL)
        {
            ast_node_list_destroy(&n->data.pipeline.commands);
        }
        break;

    case AST_AND_OR_LIST:
        ast_node_destroy(&n->data.andor_list.left);
        ast_node_destroy(&n->data.andor_list.right);
        break;

    case AST_COMMAND_LIST:
        ast_node_list_destroy(&n->data.command_list.items);
        cmd_separator_list_destroy(&(*node)->data.command_list.separators);

        break;

    case AST_SUBSHELL:
    case AST_BRACE_GROUP:
        ast_node_destroy(&n->data.compound.body);
        break;

    case AST_IF_CLAUSE:
        ast_node_destroy(&n->data.if_clause.condition);
        ast_node_destroy(&n->data.if_clause.then_body);
        if (n->data.if_clause.elif_list != NULL)
        {
            ast_node_list_destroy(&n->data.if_clause.elif_list);
        }
        ast_node_destroy(&n->data.if_clause.else_body);
        break;

    case AST_WHILE_CLAUSE:
    case AST_UNTIL_CLAUSE:
        ast_node_destroy(&n->data.loop_clause.condition);
        ast_node_destroy(&n->data.loop_clause.body);
        break;

    case AST_FOR_CLAUSE:
        if (n->data.for_clause.variable != NULL)
        {
            string_destroy(&n->data.for_clause.variable);
        }
        if (n->data.for_clause.words != NULL)
        {
            // AST owns these tokens - destroy them
            token_list_destroy(&n->data.for_clause.words);
        }
        ast_node_destroy(&n->data.for_clause.body);
        break;

    case AST_CASE_CLAUSE:
        if (n->data.case_clause.word != NULL)
        {
            // AST owns this token - destroy it
            token_destroy(&n->data.case_clause.word);
        }
        if (n->data.case_clause.case_items != NULL)
        {
            ast_node_list_destroy(&n->data.case_clause.case_items);
        }
        break;

    case AST_CASE_ITEM:
        if (n->data.case_item.patterns != NULL)
        {
            // AST owns these tokens - destroy them
            token_list_destroy(&n->data.case_item.patterns);
        }
        ast_node_destroy(&n->data.case_item.body);
        break;

    case AST_FUNCTION_DEF:
        if (n->data.function_def.name != NULL)
        {
            string_destroy(&n->data.function_def.name);
        }
        ast_node_destroy(&n->data.function_def.body);
        if (n->data.function_def.redirections != NULL)
        {
            ast_node_list_destroy(&n->data.function_def.redirections);
        }
        break;

    case AST_REDIRECTED_COMMAND:
        ast_node_destroy(&n->data.redirected_command.command);
        if (n->data.redirected_command.redirections != NULL)
        {
            ast_node_list_destroy(&n->data.redirected_command.redirections);
        }
        break;

    case AST_REDIRECTION:
        if (n->data.redirection.target != NULL)
        {
            // AST owns this token - destroy it
            token_destroy(&n->data.redirection.target);
        }
        if (n->data.redirection.io_location != NULL)
        {
            string_destroy(&n->data.redirection.io_location);
        }
        if (n->data.redirection.heredoc_content != NULL)
        {
            string_destroy(&n->data.redirection.heredoc_content);
        }
        break;

    default:
        break;
    }

    xfree(n);
    *node = NULL;
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
    node->data.command_list.separators = cmd_separator_list_create(); // Allocate here
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
    node->data.loop_clause.condition = condition;
    node->data.loop_clause.body = body;
    return node;
}

ast_node_t *ast_create_until_clause(ast_node_t *condition, ast_node_t *body)
{
    ast_node_t *node = ast_node_create(AST_UNTIL_CLAUSE);
    node->data.loop_clause.condition = condition;
    node->data.loop_clause.body = body;
    return node;
}

ast_node_t *ast_create_for_clause(const string_t *variable, token_list_t *words,
                                 ast_node_t *body)
{
    ast_node_t *node = ast_node_create(AST_FOR_CLAUSE);
    node->data.for_clause.variable = string_create_from(variable);
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

ast_node_t *ast_create_function_def(const string_t *name, ast_node_t *body,
                                   ast_node_list_t *redirections)
{
    ast_node_t *node = ast_node_create(AST_FUNCTION_DEF);
    node->data.function_def.name = string_create_from(name);
    node->data.function_def.body = body;
    node->data.function_def.redirections = redirections;
    return node;
}

ast_node_t *ast_create_redirection(redirection_type_t redir_type, int io_number,
                                  string_t *io_location, token_t *target)
{
    ast_node_t *node = ast_node_create(AST_REDIRECTION);
    node->data.redirection.redir_type = redir_type;
    node->data.redirection.io_number = io_number;
    node->data.redirection.io_location = io_location;
    node->data.redirection.target = target;
    node->data.redirection.heredoc_content = NULL;
    return node;
}

ast_node_t *ast_create_redirected_command(ast_node_t *command, ast_node_list_t *redirections)
{
    ast_node_t *node = ast_node_create(AST_REDIRECTED_COMMAND);
    node->data.redirected_command.command = command;
    node->data.redirected_command.redirections = redirections;
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

void ast_node_list_destroy(ast_node_list_t **list)
{
    if (!list) return;
    ast_node_list_t *l = *list;
    
    if (l == NULL)
        return;

    for (int i = 0; i < l->size; i++)
    {
        ast_node_destroy(&l->nodes[i]);
    }

    xfree(l->nodes);
    xfree(l);
    *list = NULL;
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

bool ast_node_command_list_has_separators(const ast_node_t *node)
{
    Expects_not_null(node);
    if (node->type != AST_COMMAND_LIST)
    {
        return false;
    }
    return node->data.command_list.separators != NULL &&
           node->data.command_list.separators->len > 0;
}

int ast_node_command_list_separator_count(const ast_node_t *node)
{
    Expects_not_null(node);
    Expects_eq(node->type, AST_COMMAND_LIST);
    if (node->data.command_list.separators == NULL)
    {
        return 0;
    }
    return node->data.command_list.separators->len;
}

cmd_separator_t ast_node_command_list_get_separator(const ast_node_t *node, int index)
{
    Expects_not_null(node);
    Expects_eq(node->type, AST_COMMAND_LIST);
    Expects_lt(index, ast_node_command_list_separator_count(node));
    return node->data.command_list.separators->separators[index];
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
    case AST_FUNCTION_DEF:
        return "FUNCTION_DEF";
    case AST_REDIRECTION:
        return "REDIRECTION";
    case AST_CASE_ITEM:
        return "CASE_ITEM";
    case AST_REDIRECTED_COMMAND:
        return "REDIRECTED_COMMAND";
    default:
        return "UNKNOWN";
    }
}

const char *redirection_type_to_string(redirection_type_t type)
{
    switch (type)
    {
    case REDIR_INPUT:
        return "<";
    case REDIR_OUTPUT:
        return ">";
    case REDIR_APPEND:
        return ">>";
    case REDIR_HEREDOC:
        return "<<";
    case REDIR_HEREDOC_STRIP:
        return "<<-";
    case REDIR_DUP_INPUT:
        return "<&";
    case REDIR_DUP_OUTPUT:
        return ">&";
    case REDIR_READWRITE:
        return "<>";
    case REDIR_CLOBBER:
        return ">|";
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
        if (node->data.simple_command.assignments != NULL &&
            node->data.simple_command.assignments->size > 0)
        {
            for (int i = 0; i < indent_level + 1; i++)
                string_append_cstr(result, "  ");
            string_append_cstr(result, "assignments: ");
            string_t *assignments_str = token_list_to_string(node->data.simple_command.assignments);
            string_append(result, assignments_str);
            string_destroy(&assignments_str);
            string_append_cstr(result, "\n");
        }
        if (node->data.simple_command.words != NULL &&
            node->data.simple_command.words->size > 0)
        {
            for (int i = 0; i < indent_level + 1; i++)
                string_append_cstr(result, "  ");
            string_append_cstr(result, "words: ");
            string_t *words_str = token_list_to_string(node->data.simple_command.words);
            string_append(result, words_str);
            string_destroy(&words_str);
            string_append_cstr(result, "\n");
        }
        if (node->data.simple_command.redirections != NULL &&
            ast_node_list_size(node->data.simple_command.redirections) > 0)
        {
            for (int i = 0; i < indent_level + 1; i++)
                string_append_cstr(result, "  ");
            string_append_cstr(result, "redirections:\n");
            for (int i = 0; i < ast_node_list_size(node->data.simple_command.redirections); i++)
            {
                ast_node_t *redir = ast_node_list_get(node->data.simple_command.redirections, i);
                ast_node_to_string_helper(redir, result, indent_level + 2);
            }
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
        ast_node_to_string_helper(node->data.loop_clause.condition, result, indent_level + 2);
        for (int i = 0; i < indent_level + 1; i++)
            string_append_cstr(result, "  ");
        string_append_cstr(result, "body:\n");
        ast_node_to_string_helper(node->data.loop_clause.body, result, indent_level + 2);
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

    case AST_FUNCTION_DEF:
        if (node->data.function_def.name != NULL)
        {
            for (int i = 0; i < indent_level + 1; i++)
                string_append_cstr(result, "  ");
            string_append_cstr(result, "name: ");
            string_append(result, node->data.function_def.name);
            string_append_cstr(result, "\n");
        }
        ast_node_to_string_helper(node->data.function_def.body, result, indent_level + 1);
        break;

    case AST_REDIRECTED_COMMAND:
        if (node->data.redirected_command.command != NULL)
        {
            for (int i = 0; i < indent_level + 1; i++)
                string_append_cstr(result, "  ");
            string_append_cstr(result, "command:\n");
            ast_node_to_string_helper(node->data.redirected_command.command, result, indent_level + 2);
        }
        if (node->data.redirected_command.redirections != NULL &&
            ast_node_list_size(node->data.redirected_command.redirections) > 0)
        {
            for (int i = 0; i < indent_level + 1; i++)
                string_append_cstr(result, "  ");
            string_append_cstr(result, "redirections:\n");
            for (int i = 0; i < ast_node_list_size(node->data.redirected_command.redirections); i++)
            {
                ast_node_t *redir = ast_node_list_get(node->data.redirected_command.redirections, i);
                ast_node_to_string_helper(redir, result, indent_level + 2);
            }
        }
        break;

    case AST_REDIRECTION:
        for (int i = 0; i < indent_level + 1; i++)
            string_append_cstr(result, "  ");
        string_append_cstr(result, "type: ");
        string_append_cstr(result, redirection_type_to_string(node->data.redirection.redir_type));
        string_append_cstr(result, "\n");
        if (node->data.redirection.io_number >= 0)
        {
            for (int i = 0; i < indent_level + 1; i++)
                string_append_cstr(result, "  ");
            string_append_cstr(result, "io_number: ");
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", node->data.redirection.io_number);
            string_append_cstr(result, buf);
            string_append_cstr(result, "\n");
        }
        if (node->data.redirection.io_location != NULL)
        {
            for (int i = 0; i < indent_level + 1; i++)
                string_append_cstr(result, "  ");
            string_append_cstr(result, "io_location: ");
            string_append(result, node->data.redirection.io_location);
            string_append_cstr(result, "\n");
        }
        if (node->data.redirection.target != NULL)
        {
            for (int i = 0; i < indent_level + 1; i++)
                string_append_cstr(result, "  ");
            string_append_cstr(result, "target: ");
            string_t *target_str = token_to_string(node->data.redirection.target);
            string_append(result, target_str);
            string_destroy(&target_str);
            string_append_cstr(result, "\n");
        }
        if (node->data.redirection.heredoc_content != NULL)
        {
            for (int i = 0; i < indent_level + 1; i++)
                string_append_cstr(result, "  ");
            string_append_cstr(result, "heredoc_content: ");
            string_append(result, node->data.redirection.heredoc_content);
            string_append_cstr(result, "\n");
        }
        break;

    default:
        break;
    }
}

string_t *ast_node_to_string(const ast_node_t *node)
{
    string_t *result = string_create();
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

void ast_print(const ast_node_t *root)
{
    string_t *str = ast_tree_to_string(root);
    puts(string_cstr(str));
    string_destroy(&str);
}

cmd_separator_list_t *cmd_separator_list_create(void)
{
    cmd_separator_list_t *list = (cmd_separator_list_t *)xmalloc(sizeof(cmd_separator_list_t));
    list->separators = (cmd_separator_t *)xmalloc(INITIAL_LIST_CAPACITY * sizeof(cmd_separator_t ));
    list->len = 0;
    list->capacity = INITIAL_LIST_CAPACITY;
    return list;
}

void cmd_separator_list_destroy(cmd_separator_list_t **list)
{
    if (!list || !*list) return;
    cmd_separator_list_t *l = *list;
    xfree(l->separators);
    xfree(l);
    *list = NULL;
}

void cmd_separator_list_add(cmd_separator_list_t *list, cmd_separator_t sep)
{
    Expects_not_null(list);
    if (list->len >= list->capacity) {
        int new_capacity = list->capacity * 2;
        list->separators = (cmd_separator_t *)xrealloc(list->separators, new_capacity * sizeof(cmd_separator_t));
        list->capacity = new_capacity;
    }
    list->separators[list->len++] = sep;
}

cmd_separator_t cmd_separator_list_get(const cmd_separator_list_t *list, int index)
{
    Expects_not_null(list);
    Expects_lt(index, list->len);
    return list->separators[index];
}

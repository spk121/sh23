#include "gprint.h"
#include "string_t.h"
#include "token.h"
#include <stdio.h>

static void indent(int n)
{
    for (int i = 0; i < n; i++)
        putchar(' ');
}

static const char *gkind_name(gnode_type_t k)
{
    switch (k)
    {
#define X(name)                                                                                    \
    case name:                                                                                     \
        return #name;

        /* Top-level */
        X(G_PROGRAM)
        X(G_COMPLETE_COMMANDS)
        X(G_COMPLETE_COMMAND)
        X(G_LIST)
        X(G_AND_OR)
        X(G_PIPELINE)
        X(G_PIPE_SEQUENCE)
        X(G_COMMAND)

        /* Simple command structure */
        X(G_SIMPLE_COMMAND)
        X(G_CMD_PREFIX)
        X(G_CMD_WORD)
        X(G_CMD_SUFFIX)
        X(G_CMD_NAME)
        X(G_ASSIGNMENT_WORD)
        X(G_WORD_NODE)

        /* Redirections */
        X(G_REDIRECT_LIST)
        X(G_IO_REDIRECT)
        X(G_IO_FILE)
        X(G_IO_HERE)
        X(G_FILENAME)
        X(G_HERE_END)

        /* Compound commands */
        X(G_COMPOUND_COMMAND)
        X(G_SUBSHELL)
        X(G_BRACE_GROUP)
        X(G_IF_CLAUSE)
        X(G_ELSE_PART)
        X(G_WHILE_CLAUSE)
        X(G_UNTIL_CLAUSE)
        X(G_FOR_CLAUSE)
        X(G_CASE_CLAUSE)
        X(G_CASE_LIST)
        X(G_CASE_LIST_NS)
        X(G_CASE_ITEM)
        X(G_CASE_ITEM_NS)
        X(G_PATTERN_LIST)
        X(G_DO_GROUP)
        X(G_COMPOUND_LIST)
        X(G_TERM)

        /* Function definitions */
        X(G_FUNCTION_DEFINITION)
        X(G_FUNCTION_BODY)
        X(G_FNAME)

        /* Separators / structure */
        X(G_SEPARATOR_OP)
        X(G_SEPARATOR)
        X(G_SEQUENTIAL_SEP)
        X(G_NEWLINE_LIST)
        X(G_LINEBREAK)

        /* Leaf wrappers */
        X(G_NAME_NODE)
        X(G_IN_NODE)
        X(G_WORDLIST)
        X(G_IO_NUMBER_NODE)
        X(G_IO_LOCATION_NODE)

#undef X
    }
    return "<unknown>";
}

static void gprint_node(const gnode_t *node, int depth);

static void gprint_token(const token_t *tok, int depth)
{
    indent(depth);
    if (!tok)
    {
        printf("TOKEN <null>\n");
        return;
    }

    if (token_get_type(tok) == TOKEN_WORD) {
        string_t *str = token_get_all_text(tok);
        printf("TOKEN_%s(\"%s\")\n", token_type_to_string(token_get_type(tok)),
               str ? string_cstr(str) : "");
        string_destroy(&str);
    }
    else
    {
        printf("TOKEN_%s\n", token_type_to_string(token_get_type(tok)));
    }

}

static void gprint_list(const gnode_list_t *list, int depth)
{
    indent(depth);
    printf("[\n");

    for (size_t i = 0; i < list->size; i++)
    {
        gnode_t *child = list->nodes[i];
        gprint_node(child, depth + 2);
    }

    indent(depth);
    printf("]\n");
}

static void gprint_node(const gnode_t *node, int depth)
{
    if (!node)
    {
        indent(depth);
        printf("<null>\n");
        return;
    }

    indent(depth);
    printf("%s {\n", gkind_name(node->type));

    switch (node->type)
    {
    /* Token nodes */
    case G_WORD_NODE:
    case G_ASSIGNMENT_WORD:
    case G_CMD_NAME:
    case G_CMD_WORD:
    case G_NAME_NODE:
    case G_IO_NUMBER_NODE:
    case G_IO_LOCATION_NODE:
    case G_SEPARATOR_OP:
        gprint_token(node->data.token, depth + 2);
        break;

    /* String nodes */
    case G_FNAME:
    case G_FILENAME:
    case G_HERE_END:
        indent(depth + 2);
        printf("string: \"%s\"\n", node->data.string ? string_cstr(node->data.string) : "<null>");
        break;

    /* Single-child nodes */
    case G_PROGRAM:
    case G_COMPOUND_COMMAND:
    case G_SUBSHELL:
    case G_BRACE_GROUP:
    case G_FUNCTION_BODY:
    case G_SEPARATOR:
    case G_LINEBREAK:
        indent(depth + 2);
        printf("child:\n");
        gprint_node(node->data.child, depth + 4);
        break;

    /* List nodes */
    case G_COMPLETE_COMMANDS:
    case G_LIST:
    case G_PIPELINE:
    case G_PIPE_SEQUENCE:
    case G_SIMPLE_COMMAND:
    case G_CMD_PREFIX:
    case G_CMD_SUFFIX:
    case G_COMPOUND_LIST:
    case G_TERM:
    case G_CASE_LIST:
    case G_CASE_LIST_NS:
    case G_PATTERN_LIST:
    case G_WORDLIST:
    case G_REDIRECT_LIST:
    case G_DO_GROUP:
    case G_NEWLINE_LIST:
        indent(depth + 2);
        printf("list:\n");
        gprint_list(node->data.list, depth + 4);
        break;

    /* Pair nodes */
    case G_COMPLETE_COMMAND:
    case G_ELSE_PART:
        indent(depth + 2);
        printf("left:\n");
        gprint_node(node->data.pair.left, depth + 4);
        indent(depth + 2);
        printf("right:\n");
        gprint_node(node->data.pair.right, depth + 4);
        break;

    /* Multi-child nodes (default case) */
    case G_COMMAND:
    case G_AND_OR:
    case G_IF_CLAUSE:
    case G_WHILE_CLAUSE:
    case G_UNTIL_CLAUSE:
    case G_FOR_CLAUSE:
    case G_CASE_CLAUSE:
    case G_CASE_ITEM:
    case G_CASE_ITEM_NS:
    case G_FUNCTION_DEFINITION:
    case G_IO_REDIRECT:
    case G_IO_FILE:
    case G_IO_HERE:
    case G_IN_NODE:
    case G_SEQUENTIAL_SEP:
    default:
        indent(depth + 2);
        printf("multi.a:\n");
        gprint_node(node->data.multi.a, depth + 4);

        indent(depth + 2);
        printf("multi.b:\n");
        gprint_node(node->data.multi.b, depth + 4);

        indent(depth + 2);
        printf("multi.c:\n");
        gprint_node(node->data.multi.c, depth + 4);

        indent(depth + 2);
        printf("multi.d:\n");
        gprint_node(node->data.multi.d, depth + 4);
        break;
    }

    indent(depth);
    printf("}\n");
}

void gprint(const gnode_t *node)
{
    gprint_node(node, 0);
}

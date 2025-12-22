#include "gprint.h"
#include <stdio.h>

static void indent(int n)
{
    for (int i = 0; i < n; i++)
        putchar(' ');
}

static const char *gkind_name(gkind_t k)
{
    switch (k)
    {
#define X(name)                                                                                    \
    case name:                                                                                     \
        return #name;

    X(G_PROGRAM)
        X(G_COMPLETE_COMMANDS)
        X(G_COMPLETE_COMMAND)
        X(G_LIST)
        X(G_AND_OR)
        X(G_PIPELINE)
        X(G_PIPE_SEQUENCE)
        X(G_COMMAND)
        X(G_SIMPLE_COMMAND)
        X(G_COMPOUND_COMMAND)
        X(G_SUBSHELL)
        X(G_BRACE_GROUP)
        X(G_FOR_CLAUSE)
        X(G_CASE_CLAUSE)
        X(G_CASE_LIST)
        X(G_CASE_LIST_NS)
        X(G_CASE_ITEM)
        X(G_CASE_ITEM_NS)
        X(G_PATTERN_LIST)
        X(G_IF_CLAUSE)
        X(G_ELSE_PART)
        X(G_WHILE_CLAUSE)
        X(G_UNTIL_CLAUSE)
        X(G_FUNCTION_DEFINITION)
        X(G_DO_GROUP)
        X(G_COMPOUND_LIST)
        X(G_TERM)
        X(G_REDIRECT_LIST)
        X(G_IO_REDIRECT)
        X(G_IO_FILE)
        X(G_IO_HERE)
        X(G_FILENAME)
        X(G_WORDLIST)
        X(G_NAME_NODE)
        X(G_WORD_NODE)
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

    printf("TOKEN_%s(\"%s\")\n", token_type_name(token_get_type(tok)),
           tok->lexeme ? tok->lexeme : "");
}

static void gprint_list(const glist_t *list, int depth)
{
    indent(depth);
    printf("[\n");

    for (size_t i = 0; i < list->size; i++)
    {
        gnode_t *child = list->items[i];
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
    printf("%s {\n", gkind_name(node->kind));

    switch (node->kind)
    {
    case G_WORD_NODE:
    case G_NAME_NODE:
    case G_IO_NUMBER_NODE:
    case G_IO_LOCATION_NODE:
        gprint_token(node->data.token, depth + 2);
        break;

    case G_PROGRAM:
    case G_SUBSHELL:
    case G_BRACE_GROUP:
    case G_DO_GROUP:
        indent(depth + 2);
        printf("child:\n");
        gprint_node(node->data.child, depth + 4);
        break;

    case G_COMPLETE_COMMANDS:
    case G_LIST:
    case G_PIPELINE:
    case G_PIPE_SEQUENCE:
    case G_COMPOUND_LIST:
    case G_TERM:
    case G_CASE_LIST:
    case G_CASE_LIST_NS:
    case G_PATTERN_LIST:
    case G_WORDLIST:
    case G_REDIRECT_LIST:
        indent(depth + 2);
        printf("list:\n");
        gprint_list(node->data.list, depth + 4);
        break;

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

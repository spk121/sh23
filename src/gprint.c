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

    /* Use the stored payload_type instead of computing it */
    switch (node->payload_type)
    {
    case GNODE_PAYLOAD_TOKEN:
        gprint_token(node->data.token, depth + 2);
        break;

    case GNODE_PAYLOAD_STRING:
        indent(depth + 2);
        printf("string: \"%s\"\n", node->data.string ? string_cstr(node->data.string) : "<null>");
        break;

    case GNODE_PAYLOAD_CHILD:
        indent(depth + 2);
        printf("child:\n");
        gprint_node(node->data.child, depth + 4);
        break;

    case GNODE_PAYLOAD_LIST:
        indent(depth + 2);
        printf("list:\n");
        gprint_list(node->data.list, depth + 4);
        break;

    case GNODE_PAYLOAD_PAIR:
    case GNODE_PAYLOAD_MULTI:
        /* PAIR uses only .a and .b, MULTI uses all four fields.
           Print all four for consistency. */
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

    case GNODE_PAYLOAD_INDETERMINATE:
        /* This should not happen - indeterminate payload should have been
           resolved to a concrete type when the node was created/modified */
        indent(depth + 2);
        printf("ERROR: indeterminate payload for type %d\n", (int)node->type);
        break;

    case GNODE_PAYLOAD_NONE:
    default:
        /* No payload to print */
        break;
    }

    indent(depth);
    printf("}\n");
}

void gprint(const gnode_t *node)
{
    gprint_node(node, 0);
}

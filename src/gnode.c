#include "gnode.h"
#include "xalloc.h"
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * List Helpers
 * ============================================================================
 */

#define INITIAL_LIST_CAPACITY 8

gnode_list_t *g_list_create(void)
{
    gnode_list_t *list = xcalloc(1, sizeof(gnode_list_t));
    list->nodes = xcalloc(INITIAL_LIST_CAPACITY, sizeof(gnode_t *));
    list->capacity = INITIAL_LIST_CAPACITY;
    return list;
}

void g_list_append(gnode_list_t *list, gnode_t *node)
{
    if (list->size >= list->capacity)
    {
        int newcap = list->capacity * 2;
        list->nodes = xrealloc(list->nodes, newcap * sizeof(gnode_t *));
        list->capacity = newcap;
    }
    list->nodes[list->size++] = node;
}

void g_list_destroy(gnode_list_t **plist)
{
    if (!plist || !*plist)
        return;
    gnode_list_t *list = *plist;

    for (int i = 0; i < list->size; i++)
        g_node_destroy(&list->nodes[i]);

    xfree(list->nodes);
    xfree(list);
    *plist = NULL;
}

/* ============================================================================
 * Node Constructors
 * ============================================================================
 */

gnode_t *g_node_create(gnode_type_t type)
{
    gnode_t *node = xcalloc(1, sizeof(gnode_t));
    node->type = type;
    return node;
}

gnode_t *g_node_create_token(gnode_type_t type, token_t *tok)
{
    gnode_t *node = g_node_create(type);
    node->data.token = tok;
    return node;
}

gnode_t *g_node_create_string(gnode_type_t type, const string_t *str)
{
    gnode_t *node = g_node_create(type);
    node->data.string = string_create_from(str);
    return node;
}

/* ============================================================================
 * Node Destruction
 * ============================================================================
 */

void g_node_destroy(gnode_t **pnode)
{
    if (!pnode || !*pnode)
        return;
    gnode_t *node = *pnode;

    switch (node->type)
    {
    /* Token wrappers */
    case G_WORD_NODE:
    case G_ASSIGNMENT_WORD:
    case G_NAME_NODE:
    case G_IN_NODE:
    case G_IO_NUMBER_NODE:
    case G_IO_LOCATION_NODE:
        if (node->data.token)
            token_destroy(&node->data.token);
        break;

    /* String wrappers */
    case G_FNAME:
    case G_FILENAME:
    case G_HERE_END:
        if (node->data.string)
            string_destroy(&node->data.string);
        break;

    /* List nodes */
    case G_PROGRAM:
    case G_COMPLETE_COMMANDS:
    case G_LIST:
    case G_AND_OR:
    case G_PIPELINE:
    case G_PIPE_SEQUENCE:
    case G_COMMAND:
    case G_SIMPLE_COMMAND:
    case G_CMD_PREFIX:
    case G_CMD_SUFFIX:
    case G_REDIRECT_LIST:
    case G_CASE_LIST:
    case G_CASE_LIST_NS:
    case G_PATTERN_LIST:
    case G_WORDLIST:
    case G_COMPOUND_LIST:
    case G_TERM:
    case G_DO_GROUP:
        g_list_destroy(&node->data.list);
        break;

    /* Pair nodes */
    case G_COMPLETE_COMMAND:
    case G_ELSE_PART:
        g_node_destroy(&node->data.pair.left);
        g_node_destroy(&node->data.pair.right);
        break;

    /* Single-child nodes */
    case G_CMD_WORD:
    case G_CMD_NAME:
    case G_SUBSHELL:
    case G_BRACE_GROUP:
    case G_FUNCTION_BODY:
    case G_SEPARATOR:
    case G_LINEBREAK:
        g_node_destroy(&node->data.child);
        break;

    /* Multi-child nodes */
    case G_IF_CLAUSE:
    case G_WHILE_CLAUSE:
    case G_UNTIL_CLAUSE:
    case G_FOR_CLAUSE:
    case G_CASE_CLAUSE:
    case G_CASE_ITEM:
    case G_CASE_ITEM_NS:
    case G_FUNCTION_DEFINITION:
        g_node_destroy(&node->data.multi.a);
        g_node_destroy(&node->data.multi.b);
        g_node_destroy(&node->data.multi.c);
        g_node_destroy(&node->data.multi.d);
        break;

    default:
        break;
    }

    xfree(node);
    *pnode = NULL;
}

/* ============================================================================
 * Debugging
 * ============================================================================
 */

string_t *g_node_to_string(const gnode_t *node)
{
    string_t *s = string_create();
    if (!node)
    {
        string_append_cstr(s, "(null)");
        return s;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "<GNode type=%d>", node->type);
    string_append_cstr(s, buf);
    return s;
}

void g_node_print(const gnode_t *node)
{
    string_t *s = g_node_to_string(node);
    puts(string_cstr(s));
    string_destroy(&s);
}


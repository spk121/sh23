#include "gnode.h"
#include "xalloc.h"
#include <stdint.h>
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

// Minimum plausible heap address threshold for detecting obviously invalid pointers
// This is a heuristic - pointers below this value (like 0x1, 0x2) are likely corrupted
#define MIN_PLAUSIBLE_HEAP_ADDR 4096

void g_list_destroy(gnode_list_t **plist)
{
    if (!plist || !*plist)
        return;
    gnode_list_t *list = *plist;

    /* Validate list pointer before using it - check for obviously invalid addresses
     * like 0x1 which indicate a corrupted union member or uninitialized data */
    if ((uintptr_t)list < MIN_PLAUSIBLE_HEAP_ADDR)
    {
        fprintf(stderr, "g_list_destroy: invalid list pointer %p (likely uninitialized union member)\n", (void*)list);
        *plist = NULL;
        return;
    }

    /* Validate nodes pointer similarly */
    if (list->nodes && (uintptr_t)list->nodes < MIN_PLAUSIBLE_HEAP_ADDR)
    {
        fprintf(stderr, "g_list_destroy: invalid nodes pointer %p in list %p (likely uninitialized or corrupted)\n",
                (void*)list->nodes, (void*)list);
        fprintf(stderr, "  list->size=%d, list->capacity=%d\n", list->size, list->capacity);
        *plist = NULL;
        return;
    }

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
    node->payload_type = gnode_get_payload_type(type);
    return node;
}

gnode_t *g_node_create_token(gnode_type_t type, token_t *tok)
{
    gnode_t *node = g_node_create(type);
    node->payload_type = GNODE_PAYLOAD_TOKEN;
    node->data.token = tok;
    return node;
}

gnode_t *g_node_create_string(gnode_type_t type, const string_t *str)
{
    gnode_t *node = g_node_create(type);
    node->payload_type = GNODE_PAYLOAD_STRING;
    node->data.string = string_create_from(str);
    return node;
}

/* ============================================================================
 * Payload Type Mapping
 * ============================================================================
 */

/**
 * Returns the payload kind stored in gnode_t::data for a given AST node type.
 *
 * This function provides a central mapping from gnode_type_t (the syntactic
 * category of a grammar node) to gnode_payload_t (which member of the
 * gnode_t::data union is valid for that node).
 *
 * The return value indicates:
 *   - GNODE_PAYLOAD_LIST   : use the 'list' member (gnode_list_t *)
 *   - GNODE_PAYLOAD_TOKEN  : use the 'token' member (token_t *)
 *   - GNODE_PAYLOAD_STRING : use the 'string' member (string_t *)
 *   - GNODE_PAYLOAD_PAIR   : use the 'pair' struct (left/right children)
 *   - GNODE_PAYLOAD_CHILD  : use the 'child' member (single child)
 *   - GNODE_PAYLOAD_MULTI  : use the 'multi' struct (a/b/c/d children)
 *   - GNODE_PAYLOAD_NONE   : no payload at all
 *   - GNODE_PAYLOAD_INDETERMINATE : context-dependent payload layout
 *
 * Special cases for GNODE_PAYLOAD_INDETERMINATE:
 *   - G_COMMAND and G_IN_NODE have context-dependent payload layouts that are
 *     not captured by a single gnode_payload_t value.
 *
 * Special case for GNODE_PAYLOAD_NONE:
 *   - G_SEQUENTIAL_SEP carries no payload at all.
 *
 * Callers should use node->payload_type directly instead of this function,
 * as the actual payload_type is set when nodes are created/modified.
 */
gnode_payload_t gnode_get_payload_type(gnode_type_t type)
{
    switch (type)
    {
    /* Token wrappers - leaf nodes that wrap tokens */
    case G_WORD_NODE:
    case G_ASSIGNMENT_WORD:
    case G_NAME_NODE:
    case G_IO_NUMBER_NODE:
    case G_IO_LOCATION_NODE:
    case G_SEPARATOR_OP:
    case G_CMD_NAME:
    case G_CMD_WORD:
        return GNODE_PAYLOAD_TOKEN;

    /* String wrappers */
    case G_FNAME:
    case G_FILENAME:
    case G_HERE_END:
        return GNODE_PAYLOAD_STRING;

    /* List nodes */
    case G_COMPLETE_COMMANDS:
    case G_LIST:
    case G_PIPELINE:
    case G_PIPE_SEQUENCE:
    case G_SIMPLE_COMMAND:
    case G_CMD_SUFFIX:
    case G_REDIRECT_LIST:
    case G_CASE_LIST:
    case G_CASE_LIST_NS:
    case G_PATTERN_LIST:
    case G_WORDLIST:
    case G_COMPOUND_LIST:
    case G_TERM:
    case G_DO_GROUP:
    case G_NEWLINE_LIST:
        return GNODE_PAYLOAD_LIST;

    /* Pair nodes - NONE currently use .pair union member */
    case G_ELSE_PART:
        /* else_part can be either simple (multi.a only) or elif (multi.a/b/c) */
        return GNODE_PAYLOAD_MULTI;
        break;

    /* Single-child nodes */
    case G_PROGRAM:
    case G_CMD_PREFIX:
    case G_SUBSHELL:
    case G_BRACE_GROUP:
    case G_FUNCTION_BODY:
    case G_SEPARATOR:
    case G_LINEBREAK:
    case G_COMPOUND_COMMAND:
        return GNODE_PAYLOAD_CHILD;

    /* Multi-child nodes */
    case G_AND_OR:
    case G_COMPLETE_COMMAND:
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
        return GNODE_PAYLOAD_MULTI;

    /* Nodes with context-dependent payload */
    case G_COMMAND:
    case G_IN_NODE:
        /* G_COMMAND can use either .child (outer wrapper) or .multi (with redirects).
         * G_IN_NODE can use either .token (just 'in' keyword) or .multi (in + wordlist).
         * Actual payload_type must be set when node is created/modified. */
        return GNODE_PAYLOAD_INDETERMINATE;

    /* Node with no payload */
    case G_UNSPECIFIED:
    case G_SEQUENTIAL_SEP:
    default:
        return GNODE_PAYLOAD_NONE;
    }
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

    /* Use the stored payload_type instead of computing it */
    switch (node->payload_type)
    {
    case GNODE_PAYLOAD_TOKEN:
        if (node->data.token)
            token_destroy(&node->data.token);
        node->payload_type = GNODE_PAYLOAD_NONE;
        break;

    case GNODE_PAYLOAD_STRING:
        if (node->data.string)
            string_destroy(&node->data.string);
        node->payload_type = GNODE_PAYLOAD_NONE;
        break;

    case GNODE_PAYLOAD_LIST:
        g_list_destroy(&node->data.list);
        node->payload_type = GNODE_PAYLOAD_NONE;
        break;

    case GNODE_PAYLOAD_CHILD:
        g_node_destroy(&node->data.child);
        node->payload_type = GNODE_PAYLOAD_NONE;
        break;

    case GNODE_PAYLOAD_PAIR:
        g_node_destroy(&node->data.pair.left);
        g_node_destroy(&node->data.pair.right);
        node->payload_type = GNODE_PAYLOAD_NONE;
        break;

    case GNODE_PAYLOAD_MULTI:
        g_node_destroy(&node->data.multi.a);
        g_node_destroy(&node->data.multi.b);
        g_node_destroy(&node->data.multi.c);
        g_node_destroy(&node->data.multi.d);
        node->payload_type = GNODE_PAYLOAD_NONE;
        break;

    case GNODE_PAYLOAD_INDETERMINATE:
        /* This should not happen - indeterminate payload should have been
           resolved to a concrete type when the node was created/modified */
        fprintf(stderr, "g_node_destroy: node type %d has indeterminate payload\n", 
                (int)node->type);
        break;

    case GNODE_PAYLOAD_NONE:
    default:
        /* No payload to destroy */
        break;
    }
    node->type = G_UNSPECIFIED;
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


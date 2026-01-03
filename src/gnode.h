#ifndef GNODE_H
#define GNODE_H

#include "string_t.h"
#include "token.h"
#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * Grammar AST Node Types (POSIX-aligned)
 * ============================================================================
 */

typedef enum
{
    /* Top-level */
    G_PROGRAM,
    G_COMPLETE_COMMANDS,
    G_COMPLETE_COMMAND,
    G_LIST,
    G_AND_OR,
    G_PIPELINE,
    G_PIPE_SEQUENCE,
    G_COMMAND,

    /* Simple command structure */
    G_SIMPLE_COMMAND,
    G_CMD_PREFIX,
    G_CMD_WORD,
    G_CMD_SUFFIX,
    G_CMD_NAME,
    G_ASSIGNMENT_WORD,
    G_WORD_NODE,

    /* Redirections */
    G_REDIRECT_LIST,
    G_IO_REDIRECT,
    G_IO_FILE,
    G_IO_HERE,
    G_FILENAME,
    G_HERE_END,

    /* Compound commands */
    G_COMPOUND_COMMAND,
    G_SUBSHELL,
    G_BRACE_GROUP,
    G_IF_CLAUSE,
    G_ELSE_PART,
    G_WHILE_CLAUSE,
    G_UNTIL_CLAUSE,
    G_FOR_CLAUSE,
    G_CASE_CLAUSE,
    G_CASE_LIST,
    G_CASE_LIST_NS,
    G_CASE_ITEM,
    G_CASE_ITEM_NS,
    G_PATTERN_LIST,
    G_DO_GROUP,
    G_COMPOUND_LIST,
    G_TERM,

    /* Function definitions */
    G_FUNCTION_DEFINITION,
    G_FUNCTION_BODY,
    G_FNAME,

    /* Separators / structure */
    G_SEPARATOR_OP,
    G_SEPARATOR,
    G_SEQUENTIAL_SEP,
    G_NEWLINE_LIST,
    G_LINEBREAK,

    /* Leaf wrappers */
    G_NAME_NODE,
    G_IN_NODE,
    G_WORDLIST,
    G_IO_NUMBER_NODE,
    G_IO_LOCATION_NODE

} gnode_type_t;

/* ============================================================================
 * Grammar AST Payload Types
 * ============================================================================
 */

typedef enum
{
    GNODE_PAYLOAD_NONE,
    GNODE_PAYLOAD_LIST,
    GNODE_PAYLOAD_TOKEN,
    GNODE_PAYLOAD_STRING,
    GNODE_PAYLOAD_PAIR,
    GNODE_PAYLOAD_CHILD,
    GNODE_PAYLOAD_MULTI
} gnode_payload_t;

/* ============================================================================
 * Grammar AST Node Structure
 * ============================================================================
 */

typedef struct gnode_t gnode_t;
typedef struct gnode_list_t gnode_list_t;

struct gnode_list_t
{
    gnode_t **nodes;
    int size;
    int capacity;
};

struct gnode_t
{
    gnode_type_t type;

    /* Location info (optional but useful) */
    int first_line;
    int first_column;
    int last_line;
    int last_column;

    union {
        /* Generic list of children */
        gnode_list_t *list;

        /* Token wrapper */
        token_t *token;

        /* String wrapper (for NAME, FNAME, etc.) */
        string_t *string;

        /* For nodes that contain two children (e.g., AND_OR) */
        struct
        {
            gnode_t *left;
            gnode_t *right;
        } pair;

        /* For nodes with a single child */
        gnode_t *child;

        /* For nodes with multiple named children */
        struct
        {
            gnode_t *a;
            gnode_t *b;
            gnode_t *c;
            gnode_t *d;
        } multi;
    } data;
};

/* ============================================================================
 * Constructors
 * ============================================================================
 */

gnode_t *g_node_create(gnode_type_t type);
gnode_t *g_node_create_token(gnode_type_t type, token_t *tok);
gnode_t *g_node_create_string(gnode_type_t type, const string_t *str);

gnode_list_t *g_list_create(void);
void g_list_append(gnode_list_t *list, gnode_t *node);

/* ============================================================================
 * Destruction
 * ============================================================================
 */

void g_node_destroy(gnode_t **node);
void g_list_destroy(gnode_list_t **list);

/* ============================================================================
 * Payload Type Mapping
 * ============================================================================
 */

gnode_payload_t gnode_get_payload_type(gnode_type_t type);

/* ============================================================================
 * Debugging
 * ============================================================================
 */

string_t *g_node_to_string(const gnode_t *node);
void g_node_print(const gnode_t *node);

#endif /* AST_GRAMMAR_H */

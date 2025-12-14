#ifndef AST_H
#define AST_H

#include "string_t.h"
#include "token.h"
#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * AST Node Type Enumeration
 * ============================================================================ */

/**
 * AST node types representing shell grammar constructs.
 * These map to the POSIX shell grammar productions.
 */
typedef enum
{
    AST_PROGRAM,            // top-level program node

    /* Basic command constructs */
    AST_SIMPLE_COMMAND,    // command with arguments and redirections
    AST_PIPELINE,          // sequence of commands connected by pipes
    AST_AND_OR_LIST,       // commands connected by && or ||
    AST_COMMAND_LIST,      // commands separated by ; or & or newlines

    /* Compound commands */
    AST_SUBSHELL,          // ( command_list )
    AST_BRACE_GROUP,       // { command_list; }
    AST_IF_CLAUSE,         // if/then/else/elif/fi
    AST_WHILE_CLAUSE,      // while/do/done
    AST_UNTIL_CLAUSE,      // until/do/done
    AST_FOR_CLAUSE,        // for/in/do/done
    AST_CASE_CLAUSE,       // case/in/esac
    AST_FUNCTION_DEF,      // name() compound-command [redirections]

    /* Auxiliary nodes */
    AST_REDIRECTION,       // I/O redirection
    AST_WORD,              // word node (wraps token)
    AST_CASE_ITEM,         // pattern) command_list ;;

    AST_NODE_TYPE_COUNT
} ast_node_type_t;

/* ============================================================================
 * AST Operator Enumerations
 * ============================================================================ */

/**
 * Pipeline operators
 */
typedef enum
{
    PIPE_OP_PIPE,       // |
    PIPE_OP_PIPE_AMPER, // |& (bash extension, not implemented yet)
} pipe_operator_t;

/**
 * And/Or list operators
 */
typedef enum
{
    ANDOR_OP_AND, // &&
    ANDOR_OP_OR,  // ||
} andor_operator_t;

/**
 * List separators
 */
typedef enum
{
    LIST_SEP_SEQUENTIAL, // ; or newline
    LIST_SEP_BACKGROUND, // &
    LIST_SEP_EOL
} cmd_separator_t;

/* Command separator list structure */
typedef struct
{
    cmd_separator_t *separators;
    int len;
    int capacity;
} cmd_separator_list_t;

/**
 * Redirection types
 */
typedef enum
{
    REDIR_INPUT,       // <
    REDIR_OUTPUT,      // >
    REDIR_APPEND,      // >>
    REDIR_HEREDOC,     // <<
    REDIR_HEREDOC_STRIP, // <<-
    REDIR_DUP_INPUT,   // <&
    REDIR_DUP_OUTPUT,  // >&
    REDIR_READWRITE,   // <>
    REDIR_CLOBBER,     // >|
} redirection_type_t;

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct ast_node_t ast_node_t;
typedef struct ast_node_list_t ast_node_list_t;

/* ============================================================================
 * AST Node List Structure
 * ============================================================================ */

/**
 * Dynamic array of AST nodes
 */
struct ast_node_list_t
{
    ast_node_t **nodes;
    int size;
    int capacity;
};

/* ============================================================================
 * AST Node Structure
 * ============================================================================ */

/**
 * Generic AST node structure.
 * The interpretation of fields depends on the node type.
 */
struct ast_node_t
{
    ast_node_type_t type;

    /* Location tracking from source tokens */
    int first_line;
    int first_column;
    int last_line;
    int last_column;

    /* Common fields used by different node types */
    union
    {
        /* AST_SIMPLE_COMMAND */
        struct
        {
            token_list_t *words;           // command name and arguments
            ast_node_list_t *redirections; // redirections for this command
            token_list_t *assignments;     // variable assignments (name=value)
        } simple_command;

        /* AST_PIPELINE */
        struct
        {
            ast_node_list_t *commands; // list of commands in the pipeline
            bool is_negated;           // true if pipeline starts with !
        } pipeline;

        /* AST_AND_OR_LIST */
        struct
        {
            ast_node_t *left;
            ast_node_t *right;
            andor_operator_t op; // && or ||
        } andor_list;

        /* AST_COMMAND_LIST */
        struct
        {
            ast_node_list_t *items;       // list of commands/pipelines
            cmd_separator_list_t *separators; // separator after each item
        } command_list;

        /* AST_SUBSHELL, AST_BRACE_GROUP */
        struct
        {
            ast_node_t *body; // command list inside ( ) or { }
        } compound;

        /* AST_IF_CLAUSE */
        struct
        {
            ast_node_t *condition;      // condition to test
            ast_node_t *then_body;      // commands if condition is true
            ast_node_list_t *elif_list; // list of elif nodes (each is an if_clause)
            ast_node_t *else_body;      // commands if all conditions are false (optional)
        } if_clause;

        /* AST_WHILE_CLAUSE, AST_UNTIL_CLAUSE */
        struct
        {
            ast_node_t *condition; // condition to test
            ast_node_t *body;      // commands to execute in loop
        } loop_clause;

        /* AST_FOR_CLAUSE */
        struct
        {
            string_t *variable; // loop variable name
            token_list_t *words; // words to iterate over (can be NULL for "$@")
            ast_node_t *body;   // commands to execute in loop
        } for_clause;

        /* AST_CASE_CLAUSE */
        struct
        {
            token_t *word;              // word to match
            ast_node_list_t *case_items; // list of case items
        } case_clause;

        /* AST_CASE_ITEM */
        struct
        {
            token_list_t *patterns; // list of patterns
            ast_node_t *body;       // commands to execute if pattern matches
        } case_item;

        /* AST_FUNCTION_DEF */
        struct
        {
            string_t *name;                // function name
            ast_node_t *body;              // compound command (function body)
            ast_node_list_t *redirections; // optional redirections
        } function_def;

        /* AST_REDIRECTION */
        struct
        {
            redirection_type_t redir_type;
            int io_number;         // file descriptor (or -1 for default)
            token_t *target;       // filename or fd for redirection
            string_t *heredoc_content; // for heredoc
        } redirection;
    } data;
};

typedef struct ast_node_t ast_t;

/* ============================================================================
 * AST Node Lifecycle Functions
 * ============================================================================ */

/**
 * Create a new AST node of the specified type.
 * Returns NULL on allocation failure.
 */
ast_node_t *ast_node_create(ast_node_type_t type);

/**
 * Destroy an AST node and free all associated memory.
 * Safe to call with NULL.
 * Recursively destroys child nodes.
 */
void ast_node_destroy(ast_node_t **node);

/* ============================================================================
 * AST Node Accessors
 * ============================================================================ */

/**
 * Get the type of an AST node.
 */
ast_node_type_t ast_node_get_type(const ast_node_t *node);

/**
 * Set location information for an AST node.
 */
void ast_node_set_location(ast_node_t *node, int first_line, int first_column, 
                          int last_line, int last_column);

/* ============================================================================
 * AST Node Creation Helpers
 * ============================================================================ */

/**
 * Create a simple command node.
 */
ast_node_t *ast_create_simple_command(token_list_t *words, 
                                     ast_node_list_t *redirections,
                                     token_list_t *assignments);

/**
 * Create a pipeline node.
 */
ast_node_t *ast_create_pipeline(ast_node_list_t *commands, bool is_negated);

/**
 * Create an and/or list node.
 */
ast_node_t *ast_create_andor_list(ast_node_t *left, ast_node_t *right, 
                                 andor_operator_t op);

/**
 * Create a command list node.
 */
ast_node_t *ast_create_command_list(void);

/**
 * Create a subshell node.
 */
ast_node_t *ast_create_subshell(ast_node_t *body);

/**
 * Create a brace group node.
 */
ast_node_t *ast_create_brace_group(ast_node_t *body);

/**
 * Create an if clause node.
 */
ast_node_t *ast_create_if_clause(ast_node_t *condition, ast_node_t *then_body);

/**
 * Create a while clause node.
 */
ast_node_t *ast_create_while_clause(ast_node_t *condition, ast_node_t *body);

/**
 * Create an until clause node.
 */
ast_node_t *ast_create_until_clause(ast_node_t *condition, ast_node_t *body);

/**
 * Create a for clause node.
 */
ast_node_t *ast_create_for_clause(const string_t *variable, token_list_t *words, 
                                 ast_node_t *body);

/**
 * Create a case clause node.
 */
ast_node_t *ast_create_case_clause(token_t *word);

/**
 * Create a case item node.
 */
ast_node_t *ast_create_case_item(token_list_t *patterns, ast_node_t *body);

/**
 * Create a function definition node.
 */
ast_node_t *ast_create_function_def(const string_t *name, ast_node_t *body,
                                   ast_node_list_t *redirections);

/**
 * Create a redirection node.
 */
ast_node_t *ast_create_redirection(redirection_type_t redir_type, int io_number, 
                                  token_t *target);

/* ============================================================================
 * AST Node List Functions
 * ============================================================================ */

/**
 * Create a new AST node list.
 * Returns NULL on allocation failure.
 */
ast_node_list_t *ast_node_list_create(void);

/**
 * Destroy an AST node list and all contained nodes.
 * Safe to call with NULL.
 */
void ast_node_list_destroy(ast_node_list_t **list);

/**
 * Append an AST node to a list.
 * The list takes ownership of the node.
 * Returns 0 on success, -1 on failure.
 */
int ast_node_list_append(ast_node_list_t *list, ast_node_t *node);

/**
 * Get the number of nodes in a list.
 */
int ast_node_list_size(const ast_node_list_t *list);

/**
 * Get a node by index.
 * Returns NULL if index is out of bounds.
 */
ast_node_t *ast_node_list_get(const ast_node_list_t *list, int index);


bool ast_node_command_list_has_separators(const ast_node_t *node);
int ast_node_command_list_separator_count(const ast_node_t *node);
cmd_separator_t ast_node_command_list_get_separator(const ast_node_t *node, int index);

/* ============================================================================
 * AST Utility Functions
 * ============================================================================ */

/**
 * Convert an AST node type to a human-readable string.
 */
const char *ast_node_type_to_string(ast_node_type_t type);

/**
 * Create a debug string representation of an AST node.
 * Caller is responsible for freeing the returned string.
 */
string_t *ast_node_to_string(const ast_node_t *node);

/**
 * Create a debug string representation of an AST (tree format).
 * Caller is responsible for freeing the returned string.
 */
string_t *ast_tree_to_string(const ast_node_t *root);

void ast_print(const ast_node_t *root);

/* ============================================================================
 * Command Separator List Functions
 * ============================================================================ */
cmd_separator_list_t *cmd_separator_list_create(void);
void cmd_separator_list_destroy(cmd_separator_list_t **list);
void cmd_separator_list_add(cmd_separator_list_t *list, cmd_separator_t sep);
cmd_separator_t cmd_separator_list_get(const cmd_separator_list_t *list, int index);

#endif /* AST_H */

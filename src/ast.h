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
    /* Basic command constructs */
    AST_SIMPLE_COMMAND,    // command with arguments and redirections
    AST_PIPELINE,          // sequence of commands connected by pipes
    AST_AND_OR_LIST,       // commands connected by && or ||
    AST_COMMAND_LIST,      // commands separated by ; or & or newlines

    /* Compound commands */
    AST_SUBSHELL,          // ( command_list )
    AST_BRACE_GROUP,       // { command_list; }
    AST_IF_CLAUSE,         // if/then/else/fi
    AST_WHILE_CLAUSE,      // while/do/done
    AST_UNTIL_CLAUSE,      // until/do/done
    AST_FOR_CLAUSE,        // for/in/do/done
    AST_CASE_CLAUSE,       // case/in/esac
    AST_FUNCTION_DEF,      // name() compound-command [redirections]
    AST_REDIRECTED_COMMAND, // a decorator for commands with redirections. It may have multiple redirections.
                           // It can have many different command types as its child.

    /* Auxiliary nodes */
    AST_REDIRECTION,        // A single I/O redirection. There are one or more of these per
                            // REDIRECTED_COMMAND.
    AST_CASE_ITEM,         // pattern) command_list ;;
    AST_FUNCTION_STORED,   // placeholder for function node moved to function store

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
    PIPE_NORMAL,      // pipe stdout only
    PIPE_MERGE_STDERR // pipe stdout + stderr
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
 * Command list separators.
 *
 * DESIGN DECISION: Each command in a command_list has an associated separator,
 * including the last command (which gets CMD_EXEC_END). This ensures:
 *   - items.size == separators.len (1:1 correspondence)
 *   - Simpler indexing: separator[i] describes what follows command[i]
 *   - Executor can easily determine if a command should run in background
 *
 * Example: "echo foo; echo bar; echo baz"
 *   Command 0: "echo foo"  -> separator: CMD_EXEC_SEQUENTIAL
 *   Command 1: "echo bar"  -> separator: CMD_EXEC_SEQUENTIAL
 *   Command 2: "echo baz"  -> separator: CMD_EXEC_END (no actual token)
 */
typedef enum
{
    CMD_EXEC_SEQUENTIAL, // run, wait, then run next
    CMD_EXEC_BACKGROUND, // run without waiting
    CMD_EXEC_END         // no more commands
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
    REDIR_READ,        // <      open file for reading
    REDIR_WRITE,       // >      truncate + write
    REDIR_APPEND,      // >>     append
    REDIR_READWRITE,   // <>     read/write
    REDIR_WRITE_FORCE, // >|     force overwrite (ignore noclobber)

    REDIR_FD_DUP_IN,  // <&     duplicate input FD
    REDIR_FD_DUP_OUT, // >&     duplicate output FD

    REDIR_FROM_BUFFER,      // <<     heredoc, but now it's a buffer
    REDIR_FROM_BUFFER_STRIP // <<-    strip leading tabs
} redirection_type_t;

typedef enum
{
    REDIR_TARGET_INVALID,  // should never happen
    REDIR_TARGET_FILE,    // target is a filename
    REDIR_TARGET_FD,       // target is a numeric fd
    REDIR_TARGET_CLOSE,   // target is '-', indicating close fd
    REDIR_TARGET_FD_STRING, // io_location is a string (e.g., <&var). Probably UNUSED
    REDIR_TARGET_BUFFER // buffer (heredoc)
} redir_target_kind_t;

typedef enum
{
    CASE_ACTION_NONE,
    CASE_ACTION_BREAK,
    CASE_ACTION_FALLTHROUGH
} case_action_t;

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
    int padding;

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
                                           // NOTE: simple commands's redirections apply only to this command,
                                           // while AST_REDIRECTED_COMMAND's redirections apply to the entire command or compound.
            token_list_t *assignments;     // variable assignments (name=value)
        } simple_command;

        /* AST_PIPELINE */
        struct
        {
            ast_node_list_t *commands; // list of commands in the pipeline
            bool is_negated;           // true if pipeline starts with !
            char padding[7];
        } pipeline;

        /* AST_AND_OR_LIST */
        struct
        {
            ast_node_t *left;
            ast_node_t *right;
            andor_operator_t op; // && or ||
            int padding;
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
            ast_node_list_t *elif_list; // list of elif nodes (each is an AST_ELIF_CLAUSE)
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
            case_action_t action; // ;;, ;&, or none
        } case_item;

        /* AST_FUNCTION_DEF */
        struct
        {
            string_t *name;                // function name
            ast_node_t *body;              // compound command (function body)
            ast_node_list_t *redirections; // optional redirections
        } function_def;

        /* AST_REDIRECTED_COMMAND */
        struct
        {
            ast_node_t *command;           // wrapped command (compound/simple/function/etc.)
            ast_node_list_t *redirections; // trailing redirections applied to the command
        } redirected_command;

        /* AST_REDIRECTION */
        struct
        {
            redirection_type_t redir_type;
            int io_number;                // fd being redirected (or -1)
            redir_target_kind_t operand; // operand type
#ifndef FUTURE
            int padding;
            string_t *fd_string;     // used only when operand == REDIR_TARGET_FD_STRING
            token_t *target;           // used when operand == FILENAME or FD
            string_t *buffer;        // used when operand == BUFFER (heredoc content)
#else
            redir_payload_t payload_type;
            union {
                string_t *fd_string;     // used only when operand == REDIR_TARGET_FD_STRING
                token_t *target;           // used when operand == FILENAME or FD
                string_t *buffer;        // used when operand == BUFFER (heredoc content)
            } data;
#endif
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

ast_node_t *ast_node_clone(const ast_node_t *node);

/**
 * Create a placeholder node indicating a function was moved to the function store.
 * This is used to replace function definition nodes after ownership transfer.
 * Returns NULL on allocation failure.
 */
ast_node_t *ast_node_create_function_stored(void);

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
                          int last_line,
                           int last_column);

void ast_command_list_node_append_item(ast_node_t *node, ast_node_t *item);

void ast_command_list_node_append_separator(ast_node_t *node, cmd_separator_t separator);

void ast_redirection_node_set_buffer_content(ast_node_t *node, const string_t *content);

redirection_type_t ast_redirection_node_get_redir_type(const ast_node_t *node);

const char *redirection_type_to_string(redirection_type_t type);
/* ============================================================================
 * AST Node Creation Helpers
 * ============================================================================ */

/*
 * OWNERSHIP POLICY FOR TOKENS IN AST:
 *
 * The AST takes FULL OWNERSHIP of all token_t pointers and token_list_t
 * structures passed to ast_create_* functions. This includes:
 *   - Individual token_t* (e.g., for case_clause.word, redirection.target)
 *   - token_list_t* (e.g., for simple_command.words, for_clause.words)
 *
 * When an AST node is destroyed via ast_node_destroy():
 *   - All token_t objects are destroyed via token_destroy()
 *   - All token_list_t structures are destroyed via token_list_destroy()
 *   - This recursively destroys all tokens within the lists
 *
 * The caller must NOT:
 *   - Destroy tokens after passing them to AST
 *   - Keep references to tokens after passing them to AST
 *   - Use token_list_destroy() on lists after passing them to AST
 *
 * The caller should:
 *   - Call token_list_release_tokens() on the original token_list from
 *     the parser to clear pointers without destroying tokens
 *   - Then free the token_list structure itself
 */

/**
 * Create a simple command node.
 * OWNERSHIP: Takes ownership of words, redirections, and assignments.
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
 * OWNERSHIP: Takes ownership of words token list (clones variable string).
 */
ast_node_t *ast_create_for_clause(const string_t *variable, token_list_t *words,
                                 ast_node_t *body);

/**
 * Create a case clause node.
 * OWNERSHIP: Takes ownership of word token.
 */
ast_node_t *ast_create_case_clause(token_t *word);

/**
 * Create a case item node.
 * OWNERSHIP: Takes ownership of patterns token list.
 */
ast_node_t *ast_create_case_item(token_list_t *patterns, ast_node_t *body);

/**
 * Create a function definition node.
 */
ast_node_t *ast_create_function_def(const string_t *name, ast_node_t *body,
                                   ast_node_list_t *redirections);

/**
 * Create a redirected command wrapper node.
 */
ast_node_t *ast_create_redirected_command(ast_node_t *command, ast_node_list_t *redirections);

/**
 * Create a redirection node.
 * OWNERSHIP: Takes ownership of target token.
 */
ast_node_t *ast_create_redirection(redirection_type_t redir_type,
                                   redir_target_kind_t operand, int io_number,
                                  string_t *fd_string, token_t *target);

/* ============================================================================
 * AST Node List Functions
 * ============================================================================ */

/**
 * Create a new AST node list.
 * Returns NULL on allocation failure.
 */
ast_node_list_t *ast_node_list_create(void);

// Deep clone an AST node list
ast_node_list_t *ast_node_list_clone(const ast_node_list_t *other);

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

/* ============================================================================
 * Command List Separator Access Functions
 * ============================================================================ */

/**
 * Check if a command list node has any separators.
 * INVARIANT: For command lists, separator_count() == item_count()
 */
bool ast_node_command_list_has_separators(const ast_node_t *node);

/**
 * Get the number of separators in a command list.
 * INVARIANT: Returns the same value as ast_node_list_size() for the items.
 */
int ast_node_command_list_separator_count(const ast_node_t *node);

/**
 * Get a separator by index.
 * The separator at index i describes what follows command i.
 * The last command will have CMD_EXEC_END if no separator token was present.
 */
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

// Deep clone a command separator list
cmd_separator_list_t *cmd_separator_list_clone(const cmd_separator_list_t *other);

void cmd_separator_list_destroy(cmd_separator_list_t **lst);
void cmd_separator_list_add(cmd_separator_list_t *list, cmd_separator_t sep);
cmd_separator_t cmd_separator_list_get(const cmd_separator_list_t *list, int index);

#endif /* AST_H */

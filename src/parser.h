#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "string_t.h"
#include "token.h"
#include <stdbool.h>

/* ============================================================================
 * Parser Status (return codes)
 * ============================================================================ */

typedef enum
{
    PARSE_OK = 0,       // successful parsing
    PARSE_ERROR,        // syntax error during parsing
    PARSE_INCOMPLETE,   // need more tokens to complete parsing
    PARSE_EMPTY,        // empty input (no tokens to parse)
} parse_status_t;

/* ============================================================================
 * Parser Context
 * ============================================================================ */

typedef struct parser_t
{
    /* Input tokens */
    token_list_t *tokens;
    int position; // current position in token list

    /* Error reporting */
    string_t *error_msg;
    int error_line;
    int error_column;

    /* Parser state */
    bool allow_in_keyword; // context flag for "in" keyword in for loops

} parser_t;

/* ============================================================================
 * Parser Lifecycle Functions
 * ============================================================================ */

/**
 * Create a new parser.
 */
parser_t *parser_create(void);

/**
 * Destroy a parser and free all associated memory.
 * Safe to call with NULL.
 * Does not destroy the token list (caller retains ownership).
 */
void parser_destroy(parser_t *parser);

/* ============================================================================
 * Main Parsing Functions
 * ============================================================================ */

/**
 * Parse a token list into an AST.
 * 
 * @param parser The parser context
 * @param tokens The list of tokens to parse (parser does not take ownership)
 * @param out_ast Pointer to store the resulting AST root node
 * 
 * @return PARSE_OK on success, PARSE_ERROR on error, PARSE_EMPTY if no tokens
 * 
 * On success, caller takes ownership of the AST and must free it.
 */
parse_status_t parser_parse(parser_t *parser, token_list_t *tokens, ast_node_t **out_ast);

/**
 * Parse a complete program (command list).
 * The caller is responsible for creating and initializing the parser
 * and the AST node list.
 */
parse_status_t parser_parse_program(parser_t *parser, ast_node_t **out_node);

/**
 * Parse a command list (commands separated by ; or & or newlines).
 */
parse_status_t parser_parse_command_list(parser_t *parser, ast_node_t **out_node);

/**
 * Parse an and_or list (commands connected by && or ||).
 */
parse_status_t parser_parse_andor_list(parser_t *parser, ast_node_t **out_node);

/**
 * Parse a pipeline (commands connected by |).
 */
parse_status_t parser_parse_pipeline(parser_t *parser, ast_node_t **out_node);

/**
 * Parse a command (simple or compound).
 */
parse_status_t parser_parse_command(parser_t *parser, ast_node_t **out_node);

/**
 * Parse a simple command.
 */
parse_status_t parser_parse_simple_command(parser_t *parser, ast_node_t **out_node);

/**
 * Parse a compound command (if, while, for, case, subshell, brace group).
 */
parse_status_t parser_parse_compound_command(parser_t *parser, ast_node_t **out_node);

/**
 * Parse an if clause.
 */
parse_status_t parser_parse_if_clause(parser_t *parser, ast_node_t **out_node);

/**
 * Parse a while clause.
 */
parse_status_t parser_parse_while_clause(parser_t *parser, ast_node_t **out_node);

/**
 * Parse an until clause.
 */
parse_status_t parser_parse_until_clause(parser_t *parser, ast_node_t **out_node);

/**
 * Parse a for clause.
 */
parse_status_t parser_parse_for_clause(parser_t *parser, ast_node_t **out_node);

/**
 * Parse a case clause.
 */
parse_status_t parser_parse_case_clause(parser_t *parser, ast_node_t **out_node);

/**
 * Parse a subshell ( command_list ).
 */
parse_status_t parser_parse_subshell(parser_t *parser, ast_node_t **out_node);

/**
 * Parse a brace group { command_list; }.
 */
parse_status_t parser_parse_brace_group(parser_t *parser, ast_node_t **out_node);

/**
 * Parse a function definition name() compound-command.
 */
parse_status_t parser_parse_function_def(parser_t *parser, ast_node_t **out_node);

/**
 * Parse a redirection.
 */
parse_status_t parser_parse_redirection(parser_t *parser, ast_node_t **out_node);

/* ============================================================================
 * Token Access Functions
 * ============================================================================ */

/**
 * Get the current token without advancing.
 * Returns NULL if at end of input.
 */
token_t *parser_current_token(const parser_t *parser);

/**
 * Get the type of the current token.
 * Returns TOKEN_EOF if at end of input.
 */
token_type_t parser_current_token_type(const parser_t *parser);

/**
 * Advance to the next token.
 * Returns true if there is a next token, false if at end.
 */
bool parser_advance(parser_t *parser);

/**
 * Check if the current token matches the expected type.
 * If it matches, advances to next token and returns true.
 * Otherwise, returns false without advancing.
 */
bool parser_accept(parser_t *parser, token_type_t type);

/**
 * Expect the current token to match the expected type.
 * If it matches, advances to next token and returns PARSE_OK.
 * Otherwise, sets error and returns PARSE_ERROR.
 */
parse_status_t parser_expect(parser_t *parser, token_type_t type);

/**
 * Check if at end of input.
 */
bool parser_at_end(const parser_t *parser);

/**
 * Skip optional newlines.
 */
void parser_skip_newlines(parser_t *parser);

/* ============================================================================
 * Error Handling Functions
 * ============================================================================ */

/**
 * Set a parse error with formatted message.
 */
void parser_set_error(parser_t *parser, const char *format, ...);

/**
 * Get the error message from the last failed operation.
 * Returns NULL if no error.
 */
const char *parser_get_error(const parser_t *parser);

/**
 * Clear the error state.
 */
void parser_clear_error(parser_t *parser);

#endif /* PARSER_H */

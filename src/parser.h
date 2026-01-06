#ifndef PARSER_H
#define PARSER_H

#include "gnode.h"
#include "string_t.h"
#include "token.h"
#include <stdbool.h>

/* ============================================================================
 * Parser Status (return codes)
 * ============================================================================ */

typedef enum
{
    PARSE_OK = 0,     // successful parsing
    PARSE_ERROR,      // syntax error during parsing
    PARSE_INCOMPLETE, // need more tokens to complete parsing
    PARSE_EMPTY,      // empty input (no tokens to parse)
} parse_status_t;

/* ============================================================================
 * Parser Context
 * ============================================================================ */

typedef struct parser_t
{
    /* Input tokens */
    token_list_t *tokens;
    int position; // current position in token list
    int padding;

    /* Error reporting */
    string_t *error_msg;
    int error_line;
    int error_column;
} parser_t;

/* ============================================================================
 * Parser Lifecycle Functions
 * ============================================================================ */

/**
 * Create a new parser.
 */
parser_t *parser_create(void);

/**
 * Create a new parser with a token list.
 *
 * @param tokens The list of tokens to parse (parser does not take ownership)
 *
 * OWNERSHIP POLICY:
 *   - The parser does NOT take ownership of the token_list structure itself
 *   - The resulting grammar AST DOES take ownership of individual token_t objects
 *     from the list
 *   - After successful parsing, the caller must:
 *     1. Call token_list_release_tokens() to clear pointers without
 *        destroying tokens (which are now owned by the grammar AST)
 *     2. Free the token_list structure itself
 *     3. Eventually destroy the grammar AST with g_node_destroy(), which will
 *        destroy all the tokens
 *   - On parse failure, the token_list retains all its tokens and should
 *     be destroyed normally with token_list_destroy()
 */
parser_t *parser_create_with_tokens(token_list_t *tokens);

/**
 * Destroy a parser and free all associated memory.
 * Safe to call with NULL.
 * Does not destroy the token list (caller retains ownership).
 */
void parser_destroy(parser_t **parser);

/* ============================================================================
 * Main Parsing Functions
 * ============================================================================ */

/**
 * Parse the token list into a grammar AST.
 *
 * @param parser The parser context (must have tokens set)
 * @param out_gnode Pointer to store the resulting grammar AST root node
 *
 * @return PARSE_OK on success, PARSE_ERROR on error, PARSE_EMPTY if no tokens
 *
 * On success, caller takes ownership of the grammar AST and must free it.
 */
parse_status_t parser_parse_program(parser_t *parser, gnode_t **out_gnode);

/* ============================================================================
 * Grammar Parsing Functions (POSIX-aligned)
 * ============================================================================ */

parse_status_t gparse_program(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_complete_commands(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_complete_command(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_list(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_and_or(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_pipeline(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_pipe_sequence(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_command(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_compound_command(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_subshell(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_compound_list(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_term(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_for_clause(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_in_clause(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_wordlist(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_case_clause(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_case_list_ns(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_case_list(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_case_item_ns(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_case_item(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_pattern_list(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_if_clause(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_else_part(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_while_clause(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_until_clause(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_function_definition(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_brace_group(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_do_group(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_simple_command(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_redirect_list(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_io_redirect(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_io_file(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_filename(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_io_here(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_separator_op(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_separator(parser_t *parser, gnode_t **out_node);

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

/**
 * Peek at a token at an offset from current position.
 * Returns NULL if offset is out of bounds.
 */
token_t *parser_peek_token(const parser_t *parser, int offset);

/**
 * Get the previous token.
 * Returns NULL if at start of input.
 */
token_t *parser_previous_token(const parser_t *parser);

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

/**
 * Check if the most recent parse failure was due to unexpected EOF.
 */
bool parser_error_is_unexpected_eof(const parser_t *parser, parse_status_t status);

/* ============================================================================
 * Test Functions
 * ============================================================================ */

parse_status_t parser_string_to_gnodes(const char *input, gnode_t **out_node);

gnode_t *parser_parse_string(const char *input);

#endif /* PARSER_H */
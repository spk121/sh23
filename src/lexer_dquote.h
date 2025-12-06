#ifndef LEXER_DQUOTE_H
#define LEXER_DQUOTE_H

/**
 * @file lexer_dquote.h
 * @brief POSIX shell double-quote lexer
 *
 * This module implements lexing of text within double quotes ("...").
 *
 * POSIX Double Quote Rules (per POSIX.1-2017 Section 2.2.3):
 * - Enclosing characters in double quotes preserves the literal value
 *   of all characters within the quotes, with the exception of:
 *   - $ (dollar sign) - retains special meaning for parameter expansion
 *   - ` (backtick) - retains special meaning for command substitution
 *   - \ (backslash) - retains special meaning only when followed by:
 *     $, `, ", \, or <newline>
 *   - " (double quote) - terminates the quoted string
 *
 * Inside double quotes:
 * - Parameter expansion ($var, ${var}, etc.) is performed
 * - Command substitution ($(...) and `...`) is performed
 * - Arithmetic expansion ($((...))) is performed
 * - Backslash escapes only: \$, \`, \", \\, \<newline>
 * - All other backslash sequences are preserved literally
 * - Field splitting is NOT performed on expansion results
 * - Pathname expansion is NOT performed
 *
 * The lexer produces a list of parts representing the quoted content.
 */

#include "string.h"
#include "token.h"
#include <stdbool.h>

/**
 * @enum lexer_dquote_result_t
 * @brief Result codes for double-quote lexer operations
 */
typedef enum
{
    LEXER_DQUOTE_OK = 0,                  /**< Successfully lexed quoted content */
    LEXER_DQUOTE_UNTERMINATED,            /**< Reached end of input without closing quote */
    LEXER_DQUOTE_UNTERMINATED_EXPANSION,  /**< Unclosed expansion (${, $(, etc.) */
    LEXER_DQUOTE_ERROR                    /**< General error */
} lexer_dquote_result_t;

/**
 * @struct lexer_dquote_t
 * @brief State for double-quote lexer
 */
typedef struct lexer_dquote_t
{
    const string_t *input;   /**< Input string being lexed */
    int pos;                 /**< Current position in input */
    int line;                /**< Current line number */
    int column;              /**< Current column number */
    int start_line;          /**< Line where quote started */
    int start_column;        /**< Column where quote started */
} lexer_dquote_t;

/**
 * Initialize a double-quote lexer.
 *
 * @param lexer The lexer to initialize
 * @param input The input string (must not be NULL)
 * @param start_pos Position in input after the opening double quote
 * @param line Current line number
 * @param column Current column number
 */
void lexer_dquote_init(lexer_dquote_t *lexer, const string_t *input, int start_pos, int line, int column);

/**
 * Lex the content inside double quotes.
 *
 * This function reads characters from the current position until
 * it encounters a closing double quote. It handles:
 * - Literal text (accumulated as PART_LITERAL with was_double_quoted=true)
 * - Parameter expansion ($var, ${var}, $@, $$, etc.)
 * - Command substitution ($(...) and `...`)
 * - Arithmetic expansion ($((...)))
 * - Backslash escapes for $, `, ", \, and newline
 *
 * @param lexer The double-quote lexer state
 * @param parts Output: A part_list_t containing the parsed parts
 * @return LEXER_DQUOTE_OK on success, error code otherwise
 */
lexer_dquote_result_t lexer_dquote_lex(lexer_dquote_t *lexer, part_list_t **parts);

/**
 * Get the current position in the input.
 *
 * @param lexer The lexer state
 * @return Current position (after last processed character or closing quote)
 */
int lexer_dquote_get_pos(const lexer_dquote_t *lexer);

/**
 * Get the current line number.
 *
 * @param lexer The lexer state
 * @return Current line number
 */
int lexer_dquote_get_line(const lexer_dquote_t *lexer);

/**
 * Get the current column number.
 *
 * @param lexer The lexer state
 * @return Current column number
 */
int lexer_dquote_get_column(const lexer_dquote_t *lexer);

#endif /* LEXER_DQUOTE_H */

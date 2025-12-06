#ifndef LEXER_SQUOTE_H
#define LEXER_SQUOTE_H

/**
 * @file lexer_squote.h
 * @brief POSIX shell single-quote lexer
 *
 * This module implements lexing of text within single quotes ('...').
 *
 * POSIX Single Quote Rules (per POSIX.1-2017 Section 2.2.2):
 * - Enclosing characters in single quotes preserves the literal value
 *   of each character within the quotes.
 * - A single quote cannot occur within single quotes.
 * - No escape sequences or expansions are recognized inside single quotes.
 *
 * The lexer reads characters after the opening single quote until
 * a closing single quote is found, treating all characters literally.
 */

#include "string.h"
#include "token.h"
#include <stdbool.h>

/**
 * @enum lexer_squote_result_t
 * @brief Result codes for single-quote lexer operations
 */
typedef enum
{
    LEXER_SQUOTE_OK = 0,         /**< Successfully lexed quoted content */
    LEXER_SQUOTE_UNTERMINATED,   /**< Reached end of input without closing quote */
    LEXER_SQUOTE_ERROR           /**< General error */
} lexer_squote_result_t;

/**
 * @struct lexer_squote_t
 * @brief State for single-quote lexer
 */
typedef struct lexer_squote_t
{
    const string_t *input;   /**< Input string being lexed */
    int pos;                 /**< Current position in input */
    int line;                /**< Current line number */
    int column;              /**< Current column number */
    int start_line;          /**< Line where quote started */
    int start_column;        /**< Column where quote started */
} lexer_squote_t;

/**
 * Initialize a single-quote lexer.
 *
 * @param lexer The lexer to initialize
 * @param input The input string (must not be NULL)
 * @param start_pos Position in input after the opening single quote
 * @param line Current line number
 * @param column Current column number
 */
void lexer_squote_init(lexer_squote_t *lexer, const string_t *input, int start_pos, int line, int column);

/**
 * Lex the content inside single quotes.
 *
 * This function reads characters from the current position until
 * it encounters a closing single quote. All characters (except the
 * closing quote) are treated literally with no escape processing
 * or expansion.
 *
 * @param lexer The single-quote lexer state
 * @param part Output: A PART_LITERAL with was_single_quoted=true
 * @return LEXER_SQUOTE_OK on success, LEXER_SQUOTE_UNTERMINATED if
 *         end-of-input reached before closing quote
 */
lexer_squote_result_t lexer_squote_lex(lexer_squote_t *lexer, part_t **part);

/**
 * Get the current position in the input.
 *
 * @param lexer The lexer state
 * @return Current position (after last processed character or closing quote)
 */
int lexer_squote_get_pos(const lexer_squote_t *lexer);

/**
 * Get the current line number.
 *
 * @param lexer The lexer state
 * @return Current line number
 */
int lexer_squote_get_line(const lexer_squote_t *lexer);

/**
 * Get the current column number.
 *
 * @param lexer The lexer state
 * @return Current column number
 */
int lexer_squote_get_column(const lexer_squote_t *lexer);

#endif /* LEXER_SQUOTE_H */

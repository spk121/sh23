/**
 * @file lexer_squote.c
 * @brief POSIX shell single-quote lexer implementation
 *
 * This module implements lexing of text within single quotes ('...').
 *
 * POSIX Single Quote Rules (per POSIX.1-2017 Section 2.2.2):
 * - Enclosing characters in single quotes preserves the literal value
 *   of each character within the quotes.
 * - A single quote cannot occur within single quotes.
 * - No escape sequences or expansions are recognized inside single quotes.
 *
 * Key differences from normal lexing context:
 * - No escape sequences (\n, \t, etc.) are recognized
 * - Dollar sign ($) has no special meaning (no parameter expansion)
 * - Backtick (`) has no special meaning (no command substitution)
 * - Backslash (\) has no special meaning (treated literally)
 * - Newlines are preserved literally
 * - Only the closing single quote (') terminates the quoted region
 *
 * Key differences from double-quote context:
 * - Double quotes allow $, `, and certain backslash escapes
 * - Single quotes are simpler: everything is literal except '
 */

#include "lexer_squote.h"
#include "logging.h"
#include "xalloc.h"
#include <stdlib.h>

/* ============================================================================
 * Initialization
 * ============================================================================ */

void lexer_squote_init(lexer_squote_t *lexer, const string_t *input, int start_pos, int line, int column)
{
    Expects_not_null(lexer);
    Expects_not_null(input);
    Expects_ge(start_pos, 0);
    Expects_ge(line, 1);
    Expects_ge(column, 1);

    lexer->input = input;
    lexer->pos = start_pos;
    lexer->line = line;
    lexer->column = column;
    lexer->start_line = line;
    lexer->start_column = column;
}

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/**
 * Peek at the current character without advancing.
 * Returns '\0' if at end of input.
 */
static char peek_char(const lexer_squote_t *lexer)
{
    if (lexer->pos >= string_length(lexer->input))
    {
        return '\0';
    }
    return string_char_at(lexer->input, lexer->pos);
}

/**
 * Advance position by one character, updating line/column tracking.
 */
static void advance_char(lexer_squote_t *lexer)
{
    if (lexer->pos >= string_length(lexer->input))
    {
        return;
    }

    char c = string_char_at(lexer->input, lexer->pos);
    lexer->pos++;

    if (c == '\n')
    {
        lexer->line++;
        lexer->column = 1;
    }
    else
    {
        lexer->column++;
    }
}

/* ============================================================================
 * Main Lexing Function
 * ============================================================================ */

lexer_squote_result_t lexer_squote_lex(lexer_squote_t *lexer, part_t **part)
{
    Expects_not_null(lexer);
    Expects_not_null(part);

    *part = NULL;

    /* Accumulate literal content */
    string_t *content = string_create_empty(64);

    while (1)
    {
        char c = peek_char(lexer);

        if (c == '\0')
        {
            /* End of input before closing quote */
            string_destroy(content);
            return LEXER_SQUOTE_UNTERMINATED;
        }

        if (c == '\'')
        {
            /* Closing single quote found */
            advance_char(lexer); /* consume the closing quote */
            break;
        }

        /* All other characters are literal - append and advance */
        string_append_ascii_char(content, c);
        advance_char(lexer);
    }

    /* Create a PART_LITERAL marked as single-quoted */
    *part = part_create_literal(content);
    if (*part != NULL)
    {
        part_set_quoted(*part, true, false); /* single_quoted=true, double_quoted=false */
    }

    string_destroy(content);

    return LEXER_SQUOTE_OK;
}

/* ============================================================================
 * Accessors
 * ============================================================================ */

int lexer_squote_get_pos(const lexer_squote_t *lexer)
{
    Expects_not_null(lexer);
    return lexer->pos;
}

int lexer_squote_get_line(const lexer_squote_t *lexer)
{
    Expects_not_null(lexer);
    return lexer->line;
}

int lexer_squote_get_column(const lexer_squote_t *lexer)
{
    Expects_not_null(lexer);
    return lexer->column;
}

/**
 * @file lexer_squote.c
 * @brief Lexer module for POSIX shell single-quoted strings
 *
 * This module handles lexing of characters inside single quotes ('...').
 * Per POSIX specification, single quotes preserve the literal value of
 * each character within the quotes. A single quote may not occur between
 * single quotes, even when preceded by a backslash.
 */

#define LEXER_INTERNAL
#include "lexer_squote.h"

#include "lexer_t.h"
#include "token.h"

/**
 * Append a character from a single-quoted context to the current word.
 *
 * This creates or extends a literal part with the single-quoted flag set,
 * which prevents any expansion of the character.
 */
static void lexer_append_squote_char_to_word(lexer_t *lx, char c)
{
    Expects_not_null(lx);
    Expects_not_null(lx->current_token);

    // Check if last part is a literal with single-quoted flag
    int part_count = token_part_count(lx->current_token);
    if (part_count > 0)
    {
        part_t *last_part = token_get_part(lx->current_token, part_count - 1);
        if (part_get_type(last_part) == PART_LITERAL
            && part_was_single_quoted(last_part)
            && !part_was_double_quoted(last_part))
        {
            // Append to existing single-quoted literal part
            token_append_char_to_last_literal_part(lx->current_token, c);
            return;
        }
    }

    // Create new single-quoted literal part
    char buf[2] = {c, '\0'};
    string_t *s = string_create_from_cstr(buf);
    part_t *part = part_create_literal(s);
    part_set_quoted(part, true, false); // single-quoted, not double-quoted
    token_add_part(lx->current_token, part);
    string_destroy(&s);
}

lex_status_t lexer_process_squote(lexer_t *lx)
{
    Expects_not_null(lx);

    // We enter this function right after consuming the opening '
    // So we are already inside single-quote mode

    // Ensure we have a word token to build
    if (!lx->in_word)
    {
        lexer_start_word(lx);
    }

    // Mark the token as quoted
    token_set_quoted(lx->current_token, true);

    while (!lexer_at_end(lx))
    {
        char c = lexer_peek(lx);

        if (c == '\'')
        {
            // Found the closing quote
            lexer_advance(lx);  // consume the '
            lexer_pop_mode(lx); // back to previous mode (usually NORMAL or DOUBLE_QUOTE)

            // Don't finalize the word here - the calling mode will decide
            // when the word ends based on delimiters
            return LEX_OK;
        }

        // All other characters, including \, $, `, ", newline — literal!
        lexer_append_squote_char_to_word(lx, c);
        lexer_advance(lx);
    }

    // End of input without closing quote
    return LEX_INCOMPLETE;
}

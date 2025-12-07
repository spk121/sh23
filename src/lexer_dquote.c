/**
 * @file lexer_dquote.c
 * @brief Lexer module for POSIX shell double-quoted strings
 *
 * This module handles lexing of characters inside double quotes ("...").
 * Per POSIX specification, double quotes preserve the literal value of
 * all characters except: $ (parameter expansion), ` (command substitution),
 * \ (escape), and " (closing quote).
 *
 * Within double quotes, backslash escapes only the following characters:
 *   $ ` " \ newline
 * For any other character, the backslash is preserved literally.
 */

#include "lexer_dquote.h"
#include "lexer.h"
#include "token.h"
#include <ctype.h>

/**
 * Characters that can be escaped with backslash inside double quotes.
 * Per POSIX, only these characters lose the backslash when escaped.
 */
static bool is_dquote_escapable(char c)
{
    return (c == '$' || c == '`' || c == '"' || c == '\\' || c == '\n');
}

/**
 * Check if a character is a special parameter character.
 * These can follow $ directly without braces.
 */
static bool is_special_param_char(char c)
{
    return (isdigit(c) || c == '#' || c == '?' || c == '-' || c == '$' || c == '!' || c == '@' ||
            c == '*' || c == '_');
}

/**
 * Check if a character can start a parameter name.
 */
static bool is_name_start_char(char c)
{
    return (isalpha(c) || c == '_');
}

/**
 * Append a character from a double-quoted context to the current word.
 *
 * This creates or extends a literal part with the double-quoted flag set,
 * which affects field splitting behavior on expansions.
 */
static void lexer_append_dquote_char_to_word(lexer_t *lx, char c)
{
    Expects_not_null(lx);
    Expects_not_null(lx->current_token);

    // Check if last part is a literal with double-quoted flag
    int part_count = token_part_count(lx->current_token);
    if (part_count > 0)
    {
        part_t *last_part = token_get_part(lx->current_token, part_count - 1);
        if (part_get_type(last_part) == PART_LITERAL && part_was_double_quoted(last_part) &&
            !part_was_single_quoted(last_part))
        {
            // Append to existing double-quoted literal part
            token_append_char_to_last_literal_part(lx->current_token, c);
            return;
        }
    }

    // Create new double-quoted literal part
    char buf[2] = {c, '\0'};
    string_t *s = string_create_from_cstr(buf);
    part_t *part = part_create_literal(s);
    part_set_quoted(part, false, true); // not single-quoted, double-quoted
    token_add_part(lx->current_token, part);
    string_destroy(s);
}

lex_status_t lexer_process_dquote(lexer_t *lx)
{
    Expects_not_null(lx);

    // We enter this function right after consuming the opening "
    // So we are already inside double-quote mode

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

        if (c == '"')
        {
            // Found the closing quote
            lexer_advance(lx);  // consume the "
            lexer_pop_mode(lx); // back to previous mode

            // Don't finalize the word here - the calling mode will decide
            // when the word ends based on delimiters
            return LEX_OK;
        }

        // Handle backslash escapes
        if (c == '\\')
        {
            char next_c = lexer_peek_ahead(lx, 1);

            // Handle trailing backslash gracefully
            if (next_c == '\0')
            {
                lexer_append_dquote_char_to_word(lx, '\\');
                lexer_advance(lx);
                return LEX_INCOMPLETE;
            }

            lexer_advance(lx); // consume backslash

            if (is_dquote_escapable(next_c))
            {
                if (next_c == '\n')
                {
                    lexer_advance(lx); // consume newline
                    lx->line_no++;
                    lx->col_no = 1;
                    continue;
                }
                else
                {
                    lexer_append_dquote_char_to_word(lx, next_c);
                    lexer_advance(lx);
                }
            }
            else
            {
                // Backslash not escaping anything → keep both
                lexer_append_dquote_char_to_word(lx, '\\');
                lexer_append_dquote_char_to_word(lx, next_c);
                lexer_advance(lx);
            }
            continue;
        }

        if (c == '`')
        {
            // Command substitution with backticks
            lexer_advance(lx);
            lexer_push_mode(lx, LEX_CMD_SUBST_BACKTICK);
            return LEX_INCOMPLETE;
        }

        if (c == '$')
        {
            char c2 = lexer_peek_ahead(lx, 1);
            if (c2 == '\0')
            {
                // $ at end of input - need more input to determine type
                return LEX_INCOMPLETE;
            }

            char c3 = lexer_peek_ahead(lx, 2);

            if (c2 == '{')
            {
                // Braced parameter expansion: ${...}
                lexer_push_mode(lx, LEX_PARAM_EXP_BRACED);
                lexer_advance(lx); // consume $
                lexer_advance(lx); // consume {
                return LEX_INCOMPLETE;
            }
            else if (c2 == '(' && c3 == '(')
            {
                // Arithmetic expansion: $((...))
                lexer_push_mode(lx, LEX_ARITH_EXP);
                lexer_advance(lx); // consume $
                lexer_advance(lx); // consume (
                lexer_advance(lx); // consume (
                return LEX_INCOMPLETE;
            }
            else if (c2 == '(')
            {
                // Command substitution: $(...)
                lexer_push_mode(lx, LEX_CMD_SUBST_PAREN);
                lexer_advance(lx); // consume $
                lexer_advance(lx); // consume (
                return LEX_INCOMPLETE;
            }
            else if (is_name_start_char(c2) || is_special_param_char(c2))
            {
                // Unbraced parameter expansion: $var or $1, $?, etc.
                lexer_push_mode(lx, LEX_PARAM_EXP_UNBRACED);
                lexer_advance(lx); // consume $
                return LEX_INCOMPLETE;
            }
            else
            {
                // Just a literal $ (not followed by valid expansion start)
                lexer_append_dquote_char_to_word(lx, c);
                lexer_advance(lx);
            }
            continue;
        }

        // All other characters are literal inside double quotes
        lexer_append_dquote_char_to_word(lx, c);
        lexer_advance(lx);
    }

    // End of input without closing quote
    return LEX_INCOMPLETE;
}

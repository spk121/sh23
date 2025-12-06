/**
 * @file lexer_cmd_subst.c
 * @brief Lexer module for POSIX shell command substitution
 *
 * This module handles lexing of command substitution:
 * - Parenthesized form: $(command)
 * - Backtick form: `command`
 *
 * Per POSIX specification, command substitution allows the output
 * of a command to replace the command itself. Both forms are
 * functionally equivalent but have different quoting rules.
 *
 * In the $(...) form, all characters are processed normally,
 * following standard shell parsing rules.
 *
 * In the backtick form, backslashes retain their literal meaning
 * except when followed by $, `, \, or newline.
 */

#include "lexer_cmd_subst.h"
#include "lexer.h"
#include "logging.h"
#include "token.h"

/**
 * Check if a character is escapable within backtick command substitution.
 * Per POSIX, within backticks, backslash only escapes: $ ` \ newline
 */
static bool is_backtick_escapable(char c)
{
    return (c == '$' || c == '`' || c == '\\' || c == '\n');
}

lex_status_t lexer_process_cmd_subst_paren(lexer_t *lx)
{
    Expects_not_null(lx);

    // We enter after $( has been consumed
    // Ensure we have a word token to build
    if (!lx->in_word)
    {
        lexer_start_word(lx);
    }

    // Track parenthesis nesting depth
    int paren_depth = 1;

    // Create a token list to hold the nested command tokens
    token_list_t *nested = token_list_create();

    // Buffer to accumulate raw command text
    string_t *cmd_text = string_create_empty(64);

    while (!lexer_at_end(lx))
    {
        char c = lexer_peek(lx);

        // Handle closing parenthesis
        if (c == ')')
        {
            paren_depth--;
            if (paren_depth == 0)
            {
                // Found the matching closing paren
                lexer_advance(lx); // consume )

                // Add command substitution part to current token
                // For now, store the raw command text in a literal
                // The nested token list can be populated by a sub-lexer
                // in a more complete implementation
                part_t *part = part_create_command_subst(nested);

                // Store raw text for later parsing
                if (string_length(cmd_text) > 0)
                {
                    part->text = cmd_text;
                }
                else
                {
                    string_destroy(cmd_text);
                }

                // Mark as double-quoted if inside double quotes
                if (lexer_in_mode(lx, LEX_DOUBLE_QUOTE))
                {
                    part_set_quoted(part, false, true);
                }

                token_add_part(lx->current_token, part);
                lx->current_token->needs_expansion = true;

                // Field splitting only if not in double quotes
                if (!lexer_in_mode(lx, LEX_DOUBLE_QUOTE))
                {
                    lx->current_token->needs_field_splitting = true;
                }

                lexer_pop_mode(lx);
                return LEX_OK;
            }
            // Nested paren, include in command text
            string_append_ascii_char(cmd_text, c);
            lexer_advance(lx);
            continue;
        }

        // Handle opening parenthesis (nesting)
        if (c == '(')
        {
            paren_depth++;
            string_append_ascii_char(cmd_text, c);
            lexer_advance(lx);
            continue;
        }

        // Handle escape sequences
        if (c == '\\')
        {
            char next_c = lexer_peek_ahead(lx, 1);
            if (next_c == '\0')
            {
                // Backslash at end of input - need more input
                string_destroy(cmd_text);
                token_list_destroy(nested);
                return LEX_INCOMPLETE;
            }

            if (next_c == '\n')
            {
                // Line continuation - consume both, don't add to text
                lexer_advance(lx);
                lexer_advance(lx);
            }
            else
            {
                // Include both backslash and following character
                string_append_ascii_char(cmd_text, c);
                lexer_advance(lx);
                string_append_ascii_char(cmd_text, next_c);
                lexer_advance(lx);
            }
            continue;
        }

        // Handle single quotes - everything literal until closing quote
        if (c == '\'')
        {
            string_append_ascii_char(cmd_text, c);
            lexer_advance(lx);
            while (!lexer_at_end(lx) && lexer_peek(lx) != '\'')
            {
                string_append_ascii_char(cmd_text, lexer_peek(lx));
                lexer_advance(lx);
            }
            if (lexer_at_end(lx))
            {
                string_destroy(cmd_text);
                token_list_destroy(nested);
                return LEX_INCOMPLETE;
            }
            string_append_ascii_char(cmd_text, lexer_peek(lx)); // closing quote
            lexer_advance(lx);
            continue;
        }

        // Handle double quotes - track nesting
        if (c == '"')
        {
            string_append_ascii_char(cmd_text, c);
            lexer_advance(lx);
            while (!lexer_at_end(lx))
            {
                char inner = lexer_peek(lx);
                if (inner == '"')
                {
                    string_append_ascii_char(cmd_text, inner);
                    lexer_advance(lx);
                    break;
                }
                if (inner == '\\')
                {
                    char next = lexer_peek_ahead(lx, 1);
                    if (next == '\0')
                    {
                        string_destroy(cmd_text);
                        token_list_destroy(nested);
                        return LEX_INCOMPLETE;
                    }
                    string_append_ascii_char(cmd_text, inner);
                    lexer_advance(lx);
                    string_append_ascii_char(cmd_text, next);
                    lexer_advance(lx);
                    continue;
                }
                string_append_ascii_char(cmd_text, inner);
                lexer_advance(lx);
            }
            if (lexer_at_end(lx))
            {
                string_destroy(cmd_text);
                token_list_destroy(nested);
                return LEX_INCOMPLETE;
            }
            continue;
        }

        // Handle nested command substitution $(...)
        if (c == '$' && lexer_peek_ahead(lx, 1) == '(')
        {
            string_append_ascii_char(cmd_text, c);
            lexer_advance(lx);
            string_append_ascii_char(cmd_text, '(');
            lexer_advance(lx);
            paren_depth++; // Track as nested paren
            continue;
        }

        // Handle backtick inside $(...) - include as-is
        if (c == '`')
        {
            string_append_ascii_char(cmd_text, c);
            lexer_advance(lx);
            // Read until matching backtick
            while (!lexer_at_end(lx) && lexer_peek(lx) != '`')
            {
                char inner = lexer_peek(lx);
                if (inner == '\\')
                {
                    char next = lexer_peek_ahead(lx, 1);
                    if (next != '\0' && is_backtick_escapable(next))
                    {
                        string_append_ascii_char(cmd_text, inner);
                        lexer_advance(lx);
                        string_append_ascii_char(cmd_text, next);
                        lexer_advance(lx);
                        continue;
                    }
                }
                string_append_ascii_char(cmd_text, inner);
                lexer_advance(lx);
            }
            if (lexer_at_end(lx))
            {
                string_destroy(cmd_text);
                token_list_destroy(nested);
                return LEX_INCOMPLETE;
            }
            string_append_ascii_char(cmd_text, lexer_peek(lx)); // closing backtick
            lexer_advance(lx);
            continue;
        }

        // All other characters - add to command text
        string_append_ascii_char(cmd_text, c);
        lexer_advance(lx);
    }

    // End of input without closing paren
    string_destroy(cmd_text);
    token_list_destroy(nested);
    return LEX_INCOMPLETE;
}

lex_status_t lexer_process_cmd_subst_backtick(lexer_t *lx)
{
    Expects_not_null(lx);

    // We enter after the opening ` has been consumed
    // Ensure we have a word token to build
    if (!lx->in_word)
    {
        lexer_start_word(lx);
    }

    // Create a token list to hold the nested command tokens
    token_list_t *nested = token_list_create();

    // Buffer to accumulate raw command text
    string_t *cmd_text = string_create_empty(64);

    // Check if we're inside double quotes (affects backslash handling)
    bool in_dquote = lexer_in_mode(lx, LEX_DOUBLE_QUOTE);

    while (!lexer_at_end(lx))
    {
        char c = lexer_peek(lx);

        // Handle closing backtick
        if (c == '`')
        {
            // Found the closing backtick
            lexer_advance(lx); // consume `

            // Add command substitution part to current token
            part_t *part = part_create_command_subst(nested);

            // Store raw text for later parsing
            if (string_length(cmd_text) > 0)
            {
                part->text = cmd_text;
            }
            else
            {
                string_destroy(cmd_text);
            }

            // Mark as double-quoted if inside double quotes
            if (in_dquote)
            {
                part_set_quoted(part, false, true);
            }

            token_add_part(lx->current_token, part);
            lx->current_token->needs_expansion = true;

            // Field splitting only if not in double quotes
            if (!in_dquote)
            {
                lx->current_token->needs_field_splitting = true;
            }

            lexer_pop_mode(lx);
            return LEX_OK;
        }

        // Handle escape sequences
        // Per POSIX, within backticks, backslash only escapes: $ ` \ newline
        if (c == '\\')
        {
            char next_c = lexer_peek_ahead(lx, 1);
            if (next_c == '\0')
            {
                // Backslash at end of input - need more input
                string_destroy(cmd_text);
                token_list_destroy(nested);
                return LEX_INCOMPLETE;
            }

            if (is_backtick_escapable(next_c))
            {
                // Escape sequence: consume backslash
                lexer_advance(lx);

                if (next_c == '\n')
                {
                    // Line continuation - consume newline but don't add to text
                    lexer_advance(lx);
                }
                else
                {
                    // Add the escaped character (without backslash)
                    string_append_ascii_char(cmd_text, next_c);
                    lexer_advance(lx);
                }
            }
            else
            {
                // Backslash is literal - include both
                string_append_ascii_char(cmd_text, c);
                lexer_advance(lx);
            }
            continue;
        }

        // All other characters - add to command text
        string_append_ascii_char(cmd_text, c);
        lexer_advance(lx);
    }

    // End of input without closing backtick
    string_destroy(cmd_text);
    token_list_destroy(nested);
    return LEX_INCOMPLETE;
}

/**
 * @file lexer_arith_exp.c
 * @brief Lexer module for POSIX shell arithmetic expansion
 *
 * This module handles lexing of arithmetic expansion: $((...))
 *
 * Per POSIX specification, arithmetic expansion provides a mechanism
 * for evaluating an arithmetic expression and substituting its value.
 * The format is: $((expression))
 *
 * The expression is treated as if it were in double quotes, except that
 * a double quote inside the expression is not treated specially. The
 * shell expands all tokens in the expression for parameter expansion,
 * command substitution, and quote removal.
 *
 * Key characteristics:
 * - Nested parentheses are allowed for grouping: $(( (1+2)*3 ))
 * - The closing delimiter is )) (two consecutive close parentheses)
 * - Variable names can appear with or without $ prefix
 * - Command substitutions $(...) can be nested
 * - Parameter expansions ${...} can be nested
 */

#define LEXER_INTERNAL
#include "lexer_arith_exp.h"

#include "lexer.h"
#include "logging.h"
#include "token.h"

lex_status_t lexer_process_arith_exp(lexer_t *lx)
{
    Expects_not_null(lx);

    if (!lx->in_word)
        lexer_start_word(lx);

    string_clear(lx->operator_buffer);

    string_t *expr_text = string_create();

    while (!lexer_at_end(lx))
    {
        char c = lexer_peek(lx);

        // Handle closing delimiter: )) at depth 0
        if (c == ')' && string_length(lx->operator_buffer) == 0)
        {
            char c2 = lexer_peek_ahead(lx, 1);
            if (c2 == ')')
            {
                // Valid closing: ))
                lexer_advance(lx); // consume first )
                lexer_advance(lx); // consume second )

                part_t *part = part_create_arithmetic(expr_text);
                if (lexer_in_mode(lx, LEX_DOUBLE_QUOTE))
                    part_set_quoted(part, false, true);

                token_add_part(lx->current_token, part);
                lx->current_token->needs_expansion = true;
                if (!lexer_in_mode(lx, LEX_DOUBLE_QUOTE))
                    lx->current_token->needs_field_splitting = true;

                lexer_pop_mode(lx);
                string_destroy(&expr_text);
                string_clear(lx->operator_buffer);
                return LEX_OK;
            }
            else if (c2 == '\0')
            {
                // EOF after single ) — need more input
                string_destroy(&expr_text);
                string_clear(lx->operator_buffer);
                return LEX_INCOMPLETE;
            }
            else
            {
                // Single ) not followed by ) at depth 0 — syntax error
                string_destroy(&expr_text);
                lexer_set_error(lx, "Unbalanced parentheses in arithmetic expansion");
                string_clear(lx->operator_buffer);
                return LEX_ERROR;
            }
        }
        else if (c == '(' || c == '{')
        {
            // Track opening bracket for nested grouping / expansion
            string_append_char(lx->operator_buffer, c);
            string_append_char(expr_text, c);
            lexer_advance(lx);
        }
        else if (c == ')' || c == '}')
        {
            // Closing bracket — must match top of stack
            if (string_length(lx->operator_buffer) == 0)
            {
                // Unmatched closing bracket with empty stack — syntax error
                string_destroy(&expr_text);
                lexer_set_error(lx, "Unbalanced delimiter '%c' in arithmetic expansion", c);
                string_clear(lx->operator_buffer);
                return LEX_ERROR;
            }
            char open = string_pop_back(lx->operator_buffer);
            if ((open == '(' && c != ')') || (open == '{' && c != '}'))
            {
                string_destroy(&expr_text);
                lexer_set_error(lx, "Unbalanced delimiters in arithmetic expansion: '%c' vs '%c'",
                                open, c);
                string_clear(lx->operator_buffer);
                return LEX_ERROR;
            }
            string_append_char(expr_text, c);
            lexer_advance(lx);
        }
        else if (c == '\\')
        {
            // Backslash escaping inside arithmetic: $, `, \, and newline.
            char next = lexer_peek_ahead(lx, 1);
            if (next == '$' || next == '`' || next == '\\')
            {
                // Escape: discard backslash, keep the escaped character
                string_append_char(expr_text, next);
                lexer_advance(lx); // skip backslash
                lexer_advance(lx); // skip escaped char
            }
            else if (next == '\n')
            {
                // Line continuation: discard both \ and newline.
                // line_no/col_no update delegated to lexer_advance.
                lexer_advance(lx); // skip backslash
                lexer_advance(lx); // skip newline
            }
            else
            {
                // Non-escapable character: keep backslash literally.
                // Next character will be processed on the next iteration.
                string_append_char(expr_text, '\\');
                lexer_advance(lx);
            }
        }
        else if (c == '$')
        {
            char c2 = lexer_peek_ahead(lx, 1);
            if (c2 == '(' || c2 == '{')
            {
                // Start of nested $(...) or ${...}: copy both chars and push
                // the opener so its matching closer is not mistaken for the
                // arithmetic closing ))
                string_append_char(expr_text, c);
                string_append_char(expr_text, c2);
                string_append_char(lx->operator_buffer, c2);
                lexer_advance(lx); // skip $
                lexer_advance(lx); // skip ( or {
            }
            else
            {
                // Plain $ (e.g. $x, $1, $?) — copy literally
                string_append_char(expr_text, c);
                lexer_advance(lx);
            }
        }
        else if (c == '`')
        {
            // Backtick command substitution: copy literally without stack
            // tracking. Valid shell syntax cannot contain a bare ) inside a
            // backtick expression, so the closing )) cannot be misidentified.
            // Malformed input will be caught when the expression is re-lexed
            // at execution time.
            string_append_char(expr_text, c);
            lexer_advance(lx);
        }
        else
        {
            // Regular character
            string_append_char(expr_text, c);
            lexer_advance(lx);
        }
    }

    // End of input without finding closing ))
    string_destroy(&expr_text);
    string_clear(lx->operator_buffer);
    return LEX_INCOMPLETE;
}

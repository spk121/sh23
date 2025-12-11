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

#include "lexer_arith_exp.h"
#include "lexer.h"
#include "logging.h"
#include "token.h"

lex_status_t lexer_process_arith_exp(lexer_t *lx)
{
    Expects_not_null(lx);

    if (!lx->in_word)
        lexer_start_word(lx);

    int paren_depth = 0;
    string_t *expr_text = string_create();

    while (!lexer_at_end(lx))
    {
        char c = lexer_peek(lx);

        // Closing ))
        if (c == ')' && paren_depth == 0)
        {
            if (lexer_peek_ahead(lx, 1) == ')')
            {
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
                return LEX_OK;
            }
            else if (lx->pos + 1 < string_length(lx->input))
            {
                // Single ) at depth 0 not followed by ) and not at EOF is an error
                string_destroy(&expr_text);
                lexer_set_error(lx, "Unbalanced parentheses in arithmetic expansion");
                return LEX_ERROR;
            }
            // If we're at EOF (pos + 1 >= length), fall through and will return INCOMPLETE
        }

        // Count parentheses
        if (c == '(')
            paren_depth++;
        else if (c == ')')
            paren_depth--;

        // Backslash escaping: only $, `, \, and newline
        if (c == '\\')
        {
            char next = lexer_peek_ahead(lx, 1);
            if (next == '$' || next == '`' || next == '\\')
            {
                string_append_char(expr_text, next);
                lexer_advance(lx); // skip <backslash>
                lexer_advance(lx); // skip escaped char
                continue;
            }
            if (next == '\n')
            {
                lexer_advance(lx); // skip '\'
                lexer_advance(lx); // skip \n
                lx->line_no++;
                lx->col_no = 1;
                continue;
            }
            // Otherwise: keep both \ and next char
            string_append_char(expr_text, '\\');
            lexer_advance(lx);
            continue;
        }

        // Nested expansions — copy raw text, do not re-lex
        if (c == '$')
        {
            char c2 = lexer_peek_ahead(lx, 1);
            if (c2 == '(' || c2 == '{')
            {
                // Let nested lexer handle it — just copy the text
                // This is safe because nested modes will append to the same word
                string_append_char(expr_text, '$');
                lexer_advance(lx);
                continue;
            }
        }

        if (c == '`')
        {
            string_append_char(expr_text, '`');
            lexer_advance(lx);
            continue;
        }

        string_append_char(expr_text, c);
        lexer_advance(lx);
    }

    string_destroy(&expr_text);
    return LEX_INCOMPLETE;
}

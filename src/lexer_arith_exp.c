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

    // We enter after $(( has been consumed.
    // Note: On LEX_INCOMPLETE, the mode stack is left unchanged so the caller
    // can provide more input and retry. The mode is only popped on LEX_OK.
    // Ensure we have a word token to build
    if (!lx->in_word)
    {
        lexer_start_word(lx);
    }

    // Track parenthesis nesting depth
    // We start at 0 because we're looking for )) at depth 0
    int paren_depth = 0;

    // Create a token list to hold the nested expression tokens
    token_list_t *nested = token_list_create();

    // Buffer to accumulate raw expression text
    string_t *expr_text = string_create_empty(64);

    while (!lexer_at_end(lx))
    {
        char c = lexer_peek(lx);

        // Handle closing parenthesis
        if (c == ')')
        {
            if (paren_depth == 0)
            {
                // Check if this is )) - the closing of arithmetic expansion
                char c2 = lexer_peek_ahead(lx, 1);
                if (c2 == ')')
                {
                    // Found the closing ))
                    lexer_advance(lx); // consume first )
                    lexer_advance(lx); // consume second )

                    // Add arithmetic expansion part to current token
                    part_t *part = part_create_arithmetic(nested);

                    // Store raw text for later parsing
                    // Note: Ownership of expr_text is transferred to the part
                    if (string_length(expr_text) > 0)
                    {
                        part->text = expr_text; // part takes ownership
                    }
                    else
                    {
                        string_destroy(expr_text);
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
                else if (c2 == '\0')
                {
                    // End of input after single ) at depth 0 - need more input
                    // Consume the ) and let the while loop exit normally
                    string_append_ascii_char(expr_text, c);
                    lexer_advance(lx);
                    // Fall through to exit the while loop at end of input
                }
                else
                {
                    // Single ) at depth 0 followed by non-) character - unbalanced parens
                    // This is a syntax error: more closing parens than opening parens
                    string_destroy(expr_text);
                    token_list_destroy(nested);
                    lexer_set_error(lx, "Unbalanced parentheses in arithmetic expansion");
                    return LEX_ERROR;
                }
            }
            else
            {
                // Single ) decreases nesting depth (only when paren_depth > 0)
                paren_depth--;
                string_append_ascii_char(expr_text, c);
                lexer_advance(lx);
            }
            continue;
        }

        // Handle opening parenthesis (nesting)
        if (c == '(')
        {
            paren_depth++;
            string_append_ascii_char(expr_text, c);
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
                string_destroy(expr_text);
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
                string_append_ascii_char(expr_text, c);
                lexer_advance(lx);
                string_append_ascii_char(expr_text, next_c);
                lexer_advance(lx);
            }
            continue;
        }

        // Handle single quotes - everything literal until closing quote
        if (c == '\'')
        {
            string_append_ascii_char(expr_text, c);
            lexer_advance(lx);
            while (!lexer_at_end(lx) && lexer_peek(lx) != '\'')
            {
                string_append_ascii_char(expr_text, lexer_peek(lx));
                lexer_advance(lx);
            }
            if (lexer_at_end(lx))
            {
                string_destroy(expr_text);
                token_list_destroy(nested);
                return LEX_INCOMPLETE;
            }
            string_append_ascii_char(expr_text, lexer_peek(lx)); // closing quote
            lexer_advance(lx);
            continue;
        }

        // Handle double quotes - track contents
        if (c == '"')
        {
            string_append_ascii_char(expr_text, c);
            lexer_advance(lx);
            while (!lexer_at_end(lx))
            {
                char inner = lexer_peek(lx);
                if (inner == '"')
                {
                    string_append_ascii_char(expr_text, inner);
                    lexer_advance(lx);
                    break;
                }
                if (inner == '\\')
                {
                    char next = lexer_peek_ahead(lx, 1);
                    if (next == '\0')
                    {
                        string_destroy(expr_text);
                        token_list_destroy(nested);
                        return LEX_INCOMPLETE;
                    }
                    string_append_ascii_char(expr_text, inner);
                    lexer_advance(lx);
                    string_append_ascii_char(expr_text, next);
                    lexer_advance(lx);
                    continue;
                }
                string_append_ascii_char(expr_text, inner);
                lexer_advance(lx);
            }
            if (lexer_at_end(lx))
            {
                string_destroy(expr_text);
                token_list_destroy(nested);
                return LEX_INCOMPLETE;
            }
            continue;
        }

        // Handle nested command substitution $(...)
        // Note: We track parentheses for both nested arithmetic $((
        // and nested command substitution $(. For arithmetic expansion,
        // we increment by 2 because $(( adds two parentheses that will
        // each need a matching ) before we can close the outer expansion.
        if (c == '$' && lexer_peek_ahead(lx, 1) == '(')
        {
            char c3 = lexer_peek_ahead(lx, 2);
            if (c3 == '(')
            {
                // Nested arithmetic expansion $((
                // Increment by 2 because we're adding two open parens
                string_append_ascii_char(expr_text, c);
                lexer_advance(lx);
                string_append_ascii_char(expr_text, '(');
                lexer_advance(lx);
                string_append_ascii_char(expr_text, '(');
                lexer_advance(lx);
                paren_depth += 2;
            }
            else
            {
                // Nested command substitution $(
                string_append_ascii_char(expr_text, c);
                lexer_advance(lx);
                string_append_ascii_char(expr_text, '(');
                lexer_advance(lx);
                paren_depth++;
            }
            continue;
        }

        // Handle nested parameter expansion ${...}
        if (c == '$' && lexer_peek_ahead(lx, 1) == '{')
        {
            string_append_ascii_char(expr_text, c);
            lexer_advance(lx);
            string_append_ascii_char(expr_text, '{');
            lexer_advance(lx);
            // Read until matching }
            int brace_depth = 1;
            while (!lexer_at_end(lx) && brace_depth > 0)
            {
                char inner = lexer_peek(lx);
                if (inner == '{')
                {
                    brace_depth++;
                }
                else if (inner == '}')
                {
                    brace_depth--;
                }
                else if (inner == '\\')
                {
                    char next = lexer_peek_ahead(lx, 1);
                    if (next != '\0')
                    {
                        string_append_ascii_char(expr_text, inner);
                        lexer_advance(lx);
                        string_append_ascii_char(expr_text, next);
                        lexer_advance(lx);
                        continue;
                    }
                }
                string_append_ascii_char(expr_text, inner);
                lexer_advance(lx);
            }
            if (lexer_at_end(lx) && brace_depth > 0)
            {
                string_destroy(expr_text);
                token_list_destroy(nested);
                return LEX_INCOMPLETE;
            }
            continue;
        }

        // Handle backtick command substitution inside arithmetic
        if (c == '`')
        {
            string_append_ascii_char(expr_text, c);
            lexer_advance(lx);
            // Read until matching backtick
            while (!lexer_at_end(lx) && lexer_peek(lx) != '`')
            {
                char inner = lexer_peek(lx);
                if (inner == '\\')
                {
                    char next = lexer_peek_ahead(lx, 1);
                    if (next != '\0')
                    {
                        string_append_ascii_char(expr_text, inner);
                        lexer_advance(lx);
                        string_append_ascii_char(expr_text, next);
                        lexer_advance(lx);
                        continue;
                    }
                }
                string_append_ascii_char(expr_text, inner);
                lexer_advance(lx);
            }
            if (lexer_at_end(lx))
            {
                string_destroy(expr_text);
                token_list_destroy(nested);
                return LEX_INCOMPLETE;
            }
            string_append_ascii_char(expr_text, lexer_peek(lx)); // closing backtick
            lexer_advance(lx);
            continue;
        }

        // All other characters - add to expression text
        string_append_ascii_char(expr_text, c);
        lexer_advance(lx);
    }

    // End of input without closing ))
    string_destroy(expr_text);
    token_list_destroy(nested);
    return LEX_INCOMPLETE;
}

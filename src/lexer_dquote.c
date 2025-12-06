/**
 * @file lexer_dquote.c
 * @brief POSIX shell double-quote lexer implementation
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
 * Key differences from normal lexing context:
 * - Field splitting is NOT performed on expansion results
 * - Pathname expansion (globbing) is NOT performed
 * - Single quotes have no special meaning (treated literally)
 * - Most backslash sequences are preserved literally
 *
 * Key differences from single-quote context:
 * - Single quotes treat everything literally (no expansions)
 * - Double quotes allow parameter, command, and arithmetic expansion
 * - Double quotes process specific backslash escapes
 */

#include "lexer_dquote.h"
#include "logging.h"
#include "xalloc.h"
#include <ctype.h>
#include <stdlib.h>

/* ============================================================================
 * Initialization
 * ============================================================================ */

void lexer_dquote_init(lexer_dquote_t *lexer, const string_t *input, int start_pos, int line, int column)
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
static char peek_char(const lexer_dquote_t *lexer)
{
    if (lexer->pos >= string_length(lexer->input))
    {
        return '\0';
    }
    return string_char_at(lexer->input, lexer->pos);
}

/**
 * Peek at the character at offset from current position.
 * Returns '\0' if position is beyond end of input.
 */
static char peek_char_at(const lexer_dquote_t *lexer, int offset)
{
    int pos = lexer->pos + offset;
    if (pos < 0 || pos >= string_length(lexer->input))
    {
        return '\0';
    }
    return string_char_at(lexer->input, pos);
}

/**
 * Advance position by one character, updating line/column tracking.
 */
static void advance_char(lexer_dquote_t *lexer)
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

/**
 * Check if a character is valid as the first character of a parameter name.
 * Per POSIX: Name must start with underscore or letter.
 */
static bool is_param_start_char(char c)
{
    return isalpha((unsigned char)c) || c == '_';
}

/**
 * Check if a character is valid in a parameter name (not first position).
 * Per POSIX: Name can contain underscore, letters, or digits.
 */
static bool is_param_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

/**
 * Check if a character is a special parameter.
 * Per POSIX: @, *, #, ?, -, $, !, 0-9
 */
static bool is_special_param_char(char c)
{
    return c == '@' || c == '*' || c == '#' || c == '?' || c == '-' || c == '$' || c == '!' || isdigit((unsigned char)c);
}

/**
 * Flush accumulated literal content to parts list.
 */
static void flush_literal(part_list_t *parts, string_t *content)
{
    if (string_length(content) > 0)
    {
        part_t *lit_part = part_create_literal(content);
        if (lit_part != NULL)
        {
            part_set_quoted(lit_part, false, true); /* double_quoted=true */
            part_list_append(parts, lit_part);
        }
        string_clear(content);
    }
}

/**
 * Lex a simple parameter name ($foo, $1, $@, etc.)
 * Caller has already consumed the '$'.
 * Returns the parameter name (without '$').
 */
static string_t *lex_simple_parameter(lexer_dquote_t *lexer)
{
    string_t *name = string_create_empty(16);
    char c = peek_char(lexer);

    if (is_special_param_char(c))
    {
        /* Single special character parameter: $@, $*, $#, $?, $-, $$, $!, $0-$9 */
        string_append_ascii_char(name, c);
        advance_char(lexer);
    }
    else if (is_param_start_char(c))
    {
        /* Regular parameter name */
        while (is_param_char(peek_char(lexer)))
        {
            string_append_ascii_char(name, peek_char(lexer));
            advance_char(lexer);
        }
    }
    /* If neither, name will be empty (bare $ at end or $ followed by non-param char) */

    return name;
}

/**
 * Lex a braced parameter expansion ${...}.
 * Caller has already consumed the '${'.
 * This is a simplified version that just captures the name.
 * Full parameter expansion operators (${var:-word}, etc.) would need
 * recursive lexing.
 */
static lexer_dquote_result_t lex_braced_parameter(lexer_dquote_t *lexer, string_t **name)
{
    *name = string_create_empty(16);
    int brace_depth = 1;

    /* Handle special prefix characters like # for length */
    char c = peek_char(lexer);
    if (c == '#')
    {
        /* Could be ${#var} (length) or ${#} (number of positional params) */
        string_append_ascii_char(*name, c);
        advance_char(lexer);
    }
    else if (c == '!')
    {
        /* Indirect expansion ${!var} */
        string_append_ascii_char(*name, c);
        advance_char(lexer);
    }

    /* Read until matching close brace */
    while (brace_depth > 0)
    {
        c = peek_char(lexer);

        if (c == '\0')
        {
            string_destroy(*name);
            *name = NULL;
            return LEXER_DQUOTE_UNTERMINATED_EXPANSION;
        }

        if (c == '{')
        {
            brace_depth++;
            string_append_ascii_char(*name, c);
            advance_char(lexer);
        }
        else if (c == '}')
        {
            brace_depth--;
            if (brace_depth > 0)
            {
                string_append_ascii_char(*name, c);
            }
            advance_char(lexer);
        }
        else if (c == '\\')
        {
            /* Backslash in braces - include both characters */
            string_append_ascii_char(*name, c);
            advance_char(lexer);
            char next = peek_char(lexer);
            if (next != '\0')
            {
                string_append_ascii_char(*name, next);
                advance_char(lexer);
            }
        }
        else
        {
            string_append_ascii_char(*name, c);
            advance_char(lexer);
        }
    }

    return LEXER_DQUOTE_OK;
}

/**
 * Lex a command substitution $(...).
 * Caller has already consumed the '$('.
 * Returns nested content (for later parsing by main lexer).
 */
static lexer_dquote_result_t lex_command_subst(lexer_dquote_t *lexer, string_t **content)
{
    *content = string_create_empty(64);
    int paren_depth = 1;

    while (paren_depth > 0)
    {
        char c = peek_char(lexer);

        if (c == '\0')
        {
            string_destroy(*content);
            *content = NULL;
            return LEXER_DQUOTE_UNTERMINATED_EXPANSION;
        }

        if (c == '(')
        {
            paren_depth++;
            string_append_ascii_char(*content, c);
            advance_char(lexer);
        }
        else if (c == ')')
        {
            paren_depth--;
            if (paren_depth > 0)
            {
                string_append_ascii_char(*content, c);
            }
            advance_char(lexer);
        }
        else if (c == '\\')
        {
            /* Include backslash and following character */
            string_append_ascii_char(*content, c);
            advance_char(lexer);
            char next = peek_char(lexer);
            if (next != '\0')
            {
                string_append_ascii_char(*content, next);
                advance_char(lexer);
            }
        }
        else if (c == '\'')
        {
            /* Single quotes inside command substitution */
            string_append_ascii_char(*content, c);
            advance_char(lexer);
            while (peek_char(lexer) != '\0' && peek_char(lexer) != '\'')
            {
                string_append_ascii_char(*content, peek_char(lexer));
                advance_char(lexer);
            }
            if (peek_char(lexer) == '\'')
            {
                string_append_ascii_char(*content, '\'');
                advance_char(lexer);
            }
        }
        else if (c == '"')
        {
            /* Double quotes inside command substitution - need to handle nested quotes */
            string_append_ascii_char(*content, c);
            advance_char(lexer);
            bool in_escape = false;
            while (peek_char(lexer) != '\0')
            {
                char qc = peek_char(lexer);
                if (in_escape)
                {
                    string_append_ascii_char(*content, qc);
                    advance_char(lexer);
                    in_escape = false;
                }
                else if (qc == '\\')
                {
                    string_append_ascii_char(*content, qc);
                    advance_char(lexer);
                    in_escape = true;
                }
                else if (qc == '"')
                {
                    string_append_ascii_char(*content, qc);
                    advance_char(lexer);
                    break;
                }
                else
                {
                    string_append_ascii_char(*content, qc);
                    advance_char(lexer);
                }
            }
        }
        else
        {
            string_append_ascii_char(*content, c);
            advance_char(lexer);
        }
    }

    return LEXER_DQUOTE_OK;
}

/**
 * Lex a backtick command substitution `...`.
 * Caller has already consumed the opening backtick.
 */
static lexer_dquote_result_t lex_backtick_subst(lexer_dquote_t *lexer, string_t **content)
{
    *content = string_create_empty(64);

    while (1)
    {
        char c = peek_char(lexer);

        if (c == '\0')
        {
            string_destroy(*content);
            *content = NULL;
            return LEXER_DQUOTE_UNTERMINATED_EXPANSION;
        }

        if (c == '`')
        {
            advance_char(lexer); /* consume closing backtick */
            break;
        }

        if (c == '\\')
        {
            /* In backticks, backslash only escapes $, `, \, and newline */
            char next = peek_char_at(lexer, 1);
            if (next == '$' || next == '`' || next == '\\' || next == '\n')
            {
                advance_char(lexer); /* skip backslash */
                if (next == '\n')
                {
                    /* Line continuation - skip both */
                    advance_char(lexer);
                }
                else
                {
                    string_append_ascii_char(*content, next);
                    advance_char(lexer);
                }
            }
            else
            {
                /* Backslash is literal */
                string_append_ascii_char(*content, c);
                advance_char(lexer);
            }
        }
        else
        {
            string_append_ascii_char(*content, c);
            advance_char(lexer);
        }
    }

    return LEXER_DQUOTE_OK;
}

/**
 * Lex arithmetic expansion $((...)).
 * Caller has already consumed '$((' (verified second paren).
 */
static lexer_dquote_result_t lex_arithmetic_expansion(lexer_dquote_t *lexer, string_t **content)
{
    *content = string_create_empty(32);
    int paren_depth = 2; /* We're inside $(( */

    while (paren_depth > 0)
    {
        char c = peek_char(lexer);

        if (c == '\0')
        {
            string_destroy(*content);
            *content = NULL;
            return LEXER_DQUOTE_UNTERMINATED_EXPANSION;
        }

        if (c == '(')
        {
            paren_depth++;
            string_append_ascii_char(*content, c);
            advance_char(lexer);
        }
        else if (c == ')')
        {
            paren_depth--;
            if (paren_depth >= 1)
            {
                /* Still inside, or this is the first ) of )) */
                if (paren_depth == 1 && peek_char_at(lexer, 1) == ')')
                {
                    /* Found )) - consume both and exit */
                    advance_char(lexer);
                    advance_char(lexer);
                    paren_depth = 0;
                }
                else if (paren_depth >= 1)
                {
                    string_append_ascii_char(*content, c);
                    advance_char(lexer);
                }
            }
            else
            {
                advance_char(lexer);
            }
        }
        else
        {
            string_append_ascii_char(*content, c);
            advance_char(lexer);
        }
    }

    return LEXER_DQUOTE_OK;
}

/* ============================================================================
 * Main Lexing Function
 * ============================================================================ */

lexer_dquote_result_t lexer_dquote_lex(lexer_dquote_t *lexer, part_list_t **parts)
{
    Expects_not_null(lexer);
    Expects_not_null(parts);

    *parts = part_list_create();
    string_t *literal_accumulator = string_create_empty(64);

    while (1)
    {
        char c = peek_char(lexer);

        if (c == '\0')
        {
            /* End of input before closing quote */
            string_destroy(literal_accumulator);
            part_list_destroy(*parts);
            *parts = NULL;
            return LEXER_DQUOTE_UNTERMINATED;
        }

        if (c == '"')
        {
            /* Closing double quote found */
            flush_literal(*parts, literal_accumulator);
            advance_char(lexer); /* consume the closing quote */
            break;
        }

        if (c == '\\')
        {
            /* Backslash escape handling */
            char next = peek_char_at(lexer, 1);

            /* In double quotes, backslash only escapes: $, `, ", \, newline */
            if (next == '$' || next == '`' || next == '"' || next == '\\')
            {
                advance_char(lexer); /* skip backslash */
                string_append_ascii_char(literal_accumulator, next);
                advance_char(lexer); /* consume escaped char */
            }
            else if (next == '\n')
            {
                /* Line continuation - skip both backslash and newline */
                advance_char(lexer);
                advance_char(lexer);
            }
            else
            {
                /* Backslash followed by other char - both are literal */
                string_append_ascii_char(literal_accumulator, c);
                advance_char(lexer);
            }
        }
        else if (c == '$')
        {
            /* Dollar sign - check for expansion */
            char next = peek_char_at(lexer, 1);

            if (next == '(')
            {
                /* Could be $(...) or $((...)) */
                flush_literal(*parts, literal_accumulator);
                advance_char(lexer); /* consume $ */
                advance_char(lexer); /* consume ( */

                if (peek_char(lexer) == '(')
                {
                    /* Arithmetic expansion $((...)) */
                    advance_char(lexer); /* consume second ( */
                    string_t *arith_content = NULL;
                    lexer_dquote_result_t result = lex_arithmetic_expansion(lexer, &arith_content);
                    if (result != LEXER_DQUOTE_OK)
                    {
                        string_destroy(literal_accumulator);
                        part_list_destroy(*parts);
                        *parts = NULL;
                        return result;
                    }
                    /* Create arithmetic expansion part.
                     * Note: The nested token_list is empty because recursive
                     * lexing of the arithmetic expression requires the main
                     * lexer (lexer_normal.c) to be implemented. The arith_content
                     * contains the raw text that would be recursively lexed.
                     * When lexer_normal.c is created, it should call back into
                     * the appropriate lexer context to parse the expression. */
                    part_t *arith_part = part_create_arithmetic(token_list_create());
                    if (arith_part != NULL)
                    {
                        part_set_quoted(arith_part, false, true);
                        part_list_append(*parts, arith_part);
                    }
                    string_destroy(arith_content);
                }
                else
                {
                    /* Command substitution $(...) */
                    string_t *cmd_content = NULL;
                    lexer_dquote_result_t result = lex_command_subst(lexer, &cmd_content);
                    if (result != LEXER_DQUOTE_OK)
                    {
                        string_destroy(literal_accumulator);
                        part_list_destroy(*parts);
                        *parts = NULL;
                        return result;
                    }
                    /* Create command substitution part.
                     * Note: The nested token_list is empty because recursive
                     * lexing of the command requires the main lexer (lexer_normal.c)
                     * to be implemented. The cmd_content contains the raw text
                     * that would be recursively lexed by the main lexer. */
                    part_t *cmd_part = part_create_command_subst(token_list_create());
                    if (cmd_part != NULL)
                    {
                        part_set_quoted(cmd_part, false, true);
                        part_list_append(*parts, cmd_part);
                    }
                    string_destroy(cmd_content);
                }
            }
            else if (next == '{')
            {
                /* Braced parameter expansion ${...} */
                flush_literal(*parts, literal_accumulator);
                advance_char(lexer); /* consume $ */
                advance_char(lexer); /* consume { */

                string_t *param_name = NULL;
                lexer_dquote_result_t result = lex_braced_parameter(lexer, &param_name);
                if (result != LEXER_DQUOTE_OK)
                {
                    string_destroy(literal_accumulator);
                    part_list_destroy(*parts);
                    *parts = NULL;
                    return result;
                }

                part_t *param_part = part_create_parameter(param_name);
                if (param_part != NULL)
                {
                    part_set_quoted(param_part, false, true);
                    part_list_append(*parts, param_part);
                }
                string_destroy(param_name);
            }
            else if (is_special_param_char(next) || is_param_start_char(next))
            {
                /* Simple parameter $var, $1, $@, etc. */
                flush_literal(*parts, literal_accumulator);
                advance_char(lexer); /* consume $ */

                string_t *param_name = lex_simple_parameter(lexer);
                if (string_length(param_name) > 0)
                {
                    part_t *param_part = part_create_parameter(param_name);
                    if (param_part != NULL)
                    {
                        part_set_quoted(param_part, false, true);
                        part_list_append(*parts, param_part);
                    }
                }
                string_destroy(param_name);
            }
            else
            {
                /* Bare $ followed by non-expansion char - treat as literal */
                string_append_ascii_char(literal_accumulator, c);
                advance_char(lexer);
            }
        }
        else if (c == '`')
        {
            /* Backtick command substitution */
            flush_literal(*parts, literal_accumulator);
            advance_char(lexer); /* consume opening backtick */

            string_t *cmd_content = NULL;
            lexer_dquote_result_t result = lex_backtick_subst(lexer, &cmd_content);
            if (result != LEXER_DQUOTE_OK)
            {
                string_destroy(literal_accumulator);
                part_list_destroy(*parts);
                *parts = NULL;
                return result;
            }

            /* Create command substitution part (backtick form).
             * Note: The nested token_list is empty because recursive
             * lexing of the command requires the main lexer (lexer_normal.c)
             * to be implemented. The cmd_content contains the raw text
             * that would be recursively lexed by the main lexer. */
            part_t *cmd_part = part_create_command_subst(token_list_create());
            if (cmd_part != NULL)
            {
                part_set_quoted(cmd_part, false, true);
                part_list_append(*parts, cmd_part);
            }
            string_destroy(cmd_content);
        }
        else
        {
            /* Regular character - add to literal accumulator */
            string_append_ascii_char(literal_accumulator, c);
            advance_char(lexer);
        }
    }

    string_destroy(literal_accumulator);
    return LEXER_DQUOTE_OK;
}

/* ============================================================================
 * Accessors
 * ============================================================================ */

int lexer_dquote_get_pos(const lexer_dquote_t *lexer)
{
    Expects_not_null(lexer);
    return lexer->pos;
}

int lexer_dquote_get_line(const lexer_dquote_t *lexer)
{
    Expects_not_null(lexer);
    return lexer->line;
}

int lexer_dquote_get_column(const lexer_dquote_t *lexer)
{
    Expects_not_null(lexer);
    return lexer->column;
}

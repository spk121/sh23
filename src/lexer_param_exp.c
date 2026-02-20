/**
 * @file lexer_param_exp.c
 * @brief Lexer module for POSIX shell parameter expansions
 *
 * This module handles lexing of parameter expansions:
 * - Braced expansions: ${var}, ${var:-word}, ${#var}, etc.
 * - Unbraced expansions: $var, $1, $?, etc.
 *
 * Per POSIX specification, parameter expansion provides a way to
 * substitute the value of a parameter. The simplest form is $parameter.
 * Braces are required when parameter is followed by a character that
 * could be interpreted as part of the name.
 */

#include <ctype.h>

#define LEXER_INTERNAL
#include "lexer_param_exp.h"

#include "lexer_t.h"
#include "token.h"

/**
 * Check if a character is a special parameter character.
 * Special parameters are: @, *, #, ?, -, $, !, 0-9
 */
static bool is_special_param_char(char c)
{
    return (c == '@' || c == '*' || c == '#' || c == '?' || c == '-' || c == '$' || c == '!' ||
            isdigit(c));
}

/**
 * Check if a character can start a parameter name.
 * Per POSIX 3.216, a name begins with an underscore or alphabetic character.
 */
static bool is_name_start_char(char c)
{
    return (isalpha(c) || c == '_');
}

/**
 * Check if a character can continue a parameter name.
 * Per POSIX 3.216, a name contains underscores, digits, and alphabetics.
 */
static bool is_name_char(char c)
{
    return (isalnum(c) || c == '_');
}

/**
 * Create a PART_PARAMETER and add it to the current token.
 */
static void lexer_add_param_part(lexer_t *lx, const char *name, int name_len, param_subtype_t kind,
                                 bool has_colon, const char *word, int word_len)
{
    Expects_not_null(lx);
    Expects_not_null(lx->current_token);
    Expects_not_null(name);
    Expects_gt(name_len, 0);

    string_t *param_name = string_create_from_cstr_len(name, name_len);

    part_t *part = part_create_parameter(param_name);
    part->param_kind = kind;
    part->has_colon = has_colon;

    if (word != NULL && word_len > 0)
        part->word = string_create_from_cstr_len(word, word_len);

    if (lexer_in_mode(lx, LEX_DOUBLE_QUOTE))
        part_set_quoted(part, false, true);

    token_add_part(lx->current_token, part);
    lx->current_token->needs_expansion = true;

    string_destroy(&param_name);
}

lex_status_t lexer_process_param_exp_unbraced(lexer_t *lx)
{
    Expects_not_null(lx);

    if (!lx->in_word)
        lexer_start_word(lx);

    if (lexer_at_end(lx))
        return LEX_INCOMPLETE;

    char c = lexer_peek(lx);

    if (is_special_param_char(c))
    {
        char name[2] = {c, '\0'};
        lexer_advance(lx);
        lexer_add_param_part(lx, name, 1, PARAM_PLAIN, false, NULL, 0);
        lexer_pop_mode(lx);
        return LEX_OK;
    }

    if (is_name_start_char(c))
    {
        const char *input_data = string_data(lx->input);
        int start = lx->pos;
        int len = 0;

        while (!lexer_at_end(lx) && is_name_char(lexer_peek(lx)))
        {
            lexer_advance(lx);
            len++;
        }

        lexer_add_param_part(lx, input_data + start, len, PARAM_PLAIN, false, NULL, 0);
        lexer_pop_mode(lx);
        return LEX_OK;
    }

    // $ not followed by a valid parameter — caller should have checked
    lexer_set_error(lx, "Invalid parameter expansion");
    return LEX_ERROR;
}

lex_status_t lexer_process_param_exp_braced(lexer_t *lx)
{
    Expects_not_null(lx);

    if (!lx->in_word)
        lexer_start_word(lx);

    const char *input = string_data(lx->input);

    if (lexer_at_end(lx))
        return LEX_INCOMPLETE;

    char c = lexer_peek(lx);

    // Check for ${#...} - length expansion.
    // Must be checked before reading the parameter name.
    bool is_length = false;
    if (c == '#')
    {
        // Peek ahead: if the next char is } or another operator it's ${#}
        // which is an error, but if it's a name char or a digit it's ${#name}.
        char c2 = lexer_peek_ahead(lx, 1);
        if (is_name_start_char(c2) || isdigit(c2) || c2 == '@' || c2 == '*')
        {
            is_length = true;
            lexer_advance(lx); // consume #
            if (lexer_at_end(lx))
                return LEX_INCOMPLETE;
            c = lexer_peek(lx);
        }
        // Otherwise # is the parameter name itself (e.g. ${#} means $#)
    }

    int name_start = lx->pos;
    int name_len = 0;

    if (is_special_param_char(c))
    {
        // For length expansion, only @, *, and positional digits are valid.
        // ${#?}, ${#-}, ${#$}, ${#!} are not POSIX.
        if (is_length && c != '@' && c != '*' && !isdigit(c))
        {
            lexer_set_error(lx, "bad substitution: ${#%c} is not a valid length expansion", c);
            return LEX_ERROR;
        }
        lexer_advance(lx);
        name_len = 1;
    }
    else if (is_name_start_char(c))
    {
        while (!lexer_at_end(lx) && is_name_char(lexer_peek(lx)))
        {
            lexer_advance(lx);
            name_len++;
        }
    }
    else if (c == '}')
    {
        lexer_set_error(lx, "bad substitution: %s",
                        is_length ? "${#} requires a parameter name"
                                  : "empty parameter name in ${}");
        return LEX_ERROR;
    }
    else
    {
        lexer_set_error(lx, "bad substitution: invalid character '%c' in parameter name", c);
        return LEX_ERROR;
    }

    if (lexer_at_end(lx))
        return LEX_INCOMPLETE;

    // Length expansion: expect closing } immediately after the name
    if (is_length)
    {
        if (lexer_peek(lx) != '}')
        {
            lexer_set_error(lx, "bad substitution: expected '}' after ${#name}");
            return LEX_ERROR;
        }
        string_t *name = string_create_from_cstr_len(input + name_start, name_len);
        part_t *part = part_create_parameter(name);
        part->param_kind = PARAM_LENGTH;
        part->has_colon = false;
        part_set_quoted(part, false, lexer_in_mode(lx, LEX_DOUBLE_QUOTE));
        token_add_part(lx->current_token, part);
        lx->current_token->needs_expansion = true;
        string_destroy(&name);
        lexer_advance(lx); // consume }
        lexer_pop_mode(lx);
        return LEX_OK;
    }

    c = lexer_peek(lx);

    // Simple expansion: ${name}
    if (c == '}')
    {
        lexer_advance(lx); // consume }
        lexer_add_param_part(lx, input + name_start, name_len, PARAM_PLAIN, false, NULL, 0);
        lexer_pop_mode(lx);
        return LEX_OK;
    }

    // Operator expansions: ${name:-word}, ${name#pattern}, etc.
    bool has_colon = false;
    if (c == ':')
    {
        has_colon = true;
        lexer_advance(lx); // consume :
        if (lexer_at_end(lx))
            return LEX_INCOMPLETE;
        c = lexer_peek(lx);
    }

    param_subtype_t kind = PARAM_PLAIN;
    switch (c)
    {
    case '-':
        kind = PARAM_USE_DEFAULT;
        break;
    case '=':
        kind = PARAM_ASSIGN_DEFAULT;
        break;
    case '?':
        kind = PARAM_ERROR_IF_UNSET;
        break;
    case '+':
        kind = PARAM_USE_ALTERNATE;
        break;
    case '%':
        if (has_colon)
        {
            lexer_set_error(lx, "bad substitution: invalid operator :%");
            return LEX_ERROR;
        }
        lexer_advance(lx); // consume first %
        if (!lexer_at_end(lx) && lexer_peek(lx) == '%')
        {
            lexer_advance(lx); // consume second %
            kind = PARAM_REMOVE_LARGE_SUFFIX;
        }
        else
            kind = PARAM_REMOVE_SMALL_SUFFIX;
        break;
    case '#':
        if (has_colon)
        {
            lexer_set_error(lx, "bad substitution: invalid operator :#");
            return LEX_ERROR;
        }
        lexer_advance(lx); // consume first #
        if (!lexer_at_end(lx) && lexer_peek(lx) == '#')
        {
            lexer_advance(lx); // consume second #
            kind = PARAM_REMOVE_LARGE_PREFIX;
        }
        else
            kind = PARAM_REMOVE_SMALL_PREFIX;
        break;
    default:
        lexer_set_error(lx,
                        has_colon ? "bad substitution: invalid operator after ':'"
                                  : "bad substitution: unexpected character '%c'",
                        c);
        return LEX_ERROR;
    }

    // For the non-pattern operators (-, =, ?, +), consume the operator char.
    // The pattern operators (%, #) already consumed their chars in the switch above.
    if (kind == PARAM_USE_DEFAULT || kind == PARAM_ASSIGN_DEFAULT || kind == PARAM_ERROR_IF_UNSET ||
        kind == PARAM_USE_ALTERNATE)
    {
        lexer_advance(lx); // consume -, =, ?, or +
    }

    if (lexer_at_end(lx))
        return LEX_INCOMPLETE;

    // Read the word/pattern until the closing }, tracking nested expansions
    // so that a } inside ${var:-${other}} doesn't close the outer expansion.
    int word_start = lx->pos;
    int word_len = 0;
    int brace_depth = 0;

    while (!lexer_at_end(lx))
    {
        c = lexer_peek(lx);

        if (c == '}')
        {
            if (brace_depth == 0)
            {
                // This is the closing } of the outer expansion
                lexer_advance(lx); // consume }
                lexer_add_param_part(lx, input + name_start, name_len, kind, has_colon,
                                     word_len > 0 ? input + word_start : NULL, word_len);
                lexer_pop_mode(lx);
                return LEX_OK;
            }
            brace_depth--;
            lexer_advance(lx);
            word_len++;
        }
        else if (c == '$')
        {
            char c2 = lexer_peek_ahead(lx, 1);
            if (c2 == '{' || c2 == '(')
            {
                // Nested expansion: track the opening brace/paren so its
                // closing character doesn't prematurely end the word scan.
                brace_depth++;
                lexer_advance(lx); // consume $
                lexer_advance(lx); // consume { or (
                word_len += 2;
            }
            else
            {
                lexer_advance(lx);
                word_len++;
            }
        }
        else if (c == '(')
        {
            // Naked ( for subshell grouping inside pattern
            brace_depth++;
            lexer_advance(lx);
            word_len++;
        }
        else if (c == ')')
        {
            if (brace_depth > 0)
                brace_depth--;
            lexer_advance(lx);
            word_len++;
        }
        else if (c == '\\')
        {
            // Consume backslash and the following character together
            lexer_advance(lx);
            word_len++;
            if (!lexer_at_end(lx))
            {
                lexer_advance(lx);
                word_len++;
            }
        }
        else if (c == '\'')
        {
            // Single-quoted section: scan for closing ' without interpreting
            // anything, including }
            lexer_advance(lx); // consume opening '
            word_len++;
            while (!lexer_at_end(lx) && lexer_peek(lx) != '\'')
            {
                lexer_advance(lx);
                word_len++;
            }
            if (!lexer_at_end(lx))
            {
                lexer_advance(lx); // consume closing '
                word_len++;
            }
        }
        else if (c == '"')
        {
            // Double-quoted section: scan for closing " treating only \" as
            // an escape so that a } inside doesn't close the outer expansion
            lexer_advance(lx); // consume opening "
            word_len++;
            while (!lexer_at_end(lx) && lexer_peek(lx) != '"')
            {
                char qc = lexer_peek(lx);
                if (qc == '\\')
                {
                    lexer_advance(lx);
                    word_len++;
                    if (!lexer_at_end(lx))
                    {
                        lexer_advance(lx);
                        word_len++;
                    }
                }
                else
                {
                    lexer_advance(lx);
                    word_len++;
                }
            }
            if (!lexer_at_end(lx))
            {
                lexer_advance(lx); // consume closing "
                word_len++;
            }
        }
        else
        {
            lexer_advance(lx);
            word_len++;
        }
    }

    return LEX_INCOMPLETE;
}

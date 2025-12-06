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

#include "lexer_param_exp.h"
#include "lexer.h"
#include "token.h"
#include <ctype.h>

/**
 * Check if a character is a special parameter character.
 * Special parameters are: @, *, #, ?, -, $, !, 0-9
 */
static bool is_special_param_char(char c)
{
    return (c == '@' || c == '*' || c == '#' || c == '?' ||
            c == '-' || c == '$' || c == '!' || isdigit(c));
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
static void lexer_add_param_part(lexer_t *lx, const char *name, int name_len,
                                  param_subtype_t kind, const char *word, int word_len)
{
    Expects_not_null(lx);
    Expects_not_null(lx->current_token);
    Expects_not_null(name);
    Expects_gt(name_len, 0);

    // Create the parameter name string
    string_t *param_name = string_create_empty(name_len);
    for (int i = 0; i < name_len; i++)
    {
        string_append_ascii_char(param_name, name[i]);
    }

    // Create the part
    part_t *part = part_create_parameter(param_name);
    part->param_kind = kind;

    // If there's a word (for ${var:-word} style), store it
    if (word != NULL && word_len > 0)
    {
        part->word = string_create_empty(word_len);
        for (int i = 0; i < word_len; i++)
        {
            string_append_ascii_char(part->word, word[i]);
        }
    }

    // Check if we're inside double quotes
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

    string_destroy(param_name);
}

lex_status_t lexer_process_param_exp_unbraced(lexer_t *lx)
{
    Expects_not_null(lx);

    // We enter after $ has been consumed
    // Ensure we have a word token to build
    if (!lx->in_word)
    {
        lexer_start_word(lx);
    }

    if (lexer_at_end(lx))
    {
        return LEX_INCOMPLETE;
    }

    char c = lexer_peek(lx);

    // Check for special single-character parameters
    if (is_special_param_char(c))
    {
        char name[2] = {c, '\0'};
        lexer_advance(lx);
        lexer_add_param_part(lx, name, 1, PARAM_PLAIN, NULL, 0);
        lexer_pop_mode(lx);
        return LEX_OK;
    }

    // Check for name (starts with letter or underscore)
    if (is_name_start_char(c))
    {
        // Read the longest valid name
        const char *input_data = string_data(lx->input);
        int start = lx->pos;
        int len = 0;

        while (!lexer_at_end(lx) && is_name_char(lexer_peek(lx)))
        {
            lexer_advance(lx);
            len++;
        }

        lexer_add_param_part(lx, input_data + start, len, PARAM_PLAIN, NULL, 0);
        lexer_pop_mode(lx);
        return LEX_OK;
    }

    // If we get here, the $ wasn't followed by a valid parameter
    // This shouldn't happen as the caller checks before pushing mode
    lexer_set_error(lx, "Invalid parameter expansion");
    return LEX_ERROR;
}

lex_status_t lexer_process_param_exp_braced(lexer_t *lx)
{
    Expects_not_null(lx);

    // We enter after ${ has been consumed
    // Ensure we have a word token to build
    if (!lx->in_word)
    {
        lexer_start_word(lx);
    }

    if (lexer_at_end(lx))
    {
        return LEX_INCOMPLETE;
    }

    char c = lexer_peek(lx);
    param_subtype_t kind = PARAM_PLAIN;
    const char *input_data = string_data(lx->input);
    int name_start = lx->pos;
    int name_len = 0;

    // Check for ${#parameter} - length operator
    if (c == '#')
    {
        char c2 = lexer_peek_ahead(lx, 1);
        // ${#} is the special parameter #, not length of empty
        if (c2 == '}')
        {
            // This is ${#} - the special parameter #
            lexer_advance(lx); // consume #
            lexer_advance(lx); // consume }
            lexer_add_param_part(lx, "#", 1, PARAM_PLAIN, NULL, 0);
            lexer_pop_mode(lx);
            return LEX_OK;
        }
        // Otherwise it's ${#var} - length of var
        kind = PARAM_LENGTH;
        lexer_advance(lx); // consume #
        name_start = lx->pos;
    }

    // Check for ${!parameter} - indirect expansion (extension, but common)
    if (c == '!' && kind == PARAM_PLAIN)
    {
        char c2 = lexer_peek_ahead(lx, 1);
        if (c2 == '}')
        {
            // ${!} - the special parameter !
            lexer_advance(lx); // consume !
            lexer_advance(lx); // consume }
            lexer_add_param_part(lx, "!", 1, PARAM_PLAIN, NULL, 0);
            lexer_pop_mode(lx);
            return LEX_OK;
        }
        // ${!var} - indirect expansion
        kind = PARAM_INDIRECT;
        lexer_advance(lx); // consume !
        name_start = lx->pos;
    }

    if (lexer_at_end(lx))
    {
        return LEX_INCOMPLETE;
    }

    c = lexer_peek(lx);

    // Read the parameter name
    if (is_special_param_char(c))
    {
        // Special single-character parameter
        name_len = 1;
        lexer_advance(lx);
    }
    else if (is_name_start_char(c))
    {
        // Read the longest valid name
        while (!lexer_at_end(lx) && is_name_char(lexer_peek(lx)))
        {
            lexer_advance(lx);
            name_len++;
        }
    }
    else if (c == '}')
    {
        // Empty parameter ${} is an error
        lexer_set_error(lx, "Bad substitution: empty parameter");
        return LEX_ERROR;
    }
    else
    {
        lexer_set_error(lx, "Bad substitution: invalid character '%c'", c);
        return LEX_ERROR;
    }

    if (lexer_at_end(lx))
    {
        return LEX_INCOMPLETE;
    }

    c = lexer_peek(lx);

    // Check for closing brace - simple expansion
    if (c == '}')
    {
        lexer_advance(lx); // consume }
        lexer_add_param_part(lx, input_data + name_start, name_len, kind, NULL, 0);
        lexer_pop_mode(lx);
        return LEX_OK;
    }

    // Check for operators
    // We need to distinguish between operators with : prefix and without
    bool has_colon = false;
    if (c == ':')
    {
        has_colon = true;
        lexer_advance(lx);
        if (lexer_at_end(lx))
        {
            return LEX_INCOMPLETE;
        }
        c = lexer_peek(lx);
    }

    // Determine the operator type
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
            // :% is not valid, treat as substring
            kind = PARAM_SUBSTRING;
            // Put back the colon conceptually - we need to handle this differently
            // For now, just set error
            lexer_set_error(lx, "Bad substitution: invalid operator");
            return LEX_ERROR;
        }
        // Check for %%
        if (lexer_peek_ahead(lx, 1) == '%')
        {
            kind = PARAM_REMOVE_LARGE_SUFFIX;
            lexer_advance(lx); // consume first %
        }
        else
        {
            kind = PARAM_REMOVE_SMALL_SUFFIX;
        }
        break;
    case '#':
        if (has_colon)
        {
            lexer_set_error(lx, "Bad substitution: invalid operator");
            return LEX_ERROR;
        }
        // Check for ##
        if (lexer_peek_ahead(lx, 1) == '#')
        {
            kind = PARAM_REMOVE_LARGE_PREFIX;
            lexer_advance(lx); // consume first #
        }
        else
        {
            kind = PARAM_REMOVE_SMALL_PREFIX;
        }
        break;
    default:
        if (has_colon)
        {
            // Could be substring ${var:offset} or ${var:offset:length}
            kind = PARAM_SUBSTRING;
            // For substring, the colon is part of the syntax, and what follows is offset
            // We need to handle this specially
            // For now, we'll read until } and store as word
        }
        else
        {
            lexer_set_error(lx, "Bad substitution: unexpected character '%c'", c);
            return LEX_ERROR;
        }
        break;
    }

    // Consume the operator character (unless it's substring with colon already consumed)
    if (kind != PARAM_SUBSTRING)
    {
        lexer_advance(lx);
    }

    // Now read the word part until closing brace
    // Note: The word can contain nested expansions, quotes, etc.
    // For simplicity in this implementation, we read until }
    // without handling nested braces or quotes (basic implementation)
    int word_start = lx->pos;
    int word_len = 0;
    int brace_depth = 1;

    while (!lexer_at_end(lx))
    {
        c = lexer_peek(lx);

        if (c == '}')
        {
            brace_depth--;
            if (brace_depth == 0)
            {
                // Found the closing brace
                lexer_advance(lx); // consume }
                lexer_add_param_part(lx, input_data + name_start, name_len, kind,
                                     input_data + word_start, word_len);
                lexer_pop_mode(lx);
                return LEX_OK;
            }
        }
        else if (c == '{')
        {
            brace_depth++;
        }
        else if (c == '\\')
        {
            // Skip escaped character
            lexer_advance(lx);
            word_len++;
            if (!lexer_at_end(lx))
            {
                lexer_advance(lx);
                word_len++;
            }
            continue;
        }

        lexer_advance(lx);
        word_len++;
    }

    // Reached end of input without closing brace
    return LEX_INCOMPLETE;
}

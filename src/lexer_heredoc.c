/**
 * @file lexer_heredoc.c
 * @brief Lexer module for POSIX shell heredoc body processing
 *
 * This module handles the lexing of heredoc bodies according to POSIX
 * specification. Heredocs have unique processing rules that differ from
 * normal shell input.
 *
 * Per POSIX:
 * - If any part of the delimiter word is quoted, the heredoc body is literal
 * - Otherwise, the body undergoes expansion similar to double-quoted strings
 * - Backslash-newline sequences are removed during delimiter search
 * - For <<-, leading tabs (not spaces) are stripped from each line
 * - Within unquoted heredoc, backslash escapes: $ ` \ newline
 * - Double quotes are literal except within $(), ``, or ${}
 */

#include "lexer_heredoc.h"
#include "lexer.h"
#include "token.h"
#include <ctype.h>
#include <string.h>

/**
 * Check if a character is a special parameter character.
 * These can follow $ directly without braces.
 * NOTE: Currently unused but kept for future expansion handling in heredocs.
 */
static bool is_special_param_char(char c) __attribute__((unused));
static bool is_special_param_char(char c)
{
    return (isdigit(c) || c == '#' || c == '?' || c == '-' || c == '$' || c == '!' || c == '@' || c == '*' || c == '_');
}

/**
 * Check if a character can start a parameter name.
 * NOTE: Currently unused but kept for future expansion handling in heredocs.
 */
static bool is_name_start_char(char c) __attribute__((unused));
static bool is_name_start_char(char c)
{
    return (isalpha(c) || c == '_');
}

/**
 * Characters that can be escaped with backslash in unquoted heredoc.
 * Per POSIX, backslash in heredoc behaves like inside double quotes.
 */
static bool is_heredoc_escapable(char c)
{
    return (c == '$' || c == '`' || c == '\\' || c == '\n');
}

/**
 * Append a character from heredoc context to the current heredoc content.
 * In heredoc mode, we're building up the heredoc_content string, not a word token.
 */
static void lexer_append_heredoc_char(lexer_t *lx, char c)
{
    Expects_not_null(lx);
    Expects(lx->heredoc_index >= 0 && lx->heredoc_index < lx->heredoc_queue.size);

    // For now, we'll build content in a temporary string on the token
    // This will be attached to the appropriate token when heredoc reading completes
    // We need to accumulate content somewhere - let's use the current_token's heredoc_content
    if (!lx->current_token)
    {
        // Create a temporary token to hold the heredoc content
        lx->current_token = token_create(TOKEN_WORD);
        lx->current_token->heredoc_content = string_create_empty(256);
    }

    if (!lx->current_token->heredoc_content)
    {
        lx->current_token->heredoc_content = string_create_empty(256);
    }

    string_append_ascii_char(lx->current_token->heredoc_content, c);
}

/**
 * Check if the current line matches the heredoc delimiter.
 * This function checks if we're at the start of a line that contains only
 * the delimiter (possibly with leading tabs if strip_tabs is true).
 *
 */
static bool lexer_check_heredoc_delimiter(lexer_t *lx, const string_t *delimiter, bool strip_tabs)
{
    Expects_not_null(lx);
    Expects_not_null(delimiter);

    if (lx->col_no > 1)
        return false;
    int tabs_count = 0;

    // For <<-, skip leading tabs
    if (strip_tabs)
    {
        while (!lexer_at_end(lx) && lexer_peek_ahead(lx, tabs_count) == '\t')
        {
            tabs_count++;
        }
    }

    // Since it is an undefined behavior in POSIX to search for
    // backslash-newline sequences on delimiter lines, we will ignore them here.
    const char *delim_str = string_data(delimiter);
    int delim_len = string_length(delimiter);

    if (lexer_input_has_substring_at(lx, delim_str, tabs_count))
    {
        // Found a match, but, is it followed by newline or EOF?
        char after_delim = lexer_peek_ahead(lx, tabs_count + delim_len);
        if (after_delim == '\n' || after_delim == '\0')
        {
            // Found it! Advance position accordingly
            lx->pos += tabs_count + delim_len + (after_delim == '\n' ? 1 : 0);
            lx->col_no += tabs_count + delim_len + (after_delim == '\n' ? 1 : 0);
            return true;
        }
    }
    return false;
}

/**
 * Process expansion in unquoted heredoc body.
 * Similar to double-quote processing but double-quote itself is literal
 * except within $(), ``, or ${}.
 */
static lex_status_t lexer_process_heredoc_expansion(lexer_t *lx)
{
    Expects_not_null(lx);

    char c = lexer_peek(lx);

    if (c == '\\')
    {
        char next_c = lexer_peek_ahead(lx, 1);
        if (next_c == '\0')
        {
            // Backslash at end of input - need more input
            return LEX_INCOMPLETE;
        }

        if (is_heredoc_escapable(next_c))
        {
            // Escape sequence: consume backslash and add next char literally
            lexer_advance(lx); // consume backslash

            if (next_c == '\n')
            {
                // Line continuation - consume newline but don't add to content
                lexer_advance(lx);
            }
            else
            {
                // Add the escaped character
                lexer_append_heredoc_char(lx, next_c);
                lexer_advance(lx);
            }
        }
        else
        {
            // Backslash is literal when followed by non-escapable char
            lexer_append_heredoc_char(lx, c);
            lexer_advance(lx);
        }
        return LEX_OK;
    }

    if (c == '`')
    {
        // Command substitution with backticks
        // For now, we'll treat this as literal content
        // A full implementation would push mode and handle recursively
        lexer_append_heredoc_char(lx, c);
        lexer_advance(lx);
        return LEX_OK;
    }

    if (c == '$')
    {
        char c2 = lexer_peek_ahead(lx, 1);
        if (c2 == '\0')
        {
            // $ at end of input - need more input
            return LEX_INCOMPLETE;
        }

        // For now, treat expansions as literal content
        // A full implementation would handle parameter/command/arithmetic expansion
        lexer_append_heredoc_char(lx, c);
        lexer_advance(lx);
        return LEX_OK;
    }

    // Regular character
    lexer_append_heredoc_char(lx, c);
    lexer_advance(lx);
    return LEX_OK;
}

lex_status_t lexer_process_heredoc_body(lexer_t *lx)
{
    Expects_not_null(lx);
    Expects(lx->heredoc_index >= 0 && lx->heredoc_index < lx->heredoc_queue.size);

    heredoc_entry_t *entry = &lx->heredoc_queue.entries[lx->heredoc_index];
    bool strip_tabs = entry->strip_tabs;
    bool delimiter_quoted = entry->delimiter_quoted;
    const string_t *delimiter = entry->delimiter;

    // Initialize heredoc content if needed
    if (!lx->current_token)
    {
        lx->current_token = token_create(TOKEN_WORD);
        lx->current_token->heredoc_content = string_create_empty(256);
        lx->current_token->heredoc_delimiter = string_clone(delimiter);
        lx->current_token->heredoc_delim_quoted = delimiter_quoted;
    }

    while (!lexer_at_end(lx))
    {
        char c = lexer_peek(lx);

        // Check if this line is the delimiter
        if (lexer_check_heredoc_delimiter(lx, delimiter, strip_tabs))
        {
            // All the heredoc content is in the token's heredoc_content
            // field. We can now move it to a new part in the current WORD token.
            // Attach the content to the token that requested this heredoc
            token_t *heredoc_tok = lx->current_token;
            heredoc_tok->needs_expansion = !delimiter_quoted;
            token_add_literal_part(heredoc_tok, heredoc_tok->heredoc_content);
            string_destroy(heredoc_tok->heredoc_content);
            heredoc_tok->heredoc_content = NULL;
            lexer_finalize_word(lx);
            lexer_emit_token(lx, TOKEN_END_OF_HEREDOC);
            if (lx->heredoc_index + 1 >= lx->heredoc_queue.size)
            {
                // All heredocs processed
                lx->reading_heredoc = false;
                lexer_empty_heredoc_queue(lx);
                lx->heredoc_index = 0;
                lexer_pop_mode(lx); // Exit heredoc mode
            }
            else
            {
                // Move to next heredoc
                lx->heredoc_index++;
            }
            return LEX_OK;
        }
        // For <<-, strip all leading tabs at the start of each line
        if (strip_tabs && lx->col_no == 1 && c == '\t')
        {
            while ((c = lexer_advance(lx)) == '\t') // skip the tab, don't add to content
                ;
        }

        // Process the line content
        if (c == '\n')
        {
            lexer_append_heredoc_char(lx, c);
            lexer_advance(lx);
        }
        else if (delimiter_quoted)
        {
            // Quoted delimiter means literal content - no expansion
            lexer_append_heredoc_char(lx, c);
            lexer_advance(lx);
        }
        else
        {
            // Unquoted delimiter means expansion is performed
            lex_status_t status = lexer_process_heredoc_expansion(lx);
            if (status != LEX_OK)
            {
                return status;
            }
        }
    }

    // End of input without finding delimiter
    return LEX_INCOMPLETE;
}

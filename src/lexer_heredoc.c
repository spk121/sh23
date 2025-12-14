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

#if 0
/**
 * Check if a character is a special parameter character.
 * These can follow $ directly without braces.
 * NOTE: Currently unused but kept for future expansion handling in heredocs.
 */
static bool is_special_param_char(char c) __attribute__((unused));
static bool is_special_param_char(char c)
{
    return (isdigit(c) || c == '#' || c == '?' || c == '-' || c == '$' || c == '!' || c == '@' ||
            c == '*' || c == '_');
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
#endif

/**
 * Characters that can be escaped with backslash in unquoted heredoc.
 * Per POSIX, backslash in heredoc behaves like inside double quotes.
 */
static bool is_heredoc_escapable(char c)
{
    return (c == '$' || c == '`' || c == '\\' || c == '\n');
}

bool lexer_check_heredoc_delimiter(lexer_t *lx, const string_t *delim, bool strip_tabs)
{
    Expects_not_null(lx);
    Expects_not_null(delim);

    int pos = lx->pos;

    if (strip_tabs)
    {
        int after_tabs = string_find_first_not_of_cstr_at(lx->input, "\t", pos);
        if (after_tabs != -1)
            pos = after_tabs;
    }   

    if (string_compare_substring(lx->input, pos, delim, 0, string_length(delim)) != 0)
        return false;   

    pos += string_length(delim);

    // Must be followed by newline or EOF
    Expects_le(pos, string_length(lx->input));
    if (string_length(lx->input) == pos || string_at(lx->input, pos) == '\n')
    {
        lx->pos = pos;
        if (string_at(lx->input, pos) == '\n')
        {
            lx->pos++;
            lx->line_no++;
            lx->col_no = 1;
        }
        return true;
    }

    return false;
}

lex_status_t lexer_process_heredoc_body(lexer_t *lx)
{
    Expects_not_null(lx);
    if (lx->heredoc_index >= lx->heredoc_queue.size)
        return LEX_OK;

    heredoc_entry_t *entry = &lx->heredoc_queue.entries[lx->heredoc_index];
    const string_t *delim = entry->delimiter;
    bool strip_tabs = entry->strip_tabs;
    bool quoted = entry->delimiter_quoted;

    // Create a place to hold this heredoc's content
    if (lx->current_token == NULL)
    {
        log_debug("lexer_process_heredoc_body: creating new word token for heredoc content");
        lx->current_token = token_create_word();
        lx->current_token->heredoc_content = string_create();
    }
    string_t *content = string_create();

    while (!lexer_at_end(lx))
    {
        // Skip leading tabs if <<-
        if (strip_tabs && lx->col_no == 1)
        {
            while (lexer_peek(lx) == '\t')
                lexer_advance(lx);
        }

        // Check for delimiter line
        if (lexer_check_heredoc_delimiter(lx, delim, strip_tabs))
        {
            // Found delimiter — finish this heredoc
            string_append(lx->current_token->heredoc_content, content);
            string_destroy(&content);

            part_t *part = part_create_literal(lx->current_token->heredoc_content);
            string_destroy(&lx->current_token->heredoc_content);

            part_set_quoted(part, false, true);          // behaves like double-quoted
            if (quoted)
                part_set_quoted(part, true, false); // fully literal

            token_add_part(lx->current_token, part);
            lx->current_token->needs_expansion = !quoted;
            lexer_finalize_word(lx);

            lexer_emit_token(lx, TOKEN_END_OF_HEREDOC);

            lx->heredoc_index++;
            if (lx->heredoc_index >= lx->heredoc_queue.size)
            {
                lx->reading_heredoc = false;
                lexer_empty_heredoc_queue(lx);
                lexer_pop_mode(lx);
            }
            return LEX_OK;
        }

        // Not delimiter — read the line
        char c = lexer_peek(lx);

        if (c == '\n')
        {
            string_append_char(content, '\n');
            lexer_advance(lx);
            lx->line_no++;
            lx->col_no = 1;
            continue;
        }

        if (quoted)
        {
            // Fully literal
            string_append_char(content, c);
            lexer_advance(lx);
            continue;
        }

        // Unquoted: allow expansions, but treat metachars literally
        if (c == '\\')
        {
            char next = lexer_peek_ahead(lx, 1);
            if (next == '\n')
            {
                lexer_advance(lx);
                lexer_advance(lx);
                lx->line_no++;
                lx->col_no = 1;
                continue;
            }
            if (is_heredoc_escapable(next))
            {
                string_append_char(content, next);
                lexer_advance(lx);
                lexer_advance(lx);
                continue;
            }
            string_append_char(content, '\\');
            lexer_advance(lx);
            continue;
        }

        if (c == '$' || c == '`')
        {
            // Let nested lexer handle it — just copy raw text
            string_append_char(content, c);
            lexer_advance(lx);
            continue;
        }

        // All other characters are literal
        string_append_char(content, c);
        lexer_advance(lx);
    }

    string_append(lx->current_token->heredoc_content, content);
    string_destroy(&content);
    return LEX_INCOMPLETE;
}

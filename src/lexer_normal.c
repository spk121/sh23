#include "lexer_normal.h"
#include "lexer.h"
#include "token.h"
#include <ctype.h>
#include <limits.h>

// Operators that can appear in normal mode
static const char normal_mode_operators[TOKEN_TYPE_COUNT][4] = {
    [TOKEN_DLESSDASH] = "<<-", [TOKEN_AND_IF] = "&&",   [TOKEN_OR_IF] = "||",
    [TOKEN_DSEMI] = ";;",      [TOKEN_DLESS] = "<<",    [TOKEN_DGREAT] = ">>",
    [TOKEN_LESSAND] = "<&",    [TOKEN_GREATAND] = ">&", [TOKEN_LESSGREAT] = "<>",
    [TOKEN_CLOBBER] = ">|",    [TOKEN_PIPE] = "|",      [TOKEN_SEMI] = ";",
    [TOKEN_AMPER] = "&",       [TOKEN_LPAREN] = "(",    [TOKEN_RPAREN] = ")",
    [TOKEN_GREATER] = ">",     [TOKEN_LESS] = "<",
};

static bool try_emit_io_number(lexer_t *lx);
static bool try_emit_braced_io_location(lexer_t *lx);

static bool is_delimiter_char(char c)
{
    return (c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '&' || c == '|' || c == '<' ||
            c == '>' || c == '(' || c == ')');
}

static bool is_special_param_char(char c)
{
    return (isdigit(c) || c == '#' || c == '?' || c == '-' || c == '$' || c == '!' || c == '@' ||
            c == '*' || c == '_');
}

static bool is_word_char(char c)
{
    return !(is_delimiter_char(c) || c == '\0');
}

static bool check_operator_at_position(const lexer_t *lx, const char *op)
{
    Expects_not_null(lx);
    Expects_not_null(op);
    Expects_lt(strlen(op), INT_MAX);
    Expects_not_null(lx->input);

    int len = (int) strlen(op);
    if (lx->pos + len > string_length(lx->input))
        return false;
    const char *input_data = string_data(lx->input);
    return (strncmp(&input_data[lx->pos], op, len) == 0);
}

static token_type_t match_operator(const lexer_t *lx)
{
    Expects_not_null(lx);

    for (int i = 0; i < TOKEN_TYPE_COUNT; i++)
    {
        const char *op = normal_mode_operators[i];
        if (op[0] == '\0')
            continue; // skip uninitialized
        if (check_operator_at_position(lx, op))
        {
            return (token_type_t)i;
        }
    }
    return TOKEN_EOF; // no match
}

static void advance_over_operator(lexer_t *lx, token_type_t type)
{
    Expects_not_null(lx);

    int len = (int) strlen(normal_mode_operators[(int)type]);
    for (int i = 0; i < len; i++)
    {
        lexer_advance(lx);
    }
}

static bool is_heredoc_command_separator_token(token_type_t type)
{
    return type == TOKEN_NEWLINE || type == TOKEN_SEMI || type == TOKEN_AMPER ||
           type == TOKEN_PIPE || type == TOKEN_LPAREN ||
           type == TOKEN_EOF; // start of input also counts
}

static bool heredoc_previous_token_allows_io_number(const lexer_t *lx)
{
    if (token_list_size(lx->tokens) == 0)
        return true;

    token_t *last = token_list_get_last(lx->tokens);
    if (!last)
        return true;

    token_type_t t = token_get_type(last);
    return is_heredoc_command_separator_token(t);
}

/* Parse a heredoc delimiter according to POSIX 2.3 rule 3.
 * Quotes are only allowed to surround the ENTIRE delimiter.
 * Returns true on success, false on syntax error.
 * Sets *out_delimiter_quoted if the delimiter was fully quoted or escaped.
 */
static bool get_heredoc_delimiter(lexer_t *lx, string_t *out_delimiter, bool *out_delimiter_quoted)
{
    Expects_not_null(lx);
    Expects_not_null(out_delimiter);
    Expects_not_null(out_delimiter_quoted);

    *out_delimiter_quoted = false;
    string_clear(out_delimiter);

    // Skip leading whitespace
    lexer_skip_whitespace(lx);

    if (lexer_at_end(lx) || lexer_peek(lx) == '\n')
    {
        lexer_set_error(lx, "heredoc delimiter cannot be empty");
        return false;
    }

    char first = lexer_peek(lx);

    // Case 1: delimiter is fully single-quoted: <<'EOF'
    if (first == '\'')
    {
        lexer_advance(lx); // consume '
        *out_delimiter_quoted = true;

        while (!lexer_at_end(lx))
        {
            char c = lexer_peek(lx);
            if (c == '\'')
            {
                lexer_advance(lx);
                // Now expect delimiter end (whitespace or operator)
                lexer_skip_whitespace(lx);
                return true;
            }
            if (c == '\n')
            {
                lexer_set_error(lx, "unterminated single-quoted heredoc delimiter");
                return false;
            }
            string_append_char(out_delimiter, c);
            lexer_advance(lx);
        }
        lexer_set_error(lx, "unterminated single-quoted heredoc delimiter");
        return false;
    }

    // Case 2: delimiter is fully double-quoted: <<"EOF"
    if (first == '"')
    {
        lexer_advance(lx); // consume "
        *out_delimiter_quoted = true;

        while (!lexer_at_end(lx))
        {
            char c = lexer_peek(lx);
            if (c == '"')
            {
                lexer_advance(lx);
                lexer_skip_whitespace(lx);
                return true;
            }
            if (c == '\n')
            {
                lexer_set_error(lx, "unterminated double-quoted heredoc delimiter");
                return false;
            }
            // Inside double quotes: backslash escapes only ", $, `, \, and newline
            if (c == '\\')
            {
                char next = lexer_peek_ahead(lx, 1);
                if (next == '"' || next == '$' || next == '`' || next == '\\' || next == '\n')
                {
                    lexer_advance(lx); // skip backslash
                    if (next != '\n')
                        string_append_char(out_delimiter, next);
                    lexer_advance(lx);
                    continue;
                }
            }
            string_append_char(out_delimiter, c);
            lexer_advance(lx);
        }
        lexer_set_error(lx, "unterminated double-quoted heredoc delimiter");
        return false;
    }

    // Case 3: unquoted delimiter (may contain backslash escapes)
    while (!lexer_at_end(lx))
    {
        char c = lexer_peek(lx);

        if (c == ' ' || c == '\t' || c == '\n' || lexer_is_metachar(c) || c == '&' || c == ';')
            break; // delimiter ends here

        if (c == '\\')
        {
            char next = lexer_peek_ahead(lx, 1);
            if (next == '\n')
            {
                // Backslash-newline is removed
                lexer_advance(lx);
                lexer_advance(lx);
                lx->line_no++;
                lx->col_no = 1;
                continue;
            }
            if (next != '\0')
            {
                *out_delimiter_quoted = true; // backslash makes it quoted
                lexer_advance(lx);            // skip <backslash>
                string_append_char(out_delimiter, next);
                lexer_advance(lx);
                continue;
            }
        }

        string_append_char(out_delimiter, c);
        lexer_advance(lx);
    }

    if (string_length(out_delimiter) == 0)
    {
        lexer_set_error(lx, "heredoc delimiter cannot be empty");
        return false;
    }

    return true;
}

static void heredoc_check(lexer_t *lx, bool *found_heredoc, bool *error)
{
    Expects_not_null(lx);
    Expects_not_null(lx->tokens);
    Expects_not_null(found_heredoc);
    Expects_not_null(error);

    *found_heredoc = false;
    *error = false;

    if (lexer_input_starts_with_integer(lx) && heredoc_previous_token_allows_io_number(lx))
    {
        int digit_count = 0;
        int io_number = lexer_peek_integer(lx, &digit_count);

        // Must be immediately followed by < or >
        char after = lexer_peek_ahead(lx, digit_count);
        if (after == '<' || after == '>')
        {
            // Emit IO_NUMBER only if it's part of a redirection
            // We'll let the operator matching below handle the redirection
            lexer_emit_io_number_token(lx, io_number);
            lexer_advance_n_chars(lx, digit_count);
        }
        // If not followed by < or >, it's just a regular word → fall through
    }

    /* Now check for the heredoc operator itself (simple case or after the consumed digits) */
    if (!lexer_input_starts_with(lx, "<<"))
        return;

    lexer_advance_n_chars(lx, 2); // consume <<

    bool strip_tabs = false;
    if (lexer_peek(lx) == '-')
    {
        lexer_advance(lx);
        strip_tabs = true;
        lexer_emit_token(lx, TOKEN_DLESSDASH);
    }
    else
    {
        lexer_emit_token(lx, TOKEN_DLESS);
    }

    /* Skip whitespace before delimiter */
    lexer_skip_whitespace(lx);

    /* Parse the delimiter word */
    string_t *delimiter = string_create();
    bool delimiter_quoted = false;

    if (!get_heredoc_delimiter(lx, delimiter, &delimiter_quoted))
    {
        string_destroy(&delimiter);
        *error = true;
        return;
    }
    lexer_queue_heredoc(lx, delimiter, strip_tabs, delimiter_quoted);
    string_destroy(&delimiter);
    *found_heredoc = true;
}

lex_status_t lexer_process_one_normal_token(lexer_t *lx)
{
    Expects_not_null(lx);

    while (!lexer_at_end(lx))
    {
        char c = lexer_peek(lx);
        char c2 = lexer_peek_ahead(lx, 1);

        // Backslash-newline splicing
        if (c == '\\' && c2 == '\n')
        {
            lx->pos += 2;
            lx->line_no++;
            lx->col_no = 1;
            // Don't emit any token or newline
            continue;
        }

        // Whitespace
        if (c == ' ' || c == '\t')
        {
            if (lx->in_word)
            {
                // End current word
                lexer_finalize_word(lx);
            }
            lexer_skip_whitespace(lx);
            continue;
        }

        // Newline
        if (c == '\n')
        {
            if (lx->in_word)
            {
                // End current word
                lexer_finalize_word(lx);
            }
            lexer_advance(lx);
            lexer_emit_token(lx, TOKEN_NEWLINE);

            // After a newline, if there are pending heredocs, enter heredoc body mode
            if (lx->heredoc_queue.size > 0 && !lx->reading_heredoc)
            {
                lx->reading_heredoc = true;
                lx->heredoc_index = 0;
                lexer_push_mode(lx, LEX_HEREDOC_BODY);
                return LEX_INCOMPLETE;
            }
            continue;
        }

        // IO_NUMBER detection
        if (try_emit_io_number(lx))
        {
            continue;
        }

        // Braced IO location detection
        if (try_emit_braced_io_location(lx))
        {
            continue;
        }

        // Heredoc detection
        bool found_heredoc = false;
        bool error = false;
        heredoc_check(lx, &found_heredoc, &error);
        if (error)
            return LEX_ERROR;
        if (found_heredoc)
            continue;

        // Normal operators
        token_type_t op;
        op = match_operator(lx);
        if (op)
        {
            if (lx->in_word)
                lexer_finalize_word(lx);
            advance_over_operator(lx, op);
            lexer_emit_token(lx, op);
            return LEX_OK;
        }

        if (c == '\\')
        {
            char next_c = lexer_peek_ahead(lx, 1);
            if (next_c == '\0')
            {
                return LEX_INCOMPLETE;
            }
            // Append escaped character to word
            if (!lx->in_word)
            {
                lexer_start_word(lx);
            }
            lexer_advance(lx);                                        // consume backslash
            lexer_append_literal_char_to_word(lx, lexer_advance(lx)); // append next char
            token_set_quoted(lx->current_token, true);
            continue;
        }

        if (c == '`')
        {
            lexer_advance(lx);
            lexer_push_mode(lx, LEX_CMD_SUBST_BACKTICK);
            return LEX_INCOMPLETE;
        }

        if (c == '\'')
        {
            lexer_advance(lx); // skip opening quote
            lexer_push_mode(lx, LEX_SINGLE_QUOTE);
            return LEX_INCOMPLETE; // let single quote mode handle
        }

        if (c == '"')
        {
            lexer_advance(lx);
            lexer_push_mode(lx, LEX_DOUBLE_QUOTE);
            return LEX_INCOMPLETE;
        }
        if (c == '$')
        {
            c2 = lexer_peek_ahead(lx, 1);
            if (c2 == '\0')
            {
                return LEX_INCOMPLETE;
            }
            // Could be param, command, or arithmetic expansion
            else
            {
                char c3 = lexer_peek_ahead(lx, 2);
                if (c2 == '{')
                {
                    lexer_push_mode(lx, LEX_PARAM_EXP_BRACED);
                    lexer_advance(lx); // consume $
                    lexer_advance(lx); // consume {
                    return LEX_INCOMPLETE;
                }
                else if (c2 == '(' && c3 == '(')
                {
                    lexer_push_mode(lx, LEX_ARITH_EXP);
                    lexer_advance(lx); // consume $
                    lexer_advance(lx); // consume (
                    lexer_advance(lx); // consume (
                    return LEX_INCOMPLETE;
                }
                else if (c2 == '(')
                {
                    lexer_push_mode(lx, LEX_CMD_SUBST_PAREN);
                    lexer_advance(lx); // consume $
                    lexer_advance(lx); // consume (
                    return LEX_INCOMPLETE;
                }
                else if (is_word_char(c2) || is_special_param_char(c2))
                {
                    lexer_push_mode(lx, LEX_PARAM_EXP_UNBRACED);
                    lexer_advance(lx);
                    return LEX_INCOMPLETE;
                }
                else
                {
                    // Just a literal $
                    if (!lx->in_word)
                    {
                        lexer_start_word(lx);
                    }
                    lexer_append_literal_char_to_word(lx, lexer_advance(lx)); // append $
                    continue;
                }
            }
        }
        if (is_word_char(c) && c != '#')
        {
            if (!lx->in_word)
            {
                lexer_start_word(lx);
            }
            if (c == '=' && !lx->current_token->was_quoted)
            {
                lx->current_token->has_equals_before_quote = true;
            }
            lexer_append_literal_char_to_word(lx, lexer_advance(lx));
            continue;
        }
        else if (c == '#')
        {
            if (!lx->in_word)
            {
                // Comment - skip to end of line
                while (!lexer_at_end(lx) && lexer_peek(lx) != '\n')
                {
                    lexer_advance(lx);
                }
                continue;
            }
            // Otherwise treat as word character
            lexer_start_word(lx);
            lexer_append_literal_char_to_word(lx, lexer_advance(lx));
        }

        lexer_set_error(lx, "Unexpected character '%c'", c);
        return LEX_ERROR;
    }

    // End of input
    if (lx->in_word)
    {
        lexer_finalize_word(lx);
    }
    else if (lx->current_token != NULL)
    {
        lexer_set_error(lx, "Unexpected end of input");
        return LEX_ERROR;
    }
    lexer_emit_token(lx, TOKEN_EOF);
    return LEX_OK;
}

/*  IO_NUMBER detection */

static bool is_redirection_start_char(char c)
{
    return c == '<' || c == '>';
}

static bool previous_token_allows_io_number(const lexer_t *lx)
{
    if (token_list_size(lx->tokens) == 0)
        return true; // start of input

    token_t *last = token_list_get_last(lx->tokens);
    if (!last)
        return true;

    token_type_t t = token_get_type(last);
    return t == TOKEN_NEWLINE || t == TOKEN_SEMI || t == TOKEN_AMPER || t == TOKEN_PIPE ||
           t == TOKEN_LPAREN || t == TOKEN_AND_IF || // &&
           t == TOKEN_OR_IF ||                       // ||
           t == TOKEN_EOF;
    // Note: TOKEN_EOF covers the case after a complete command
}

static bool try_emit_io_number(lexer_t *lx)
{
    Expects_not_null(lx);

    // 1. Must be at start of potential redirection context
    if (!previous_token_allows_io_number(lx))
        return false;

    // 2. Must start with one or more digits
    if (!lexer_input_starts_with_integer(lx))
        return false;

    int digit_count = 0;
    int io_number = lexer_peek_integer(lx, &digit_count);
    if (digit_count == 0 || io_number < 0)
        return false;

    // 3. Must be IMMEDIATELY followed by a redirection operator char
    char next_char = lexer_peek_ahead(lx, digit_count);
    if (!is_redirection_start_char(next_char))
        return false;

    // Success! Emit IO_NUMBER and advance past digits
    lexer_emit_io_number_token(lx, io_number);
    lexer_advance_n_chars(lx, digit_count);

    // Do NOT advance over the redirection operator — let normal operator matching handle it
    return true;
}

static bool try_emit_braced_io_location(lexer_t *lx)
{
    Expects_not_null(lx);

    if (lexer_peek(lx) != '{')
        return false;

    char c = lexer_peek_ahead(lx, 1);
    if (!(isalnum((unsigned char)c) || c == '_'))
        return false;

    int n = 2; // already saw "{" and first identifier char
    while (true)
    {
        c = lexer_peek_ahead(lx, n);
        if (c == '\0' || c == '\n')
            return false;

        if (c == '}')
        {
            char next = lexer_peek_ahead(lx, n + 1);
            if (next == '<' || next == '>')
            {
                int len = n + 1; // include closing brace
                string_t *io_location = string_create_from_cstr_len(string_data(lx->input) + lx->pos, len);
                lexer_emit_io_location_token(lx, string_cstr(io_location));
                string_destroy(&io_location);
                lexer_advance_n_chars(lx, len);
                return true;
            }
            // '}' not followed by redirection operator – not an IO location
            return false;
        }

        if (!(isalnum((unsigned char)c) || c == '_'))
            return false;
        n++;
    }
}

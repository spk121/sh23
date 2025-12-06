#include "lexer_normal.h"
#include "lexer.h"
#include "token.h"
#include <ctype.h>

// Operators that can appear in normal mode
static const char normal_mode_operators[TOKEN_TYPE_COUNT][4] = {
    [TOKEN_DLESSDASH] = "<<-", [TOKEN_AND_IF] = "&&",  [TOKEN_OR_IF] = "||",   [TOKEN_DSEMI] = ";;",
    [TOKEN_DLESS] = "<<",      [TOKEN_DGREAT] = ">>",  [TOKEN_LESSAND] = "<&", [TOKEN_GREATAND] = ">&",
    [TOKEN_LESSGREAT] = "<>",  [TOKEN_CLOBBER] = ">|", [TOKEN_PIPE] = "|",     [TOKEN_SEMI] = ";",
    [TOKEN_AMPER] = "&",       [TOKEN_LPAREN] = "(",   [TOKEN_RPAREN] = ")",   [TOKEN_GREATER] = ">",
    [TOKEN_LESS] = "<",
};

static bool is_delimiter_char(char c)
{
    return (c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '&' || c == '|' || c == '<' || c == '>' ||
            c == '(' || c == ')');
}

static bool is_special_param_char(char c)
{
    return (isdigit(c) || c == '#' || c == '?' || c == '-' || c == '$' || c == '!' || c == '@' || c == '*' || c == '_');
}

static bool is_word_start_char(char c)
{
    return !(is_delimiter_char(c) || c == '\0');
}

static bool check_operator_at_position(const lexer_t *lx, const char *op)
{
    Expects_not_null(lx);
    Expects_not_null(op);
    Expects_not_null(lx->input);

    int len = strlen(op);
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

    int len = strlen(normal_mode_operators[(int)type]);
    for (int i = 0; i < len; i++)
    {
        lexer_advance(lx);
    }
}

lex_status_t lexer_process_one_normal_token(lexer_t *lx)
{
    Expects_not_null(lx);

    while (!lexer_at_end(lx))
    {
        char c = lexer_peek(lx);
        char c2 = lexer_peek_ahead(lx, 1);
        char c3 = lexer_peek_ahead(lx, 2);

        if (c == '\\' && c2 == '\n')
        {
            lx->pos += 2;
            lx->line_no++;
            lx->col_no = 1;
            // Don't emit any token or newline
            continue;
        }

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

        if (c == '\n')
        {
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

        // Check for heredoc operators (<< or <<-) with optional IO number prefix
        // This must be done before generic operator matching
        if ((c == '<' && c2 == '<')
              || (c >= '0' && c <= '9' && c2 == '<' && c3 == '<'))
        {
            // FIXME: handle multi-digit IO numbers
            if (c >= '0' && c <= '9')
            {
                token_t *io_token = token_create(TOKEN_IO_NUMBER);
                io_token->io_number = (int)(c - '0');
                token_set_location(io_token, lx->line_no, lx->col_no - 1, lx->line_no, lx->col_no);
                lexer_emit_token(lx, TOKEN_IO_NUMBER); 
                lexer_advance(lx);
            }
            // Advance past <<
            lexer_advance(lx);
            lexer_advance(lx);
            
            // Check for <<-
            bool strip_tabs = false;
            char c3 = lexer_peek(lx);
            if (c3 == '-')
            {
                lexer_advance(lx);
                strip_tabs = true;
            }
            
            // Emit the heredoc operator token
            if (strip_tabs)
            {
                lexer_emit_token(lx, TOKEN_DLESSDASH);
            }
            else
            {
                lexer_emit_token(lx, TOKEN_DLESS);
            }
            
            // Skip whitespace before delimiter
            lexer_skip_whitespace(lx);
            
            // Parse the delimiter word
            bool delimiter_quoted = false;
            char quote_char = '\0';
            c = lexer_peek(lx);
            
            if (c == '\'' || c == '"')
            {
                delimiter_quoted = true;
                quote_char = c;
                lexer_advance(lx); // consume opening quote
            }
            
            string_t *delimiter = string_create_empty(16);
            
            while (!lexer_at_end(lx))
            {
                char dc = lexer_peek(lx);
                
                if (delimiter_quoted && dc == quote_char)
                {
                    lexer_advance(lx); // consume closing quote
                    break;
                }
                else if (!delimiter_quoted && (dc == ' ' || dc == '\t' || dc == '\n' || dc == ';' || dc == '&' || dc == '|' || dc == '<' || dc == '>'))
                {
                    // Unquoted delimiter ends at whitespace or metacharacter
                    break;
                }
                
                string_append_ascii_char(delimiter, dc);
                lexer_advance(lx);
            }
            
            if (string_length(delimiter) == 0)
            {
                lexer_set_error(lx, "heredoc delimiter cannot be empty");
                string_destroy(delimiter);
                return LEX_ERROR;
            }
            
            // Queue the heredoc for later body reading
            // The delimiter is stored in the queue and will be used when processing the heredoc body
            lexer_queue_heredoc(lx, delimiter, strip_tabs, delimiter_quoted);
            string_destroy(delimiter);
            
            continue;
        }

        token_type_t op;
        op = match_operator(lx);
        if (op)
        {
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
            char c2 = lexer_peek_ahead(lx, 1);
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
                else if (is_word_start_char(c2) || is_special_param_char(c2))
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
        if (is_word_start_char(c))
        {
            if (!lx->in_word)
            {
                lexer_start_word(lx);
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
        // Finalize any partially built token.
        // Is there any difference between in_word and current_token here?
        lexer_finalize_word(lx);
    }
    lexer_emit_token(lx, TOKEN_EOF);
    return LEX_OK;
}

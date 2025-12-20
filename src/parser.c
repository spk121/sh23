#include "parser.h"
#include "logging.h"
#include "xalloc.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations
static int is_valid_name_cstr(const char *s);
static parse_status_t parser_parse_trailing_redirections(parser_t *parser,
                                                         ast_node_list_t **out_redirections);

/* ============================================================================
 * Constants
 * ============================================================================ */

#define PARSER_ERROR_BUFFER_SIZE 512

/* ============================================================================
 * Parser Lifecycle Functions
 * ============================================================================ */

parser_t *parser_create(void)
{
    parser_t *parser = (parser_t *)xcalloc(1, sizeof(parser_t));
    parser->error_msg = string_create();
    return parser;
}

void parser_destroy(parser_t **parser)
{
    if (!parser)
        return;
    parser_t *p = *parser;

    if (p == NULL)
        return;

    if (p->error_msg != NULL)
    {
        string_destroy(&p->error_msg);
    }

    xfree(p);
    *parser = NULL;
}

/* ============================================================================
 * Token Access Functions
 * ============================================================================ */

token_t *parser_current_token(const parser_t *parser)
{
    Expects_not_null(parser);

    if (parser->tokens == NULL || parser->position >= token_list_size(parser->tokens))
    {
        return NULL;
    }

    return token_list_get(parser->tokens, parser->position);
}

token_type_t parser_current_token_type(const parser_t *parser)
{
    token_t *tok = parser_current_token(parser);
    if (tok == NULL)
    {
        return TOKEN_EOF;
    }
    return token_get_type(tok);
}

bool parser_advance(parser_t *parser)
{
    Expects_not_null(parser);

    if (parser->position < token_list_size(parser->tokens))
    {
        parser->position++;
        return parser->position < token_list_size(parser->tokens);
    }
    return false;
}

bool parser_accept(parser_t *parser, token_type_t type)
{
    if (parser_current_token_type(parser) == type)
    {
        parser_advance(parser);
        return true;
    }
    return false;
}

parse_status_t parser_expect(parser_t *parser, token_type_t type)
{
    token_type_t got = parser_current_token_type(parser);

    if (got == type)
    {
        parser_advance(parser);
        return PARSE_OK;
    }

    if (got == TOKEN_EOF)
    {
        parser_set_error(parser, "Unexpected end of input (expected %s)",
                         token_type_to_string(type));
        return PARSE_INCOMPLETE;
    }

    parser_set_error(parser, "Expected %s but got %s", token_type_to_string(type),
                     token_type_to_string(got));
    return PARSE_ERROR;
}

bool parser_at_end(const parser_t *parser)
{
    return parser_current_token(parser) == NULL;
}

// Skip one or more newlines.
int parser_skip_newlines(parser_t *parser)
{
    int n = 0;
    while (parser_accept(parser, TOKEN_NEWLINE))
    {
        // Just skip
        n++;
    }
    return n;
}

static bool is_redirection_token(token_type_t type)
{
    return type == TOKEN_LESS || type == TOKEN_GREATER || type == TOKEN_DLESS ||
           type == TOKEN_DGREAT || type == TOKEN_LESSAND || type == TOKEN_GREATAND ||
           type == TOKEN_LESSGREAT || type == TOKEN_DLESSDASH || type == TOKEN_CLOBBER ||
           type == TOKEN_IO_NUMBER || type == TOKEN_IO_LOCATION;
}

static parse_status_t parser_attach_heredoc_bodies(parser_t *parser, ast_node_t **pending_heredocs,
                                                   int pending_count)
{
    Expects_not_null(parser);

    for (int i = 0; i < pending_count; i++)
    {
        // Allow optional newlines before heredoc body tokens
        parser_skip_newlines(parser);

        if (parser_current_token_type(parser) != TOKEN_WORD)
        {
            if (pending_count == 1)
            {
                parser_set_error(parser, "Expected here-document body");
            }
            else
            {
                parser_set_error(parser, "Expected here-document body for heredoc #%d of %d", i + 1,
                                 pending_count);
            }
            return PARSE_ERROR;
        }

        token_t *body_tok = parser_current_token(parser);

        // Extract literal text from heredoc body token
        string_t *body = token_get_all_text(body_tok);
        // Attach to corresponding redirection node
        ast_node_t *redir_node = pending_heredocs[i];
        ast_redirection_node_set_heredoc_content(redir_node, body);
        string_destroy(&body);
        // Consume the body WORD and the END_OF_HEREDOC marker
        parser_advance(parser);
        if (parser_expect(parser, TOKEN_END_OF_HEREDOC) != PARSE_OK)
        {
            return PARSE_ERROR;
        }
    }

    return PARSE_OK;
}

/* ============================================================================
 * Error Handling Functions
 * ============================================================================ */

void parser_set_error(parser_t *parser, const char *format, ...)
{
    Expects_not_null(parser);
    Expects_not_null(format);

    va_list args;
    va_start(args, format);

    char buffer[PARSER_ERROR_BUFFER_SIZE];
    vsnprintf(buffer, sizeof(buffer), format, args);

    va_end(args);

    string_clear(parser->error_msg);
    string_append_cstr(parser->error_msg, buffer);

    // Store error location from current token
    token_t *tok = parser_current_token(parser);
    if (tok != NULL)
    {
        parser->error_line = token_get_first_line(tok);
        parser->error_column = token_get_first_column(tok);
    }
}

const char *parser_get_error(const parser_t *parser)
{
    Expects_not_null(parser);

    if (string_length(parser->error_msg) == 0)
    {
        return NULL;
    }
    return string_data(parser->error_msg);
}

void parser_clear_error(parser_t *parser)
{
    Expects_not_null(parser);
    string_clear(parser->error_msg);
}

bool parser_error_is_unexpected_eof(const parser_t *parser, parse_status_t status)
{
    Expects_not_null(parser);
    return status == PARSE_INCOMPLETE && parser != NULL &&
           (parser->tokens == NULL || parser->position >= token_list_size(parser->tokens));
}

/* ============================================================================
 * Main Parsing Functions
 * ============================================================================ */

parse_status_t parser_parse(parser_t *parser, token_list_t *tokens, ast_node_t **out_ast)
{
    Expects_not_null(parser);
    Expects_not_null(tokens);
    Expects_not_null(out_ast);

    // Reset parser state
    parser->tokens = tokens;
    parser->position = 0;
    parser_clear_error(parser);

    if (token_list_size(parser->tokens) == 0)
    {
        *out_ast = NULL;
        return PARSE_EMPTY;
    }

    return parser_parse_program(parser, out_ast);
}

parse_status_t parser_parse_program(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);

    // Skip leading newlines
    parser_skip_newlines(parser);

    ast_node_t *complete_commands = NULL;
    parse_status_t status = parser_parse_complete_commands(parser, &complete_commands);
    ast_node_t *program;

    switch (status)
    {
    case PARSE_OK:
        program = ast_create_program(complete_commands);
        // Skip trailing newlines
        parser_skip_newlines(parser);
        break;
    case PARSE_EMPTY:
        // Empty program
        program = ast_create_program(NULL);
        break;
    case PARSE_INCOMPLETE:
    case PARSE_ERROR:
        ast_node_destroy(&program);
        break;
    default:
        log_error("parser_parse_program: unknown parse status %d", status);
        ast_node_destroy(&program);
        status = PARSE_ERROR;
    }

    *out_node = program;
    return status;
}

parse_status_t parser_parse_complete_commands(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    ast_node_list_t *lst = ast_node_list_create();
    parse_status_t status;
    bool continue_parsing = false;

    do
    {
        ast_node_t *complete_command = NULL;
        status = parser_parse_complete_command(parser, &complete_command);

        switch (status)
        {
        case PARSE_OK:
            ast_node_list_append(lst, complete_command);
            // Skip trailing newlines
            int n = parser_skip_newlines(parser);
            if (n > 0)
                continue_parsing = true;
            break;
        case PARSE_EMPTY:
        case PARSE_INCOMPLETE:
        case PARSE_ERROR:
            ast_node_list_destroy(&lst);
            break;
        default:
            log_error("%s: unknown parse status %d", __func__, status);
            ast_node_list_destroy(&lst);
            status = PARSE_ERROR;
            break;
        }
    } while (continue_parsing);

    if (status == PARSE_OK)
        *out_node = ast_create_complete_commands(lst);

    return status;
}

parse_status_t parser_parse_complete_command(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    ast_node_list_t *cmd_lst = ast_node_list_create();
    parse_status_t status;
    bool continue_parsing = false;

    do
    {
        ast_node_t *lst = NULL;
        status = parser_parse_list(parser, &lst);

        switch (status)
        {
        case PARSE_OK:
            ast_node_list_append(cmd_lst, lst);
            op = parser_get_separator_op(parser);
            if (op != LIST_SEP_EOL)
                continue_parsing = true;
            break;
        case PARSE_EMPTY:
        case PARSE_INCOMPLETE:
        case PARSE_ERROR:
            ast_node_list_destroy(&cmd_lst);
            break;
        default:
            log_error("%s: unknown parse status %d", __func__, status);
            ast_node_list_destroy(&cmd_lst);
            status = PARSE_ERROR;
            break;
        }
    } while (continue_parsing);

    if (status == PARSE_OK)
        *out_node = ast_create_complete_command(cmd_lst, ops_lst);

    return status;
}

parse_status_t parser_parse_list(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    ast_node_list_t *andor_lst = ast_node_list_create();
    parse_status_t status;
    bool continue_parsing = false;

    do
    {
        ast_node_t *lst = NULL;
        status = parser_parse_list(parser, &lst);

        switch (status)
        {
        case PARSE_OK:
            ast_node_list_append(andor_lst, lst);
            op = parser_get_separator_op(parser);
            if (op != LIST_SEP_EOL)
                continue_parsing = true;
            break;
        case PARSE_EMPTY:
        case PARSE_INCOMPLETE:
        case PARSE_ERROR:
            ast_node_list_destroy(&andor_lst);
            break;
        default:
            log_error("%s: unknown parse status %d", __func__, status);
            ast_node_list_destroy(&andor_lst);
            status = PARSE_ERROR;
            break;
        }
    } while (continue_parsing);

    if (status == PARSE_OK)
        *out_node = ast_create_list(andor_lst, ops_lst);

    return status;
}

parse_status_t parser_parse_command_list(parser_t *parser, parser_command_context_t context,
                                         ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    ast_node_t *list = ast_create_command_list();

    // Skip leading newlines
    parser_skip_newlines(parser);

    while (!parser_at_end(parser) && parser_current_token_type(parser) != TOKEN_EOF)
    {
        ast_node_t *item = NULL;
        parse_status_t status = parser_parse_andor_list(parser, &item);

        if (status != PARSE_OK)
        {
            ast_node_destroy(&list);
            return status;
        }

        ast_node_list_append(list->data.command_list.items, item);

        // Check for separator
        // DESIGN: We store a separator for EVERY command, including the last one.
        // The last command gets LIST_SEP_EOL if there's no actual separator token.
        // This maintains the invariant: items.size == separators.len
        // Benefits: simpler indexing and executor logic
        cmd_separator_t separator = LIST_SEP_SEQUENTIAL;
        if (parser_accept(parser, TOKEN_AMPER))
        {
            separator = LIST_SEP_BACKGROUND;
        }
        else if (parser_accept(parser, TOKEN_SEMI))
        {
            separator = LIST_SEP_SEQUENTIAL;
        }
        else if (parser_accept(parser, TOKEN_NEWLINE))
        {
            separator = LIST_SEP_SEQUENTIAL;
        }
        else
        {
            // No separator token found - this is the last command in the list
            separator = LIST_SEP_EOL;
        }

        // Store separator (always, even for last command)
        cmd_separator_list_add(list->data.command_list.separators, separator);

        // Skip additional newlines
        parser_skip_newlines(parser);

        if (parser_at_end(parser))
            break;

        bool promoted = false;
        token_t *promote_tok = parser_current_token(parser);
        if (promote_tok != NULL && token_get_type(promote_tok) == TOKEN_WORD)
        {
            if (!promoted && context == PARSE_COMMAND_IN_IF)
            {
                promoted = token_try_promote_to_then(promote_tok);
            }
            else if (!promoted && context == PARSE_COMMAND_IN_THEN)
            {
                promoted = token_try_promote_to_elif(promote_tok) ||
                           token_try_promote_to_else(promote_tok) ||
                           token_try_promote_to_fi(promote_tok);
            }
            else if (!promoted && context == PARSE_COMMAND_IN_ELIF)
            {
                promoted = token_try_promote_to_then(promote_tok);
            }
            else if (!promoted && context == PARSE_COMMAND_IN_ELSE)
            {
                promoted = token_try_promote_to_fi(promote_tok);
            }
            else if (!promoted &&
                     (context == PARSE_COMMAND_IN_WHILE || context == PARSE_COMMAND_IN_UNTIL))
            {
                promoted = token_try_promote_to_do(promote_tok);
            }
            else if (!promoted && context == PARSE_COMMAND_IN_DO)
            {
                promoted = token_try_promote_to_done(promote_tok);
            }
            else if (!promoted && context == PARSE_COMMAND_IN_FOR)
            {
                promoted =
                    token_try_promote_to_in(promote_tok) || token_try_promote_to_do(promote_tok);
            }
            else if (!promoted && context == PARSE_COMMAND_IN_BRACE_GROUP)
            {
                promoted = token_try_promote_to_rbrace(promote_tok);
            }
        }

        // RPAREN is handled in the tokenizer because it is not context-sensitive.

        // Check for end tokens
        token_type_t current = parser_current_token_type(parser);
        if (context == PARSE_COMMAND_IN_CASE && current == TOKEN_WORD)
        {
            token_try_promote_to_esac(parser_current_token(parser));
            current = parser_current_token_type(parser);
        }
        if (current == TOKEN_RPAREN || current == TOKEN_RBRACE || current == TOKEN_FI ||
            current == TOKEN_DONE || current == TOKEN_ESAC || current == TOKEN_EOF ||
            current == TOKEN_THEN || current == TOKEN_ELSE || current == TOKEN_ELIF ||
            current == TOKEN_DO || current == TOKEN_DSEMI)
        {
            break;
        }
    }

    *out_node = list;
    return PARSE_OK;
}

parse_status_t parser_parse_andor_list(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    ast_node_t *left = NULL;
    parse_status_t status = parser_parse_pipeline(parser, &left);

    if (status != PARSE_OK)
    {
        return status;
    }

    // Check for && or ||
    while (true)
    {
        token_type_t current = parser_current_token_type(parser);
        if (current != TOKEN_AND_IF && current != TOKEN_OR_IF)
        {
            break;
        }

        andor_operator_t op = (current == TOKEN_AND_IF) ? ANDOR_OP_AND : ANDOR_OP_OR;
        parser_advance(parser);

        // Skip newlines after operator
        parser_skip_newlines(parser);

        ast_node_t *right = NULL;
        status = parser_parse_pipeline(parser, &right);

        if (status != PARSE_OK)
        {
            ast_node_destroy(&left);
            return status;
        }

        left = ast_create_andor_list(left, right, op);
    }

    *out_node = left;
    return PARSE_OK;
}

parse_status_t parser_parse_pipeline(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    // You can only promote a TOKEN_WORD to TOKEN_BANG when it is
    // the first token in a pipeline.
    token_t *tok = parser_current_token(parser);
    if (token_get_type(tok) == TOKEN_WORD)
        token_try_promote_to_bang(tok);

    // Check for ! prefix
    bool is_negated = parser_accept(parser, TOKEN_BANG);

    ast_node_list_t *commands = ast_node_list_create();

    // Parse first command
    ast_node_t *cmd = NULL;
    parse_status_t status = parser_parse_command(parser, &cmd);

    if (status != PARSE_OK)
    {
        ast_node_list_destroy(&commands);
        return status;
    }

    ast_node_list_append(commands, cmd);

    // Check for pipe operators
    while (parser_accept(parser, TOKEN_PIPE))
    {
        // Skip newlines after pipe
        parser_skip_newlines(parser);

        ast_node_t *next_cmd = NULL;
        status = parser_parse_command(parser, &next_cmd);

        if (status != PARSE_OK)
        {
            ast_node_list_destroy(&commands);
            return status;
        }

        ast_node_list_append(commands, next_cmd);
    }

    // If only one command and not negated, return the command directly
    if (ast_node_list_size(commands) == 1 && !is_negated)
    {
        ast_node_t *single_cmd = ast_node_list_get(commands, 0);
        // Remove from list without destroying
        commands->nodes[0] = NULL;
        ast_node_list_destroy(&commands);
        *out_node = single_cmd;
    }
    else
    {
        *out_node = ast_create_pipeline(commands, is_negated);
    }

    return PARSE_OK;
}

parse_status_t parser_parse_command(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    token_type_t current = parser_current_token_type(parser);

    // Since we're in command position, we can promote WORD.
    if (current == TOKEN_WORD &&
        token_try_promote_to_reserved_word(parser_current_token(parser), false))
        current = parser_current_token_type(parser);

    // Also promote { to TOKEN_LBRACE
    if (current == TOKEN_WORD && token_try_promote_to_lbrace(parser_current_token(parser)))
        current = TOKEN_LBRACE;

    // Check for compound commands
    if (current == TOKEN_IF || current == TOKEN_WHILE || current == TOKEN_UNTIL ||
        current == TOKEN_FOR || current == TOKEN_CASE || current == TOKEN_LPAREN ||
        current == TOKEN_LBRACE)
    {
        ast_node_t *compound = NULL;
        parse_status_t status = parser_parse_compound_command(parser, &compound);
        if (status != PARSE_OK)
            return status;

        ast_node_list_t *redirections = NULL;
        status = parser_parse_trailing_redirections(parser, &redirections);
        if (status != PARSE_OK)
        {
            ast_node_destroy(&compound);
            return status;
        }

        if (redirections != NULL)
        {
            compound = ast_create_redirected_command(compound, redirections);
        }

        *out_node = compound;
        return PARSE_OK;
    }

    // Check for function definition: WORD followed by ()
    // We need to look ahead to see if this is "name()" pattern
    if (current == TOKEN_WORD)
    {
        // Save position for potential backtrack
        int saved_pos = parser->position;

        // Check if next token is LPAREN
        parser_advance(parser);
        if (parser_current_token_type(parser) == TOKEN_LPAREN)
        {
            parser_advance(parser);
            if (parser_current_token_type(parser) == TOKEN_RPAREN)
            {
                // This is a function definition - restore and parse
                parser->position = saved_pos;
                return parser_parse_function_def(parser, out_node);
            }
        }
        // Not a function definition - restore position
        parser->position = saved_pos;
    }

    // Otherwise, parse simple command
    return parser_parse_simple_command(parser, out_node);
}

parse_status_t parser_parse_simple_command(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    token_list_t *words = token_list_create();
    ast_node_list_t *redirections = ast_node_list_create();
    token_list_t *assignments = token_list_create();

    // Track heredoc redirections to collect bodies after command line
    ast_node_t **pending_heredocs = NULL;
    int pending_count = 0;
    int pending_capacity = 0;

    bool has_command = false;
    string_t *dbg_str = NULL;

    while (!parser_at_end(parser))
    {
        token_type_t current = parser_current_token_type(parser);

        // Check for assignment (only before command word)
        if (!has_command && current == TOKEN_ASSIGNMENT_WORD)
        {
            token_t *assign_tok = parser_current_token(parser);
            if (log_level() == LOG_DEBUG)
            {
                dbg_str = token_to_string(parser_current_token(parser));
                log_debug("parser_parse_simple_command: add ASSIGNMENT: %s", string_cstr(dbg_str));
                string_destroy(&dbg_str);
            }
            token_list_append(assignments, assign_tok);
            parser_advance(parser);
            continue;
        }

        // Check for redirection
        if (is_redirection_token(current))
        {
            ast_node_t *redir = NULL;
            if (log_level() == LOG_DEBUG)
            {
                dbg_str = token_to_string(parser_current_token(parser));
                log_debug("parser_parse_simple_command: add REDIRECTION: %s", string_cstr(dbg_str));
                string_destroy(&dbg_str);
            }
            parse_status_t status = parser_parse_redirection(parser, &redir);
            if (status != PARSE_OK)
            {
                token_list_destroy(&words);
                ast_node_list_destroy(&redirections);
                token_list_destroy(&assignments);
                return status;
            }
            ast_node_list_append(redirections, redir);
            // If this redirection is a heredoc, remember to read its body
            if (redir != NULL && redir->type == AST_REDIRECTION)
            {
                redirection_type_t rt = redir->data.redirection.redir_type;
                if (rt == REDIR_HEREDOC || rt == REDIR_HEREDOC_STRIP)
                {
                    if (pending_count >= pending_capacity)
                    {
                        int newcap = (pending_capacity == 0) ? 4 : pending_capacity * 2;
                        pending_heredocs =
                            xrealloc(pending_heredocs, newcap * sizeof(ast_node_t *));
                        pending_capacity = newcap;
                    }
                    pending_heredocs[pending_count++] = redir;
                }
            }
            continue;
        }

        // Check for word
        if (current == TOKEN_WORD)
        {
            has_command = true;
            token_t *word_tok = parser_current_token(parser);
            if (log_level() == LOG_DEBUG)
            {
                dbg_str = token_to_string(parser_current_token(parser));
                log_debug("parser_parse_simple_command: add WORD: %s", string_cstr(dbg_str));
                string_destroy(&dbg_str);
            }
            token_list_append(words, word_tok);
            parser_advance(parser);
            continue;
        }

        // Not part of simple command
        if (log_level() == LOG_DEBUG)
        {
            dbg_str = token_to_string(parser_current_token(parser));
            log_debug("parser_parse_simple_command: terminate on: %s", string_cstr(dbg_str));
            string_destroy(&dbg_str);
        }
        break;
    }

    // A simple command must have at least one of: assignments, words, or redirections
    // POSIX allows assignment-only or redirection-only commands
    if (token_list_size(assignments) == 0 && token_list_size(words) == 0 &&
        ast_node_list_size(redirections) == 0)
    {
        log_debug("parser_parse_simple_command: No command found: %d words, %d redirs, %d assigns",
                  token_list_size(words), ast_node_list_size(redirections),
                  token_list_size(assignments));
        token_list_destroy(&words);
        ast_node_list_destroy(&redirections);
        token_list_destroy(&assignments);
        parser_set_error(parser, "Expected command");
        return PARSE_ERROR;
    }

    parse_status_t heredoc_status =
        parser_attach_heredoc_bodies(parser, pending_heredocs, pending_count);
    if (pending_heredocs)
        xfree(pending_heredocs);
    if (heredoc_status != PARSE_OK)
    {
        token_list_destroy(&words);
        ast_node_list_destroy(&redirections);
        token_list_destroy(&assignments);
        return heredoc_status;
    }

    *out_node = ast_create_simple_command(words, redirections, assignments);
    return PARSE_OK;
}

static parse_status_t parser_parse_trailing_redirections(parser_t *parser,
                                                         ast_node_list_t **out_redirections)
{
    Expects_not_null(parser);
    Expects_not_null(out_redirections);

    *out_redirections = NULL;

    if (!is_redirection_token(parser_current_token_type(parser)))
    {
        return PARSE_OK;
    }

    ast_node_list_t *redirections = ast_node_list_create();
    ast_node_t **pending_heredocs = NULL;
    int pending_count = 0;
    int pending_capacity = 0;
    parse_status_t status = PARSE_OK;

    while (is_redirection_token(parser_current_token_type(parser)))
    {
        ast_node_t *redir = NULL;
        status = parser_parse_redirection(parser, &redir);
        if (status != PARSE_OK)
        {
            goto error;
        }

        ast_node_list_append(redirections, redir);

        if (redir != NULL && redir->type == AST_REDIRECTION)
        {
            redirection_type_t rt = redir->data.redirection.redir_type;
            if (rt == REDIR_HEREDOC || rt == REDIR_HEREDOC_STRIP)
            {
                if (pending_count >= pending_capacity)
                {
                    int newcap = (pending_capacity == 0) ? 4 : pending_capacity * 2;
                    pending_heredocs = xrealloc(pending_heredocs, newcap * sizeof(ast_node_t *));
                    pending_capacity = newcap;
                }
                pending_heredocs[pending_count++] = redir;
            }
        }
    }

    status = parser_attach_heredoc_bodies(parser, pending_heredocs, pending_count);
    if (status != PARSE_OK)
    {
        if (pending_heredocs)
        {
            xfree(pending_heredocs);
            pending_heredocs = NULL;
        }
        goto error;
    }
    if (pending_heredocs)
    {
        xfree(pending_heredocs);
        pending_heredocs = NULL;
    }

    if (ast_node_list_size(redirections) == 0)
    {
        ast_node_list_destroy(&redirections);
        redirections = NULL;
    }

    *out_redirections = redirections;
    return PARSE_OK;

error:
    if (pending_heredocs)
        xfree(pending_heredocs);
    ast_node_list_destroy(&redirections);
    return status;
}

parse_status_t parser_parse_compound_command(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    token_type_t current = parser_current_token_type(parser);

    switch (current)
    {
    case TOKEN_IF:
        return parser_parse_if_clause(parser, out_node);
    case TOKEN_WHILE:
        return parser_parse_while_clause(parser, out_node);
    case TOKEN_UNTIL:
        return parser_parse_until_clause(parser, out_node);
    case TOKEN_FOR:
        return parser_parse_for_clause(parser, out_node);
    case TOKEN_CASE:
        return parser_parse_case_clause(parser, out_node);
    case TOKEN_LPAREN:
        return parser_parse_subshell(parser, out_node);
    case TOKEN_LBRACE:
        return parser_parse_brace_group(parser, out_node);
    default:
        parser_set_error(parser, "Expected compound command");
        return PARSE_ERROR;
    }
}

parse_status_t parser_parse_if_clause(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    // Expect 'if'
    parse_status_t status = parser_expect(parser, TOKEN_IF);
    if (status != PARSE_OK)
        return status;

    parser_skip_newlines(parser);

    // Parse condition
    ast_node_t *condition = NULL;
    status = parser_parse_command_list(parser, PARSE_COMMAND_IN_IF, &condition);
    if (status != PARSE_OK)
        return status;

    parser_skip_newlines(parser);

    // Expect 'then'
    status = parser_expect(parser, TOKEN_THEN);
    if (status != PARSE_OK)
    {
        ast_node_destroy(&condition);
        return status;
    }

    parser_skip_newlines(parser);

    // Parse then body
    ast_node_t *then_body = NULL;
    status = parser_parse_command_list(parser, PARSE_COMMAND_IN_THEN, &then_body);
    if (status != PARSE_OK)
    {
        ast_node_destroy(&condition);
        return status;
    }

    parser_skip_newlines(parser);

    ast_node_t *if_node = ast_create_if_clause(condition, then_body);

    // Handle elif/else/fi
    while (parser_accept(parser, TOKEN_ELIF))
    {
        parser_skip_newlines(parser);

        // Parse elif condition
        ast_node_t *elif_condition = NULL;
        status = parser_parse_command_list(parser, PARSE_COMMAND_IN_ELIF, &elif_condition);
        if (status != PARSE_OK)
        {
            ast_node_destroy(&if_node);
            return status;
        }

        parser_skip_newlines(parser);

        // Expect 'then'
        status = parser_expect(parser, TOKEN_THEN);
        if (status != PARSE_OK)
        {
            ast_node_destroy(&if_node);
            ast_node_destroy(&elif_condition);
            return status;
        }

        parser_skip_newlines(parser);

        // Parse elif body
        ast_node_t *elif_body = NULL;
        status = parser_parse_command_list(parser, PARSE_COMMAND_IN_THEN, &elif_body);
        if (status != PARSE_OK)
        {
            ast_node_destroy(&if_node);
            ast_node_destroy(&elif_condition);
            return status;
        }

        // Add elif as another if_clause node
        if (if_node->data.if_clause.elif_list == NULL)
        {
            if_node->data.if_clause.elif_list = ast_node_list_create();
        }
        ast_node_t *elif_node = ast_create_if_clause(elif_condition, elif_body);
        ast_node_list_append(if_node->data.if_clause.elif_list, elif_node);

        parser_skip_newlines(parser);
    }

    // Handle else
    if (parser_accept(parser, TOKEN_ELSE))
    {
        parser_skip_newlines(parser);

        ast_node_t *else_body = NULL;
        status = parser_parse_command_list(parser, PARSE_COMMAND_IN_ELSE, &else_body);
        if (status != PARSE_OK)
        {
            ast_node_destroy(&if_node);
            return status;
        }

        if_node->data.if_clause.else_body = else_body;
        parser_skip_newlines(parser);
    }

    // Expect 'fi'
    status = parser_expect(parser, TOKEN_FI);
    if (status != PARSE_OK)
    {
        ast_node_destroy(&if_node);
        return status;
    }

    *out_node = if_node;
    return PARSE_OK;
}

parse_status_t parser_parse_while_clause(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    // Expect 'while'
    parse_status_t status = parser_expect(parser, TOKEN_WHILE);
    if (status != PARSE_OK)
        return status;

    parser_skip_newlines(parser);

    // Parse condition
    ast_node_t *condition = NULL;
    status = parser_parse_command_list(parser, PARSE_COMMAND_IN_WHILE, &condition);
    if (status != PARSE_OK)
        return status;

    parser_skip_newlines(parser);

    // Expect 'do'
    status = parser_expect(parser, TOKEN_DO);
    if (status != PARSE_OK)
    {
        ast_node_destroy(&condition);
        return status;
    }

    parser_skip_newlines(parser);

    // Parse body
    ast_node_t *body = NULL;
    status = parser_parse_command_list(parser, PARSE_COMMAND_IN_DO, &body);
    if (status != PARSE_OK)
    {
        ast_node_destroy(&condition);
        return status;
    }

    parser_skip_newlines(parser);

    // Expect 'done'
    status = parser_expect(parser, TOKEN_DONE);
    if (status != PARSE_OK)
    {
        ast_node_destroy(&condition);
        ast_node_destroy(&body);
        return status;
    }

    *out_node = ast_create_while_clause(condition, body);
    return PARSE_OK;
}

parse_status_t parser_parse_until_clause(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    // Expect 'until'
    parse_status_t status = parser_expect(parser, TOKEN_UNTIL);
    if (status != PARSE_OK)
        return status;

    parser_skip_newlines(parser);

    // Parse condition
    ast_node_t *condition = NULL;
    status = parser_parse_command_list(parser, PARSE_COMMAND_IN_UNTIL, &condition);
    if (status != PARSE_OK)
        return status;

    parser_skip_newlines(parser);

    // Expect 'do'
    status = parser_expect(parser, TOKEN_DO);
    if (status != PARSE_OK)
    {
        ast_node_destroy(&condition);
        return status;
    }

    parser_skip_newlines(parser);

    // Parse body
    ast_node_t *body = NULL;
    status = parser_parse_command_list(parser, PARSE_COMMAND_IN_DO, &body);
    if (status != PARSE_OK)
    {
        ast_node_destroy(&condition);
        return status;
    }

    parser_skip_newlines(parser);

    // Expect 'done'
    status = parser_expect(parser, TOKEN_DONE);
    if (status != PARSE_OK)
    {
        ast_node_destroy(&condition);
        ast_node_destroy(&body);
        return status;
    }

    *out_node = ast_create_until_clause(condition, body);
    return PARSE_OK;
}

parse_status_t parser_parse_for_clause(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    // Expect 'for'
    parse_status_t status = parser_expect(parser, TOKEN_FOR);
    if (status != PARSE_OK)
        return status;

    // Expect variable name (a WORD)
    if (parser_current_token_type(parser) != TOKEN_WORD)
    {
        parser_set_error(parser, "Expected variable name after 'for'");
        return PARSE_ERROR;
    }

    token_t *var_tok = parser_current_token(parser);
    // Extract and validate variable name from token
    // Reject quoted names and any expansions; only literal parts are allowed
    if (token_was_quoted(var_tok))
    {
        parser_set_error(parser, "Invalid variable name in for loop");
        return PARSE_ERROR;
    }

    string_t *var_name = string_create();
    int nparts = token_part_count(var_tok);
    for (int i = 0; i < nparts; i++)
    {
        part_t *part = token_get_part(var_tok, i);
        if (part_get_type(part) != PART_LITERAL)
        {
            parser_set_error(parser, "Invalid variable name in for loop");
            string_destroy(&var_name);
            return PARSE_ERROR;
        }
        string_append(var_name, part_get_text(part));
    }

    // Validate that a non-empty, valid shell name was extracted
    if (string_length(var_name) == 0 || !is_valid_name_cstr(string_cstr(var_name)))
    {
        parser_set_error(parser, "Invalid variable name in for loop");
        string_destroy(&var_name);
        return PARSE_ERROR;
    }

    // Reject reserved words as loop variables for compatibility
    if (token_is_reserved_word(string_cstr(var_name)))
    {
        parser_set_error(parser, "Reserved word '%s' cannot be used as for loop variable",
                         string_cstr(var_name));
        string_destroy(&var_name);
        return PARSE_ERROR;
    }

    parser_advance(parser);

    parser_skip_newlines(parser);

    token_list_t *words = NULL;

    // Check for 'in' keyword
    // Note: 'in' might still be a WORD, so we need to check and promote it
    token_t *maybe_in = parser_current_token(parser);
    if (maybe_in != NULL && token_get_type(maybe_in) == TOKEN_WORD)
    {
        token_try_promote_to_in(maybe_in);
    }

    if (parser_accept(parser, TOKEN_IN))
    {
        // Parse word list
        words = token_list_create();
        while (parser_current_token_type(parser) == TOKEN_WORD)
        {
            token_t *word = parser_current_token(parser);
            token_list_append(words, word);
            parser_advance(parser);
        }
        parser_skip_newlines(parser);
    }

    // Expect separator (semicolon or newline)
    if (!parser_accept(parser, TOKEN_SEMI))
    {
        parser_skip_newlines(parser);
    }

    token_t *maybe_do = parser_current_token(parser);
    if (maybe_do != NULL && token_get_type(maybe_do) == TOKEN_WORD)
    {
        token_try_promote_to_do(maybe_do);
    }

    // Expect 'do'
    status = parser_expect(parser, TOKEN_DO);
    if (status != PARSE_OK)
    {
        string_destroy(&var_name);
        if (words != NULL)
            token_list_destroy(&words);
        return status;
    }

    parser_skip_newlines(parser);

    // Parse body
    ast_node_t *body = NULL;
    status = parser_parse_command_list(parser, PARSE_COMMAND_IN_DO, &body);
    if (status != PARSE_OK)
    {
        string_destroy(&var_name);
        if (words != NULL)
            token_list_destroy(&words);
        return status;
    }

    parser_skip_newlines(parser);

    // Expect 'done'
    status = parser_expect(parser, TOKEN_DONE);
    if (status != PARSE_OK)
    {
        string_destroy(&var_name);
        if (words != NULL)
            token_list_destroy(&words);
        ast_node_destroy(&body);
        return status;
    }

    *out_node = ast_create_for_clause(var_name, words, body);
    string_destroy(&var_name); // ast_create_for_clause clones it
    return PARSE_OK;
}

parse_status_t parser_parse_case_clause(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    // Expect 'case'
    parse_status_t status = parser_expect(parser, TOKEN_CASE);
    if (status != PARSE_OK)
        return status;

    // Expect word
    if (parser_current_token_type(parser) != TOKEN_WORD)
    {
        parser_set_error(parser, "Expected word after 'case'");
        return PARSE_ERROR;
    }

    token_t *word = parser_current_token(parser);
    parser_advance(parser);

    parser_skip_newlines(parser);

    // Expect 'in' keyword - try to promote WORD to TOKEN_IN if needed
    token_t *maybe_in = parser_current_token(parser);
    if (maybe_in != NULL && token_get_type(maybe_in) == TOKEN_WORD)
    {
        token_try_promote_to_reserved_word(maybe_in, true);
    }

    status = parser_expect(parser, TOKEN_IN);
    if (status != PARSE_OK)
    {
        return status;
    }

    parser_skip_newlines(parser);

    ast_node_t *case_node = ast_create_case_clause(word);

    // Parse case items
    while (parser_current_token_type(parser) != TOKEN_ESAC && !parser_at_end(parser))
    {
        // Allow promoting 'esac' if it's still a WORD so loop can terminate
        token_t *ctok = parser_current_token(parser);
        if (ctok != NULL && token_get_type(ctok) == TOKEN_WORD)
        {
            token_try_promote_to_reserved_word(ctok, true);
            if (token_get_type(ctok) == TOKEN_ESAC)
                break;
        }
        // Parse pattern list
        token_list_t *patterns = token_list_create();
        // POSIX: allow optional leading '(' before the pattern list
        parser_accept(parser, TOKEN_LPAREN);

        if (parser_current_token_type(parser) != TOKEN_WORD)
        {
            // Allow empty case items or ESAC
            if (parser_current_token_type(parser) == TOKEN_ESAC)
            {
                token_list_destroy(&patterns);
                break;
            }
            parser_set_error(parser, "Expected pattern in case");
            ast_node_destroy(&case_node);
            token_list_destroy(&patterns);
            return PARSE_ERROR;
        }

        while (true)
        {
            token_t *pattern = parser_current_token(parser);
            token_list_append(patterns, pattern);
            parser_advance(parser);

            if (!parser_accept(parser, TOKEN_PIPE))
                break;
        }

        // Expect ')'
        status = parser_expect(parser, TOKEN_RPAREN);
        if (status != PARSE_OK)
        {
            ast_node_destroy(&case_node);
            token_list_destroy(&patterns);
            return status;
        }

        parser_skip_newlines(parser);

        // Parse commands (optional)
        ast_node_t *item_body = NULL;
        if (parser_current_token_type(parser) != TOKEN_DSEMI &&
            parser_current_token_type(parser) != TOKEN_ESAC)
        {
            status = parser_parse_command_list(parser, PARSE_COMMAND_IN_CASE, &item_body);
            if (status != PARSE_OK)
            {
                ast_node_destroy(&case_node);
                token_list_destroy(&patterns);
                return status;
            }
        }

        // Create case item
        ast_node_t *item = ast_create_case_item(patterns, item_body);
        ast_node_list_append(case_node->data.case_clause.case_items, item);

        parser_skip_newlines(parser);

        // Expect ';;' (optional before esac)
        if (parser_accept(parser, TOKEN_DSEMI))
        {
            parser_skip_newlines(parser);
        }
    }

    // Expect 'esac'
    status = parser_expect(parser, TOKEN_ESAC);
    if (status != PARSE_OK)
    {
        ast_node_destroy(&case_node);
        return status;
    }

    *out_node = case_node;
    return PARSE_OK;
}

parse_status_t parser_parse_subshell(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    // Expect '('
    parse_status_t status = parser_expect(parser, TOKEN_LPAREN);
    if (status != PARSE_OK)
        return status;

    parser_skip_newlines(parser);

    // Parse command list
    ast_node_t *body = NULL;
    status = parser_parse_command_list(parser, PARSE_COMMAND_IN_SUBSHELL, &body);
    if (status != PARSE_OK)
        return status;

    parser_skip_newlines(parser);

    // Expect ')'
    status = parser_expect(parser, TOKEN_RPAREN);
    if (status != PARSE_OK)
    {
        ast_node_destroy(&body);
        return status;
    }

    *out_node = ast_create_subshell(body);
    return PARSE_OK;
}

parse_status_t parser_parse_brace_group(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    // Expect '{'
    parse_status_t status = parser_expect(parser, TOKEN_LBRACE);
    if (status != PARSE_OK)
        return status;

    parser_skip_newlines(parser);

    // Check for empty brace groups
    token_t *tok = parser_current_token(parser);
    if (tok != NULL && token_get_type(tok) == TOKEN_WORD)
        token_try_promote_to_rbrace(tok);

    ast_node_t *body = NULL;
    if (parser_current_token_type(parser) != TOKEN_RBRACE)
    {
        // Parse command list
        status = parser_parse_command_list(parser, PARSE_COMMAND_IN_BRACE_GROUP, &body);
        if (status != PARSE_OK && status != PARSE_EMPTY)
        { // Allow PARSE_EMPTY for empty bodies
            return status;
        }
    }
    // If body is NULL or empty, create an empty command list
    if (body == NULL)
    {
        body = ast_create_command_list(); // Assuming this creates an empty list
    }

    parser_skip_newlines(parser);

    // POSIX requirement: brace groups require a separator before the closing }
    // Valid:   { echo foo; }  or  { echo foo\n}
    // Invalid: { echo foo }
    // Exception: Empty brace groups are allowed: { }
    if (ast_node_list_size(body->data.command_list.items) > 0)
    {
        int last_sep_idx = ast_node_command_list_separator_count(body) - 1;
        cmd_separator_t last_sep = ast_node_command_list_get_separator(body, last_sep_idx);

        if (last_sep == LIST_SEP_EOL)
        {
            // The last command had no separator token, but we're about to see }
            // This violates POSIX: brace groups require a separator before }
            parser_set_error(parser, "Expected separator (';' or newline) before '}'");
            ast_node_destroy(&body);
            return PARSE_ERROR;
        }
    }

    // Expect '}'
    status = parser_expect(parser, TOKEN_RBRACE);
    if (status != PARSE_OK)
    {
        ast_node_destroy(&body);
        return status;
    }

    *out_node = ast_create_brace_group(body);
    return PARSE_OK;
}

static int is_valid_name_cstr(const char *s)
{
    Expects_not_null(s);
    Expects_ne(*s, '\0');

    if (!(isalpha((unsigned char)*s) || *s == '_'))
        return 0;
    for (const unsigned char *p = (const unsigned char *)s + 1; *p; ++p)
    {
        if (!(isalnum(*p) || *p == '_'))
            return 0;
    }
    return 1;
}

static bool get_function_name_from_token(parser_t *parser, token_t *tok, string_t *func_name)
{
    Expects_not_null(tok);
    Expects_not_null(func_name);

    if (token_get_type(tok) != TOKEN_WORD)
        return false;

    if (token_was_quoted(tok))
    {
        parser_set_error(parser, "Function name cannot be quoted");
        return false;
    }
    for (int i = 0; i < token_part_count(tok); i++)
    {
        part_t *part = token_get_part(tok, i);
        if (part_get_type(part) != PART_LITERAL)
        {
            parser_set_error(parser, "Function name cannot contain expansions");
            return false;
        }
        string_append(func_name, part_get_text(part));
    }
    if (string_length(func_name) == 0 || !is_valid_name_cstr(string_cstr(func_name)))
    {
        parser_set_error(parser, "Invalid function name");
        return false;
    }
    return true;
}

parse_status_t parser_parse_function_def(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    // Expect function name (a WORD)
    if (parser_current_token_type(parser) != TOKEN_WORD)
    {
        parser_set_error(parser, "Expected function name");
        return PARSE_ERROR;
    }

    token_t *name_tok = parser_current_token(parser);
    // Extract function name
    string_t *func_name = string_create();
    if (!get_function_name_from_token(parser, name_tok, func_name))
    {
        // Error already set in get_function_name_from_token().
        string_destroy(&func_name);
        return PARSE_ERROR;
    }

    parser_advance(parser);

    // Expect '('
    parse_status_t status = parser_expect(parser, TOKEN_LPAREN);
    if (status != PARSE_OK)
    {
        string_destroy(&func_name);
        return status;
    }

    // Expect ')'
    status = parser_expect(parser, TOKEN_RPAREN);
    if (status != PARSE_OK)
    {
        string_destroy(&func_name);
        return status;
    }

    parser_skip_newlines(parser);

    // Promote reserved words if necessary
    if (parser_current_token_type(parser) == TOKEN_WORD)
    {
        token_try_promote_to_reserved_word(parser_current_token(parser), false);
    }

    // Parse compound command (body)
    ast_node_t *body = NULL;
    status = parser_parse_compound_command(parser, &body);
    if (status != PARSE_OK)
    {
        string_destroy(&func_name);
        return status;
    }

    // Parse optional redirections (including heredocs)
    ast_node_list_t *redirections = NULL;
    status = parser_parse_trailing_redirections(parser, &redirections);
    if (status != PARSE_OK)
    {
        string_destroy(&func_name);
        ast_node_destroy(&body);
        return status;
    }

    *out_node = ast_create_function_def(func_name, body, redirections);
    string_destroy(&func_name); // ast_create_function_def clones it
    return PARSE_OK;
}

parse_status_t parser_parse_redirection(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    int io_number = -1;
    string_t *io_location = NULL;

    // Check for IO_NUMBER or IO_LOCATION
    token_type_t prefix_type = parser_current_token_type(parser);
    if (prefix_type == TOKEN_IO_NUMBER)
    {
        token_t *num_tok = parser_current_token(parser);
        io_number = token_get_io_number(num_tok);
        parser_advance(parser);
    }
    else if (prefix_type == TOKEN_IO_LOCATION)
    {
        token_t *loc_tok = parser_current_token(parser);
        const string_t *loc = token_get_io_location(loc_tok);
        const char *loc_cstr = loc ? string_cstr(loc) : NULL;
        if (loc_cstr != NULL)
        {
            int len = string_length(loc);
            if (len >= 2 && loc_cstr[0] == '{' && loc_cstr[len - 1] == '}')
            {
                const char *inner = loc_cstr + 1;
                int inner_len = len - 2;
                if (inner_len <= 0)
                {
                    parser_set_error(parser, "Invalid IO location");
                    return PARSE_ERROR;
                }

                string_t *inner_str = string_create_from_cstr_len(inner, inner_len);
                const char *inner_cstr = string_cstr(inner_str);

                bool digits_only = true;
                long value = 0;
                for (int i = 0; i < inner_len; i++)
                {
                    unsigned char ch = (unsigned char)inner_cstr[i];
                    if (!isdigit(ch))
                    {
                        digits_only = false;
                        break;
                    }
                    value = value * 10 + (inner_cstr[i] - '0');
                }

                if (digits_only)
                {
                    io_number = (int)value;
                    io_location = inner_str; // take ownership
                }
                else if (is_valid_name_cstr(inner_cstr))
                {
                    io_location = inner_str; // take ownership
                }
                else
                {
                    string_destroy(&inner_str);
                    parser_set_error(parser, "Invalid IO location");
                    return PARSE_ERROR;
                }
            }
            else
            {
                parser_set_error(parser, "Invalid IO location");
                return PARSE_ERROR;
            }
        }
        parser_advance(parser);
    }

    // Determine redirection type
    token_type_t current = parser_current_token_type(parser);
    redirection_type_t redir_type;

    switch (current)
    {
    case TOKEN_LESS:
        redir_type = REDIR_INPUT;
        break;
    case TOKEN_GREATER:
        redir_type = REDIR_OUTPUT;
        break;
    case TOKEN_DGREAT:
        redir_type = REDIR_APPEND;
        break;
    case TOKEN_DLESS:
        // Heredoc: lexer already parsed and queued delimiter; no target token follows
        redir_type = REDIR_HEREDOC;
        break;
    case TOKEN_DLESSDASH:
        // Heredoc with tab stripping: lexer already parsed and queued delimiter
        redir_type = REDIR_HEREDOC_STRIP;
        break;
    case TOKEN_LESSAND:
        redir_type = REDIR_DUP_INPUT;
        break;
    case TOKEN_GREATAND:
        redir_type = REDIR_DUP_OUTPUT;
        break;
    case TOKEN_LESSGREAT:
        redir_type = REDIR_READWRITE;
        break;
    case TOKEN_CLOBBER:
        redir_type = REDIR_CLOBBER;
        break;
    default:
        parser_set_error(parser, "Expected redirection operator");
        if (io_location != NULL)
            string_destroy(&io_location);
        return PARSE_ERROR;
    }

    parser_advance(parser);

    if (redir_type == REDIR_HEREDOC || redir_type == REDIR_HEREDOC_STRIP)
    {
        // No delimiter token follows; lexer queued heredoc using its own parser of the delimiter.
        // We create the redirection node without a target; heredoc content will be provided later.
        *out_node = ast_create_redirection(redir_type, io_number, io_location, NULL);
        return PARSE_OK;
    }

    // Expect target (filename or fd) for non-heredoc redirections
    if (parser_current_token_type(parser) != TOKEN_WORD &&
        parser_current_token_type(parser) != TOKEN_IO_LOCATION)
    {
        parser_set_error(parser, "Expected filename after redirection operator");
        if (io_location != NULL)
            string_destroy(&io_location);
        return PARSE_ERROR;
    }

    token_t *target = parser_current_token(parser);
    parser_advance(parser);

    *out_node = ast_create_redirection(redir_type, io_number, io_location, target);
    return PARSE_OK;
}

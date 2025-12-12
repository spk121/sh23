#include "parser.h"
#include "logging.h"
#include "xalloc.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    parser->allow_in_keyword = false;
    return parser;
}

void parser_destroy(parser_t **parser)
{
    if (!parser) return;
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
    if (parser_current_token_type(parser) == type)
    {
        parser_advance(parser);
        return PARSE_OK;
    }

    parser_set_error(parser, "Expected %s but got %s",
                    token_type_to_string(type),
                    token_type_to_string(parser_current_token_type(parser)));
    return PARSE_ERROR;
}

bool parser_at_end(const parser_t *parser)
{
    return parser_current_token(parser) == NULL;
}

void parser_skip_newlines(parser_t *parser)
{
    while (parser_accept(parser, TOKEN_NEWLINE))
    {
        // Just skip
    }
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

    if (token_list_size(tokens) == 0)
    {
        *out_ast = NULL;
        return PARSE_EMPTY;
    }

    return parser_parse_program(parser, out_ast);
}

parse_status_t parser_parse_program(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    return parser_parse_command_list(parser, out_node);
}

parse_status_t parser_parse_command_list(parser_t *parser, ast_node_t **out_node)
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
        list_separator_t separator = LIST_SEP_SEQUENTIAL;
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

        // Store separator
        if (list->data.command_list.separator_count == 0)
        {
            list->data.command_list.separators = 
                (list_separator_t *)xmalloc(sizeof(list_separator_t));
        }
        else
        {
            list->data.command_list.separators = 
                (list_separator_t *)xrealloc(list->data.command_list.separators,
                    (list->data.command_list.separator_count + 1) * sizeof(list_separator_t));
        }
        list->data.command_list.separators[list->data.command_list.separator_count++] = separator;

        // Skip additional newlines
        parser_skip_newlines(parser);

        // Check for end tokens
        token_type_t current = parser_current_token_type(parser);
        if (current == TOKEN_RPAREN || current == TOKEN_RBRACE ||
            current == TOKEN_FI || current == TOKEN_DONE ||
            current == TOKEN_ESAC || current == TOKEN_EOF ||
            current == TOKEN_THEN || current == TOKEN_ELSE ||
            current == TOKEN_ELIF || current == TOKEN_DO)
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
    if (current == TOKEN_WORD)
        if (token_try_promote_to_reserved_word(parser_current_token(parser), false))
            current = parser_current_token_type(parser);

    // Check for compound commands
    if (current == TOKEN_IF || current == TOKEN_WHILE || current == TOKEN_UNTIL ||
        current == TOKEN_FOR || current == TOKEN_CASE ||
        current == TOKEN_LPAREN || current == TOKEN_LBRACE)
    {
        return parser_parse_compound_command(parser, out_node);
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

    bool has_command = false;

    while (!parser_at_end(parser))
    {
        token_type_t current = parser_current_token_type(parser);

        if (log_level() == LOG_DEBUG)
        {
            string_t *dbg_str = string_create();
            dbg_str = token_to_string(parser_current_token(parser));
            log_debug("parser_parse_simple_command: current token type: %s", string_cstr(dbg_str));
            string_destroy(&dbg_str);
        }

        // Check for assignment (only before command word)
        if (!has_command && current == TOKEN_ASSIGNMENT_WORD)
        {
            token_t *assign_tok = parser_current_token(parser);
            token_list_append(assignments, assign_tok);
            parser_advance(parser);
            continue;
        }

        // Check for redirection
        if (current == TOKEN_LESS || current == TOKEN_GREATER ||
            current == TOKEN_DLESS || current == TOKEN_DGREAT ||
            current == TOKEN_LESSAND || current == TOKEN_GREATAND ||
            current == TOKEN_LESSGREAT || current == TOKEN_DLESSDASH ||
            current == TOKEN_CLOBBER || current == TOKEN_IO_NUMBER)
        {
            ast_node_t *redir = NULL;
            parse_status_t status = parser_parse_redirection(parser, &redir);
            if (status != PARSE_OK)
            {
                token_list_destroy(&words);
                ast_node_list_destroy(&redirections);
                token_list_destroy(&assignments);
                return status;
            }
            ast_node_list_append(redirections, redir);
            continue;
        }

        // Check for word
        if (current == TOKEN_WORD)
        {
            has_command = true;
            token_t *word_tok = parser_current_token(parser);
            token_list_append(words, word_tok);
            parser_advance(parser);
            continue;
        }

        // Not part of simple command
        break;
    }

    // A simple command must have either assignments, words, or redirections
    if (token_list_size(assignments) == 0 &&
        token_list_size(words) == 0 &&
        ast_node_list_size(redirections) == 0)
    {
        log_debug("parser_parse_simple_command: No command found: %d words, %d redirs, %d assigns",
                  token_list_size(words),
                  ast_node_list_size(redirections),
                  token_list_size(assignments));
        token_list_destroy(&words);
        ast_node_list_destroy(&redirections);
        token_list_destroy(&assignments);
        parser_set_error(parser, "Expected command");
        return PARSE_ERROR;
    }

    *out_node = ast_create_simple_command(words, redirections, assignments);
    return PARSE_OK;
}

parse_status_t parser_parse_compound_command(parser_t *parser, ast_node_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    token_type_t current = parser_current_token_type(parser);

    if (token_try_promote_to_lbrace(parser_current_token(parser)))
    {
        current = TOKEN_LBRACE;
    }

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
    status = parser_parse_command_list(parser, &condition);
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
    status = parser_parse_command_list(parser, &then_body);
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
        status = parser_parse_command_list(parser, &elif_condition);
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
        status = parser_parse_command_list(parser, &elif_body);
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
        status = parser_parse_command_list(parser, &else_body);
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
    status = parser_parse_command_list(parser, &condition);
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
    status = parser_parse_command_list(parser, &body);
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
    status = parser_parse_command_list(parser, &condition);
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
    status = parser_parse_command_list(parser, &body);
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
    // Extract variable name from token
    string_t *var_name = string_create();
    // For simplicity, assume single literal part
    if (token_part_count(var_tok) == 1)
    {
        part_t *part = token_get_part(var_tok, 0);
        if (part_get_type(part) == PART_LITERAL)
        {
            string_append(var_name, part_get_text(part));
        }
    }
    
    // Validate that a non-empty variable name was extracted
    if (string_length(var_name) == 0)
    {
        parser_set_error(parser, "Invalid variable name in for loop");
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
        // Try to promote to TOKEN_IN
        token_try_promote_to_reserved_word(maybe_in, true);
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
    status = parser_parse_command_list(parser, &body);
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
    while (parser_current_token_type(parser) != TOKEN_ESAC &&
           !parser_at_end(parser))
    {
        // Parse pattern list
        token_list_t *patterns = token_list_create();
        
        if (parser_current_token_type(parser) != TOKEN_WORD)
        {
            // Allow empty case items or ESAC
            if (parser_current_token_type(parser) == TOKEN_ESAC)
                break;
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
            status = parser_parse_command_list(parser, &item_body);
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
    status = parser_parse_command_list(parser, &body);
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

    // Parse command list
    ast_node_t *body = NULL;
    status = parser_parse_command_list(parser, &body);
    if (status != PARSE_OK)
        return status;

    parser_skip_newlines(parser);

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
    if (token_part_count(name_tok) == 1)
    {
        part_t *part = token_get_part(name_tok, 0);
        if (part_get_type(part) == PART_LITERAL)
        {
            string_append(func_name, part_get_text(part));
        }
    }
    
    // Validate that we got a name
    if (string_length(func_name) == 0)
    {
        parser_set_error(parser, "Invalid function name");
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

    // Parse compound command (body)
    ast_node_t *body = NULL;
    status = parser_parse_compound_command(parser, &body);
    if (status != PARSE_OK)
    {
        string_destroy(&func_name);
        return status;
    }

    // Parse optional redirections
    ast_node_list_t *redirections = NULL;
    token_type_t current = parser_current_token_type(parser);
    if (current == TOKEN_LESS || current == TOKEN_GREATER ||
        current == TOKEN_DLESS || current == TOKEN_DGREAT ||
        current == TOKEN_LESSAND || current == TOKEN_GREATAND ||
        current == TOKEN_LESSGREAT || current == TOKEN_DLESSDASH ||
        current == TOKEN_CLOBBER || current == TOKEN_IO_NUMBER)
    {
        redirections = ast_node_list_create();
        while (current == TOKEN_LESS || current == TOKEN_GREATER ||
               current == TOKEN_DLESS || current == TOKEN_DGREAT ||
               current == TOKEN_LESSAND || current == TOKEN_GREATAND ||
               current == TOKEN_LESSGREAT || current == TOKEN_DLESSDASH ||
               current == TOKEN_CLOBBER || current == TOKEN_IO_NUMBER)
        {
            ast_node_t *redir = NULL;
            status = parser_parse_redirection(parser, &redir);
            if (status != PARSE_OK)
            {
                string_destroy(&func_name);
                ast_node_destroy(&body);
                ast_node_list_destroy(&redirections);
                return status;
            }
            ast_node_list_append(redirections, redir);
            current = parser_current_token_type(parser);
        }
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

    // Check for IO_NUMBER
    if (parser_current_token_type(parser) == TOKEN_IO_NUMBER)
    {
        token_t *num_tok = parser_current_token(parser);
        io_number = num_tok->io_number;
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
    //case TOKEN_DLESS:
    //    redir_type = REDIR_HEREDOC;
    //    break;
    //case TOKEN_DLESSDASH:
    //    redir_type = REDIR_HEREDOC_STRIP;
    //    break;
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
        if (current == TOKEN_DLESS || current == TOKEN_DLESSDASH)
        {
            // TODO: Implement heredoc redirection properly
            // For now, create a basic redirection node
            redir_type = (current == TOKEN_DLESS) ? REDIR_HEREDOC : REDIR_HEREDOC_STRIP;
            parser_advance(parser); // consume << or <<-
            *out_node = ast_create_redirection(redir_type, io_number, NULL);
            return PARSE_OK;
        }    
        parser_set_error(parser, "Expected redirection operator");
        return PARSE_ERROR;
    }

    parser_advance(parser);

    // Expect target (filename or fd)
    if (parser_current_token_type(parser) != TOKEN_WORD)
    {
        parser_set_error(parser, "Expected filename after redirection operator");
        return PARSE_ERROR;
    }

    // FIXME: figure out who owns the target token.
    token_t *target = parser_current_token(parser);
    parser_advance(parser);

    // Should ast_create_redirection clone the target token?
    *out_node = ast_create_redirection(redir_type, io_number, target);
    return PARSE_OK;
}

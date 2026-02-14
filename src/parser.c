#include "parser.h"
#include "logging.h"
#include "gnode.h"
#include "xalloc.h"
#include "lib.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// For testing purposes
#include "lexer.h"

// Ignore warning 4061: enumerator in switch of enum is not explicitly handled by a case label
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4061)
#endif

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

parser_t *parser_create_with_tokens_move(token_list_t **tokens)
{
    Expects_not_null(tokens);
    Expects_not_null(*tokens);

    parser_t *parser = parser_create();
    parser->tokens = *tokens;
    parser->position = 0;
    *tokens = NULL;
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
    if (p->tokens != NULL)
    {
        token_list_destroy(&p->tokens);
    }
    xfree(p);
    *parser = NULL;
}

/* ============================================================================
 * Main Parsing Function
 * ============================================================================ */

parse_status_t parser_parse_program(parser_t *parser, gnode_t **out_gnode)
{
    Expects_not_null(parser);
    Expects_not_null(out_gnode);

    // Reset parser state
    parser->position = 0;
    parser_clear_error(parser);

    if (parser->tokens == NULL || token_list_size(parser->tokens) == 0)
    {
        *out_gnode = NULL;
        return PARSE_EMPTY;
    }

    return gparse_program(parser, out_gnode);
}

/* ============================================================================
 * Token Access Functions
 * ============================================================================ */

const token_t *parser_current_token(const parser_t *parser)
{
    Expects_not_null(parser);
    if (parser->tokens == NULL || parser->position >= token_list_size(parser->tokens))
    {
        return NULL;
    }
    return token_list_get(parser->tokens, parser->position);
}

parser_token_info_t parser_current_token_info(const parser_t *parser)
{
    Expects_not_null(parser);
    parser_token_info_t info = {0};
    if (parser->tokens == NULL || parser->position >= token_list_size(parser->tokens))
    {
        info.token = NULL;
        info.offset = 0;
        info.valid = false;
    }
    else
    {
        info.token = token_list_get(parser->tokens, parser->position);
        info.offset = 0;
        info.valid = true;
    }
    return info;
}

token_type_t parser_current_token_type(const parser_t *parser)
{
    const token_t *tok = parser_current_token(parser);
    if (tok == NULL)
    {
        return TOKEN_EOF;
    }
    return token_get_type(tok);
}

static int parser_get_current_position(const parser_t *parser)
{
    Expects_not_null(parser);

    return parser->position;
}

static int parser_rewind_to_position(parser_t* parser, int position)
{
    Expects_not_null(parser);
    if (position < 0 || position >= token_list_size(parser->tokens))
    {
        return -1;
    }
    parser->position = position;
    return 0;
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
                         token_type_to_cstr(type));
        return PARSE_INCOMPLETE;
    }
    parser_set_error(parser, "Expected %s but got %s", token_type_to_cstr(type),
                     token_type_to_cstr(got));
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

const token_t *parser_peek_token(const parser_t *parser, int offset)
{
    Expects_not_null(parser);
    int pos = parser->position + offset;
    if (pos < 0 || pos >= token_list_size(parser->tokens))
    {
        return NULL;
    }
    return token_list_get(parser->tokens, pos);
}

parser_token_info_t parser_peek_token_info(const parser_t *parser, int offset)
{
    Expects_not_null(parser);
    parser_token_info_t info = {0};
    int pos = parser->position + offset;
    if (pos < 0 || pos >= token_list_size(parser->tokens))
    {
        info.token = NULL;
        info.offset = offset;
        info.valid = false;
    }
    else
    {
        info.token = token_list_get(parser->tokens, pos);
        info.offset = offset;
        info.valid = true;
    }
    return info;
}

const token_t *parser_previous_token(const parser_t *parser)
{
    Expects_not_null(parser);
    if (parser->position == 0)
    {
        return NULL;
    }
    return token_list_get(parser->tokens, parser->position - 1);
}

/* ============================================================================
 * Parser token modifiers
 * ============================================================================ */

bool parser_token_try_promote_to_lbrace(parser_t *parser, int offset)
{
    Expects_not_null(parser);
    int index = parser->position + offset;
    if (index < 0 || index >= token_list_size(parser->tokens))
    {
        return false;
    }
    token_t *tok = parser->tokens->tokens[index];
    if (tok == NULL)
    {
        return false;
    }
    if (token_get_type(tok) == TOKEN_WORD && token_try_promote_to_lbrace(tok))
    {
        return true;
    }
    return false;
}

bool parser_token_try_promote_to_rbrace(parser_t* parser, int offset)
{
    Expects_not_null(parser);
    int index = parser->position + offset;
    if (index < 0 || index >= token_list_size(parser->tokens))
    {
        return false;
    }
    token_t *tok = parser->tokens->tokens[index];
    if (tok == NULL)
    {
        return false;
    }
    if (token_get_type(tok) == TOKEN_WORD && token_try_promote_to_rbrace(tok))
    {
        return true;
    }
    return false;
}

bool parser_token_try_promote_to_bang(parser_t *parser, int offset)
{
    Expects_not_null(parser);
    int index = parser->position + offset;
    if (index < 0 || index >= token_list_size(parser->tokens))
        return false;
    token_t *tok = parser->tokens->tokens[index];
    if (tok == NULL)
        return false;
    if (token_get_type(tok) == TOKEN_WORD && token_try_promote_to_bang(tok))
        return true;
    return false;
}

bool parser_token_try_promote_to_if(parser_t *parser, int offset)
{
    Expects_not_null(parser);
    int index = parser->position + offset;
    if (index < 0 || index >= token_list_size(parser->tokens))
        return false;
    token_t *tok = parser->tokens->tokens[index];
    if (tok == NULL)
        return false;
    if (token_get_type(tok) == TOKEN_WORD && token_try_promote_to_if(tok))
        return true;
    return false;
}

bool parser_token_try_promote_to_while(parser_t *parser, int offset)
{
    Expects_not_null(parser);
    int index = parser->position + offset;
    if (index < 0 || index >= token_list_size(parser->tokens))
        return false;
    token_t *tok = parser->tokens->tokens[index];
    if (tok == NULL)
        return false;
    if (token_get_type(tok) == TOKEN_WORD && token_try_promote_to_while(tok))
        return true;
    return false;
}

bool parser_token_try_promote_to_until(parser_t *parser, int offset)
{
    Expects_not_null(parser);
    int index = parser->position + offset;
    if (index < 0 || index >= token_list_size(parser->tokens))
        return false;
    token_t *tok = parser->tokens->tokens[index];
    if (tok == NULL)
        return false;
    if (token_get_type(tok) == TOKEN_WORD && token_try_promote_to_until(tok))
        return true;
    return false;
}

bool parser_token_try_promote_to_for(parser_t *parser, int offset)
{
    Expects_not_null(parser);
    int index = parser->position + offset;
    if (index < 0 || index >= token_list_size(parser->tokens))
        return false;
    token_t *tok = parser->tokens->tokens[index];
    if (tok == NULL)
        return false;
    if (token_get_type(tok) == TOKEN_WORD && token_try_promote_to_for(tok))
        return true;
    return false;
}

bool parser_token_try_promote_to_case(parser_t *parser, int offset)
{
    Expects_not_null(parser);
    int index = parser->position + offset;
    if (index < 0 || index >= token_list_size(parser->tokens))
        return false;
    token_t *tok = parser->tokens->tokens[index];
    if (tok == NULL)
        return false;
    if (token_get_type(tok) == TOKEN_WORD && token_try_promote_to_case(tok))
        return true;
    return false;
}

bool parser_token_try_promote_to_then(parser_t *parser, int offset)
{
    Expects_not_null(parser);
    int index = parser->position + offset;
    if (index < 0 || index >= token_list_size(parser->tokens))
        return false;
    token_t *tok = parser->tokens->tokens[index];
    if (tok == NULL)
        return false;
    if (token_get_type(tok) == TOKEN_WORD && token_try_promote_to_then(tok))
        return true;
    return false;
}

bool parser_token_try_promote_to_fi(parser_t *parser, int offset)
{
    Expects_not_null(parser);
    int index = parser->position + offset;
    if (index < 0 || index >= token_list_size(parser->tokens))
        return false;
    token_t *tok = parser->tokens->tokens[index];
    if (tok == NULL)
        return false;
    if (token_get_type(tok) == TOKEN_WORD && token_try_promote_to_fi(tok))
        return true;
    return false;
}

bool parser_token_try_promote_to_elif(parser_t *parser, int offset)
{
    Expects_not_null(parser);
    int index = parser->position + offset;
    if (index < 0 || index >= token_list_size(parser->tokens))
        return false;
    token_t *tok = parser->tokens->tokens[index];
    if (tok == NULL)
        return false;
    if (token_get_type(tok) == TOKEN_WORD && token_try_promote_to_elif(tok))
        return true;
    return false;
}

bool parser_token_try_promote_to_else(parser_t *parser, int offset)
{
    Expects_not_null(parser);
    int index = parser->position + offset;
    if (index < 0 || index >= token_list_size(parser->tokens))
        return false;
    token_t *tok = parser->tokens->tokens[index];
    if (tok == NULL)
        return false;
    if (token_get_type(tok) == TOKEN_WORD && token_try_promote_to_else(tok))
        return true;
    return false;
}

bool parser_token_try_promote_to_do(parser_t *parser, int offset)
{
    Expects_not_null(parser);
    int index = parser->position + offset;
    if (index < 0 || index >= token_list_size(parser->tokens))
        return false;
    token_t *tok = parser->tokens->tokens[index];
    if (tok == NULL)
        return false;
    if (token_get_type(tok) == TOKEN_WORD && token_try_promote_to_do(tok))
        return true;
    return false;
}

bool parser_token_try_promote_to_done(parser_t *parser, int offset)
{
    Expects_not_null(parser);
    int index = parser->position + offset;
    if (index < 0 || index >= token_list_size(parser->tokens))
        return false;
    token_t *tok = parser->tokens->tokens[index];
    if (tok == NULL)
        return false;
    if (token_get_type(tok) == TOKEN_WORD && token_try_promote_to_done(tok))
        return true;
    return false;
}

bool parser_token_try_promote_to_esac(parser_t *parser, int offset)
{
    Expects_not_null(parser);
    int index = parser->position + offset;
    if (index < 0 || index >= token_list_size(parser->tokens))
        return false;
    token_t *tok = parser->tokens->tokens[index];
    if (tok == NULL)
        return false;
    if (token_get_type(tok) == TOKEN_WORD && token_try_promote_to_esac(tok))
        return true;
    return false;
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
    const token_t *tok = parser_current_token(parser);
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
 * GRAMMAR PARSING FUNCTIONS (POSIX-aligned)
 * ============================================================================ */

/* ============================================================================
 * program :
 *      linebreak complete_commands linebreak
 *    | linebreak
 * ============================================================================
 */
parse_status_t gparse_program(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    /* Create the top-level grammar node */
    gnode_t *program = nullptr;

    /* Skip leading newlines (linebreak) */
    parser_skip_newlines(parser);

    /* Try to parse complete_commands */
    gnode_t *commands = NULL;
    parse_status_t status = gparse_complete_commands(parser, &commands);

    if (status == PARSE_OK)
    {
        /* Attach complete_commands as the child of program */
        program = g_node_create(G_PROGRAM);
        program->data.child = commands;
    }
    else if (status == PARSE_EMPTY)
    {
        /* program → linebreak (empty program) */
        program = g_node_create(G_PROGRAM);
        program->data.child = NULL;
        program->payload_type = GNODE_PAYLOAD_NONE;
    }
    else
    {
        /* Error or incomplete */
        return status;
    }

    /* Skip trailing linebreak */
    parser_skip_newlines(parser);

    *out_node = program;
    return PARSE_OK;
}

/* ============================================================================
 * complete_commands: complete_commands newline_list complete_command
 *                  |                                complete_command
 * ============================================================================
 */
parse_status_t gparse_complete_commands(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    /* If at EOF, this is empty */
    if (parser_at_end(parser))
        return PARSE_EMPTY;

    /* Create the list node */
    gnode_t *cmds_node = g_node_create(G_COMPLETE_COMMANDS);
    cmds_node->data.list = g_list_create();

    /* Parse the first complete_command */
    gnode_t *cmd = NULL;
    parse_status_t status = gparse_complete_command(parser, &cmd);

    if (status != PARSE_OK)
    {
        g_node_destroy(&cmds_node);
        return status;
    }

    g_list_append(cmds_node->data.list, cmd);

    /* Loop: newline_list complete_command */
    while (true)
    {
        /* newline_list */
        if (parser_current_token_type(parser) != TOKEN_NEWLINE)
            break;

        parser_skip_newlines(parser);

        /* Try to parse another complete_command */
        gnode_t *next = NULL;
        status = gparse_complete_command(parser, &next);

        if (status == PARSE_EMPTY)
        {
            // EOF
            break;
        }
        else if (status != PARSE_OK)
        {
            /* If incomplete or error, stop and return */
            g_node_destroy(&cmds_node);
            return status;
        }

        g_list_append(cmds_node->data.list, next);
    }

    *out_node = cmds_node;
    return PARSE_OK;
}

/* ============================================================================
 * complete_command : list separator_op
 *                  | list
 * ============================================================================
 */
parse_status_t gparse_complete_command(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    gnode_t *node = g_node_create(G_COMPLETE_COMMAND);

    /* Parse list */
    gnode_t *list = NULL;
    parse_status_t status = gparse_list(parser, &list);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        return status;
    }

    node->data.multi.a = list;

    /* Optional separator_op */
    gnode_t *sep = NULL;
    status = gparse_separator_op(parser, &sep);

    if (status == PARSE_OK)
        node->data.multi.b = sep;
    else
        node->data.multi.b = NULL;

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * list             : list separator_op and_or
 *                  |                   and_or
 * ============================================================================
 */
parse_status_t gparse_list(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    /* Create the list node */
    gnode_t *list = g_node_create(G_LIST);
    list->data.list = g_list_create();

    /* Parse first and_or */
    gnode_t *first = NULL;
    parse_status_t status = gparse_and_or(parser, &first);

    if (status != PARSE_OK)
    {
        g_node_destroy(&list);
        return status;
    }

    g_list_append(list->data.list, first);

    /* Loop: separator_op and_or */
    while (true)
    {
        /* There is an ambiguity here. To continue the list,
         * there needs to be both a separator_op and an and_or.
         * If we only have a separator_op but no and_or,
         * we should not consume the separator_op because it might
         * belong to a higher-level complete_command.
         */
        int position_cur = parser_get_current_position(parser);

        gnode_t *sep = NULL;
        status = gparse_separator_op(parser, &sep);

        if (status != PARSE_OK)
            break; /* No separator_op → end of list */

        /* Parse next and_or */
        gnode_t *next = NULL;
        status = gparse_and_or(parser, &next);

        if (status != PARSE_OK)
        {
            /* Failed to parse and_or after separator_op → rewind and end list */
            g_node_destroy(&sep);
            parser_rewind_to_position(parser, position_cur);
            break;
        }

        /* Append both separator and next element */
        g_list_append(list->data.list, sep);
        g_list_append(list->data.list, next);
    }

    *out_node = list;
    return PARSE_OK;
}

/* ============================================================================
 * and_or           :                         pipeline
 *                  | and_or AND_IF linebreak pipeline
 *                  | and_or OR_IF  linebreak pipeline
 * ============================================================================
 */
parse_status_t gparse_and_or(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    /* Parse the first pipeline */
    gnode_t *left = NULL;
    parse_status_t status = gparse_pipeline(parser, &left);

    if (status != PARSE_OK)
        return status;

    /* Loop: (AND_IF | OR_IF) linebreak pipeline */
    while (true)
    {
        /* Need all 3 elements to be valid. */
        int start_pos = parser_get_current_position(parser);

        token_type_t t = parser_current_token_type(parser);

        if (t != TOKEN_AND_IF && t != TOKEN_OR_IF)
            break;

        /* Create operator node */
        gnode_t *op = g_node_create(G_AND_OR);
        op->data.token = token_clone(parser_current_token(parser));

        parser_advance(parser);

        /* Skip linebreak */
        parser_skip_newlines(parser);

        /* Parse right-hand pipeline */
        gnode_t *right = NULL;
        status = gparse_pipeline(parser, &right);

        if (status != PARSE_OK)
        {
            parser_rewind_to_position(parser, start_pos);
            g_node_destroy(&op);
            break;
        }

        /*
         * Build a new AND_OR node:
         *   multi.a = left
         *   multi.b = operator token
         *   multi.c = right
         */
        gnode_t *node = g_node_create(G_AND_OR);
        node->data.multi.a = left;
        node->data.multi.b = op;
        node->data.multi.c = right;

        /* This becomes the new left-hand side */
        left = node;
    }

    /* Handle singleton pipelines and_or nodes. */
    if (left->type == G_PIPELINE)
    {
        /* While the standard requires wrapping even singleton pipelines,
         * we'll make this small optimization of returning it directly. */
        *out_node = left;
        return PARSE_OK;
    }
    *out_node = left;
    return PARSE_OK;
}

/* ============================================================================
 * pipeline         :      pipe_sequence
 *                  | Bang pipe_sequence
 * ============================================================================
 */
parse_status_t gparse_pipeline(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;
    
    /* Try promoting "!" to TOKEN_BANG */
    parser_token_info_t info = parser_current_token_info(parser);
    token_type_t t = info.valid ? token_get_type(info.token) : TOKEN_EOF;
    if (t == TOKEN_WORD && parser_token_try_promote_to_bang(parser, info.offset))
        t = TOKEN_BANG;
    
    if (t == TOKEN_EOF)
    {
        return PARSE_EMPTY;
    }

    gnode_t *node = g_node_create(G_PIPELINE);
    node->data.list = g_list_create();


    /* Optional Bang prefix */
    if (t == TOKEN_BANG)
    {
        gnode_t *bang_node = g_node_create(G_WORD_NODE); /* reuse token wrapper */
        bang_node->data.token = token_clone(parser_current_token(parser));
        g_list_append(node->data.list, bang_node);
        parser_advance(parser);
    }

    /* Parse pipe_sequence */
    gnode_t *seq = NULL;
    parse_status_t status = gparse_pipe_sequence(parser, &seq);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        return status;
    }

    g_list_append(node->data.list, seq);

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * pipe_sequence    :                             command
 *                  | pipe_sequence '|' linebreak command
 * ============================================================================
 */
parse_status_t gparse_pipe_sequence(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    gnode_t *node = g_node_create(G_PIPE_SEQUENCE);
    node->data.list = g_list_create();

    /* Parse first command */
    gnode_t *cmd = NULL;
    parse_status_t status = gparse_command(parser, &cmd);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        return status;
    }

    g_list_append(node->data.list, cmd);

    /* Loop: '|' linebreak command */
    while (parser_current_token_type(parser) == TOKEN_PIPE)
    {
        /* Create pipe token node */
        gnode_t *pipe_node = g_node_create(G_WORD_NODE);
        pipe_node->data.token = token_clone(parser_current_token(parser));
        g_list_append(node->data.list, pipe_node);

        parser_advance(parser);

        /* Skip linebreak */
        parser_skip_newlines(parser);

        /* Parse next command */
        gnode_t *next = NULL;
        status = gparse_command(parser, &next);

        if (status != PARSE_OK)
        {
            g_node_destroy(&node);
            return status;
        }

        g_list_append(node->data.list, next);
    }

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * command          : simple_command
 *                  | compound_command
 *                  | compound_command redirect_list
 *                  | function_definition
 * ============================================================================
 */
parse_status_t gparse_command(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    gnode_t *node = g_node_create(G_COMMAND);
    node->payload_type = GNODE_PAYLOAD_CHILD;  /* Outer wrapper uses .child */

    /* Try function_definition first */
    gnode_t *func = NULL;
    parse_status_t status = gparse_function_definition(parser, &func);

    if (status == PARSE_OK)
    {
        node->data.child = func;
        *out_node = node;
        return PARSE_OK;
    }
    
    /* If function definition parsing is incomplete, don't try other alternatives */
    if (status == PARSE_INCOMPLETE)
    {
        g_node_destroy(&node);
        return PARSE_INCOMPLETE;
    }

    /* Try compound_command */
    gnode_t *compound = NULL;
    status = gparse_compound_command(parser, &compound);

    if (status == PARSE_OK)
    {
        /* Try optional redirect_list */
        gnode_t *redirects = NULL;
        parse_status_t redir_status = gparse_redirect_list(parser, &redirects);

        if (redir_status == PARSE_OK)
        {
            /* compound_command redirect_list */
            gnode_t *wrapper = g_node_create(G_COMMAND);
            wrapper->payload_type = GNODE_PAYLOAD_MULTI;  /* Inner wrapper with redirects uses .multi */
            wrapper->data.multi.a = compound;
            wrapper->data.multi.b = redirects;
            node->data.child = wrapper;
        }
        else
        {
            /* Just compound_command */
            node->data.child = compound;
        }

        *out_node = node;
        return PARSE_OK;
    }
    
    /* If compound command parsing is incomplete, don't try simple_command */
    if (status == PARSE_INCOMPLETE)
    {
        g_node_destroy(&node);
        return PARSE_INCOMPLETE;
    }

    /* Try simple_command */
    gnode_t *simple = NULL;
    status = gparse_simple_command(parser, &simple);

    if (status == PARSE_OK)
    {
        node->data.child = simple;
        *out_node = node;
        return PARSE_OK;
    }

    /* Nothing matched */
    g_node_destroy(&node);
    return status;
}

/* ============================================================================
 * compound_command : brace_group
 *                  | subshell
 *                  | for_clause
 *                  | case_clause
 *                  | if_clause
 *                  | while_clause
 *                  | until_clause
 * ============================================================================
 */
parse_status_t gparse_compound_command(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;
    parser_token_info_t info = parser_current_token_info(parser);
    Expects(info.valid);
    token_type_t t = token_get_type(info.token);
    
    /* Try promoting TOKEN_WORD to reserved words for compound commands */
    if (t == TOKEN_WORD)
    {
        if (parser_token_try_promote_to_lbrace(parser, info.offset))
            t = TOKEN_LBRACE;
        else if (parser_token_try_promote_to_if(parser, info.offset))
            t = TOKEN_IF;
        else if (parser_token_try_promote_to_while(parser, info.offset))
            t = TOKEN_WHILE;
        else if (parser_token_try_promote_to_until(parser, info.offset))
            t = TOKEN_UNTIL;
        else if (parser_token_try_promote_to_for(parser, info.offset))
            t = TOKEN_FOR;
        else if (parser_token_try_promote_to_case(parser, info.offset))
            t = TOKEN_CASE;
    }

    gnode_t *child = NULL;
    parse_status_t status;

    switch (t)
    {
    case TOKEN_LBRACE:
        status = gparse_brace_group(parser, &child);
        break;
    case TOKEN_LPAREN:
        status = gparse_subshell(parser, &child);
        break;
    case TOKEN_FOR:
        status = gparse_for_clause(parser, &child);
        break;
    case TOKEN_CASE:
        status = gparse_case_clause(parser, &child);
        break;
    case TOKEN_IF:
        status = gparse_if_clause(parser, &child);
        break;
    case TOKEN_WHILE:
        status = gparse_while_clause(parser, &child);
        break;
    case TOKEN_UNTIL:
        status = gparse_until_clause(parser, &child);
        break;
    default:
        return PARSE_ERROR;
    }

    if (status != PARSE_OK)
        return status;

    gnode_t *node = g_node_create(G_COMPOUND_COMMAND);
    node->data.child = child;

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * subshell         : '(' compound_list ')'
 * ============================================================================
 */
parse_status_t gparse_subshell(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    if (parser_current_token_type(parser) != TOKEN_LPAREN)
        return PARSE_ERROR;

    gnode_t *node = g_node_create(G_SUBSHELL);

    /* '(' */
    gnode_t *lparen = g_node_create(G_WORD_NODE);
    lparen->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* compound_list */
    gnode_t *list = NULL;
    parse_status_t status = gparse_compound_list(parser, &list);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        g_node_destroy(&lparen);
        return status;
    }

    /* ')' */
    if (parser_current_token_type(parser) != TOKEN_RPAREN)
    {
        parser_set_error(parser, "Expected ')' to close subshell");
        g_node_destroy(&node);
        g_node_destroy(&lparen);
        g_node_destroy(&list);
        return PARSE_ERROR;
    }

    gnode_t *rparen = g_node_create(G_WORD_NODE);
    rparen->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    node->data.multi.a = lparen;
    node->data.multi.b = list;
    node->data.multi.c = rparen;

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * compound_list    : linebreak term
 *                  | linebreak term separator
 * ============================================================================
 */
parse_status_t gparse_compound_list(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    gnode_t *node = g_node_create(G_COMPOUND_LIST);

    /* linebreak */
    parser_skip_newlines(parser);

    /* term */
    gnode_t *term = NULL;
    parse_status_t status = gparse_term(parser, &term);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        return status;
    }

    node->data.pair.left = term;

    /* optional separator */
    gnode_t *sep = NULL;
    status = gparse_separator(parser, &sep);

    if (status == PARSE_OK)
        node->data.pair.right = sep;
    else
        node->data.pair.right = NULL;

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * term             : term separator and_or
 *                  |                and_or
 * ============================================================================
 */
parse_status_t gparse_term(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    gnode_t *node = g_node_create(G_TERM);
    node->data.list = g_list_create();

    /* Parse first and_or */
    gnode_t *first = NULL;
    parse_status_t status = gparse_and_or(parser, &first);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        return status;
    }

    g_list_append(node->data.list, first);

    /* Loop: separator and_or */
    while (true)
    {
        /* Need both the separator and the and_or. A
         * single separator without a following and_or is handled elsewhere . */
        int position_cur = parser_get_current_position(parser);
        gnode_t *sep = NULL;
        status = gparse_separator(parser, &sep);

        if (status != PARSE_OK)
            break;

        /* Parse next and_or */
        gnode_t *next = NULL;
        status = gparse_and_or(parser, &next);

        if (status != PARSE_OK)
        {
            g_node_destroy(&sep);
            parser_rewind_to_position(parser, position_cur);
            break;
        }

        /* Append both separator and next element */
        g_list_append(node->data.list, sep);
        g_list_append(node->data.list, next);
    }

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * for_clause       : For name                                      do_group
 *                  | For name                       sequential_sep do_group
 *                  | For name linebreak in          sequential_sep do_group
 *                  | For name linebreak in wordlist sequential_sep do_group
 * ============================================================================
 */
parse_status_t gparse_for_clause(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    if (parser_current_token_type(parser) != TOKEN_FOR)
        return PARSE_ERROR;

    gnode_t *node = g_node_create(G_FOR_CLAUSE);

    /* 'for' */
    gnode_t *for_tok = g_node_create(G_WORD_NODE);
    for_tok->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* name */
    if (parser_current_token_type(parser) != TOKEN_WORD)
    {
        parser_set_error(parser, "Expected name after 'for'");
        g_node_destroy(&node);
        g_node_destroy(&for_tok);
        return PARSE_ERROR;
    }

    gnode_t *name = g_node_create(G_NAME_NODE);
    name->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* Optional: linebreak in wordlist */
    gnode_t *in_clause = NULL;
    parse_status_t status = gparse_in_clause(parser, &in_clause);

    /* sequential_sep */
    token_type_t t = parser_current_token_type(parser);
    if (t == TOKEN_SEMI)
    {
        parser_advance(parser);
        parser_skip_newlines(parser);
    }
    else if (t == TOKEN_NEWLINE)
    {
        parser_skip_newlines(parser);
    }
    else if (status != PARSE_OK)
    {
        /* If no in_clause and no separator, error */
        parser_set_error(parser, "Expected ';' or newline in for clause");
        g_node_destroy(&node);
        g_node_destroy(&for_tok);
        g_node_destroy(&name);
        return PARSE_ERROR;
    }

    /* do_group */
    gnode_t *do_grp = NULL;
    status = gparse_do_group(parser, &do_grp);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        g_node_destroy(&for_tok);
        g_node_destroy(&name);
        if (in_clause)
            g_node_destroy(&in_clause);
        return status;
    }

    node->data.multi.a = for_tok;
    node->data.multi.b = name;
    node->data.multi.c = in_clause;
    node->data.multi.d = do_grp;

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * in_clause        : linebreak in wordlist
 * ============================================================================
 */
parse_status_t gparse_in_clause(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    parser_skip_newlines(parser);

    token_type_t t = parser_current_token_type(parser);
    
    /* Check if current token is 'in' (either already promoted or needs promotion) */
    if (t == TOKEN_WORD)
    {
        /* Try to promote 'in' word to TOKEN_IN */
        token_t *tok = parser->tokens->tokens[parser->position];
        if (tok && token_try_promote_to_in(tok))
        {
            t = TOKEN_IN;
        }
    }
    
    if (t != TOKEN_IN)
        return PARSE_ERROR;

    gnode_t *node = g_node_create(G_IN_NODE);
    node->payload_type = GNODE_PAYLOAD_TOKEN;  /* Just 'in' keyword uses .token */

    /* 'in' */
    node->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* wordlist */
    gnode_t *words = NULL;
    parse_status_t status = gparse_wordlist(parser, &words);

    if (status == PARSE_OK)
    {
        gnode_t *wrapper = g_node_create(G_IN_NODE);
        wrapper->payload_type = GNODE_PAYLOAD_MULTI;  /* 'in' + wordlist uses .multi */
        wrapper->data.multi.a = node;
        wrapper->data.multi.b = words;
        *out_node = wrapper;
        return PARSE_OK;
    }

    /* No wordlist is OK */
    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * wordlist         : wordlist WORD
 *                  |          WORD
 * ============================================================================
 */
parse_status_t gparse_wordlist(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    if (parser_current_token_type(parser) != TOKEN_WORD)
        return PARSE_ERROR;

    gnode_t *node = g_node_create(G_WORDLIST);
    node->data.list = g_list_create();

    while (parser_current_token_type(parser) == TOKEN_WORD)
    {
        gnode_t *word = g_node_create(G_WORD_NODE);
        word->data.token = token_clone(parser_current_token(parser));
        g_list_append(node->data.list, word);
        parser_advance(parser);
    }

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * case_clause      : Case WORD linebreak in linebreak case_list    Esac
 *                  | Case WORD linebreak in linebreak case_list_ns Esac
 *                  | Case WORD linebreak in linebreak              Esac
 * ============================================================================
 */
parse_status_t gparse_case_clause(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    if (parser_current_token_type(parser) != TOKEN_CASE)
        return PARSE_ERROR;

    gnode_t *node = g_node_create(G_CASE_CLAUSE);

    /* 'case' */
    gnode_t *case_tok = g_node_create(G_WORD_NODE);
    case_tok->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* WORD */
    if (parser_current_token_type(parser) != TOKEN_WORD)
    {
        parser_set_error(parser, "Expected word after 'case'");
        g_node_destroy(&node);
        g_node_destroy(&case_tok);
        return PARSE_ERROR;
    }

    gnode_t *word = g_node_create(G_WORD_NODE);
    word->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* linebreak */
    parser_skip_newlines(parser);

    /* 'in' - manually promote if it's the word "in" */
    token_type_t t = parser_current_token_type(parser);
    if (t == TOKEN_WORD)
    {
        token_t *tok = parser->tokens->tokens[parser->position];
        if (tok && token_try_promote_to_in(tok))
        {
            t = TOKEN_IN;
        }
    }
    
    if (t != TOKEN_IN)
    {
        parser_set_error(parser, "Expected 'in' in case statement");
        g_node_destroy(&node);
        g_node_destroy(&case_tok);
        g_node_destroy(&word);
        return PARSE_ERROR;
    }

    gnode_t *in_tok = g_node_create(G_WORD_NODE);
    in_tok->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* linebreak */
    parser_skip_newlines(parser);

    /* Try case_list or case_list_ns */
    gnode_t *list = NULL;
    parse_status_t status = gparse_case_list(parser, &list);

    if (status != PARSE_OK)
    {
        status = gparse_case_list_ns(parser, &list);
    }

    /* Try promoting 'esac' keyword */
    parser_token_info_t info_esac = parser_current_token_info(parser);
    if (info_esac.valid && token_get_type(info_esac.token) == TOKEN_WORD)
    {
        parser_token_try_promote_to_esac(parser, info_esac.offset);
    }

    /* 'esac' */
    if (parser_current_token_type(parser) != TOKEN_ESAC)
    {
        parser_set_error(parser, "Expected 'esac' to close case statement");
        g_node_destroy(&node);
        g_node_destroy(&case_tok);
        g_node_destroy(&word);
        g_node_destroy(&in_tok);
        if (list)
            g_node_destroy(&list);
        return PARSE_ERROR;
    }

    gnode_t *esac_tok = g_node_create(G_WORD_NODE);
    esac_tok->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    node->data.multi.a = case_tok;
    node->data.multi.b = word;
    node->data.multi.c = in_tok;
    node->data.multi.d = esac_tok;

    if (list)
    {
        /* Store list in a separate structure if needed */
        gnode_t *wrapper = g_node_create(G_CASE_CLAUSE);
        wrapper->data.multi.a = node;
        wrapper->data.multi.b = list;
        *out_node = wrapper;
    }
    else
    {
        *out_node = node;
    }

    return PARSE_OK;
}

/* ============================================================================
 * case_list_ns     : case_list case_item_ns
 *                  |           case_item_ns
 * ============================================================================
 */
parse_status_t gparse_case_list_ns(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    gnode_t *node = g_node_create(G_CASE_LIST_NS);
    node->data.list = g_list_create();

    /* Try to parse case_list first */
    gnode_t *list = NULL;
    parse_status_t status = gparse_case_list(parser, &list);

    if (status == PARSE_OK)
    {
        g_list_append(node->data.list, list);
    }

    /* Parse case_item_ns */
    gnode_t *item = NULL;
    status = gparse_case_item_ns(parser, &item);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        return status;
    }

    g_list_append(node->data.list, item);

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * case_list        : case_list case_item
 *                  |           case_item
 * ============================================================================
 */
parse_status_t gparse_case_list(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    gnode_t *node = g_node_create(G_CASE_LIST);
    node->data.list = g_list_create();

    /* Parse first case_item */
    gnode_t *item = NULL;
    parse_status_t status = gparse_case_item(parser, &item);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        return status;
    }

    g_list_append(node->data.list, item);

    /* Loop: case_item */
    while (true)
    {
        /* Check if we're at 'esac' - don't try to parse it as another case_item */
        token_type_t t = parser_current_token_type(parser);
        if (t == TOKEN_ESAC)
            break;
        if (t == TOKEN_WORD)
        {
            /* Try to see if this word is "esac" */
            const token_t *tok = parser_current_token(parser);
            if (tok && !token_was_quoted(tok) && token_part_count(tok) == 1)
            {
                const part_t *part = tok->parts->parts[0];
                if (part_get_type(part) == PART_LITERAL && 
                    strcmp(string_cstr(part->text), "esac") == 0)
                {
                    break;
                }
            }
        }

        gnode_t *next = NULL;
        status = gparse_case_item(parser, &next);

        if (status != PARSE_OK)
            break;

        g_list_append(node->data.list, next);
    }

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * case_item_ns     :     pattern_list ')'               linebreak
 *                  |     pattern_list ')' compound_list linebreak
 *                  | '(' pattern_list ')'               linebreak
 *                  | '(' pattern_list ')' compound_list linebreak
 * ============================================================================
 */
parse_status_t gparse_case_item_ns(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    gnode_t *node = g_node_create(G_CASE_ITEM_NS);

    /* Optional '(' */
    gnode_t *lparen = NULL;
    if (parser_current_token_type(parser) == TOKEN_LPAREN)
    {
        lparen = g_node_create(G_WORD_NODE);
        lparen->data.token = token_clone(parser_current_token(parser));
        parser_advance(parser);
    }

    /* pattern_list */
    gnode_t *patterns = NULL;
    parse_status_t status = gparse_pattern_list(parser, &patterns);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        if (lparen)
            g_node_destroy(&lparen);
        return status;
    }

    /* ')' */
    if (parser_current_token_type(parser) != TOKEN_RPAREN)
    {
        parser_set_error(parser, "Expected ')' after pattern list");
        g_node_destroy(&node);
        if (lparen)
            g_node_destroy(&lparen);
        g_node_destroy(&patterns);
        return PARSE_ERROR;
    }

    gnode_t *rparen = g_node_create(G_WORD_NODE);
    rparen->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* Optional compound_list */
    gnode_t *list = NULL;
    status = gparse_compound_list(parser, &list);

    /* linebreak */
    parser_skip_newlines(parser);

    node->data.multi.a = lparen;
    node->data.multi.b = patterns;
    node->data.multi.c = rparen;
    node->data.multi.d = list;

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * case_item        :     pattern_list ')' linebreak     DSEMI linebreak
 *                  |     pattern_list ')' compound_list DSEMI linebreak
 *                  | '(' pattern_list ')' linebreak     DSEMI linebreak
 *                  | '(' pattern_list ')' compound_list DSEMI linebreak
 * ============================================================================
 */
parse_status_t gparse_case_item(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    gnode_t *node = g_node_create(G_CASE_ITEM);

    /* Optional '(' */
    gnode_t *lparen = NULL;
    if (parser_current_token_type(parser) == TOKEN_LPAREN)
    {
        lparen = g_node_create(G_WORD_NODE);
        lparen->data.token = token_clone(parser_current_token(parser));
        parser_advance(parser);
    }

    /* pattern_list */
    gnode_t *patterns = NULL;
    parse_status_t status = gparse_pattern_list(parser, &patterns);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        if (lparen)
            g_node_destroy(&lparen);
        return status;
    }

    /* ')' */
    if (parser_current_token_type(parser) != TOKEN_RPAREN)
    {
        parser_set_error(parser, "Expected ')' after pattern list");
        g_node_destroy(&node);
        if (lparen)
            g_node_destroy(&lparen);
        g_node_destroy(&patterns);
        return PARSE_ERROR;
    }

    gnode_t *rparen = g_node_create(G_WORD_NODE);
    rparen->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* Optional compound_list or linebreak */
    gnode_t *list = NULL;
    status = gparse_compound_list(parser, &list);

    if (status != PARSE_OK)
    {
        parser_skip_newlines(parser);
    }

    /* DSEMI */
    if (parser_current_token_type(parser) != TOKEN_DSEMI)
    {
        parser_set_error(parser, "Expected ';;' after case item");
        g_node_destroy(&node);
        if (lparen)
            g_node_destroy(&lparen);
        g_node_destroy(&patterns);
        g_node_destroy(&rparen);
        if (list)
            g_node_destroy(&list);
        return PARSE_ERROR;
    }

    gnode_t *dsemi = g_node_create(G_WORD_NODE);
    dsemi->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* linebreak */
    parser_skip_newlines(parser);

    node->data.multi.a = lparen;
    node->data.multi.b = patterns;
    node->data.multi.c = rparen;
    node->data.multi.d = list;

    /* Store dsemi in a wrapper if needed */
    gnode_t *wrapper = g_node_create(G_CASE_ITEM);
    wrapper->data.multi.a = node;
    wrapper->data.multi.b = dsemi;

    *out_node = wrapper;
    return PARSE_OK;
}

/* ============================================================================
 * pattern_list     :             WORD
 *                  | pattern_list '|' WORD
 * ============================================================================
 */
parse_status_t gparse_pattern_list(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    if (parser_current_token_type(parser) != TOKEN_WORD)
        return PARSE_ERROR;

    gnode_t *node = g_node_create(G_PATTERN_LIST);
    node->data.list = g_list_create();

    /* Parse first WORD */
    gnode_t *word = g_node_create(G_WORD_NODE);
    word->data.token = token_clone(parser_current_token(parser));
    g_list_append(node->data.list, word);
    parser_advance(parser);

    /* Loop: '|' WORD */
    while (parser_current_token_type(parser) == TOKEN_PIPE)
    {
        /* '|' */
        gnode_t *pipe = g_node_create(G_WORD_NODE);
        pipe->data.token = token_clone(parser_current_token(parser));
        g_list_append(node->data.list, pipe);
        parser_advance(parser);

        /* WORD */
        if (parser_current_token_type(parser) != TOKEN_WORD)
        {
            parser_set_error(parser, "Expected word after '|' in pattern");
            g_node_destroy(&node);
            return PARSE_ERROR;
        }

        word = g_node_create(G_WORD_NODE);
        word->data.token = token_clone(parser_current_token(parser));
        g_list_append(node->data.list, word);
        parser_advance(parser);
    }

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * if_clause        : If compound_list Then compound_list else_part Fi
 *                  | If compound_list Then compound_list           Fi
 * ============================================================================
 */
parse_status_t gparse_if_clause(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    if (parser_current_token_type(parser) != TOKEN_IF)
        return PARSE_ERROR;

    gnode_t *node = g_node_create(G_IF_CLAUSE);

    /* 'if' */
    gnode_t *if_tok = g_node_create(G_WORD_NODE);
    if_tok->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* compound_list (condition) */
    gnode_t *cond = NULL;
    parse_status_t status = gparse_compound_list(parser, &cond);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        g_node_destroy(&if_tok);
        return status;
    }

    /* Try promoting 'then' keyword */
    parser_token_info_t info_then = parser_current_token_info(parser);
    if (info_then.valid && token_get_type(info_then.token) == TOKEN_WORD)
    {
        parser_token_try_promote_to_then(parser, info_then.offset);
    }

    /* 'then' */
    if (parser_current_token_type(parser) != TOKEN_THEN)
    {
        /* Check if we're at EOF - need more input */
        if (parser_current_token_type(parser) == TOKEN_EOF)
        {
            g_node_destroy(&node);
            g_node_destroy(&if_tok);
            g_node_destroy(&cond);
            return PARSE_INCOMPLETE;
        }
        
        parser_set_error(parser, "Expected 'then' after if condition");
        g_node_destroy(&node);
        g_node_destroy(&if_tok);
        g_node_destroy(&cond);
        return PARSE_ERROR;
    }

    gnode_t *then_tok = g_node_create(G_WORD_NODE);
    then_tok->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* compound_list (then body) */
    gnode_t *then_body = NULL;
    status = gparse_compound_list(parser, &then_body);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        g_node_destroy(&if_tok);
        g_node_destroy(&cond);
        g_node_destroy(&then_tok);
        return status;
    }

    /* Optional else_part */
    gnode_t *else_part = NULL;
    status = gparse_else_part(parser, &else_part);

    /* Try promoting 'fi' keyword */
    parser_token_info_t info_fi = parser_current_token_info(parser);
    if (info_fi.valid && token_get_type(info_fi.token) == TOKEN_WORD)
    {
        parser_token_try_promote_to_fi(parser, info_fi.offset);
    }

    /* 'fi' */
    if (parser_current_token_type(parser) != TOKEN_FI)
    {
        /* Check if we're at EOF - need more input */
        if (parser_current_token_type(parser) == TOKEN_EOF)
        {
            g_node_destroy(&node);
            g_node_destroy(&if_tok);
            g_node_destroy(&cond);
            g_node_destroy(&then_tok);
            g_node_destroy(&then_body);
            if (else_part)
                g_node_destroy(&else_part);
            return PARSE_INCOMPLETE;
        }
        
        parser_set_error(parser, "Expected 'fi' to close if statement");
        g_node_destroy(&node);
        g_node_destroy(&if_tok);
        g_node_destroy(&cond);
        g_node_destroy(&then_tok);
        g_node_destroy(&then_body);
        if (else_part)
            g_node_destroy(&else_part);
        return PARSE_ERROR;
    }

    gnode_t *fi_tok = g_node_create(G_WORD_NODE);
    fi_tok->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    node->data.multi.a = if_tok;
    node->data.multi.b = cond;
    node->data.multi.c = then_tok;
    node->data.multi.d = then_body;

    if (else_part || fi_tok)
    {
        /* Need a wrapper for additional fields */
        gnode_t *wrapper = g_node_create(G_IF_CLAUSE);
        wrapper->data.multi.a = node;
        wrapper->data.multi.b = else_part;
        wrapper->data.multi.c = fi_tok;
        *out_node = wrapper;
    }
    else
    {
        *out_node = node;
    }

    return PARSE_OK;
}

/* ============================================================================
 * else_part        : Elif compound_list Then compound_list else_part
 *                  | Elif compound_list Then compound_list
 *                  | Else compound_list
 * ============================================================================
 */
parse_status_t gparse_else_part(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    /* Try promoting 'elif' or 'else' keyword */
    parser_token_info_t info = parser_current_token_info(parser);
    if (info.valid && token_get_type(info.token) == TOKEN_WORD)
    {
        if (!parser_token_try_promote_to_elif(parser, info.offset))
            parser_token_try_promote_to_else(parser, info.offset);
    }

    token_type_t t = parser_current_token_type(parser);

    if (t == TOKEN_ELIF)
    {
        gnode_t *node = g_node_create(G_ELSE_PART);

        /* 'elif' */
        gnode_t *elif_tok = g_node_create(G_WORD_NODE);
        elif_tok->data.token = token_clone(parser_current_token(parser));
        parser_advance(parser);

        /* compound_list (condition) */
        gnode_t *cond = NULL;
        parse_status_t status = gparse_compound_list(parser, &cond);

        if (status != PARSE_OK)
        {
            g_node_destroy(&node);
            g_node_destroy(&elif_tok);
            return status;
        }

        /* Try promoting 'then' keyword */
        parser_token_info_t info_then = parser_current_token_info(parser);
        if (info_then.valid && token_get_type(info_then.token) == TOKEN_WORD)
        {
            parser_token_try_promote_to_then(parser, info_then.offset);
        }

        /* 'then' */
        if (parser_current_token_type(parser) != TOKEN_THEN)
        {
            parser_set_error(parser, "Expected 'then' after elif condition");
            g_node_destroy(&node);
            g_node_destroy(&elif_tok);
            g_node_destroy(&cond);
            return PARSE_ERROR;
        }

        gnode_t *then_tok = g_node_create(G_WORD_NODE);
        then_tok->data.token = token_clone(parser_current_token(parser));
        parser_advance(parser);

        /* compound_list (then body) */
        gnode_t *then_body = NULL;
        status = gparse_compound_list(parser, &then_body);

        if (status != PARSE_OK)
        {
            g_node_destroy(&node);
            g_node_destroy(&elif_tok);
            g_node_destroy(&cond);
            g_node_destroy(&then_tok);
            return status;
        }

        /* Optional else_part */
        gnode_t *else_part = NULL;
        status = gparse_else_part(parser, &else_part);

        /* Store semantic parts: cond, then_body, optional else_part */
        /* Discard syntactic tokens (elif_tok, then_tok) */
        node->data.multi.a = cond;
        node->data.multi.b = then_body;
        node->data.multi.c = else_part;
        
        g_node_destroy(&elif_tok);
        g_node_destroy(&then_tok);

        *out_node = node;
        return PARSE_OK;
    }
    else if (t == TOKEN_ELSE)
    {
        gnode_t *node = g_node_create(G_ELSE_PART);

        /* 'else' */
        gnode_t *else_tok = g_node_create(G_WORD_NODE);
        else_tok->data.token = token_clone(parser_current_token(parser));
        parser_advance(parser);

        /* compound_list */
        gnode_t *body = NULL;
        parse_status_t status = gparse_compound_list(parser, &body);

        if (status != PARSE_OK)
        {
            g_node_destroy(&node);
            g_node_destroy(&else_tok);
            return status;
        }

        /* Store body in multi.a, multi.b and multi.c are NULL for plain else */
        node->data.multi.a = body;
        node->data.multi.b = NULL;
        node->data.multi.c = NULL;
        
        /* else_tok is not needed in the parse tree - just syntactic */
        g_node_destroy(&else_tok);

        *out_node = node;
        return PARSE_OK;
    }

    return PARSE_ERROR;
}

/* ============================================================================
 * while_clause     : While compound_list do_group
 * ============================================================================
 */
parse_status_t gparse_while_clause(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    if (parser_current_token_type(parser) != TOKEN_WHILE)
        return PARSE_ERROR;

    gnode_t *node = g_node_create(G_WHILE_CLAUSE);

    /* 'while' */
    gnode_t *while_tok = g_node_create(G_WORD_NODE);
    while_tok->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* compound_list (condition) */
    gnode_t *cond = NULL;
    parse_status_t status = gparse_compound_list(parser, &cond);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        g_node_destroy(&while_tok);
        return status;
    }

    /* do_group */
    gnode_t *do_grp = NULL;
    status = gparse_do_group(parser, &do_grp);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        g_node_destroy(&while_tok);
        g_node_destroy(&cond);
        return status;
    }

    node->data.multi.a = while_tok;
    node->data.multi.b = cond;
    node->data.multi.c = do_grp;

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * until_clause     : Until compound_list do_group
 * ============================================================================
 */
parse_status_t gparse_until_clause(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    if (parser_current_token_type(parser) != TOKEN_UNTIL)
        return PARSE_ERROR;

    gnode_t *node = g_node_create(G_UNTIL_CLAUSE);

    /* 'until' */
    gnode_t *until_tok = g_node_create(G_WORD_NODE);
    until_tok->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* compound_list (condition) */
    gnode_t *cond = NULL;
    parse_status_t status = gparse_compound_list(parser, &cond);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        g_node_destroy(&until_tok);
        return status;
    }

    /* do_group */
    gnode_t *do_grp = NULL;
    status = gparse_do_group(parser, &do_grp);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        g_node_destroy(&until_tok);
        g_node_destroy(&cond);
        return status;
    }

    node->data.multi.a = until_tok;
    node->data.multi.b = cond;
    node->data.multi.c = do_grp;

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * function_definition : fname '(' ')' linebreak function_body
 * ============================================================================
 */
parse_status_t gparse_function_definition(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    /* fname (NAME) */
    if (parser_current_token_type(parser) != TOKEN_WORD)
        return PARSE_ERROR;

    /* Check that function name is not a reserved word */
    const token_t *fname_tok = parser_current_token(parser);
    if (fname_tok && !token_was_quoted(fname_tok) && token_part_count(fname_tok) == 1)
    {
        const part_t *part = fname_tok->parts->parts[0];
        if (part_get_type(part) == PART_LITERAL)
        {
            const char *word = string_cstr(part->text);
            if (token_is_reserved_word(word))
            {
                parser_set_error(parser, "Cannot use reserved word '%s' as function name", word);
                return PARSE_ERROR;
            }
        }
    }

    /* Look ahead for '(' */
    const token_t *next = parser_peek_token(parser, 1);
    if (!next || token_get_type(next) != TOKEN_LPAREN)
        return PARSE_ERROR;

    gnode_t *node = g_node_create(G_FUNCTION_DEFINITION);

    /* fname */
    gnode_t *fname = g_node_create(G_FNAME);
    fname->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* '(' */
    gnode_t *lparen = g_node_create(G_WORD_NODE);
    lparen->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* ')' */
    if (parser_current_token_type(parser) != TOKEN_RPAREN)
    {
        parser_set_error(parser, "Expected ')' after '(' in function definition");
        g_node_destroy(&node);
        g_node_destroy(&fname);
        g_node_destroy(&lparen);
        return PARSE_ERROR;
    }

    gnode_t *rparen = g_node_create(G_WORD_NODE);
    rparen->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* linebreak */
    parser_skip_newlines(parser);

    /* function_body: compound_command [redirect_list] */
    gnode_t *compound = NULL;
    parse_status_t status = gparse_compound_command(parser, &compound);

    if (status != PARSE_OK)
    {
        /* Once we've parsed name(), we're committed to a function definition.
         * If compound_command fails, give a helpful error message. */
        if (status == PARSE_ERROR)
        {
            parser_set_error(parser, "Expected compound command (e.g., { ... }) after function declaration");
        }
        g_node_destroy(&node);
        g_node_destroy(&fname);
        g_node_destroy(&lparen);
        g_node_destroy(&rparen);
        return status;
    }

    /* Try optional redirect_list */
    gnode_t *redirects = NULL;
    parse_status_t redir_status = gparse_redirect_list(parser, &redirects);

    gnode_t *body = NULL;
    if (redir_status == PARSE_OK)
    {
        /* Create G_FUNCTION_BODY wrapper with compound_command and redirect_list */
        body = g_node_create(G_FUNCTION_BODY);
        body->data.multi.a = compound;
        body->data.multi.b = redirects;
    }
    else
    {
        /* No redirections - use compound_command directly */
        body = compound;
    }

    node->data.multi.a = fname;
    node->data.multi.b = lparen;
    node->data.multi.c = rparen;
    node->data.multi.d = body;

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * brace_group      : Lbrace compound_list Rbrace
 * ============================================================================
 */
parse_status_t gparse_brace_group(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    if (parser_current_token_type(parser) != TOKEN_LBRACE)
        return PARSE_ERROR;

    /* Make sure that the next '}' has been promoted to a TOKEN_RBRACE */
    int offset = 1;
    while (true)
    {
        parser_token_info_t info = parser_peek_token_info(parser, offset);
        if (!info.valid)
            return PARSE_INCOMPLETE;
        token_type_t t = token_get_type(info.token);
        if (t == TOKEN_EOF)
            return PARSE_INCOMPLETE;
        if ((t == TOKEN_WORD && parser_token_try_promote_to_rbrace(parser, info.offset)) || (t == TOKEN_RBRACE))
            break;
        offset++;
    }
    gnode_t *node = g_node_create(G_BRACE_GROUP);

    /* '{' */
    gnode_t *lbrace = g_node_create(G_WORD_NODE);
    lbrace->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* compound_list (optional for empty braces) */
    gnode_t *list = NULL;
    parse_status_t status = gparse_compound_list(parser, &list);

    /* If compound_list fails and we're at '}', allow empty brace group */
    if (status != PARSE_OK)
    {
        if (parser_current_token_type(parser) == TOKEN_RBRACE)
        {
            /* Empty brace group { } */
            list = NULL;
        }
        else
        {
            g_node_destroy(&node);
            g_node_destroy(&lbrace);
            return status;
        }
    }

    /* '}' */
    if (parser_current_token_type(parser) != TOKEN_RBRACE)
    {
        /* Check if we're at EOF - need more input */
        if (parser_current_token_type(parser) == TOKEN_EOF)
        {
            g_node_destroy(&node);
            g_node_destroy(&lbrace);
            g_node_destroy(&list);
            return PARSE_INCOMPLETE;
        }
        
        parser_set_error(parser, "Expected '}' to close brace group");
        g_node_destroy(&node);
        g_node_destroy(&lbrace);
        g_node_destroy(&list);
        return PARSE_ERROR;
    }

    gnode_t *rbrace = g_node_create(G_WORD_NODE);
    rbrace->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    node->data.multi.a = lbrace;
    node->data.multi.b = list;
    node->data.multi.c = rbrace;

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * do_group         : Do compound_list Done
 * ============================================================================
 */
parse_status_t gparse_do_group(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    /* Try promoting 'do' keyword */
    parser_token_info_t info_do = parser_current_token_info(parser);
    if (info_do.valid && token_get_type(info_do.token) == TOKEN_WORD)
    {
        parser_token_try_promote_to_do(parser, info_do.offset);
    }

    if (parser_current_token_type(parser) != TOKEN_DO)
        return PARSE_ERROR;

    gnode_t *node = g_node_create(G_DO_GROUP);

    /* 'do' */
    gnode_t *do_tok = g_node_create(G_WORD_NODE);
    do_tok->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* compound_list */
    gnode_t *list = NULL;
    parse_status_t status = gparse_compound_list(parser, &list);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        g_node_destroy(&do_tok);
        return status;
    }

    /* Try promoting 'done' keyword */
    parser_token_info_t info_done = parser_current_token_info(parser);
    if (info_done.valid && token_get_type(info_done.token) == TOKEN_WORD)
    {
        parser_token_try_promote_to_done(parser, info_done.offset);
    }

    /* 'done' */
    if (parser_current_token_type(parser) != TOKEN_DONE)
    {
        /* Check if we're at EOF - need more input */
        if (parser_current_token_type(parser) == TOKEN_EOF)
        {
            g_node_destroy(&node);
            g_node_destroy(&do_tok);
            g_node_destroy(&list);
            return PARSE_INCOMPLETE;
        }
        
        parser_set_error(parser, "Expected 'done' to close do group");
        g_node_destroy(&node);
        g_node_destroy(&do_tok);
        g_node_destroy(&list);
        return PARSE_ERROR;
    }

    gnode_t *done_tok = g_node_create(G_WORD_NODE);
    done_tok->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    node->data.multi.a = do_tok;
    node->data.multi.b = list;
    node->data.multi.c = done_tok;

    *out_node = node;
    return PARSE_OK;
}

static bool is_redirect_token(token_type_t t)
{
    return (t == TOKEN_IO_NUMBER || t == TOKEN_IO_LOCATION || t == TOKEN_LESS ||
            t == TOKEN_LESSAND || t == TOKEN_GREATER || t == TOKEN_GREATAND || t == TOKEN_DGREAT ||
            t == TOKEN_LESSGREAT || t == TOKEN_CLOBBER || t == TOKEN_DLESS || t == TOKEN_DLESSDASH);
}

/* ============================================================================
 * Helper: Match G_IO_HERE nodes with their TOKEN_END_OF_HEREDOC tokens
 *
 * This scans a G_SIMPLE_COMMAND node looking for G_IO_HERE redirections
 * and matches each one with the next TOKEN_END_OF_HEREDOC in the token stream.
 * This is necessary because heredoc bodies appear after the entire command line.
 * ============================================================================
 */
static parse_status_t match_heredocs_in_simple_command(parser_t *parser, gnode_t *cmd)
{
    Expects_not_null(parser);
    Expects_not_null(cmd);

    if (cmd->type != G_SIMPLE_COMMAND)
        return PARSE_OK;

    gnode_list_t *list = cmd->data.list;
    if (!list)
        return PARSE_OK;

    for (int i = 0; i < list->size; i++)
    {
        gnode_t *item = list->nodes[i];
        if (!item)
            continue;

        gnode_t *redir = NULL;

        /* Check for redirect in G_CMD_PREFIX */
        if (item->type == G_CMD_PREFIX && item->data.child &&
            item->data.child->type == G_IO_REDIRECT)
        {
            redir = item->data.child;
        }
        /* Check for redirect in G_CMD_SUFFIX (which is a list) */
        else if (item->type == G_CMD_SUFFIX && item->data.list)
        {
            gnode_list_t *suffix_list = item->data.list;
            for (int j = 0; j < suffix_list->size; j++)
            {
                gnode_t *suffix_item = suffix_list->nodes[j];
                if (suffix_item && suffix_item->type == G_IO_REDIRECT)
                {
                    /* Process this redirect */
                    gnode_t *target = suffix_item->data.multi.c;
                    if (target && target->type == G_IO_HERE && target->data.io_here.tok == NULL)
                    {
                        /* Skip newlines to find TOKEN_END_OF_HEREDOC */
                        while (parser_current_token_type(parser) == TOKEN_NEWLINE)
                            parser_advance(parser);

                        if (parser_current_token_type(parser) != TOKEN_END_OF_HEREDOC)
                        {
                            parser_set_error(parser, "Expected heredoc content for delimiter '%s'",
                                             string_cstr(target->data.io_here.here_end));
                            return PARSE_ERROR;
                        }

                        target->data.io_here.tok = token_clone(parser_current_token(parser));
                        parser_advance(parser);
                    }
                }
            }
            continue;
        }

        if (!redir)
            continue;

        /* redir is a G_IO_REDIRECT - check if it contains G_IO_HERE */
        gnode_t *target = redir->data.multi.c;
        if (!target || target->type != G_IO_HERE)
            continue;

        if (target->data.io_here.tok != NULL)
            continue; /* Already matched */

        /* Skip newlines to find TOKEN_END_OF_HEREDOC */
        while (parser_current_token_type(parser) == TOKEN_NEWLINE)
            parser_advance(parser);

        if (parser_current_token_type(parser) != TOKEN_END_OF_HEREDOC)
        {
            parser_set_error(parser, "Expected heredoc content for delimiter '%s'",
                             string_cstr(target->data.io_here.here_end));
            return PARSE_ERROR;
        }

        target->data.io_here.tok = token_clone(parser_current_token(parser));
        parser_advance(parser);
    }


    return PARSE_OK;
}

/* ============================================================================
 * Helper function: Check if the current word token is a reserved word that
 * should stop simple command parsing.
 * 
 * Reserved words like "then", "fi", "do", "done", "else", "elif", "esac" should
 * not be consumed as command names or arguments when they appear in contexts
 * where they might be closing/continuing keywords of compound constructs.
 * ============================================================================
 */
static bool is_terminating_reserved_word(const token_t *tok)
{
    if (!tok || token_get_type(tok) != TOKEN_WORD)
        return false;
    
    /* A reserved word must be a single literal part and not quoted */
    if (token_was_quoted(tok) || token_part_count(tok) != 1)
        return false;
    
    const part_t *first_part = tok->parts->parts[0];
    if (part_get_type(first_part) != PART_LITERAL)
        return false;
    
    const char *word = string_cstr(first_part->text);
    
    /* These are the reserved words that can terminate or continue
     * compound commands and should not be consumed as simple command words */
    return (strcmp(word, "then") == 0 ||
            strcmp(word, "fi") == 0 ||
            strcmp(word, "do") == 0 ||
            strcmp(word, "done") == 0 ||
            strcmp(word, "else") == 0 ||
            strcmp(word, "elif") == 0 ||
            strcmp(word, "esac") == 0);
}

/* ============================================================================
 * simple_command   : cmd_prefix cmd_word cmd_suffix
 *                  | cmd_prefix cmd_word
 *                  | cmd_prefix
 *                  | cmd_name cmd_suffix
 *                  | cmd_name
 * ============================================================================
 */
parse_status_t gparse_simple_command(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    gnode_t *node = g_node_create(G_SIMPLE_COMMAND);
    node->data.list = g_list_create();

    bool has_cmd_prefix = false;
    bool has_cmd_name = false;

    /* Try to parse cmd_prefix (redirections and assignments) */
    while (true)
    {
        token_type_t t = parser_current_token_type(parser);

        /* Check for redirection */
        if (t == TOKEN_IO_NUMBER || t == TOKEN_IO_LOCATION || t == TOKEN_LESS ||
            t == TOKEN_LESSAND || t == TOKEN_GREATER || t == TOKEN_GREATAND || t == TOKEN_DGREAT ||
            t == TOKEN_LESSGREAT || t == TOKEN_CLOBBER || t == TOKEN_DLESS || t == TOKEN_DLESSDASH)
        {
            gnode_t *redir = NULL;
            parse_status_t status = gparse_io_redirect(parser, &redir);

            if (status != PARSE_OK)
                break;

            gnode_t *prefix = g_node_create(G_CMD_PREFIX);
            prefix->data.child = redir;
            g_list_append(node->data.list, prefix);
            has_cmd_prefix = true;
            continue;
        }

        /* Check for assignment */
        if (t == TOKEN_ASSIGNMENT_WORD)
        {
            gnode_t *assign = g_node_create(G_ASSIGNMENT_WORD);
            assign->data.token = token_clone(parser_current_token(parser));
            parser_advance(parser);

            gnode_t *prefix = g_node_create(G_CMD_PREFIX);
            prefix->data.child = assign;
            g_list_append(node->data.list, prefix);
            has_cmd_prefix = true;
            continue;
        }

        break;
    }

    /* Try to parse cmd_name (WORD) - but not if it's a reserved word */
    if (parser_current_token_type(parser) == TOKEN_WORD &&
        !is_terminating_reserved_word(parser_current_token(parser)))
    {
        gnode_t *name = g_node_create(G_CMD_NAME);
        name->data.token = token_clone(parser_current_token(parser));
        g_list_append(node->data.list, name);
        parser_advance(parser);
        has_cmd_name = true;
    }

    /* Create a single suffix node */
    gnode_t *suffix = g_node_create(G_CMD_SUFFIX);
    suffix->data.list = g_list_create();
    bool has_suffix = false;

    while (true)
    {
        token_type_t t = parser_current_token_type(parser);

        if (is_redirect_token(t))
        {
            gnode_t *redir = NULL;
            if (gparse_io_redirect(parser, &redir) != PARSE_OK)
                break;

            g_list_append(suffix->data.list, redir);
            has_suffix = true;
            continue;
        }

        /* Parse WORD as cmd_suffix - but not if it's a reserved word */
        if (t == TOKEN_WORD && !is_terminating_reserved_word(parser_current_token(parser)))
        {
            gnode_t *word = g_node_create(G_CMD_WORD);
            word->data.token = token_clone(parser_current_token(parser));
            parser_advance(parser);

            g_list_append(suffix->data.list, word);
            has_suffix = true;
            continue;
        }

        break;
    }

    if (has_suffix)
        g_list_append(node->data.list, suffix);
    else
        g_node_destroy(&suffix);

    /* Must have at least cmd_prefix or cmd_name */
    if (!has_cmd_prefix && !has_cmd_name)
    {
        g_node_destroy(&node);
        
        /* Check if we're at EOF - need more input */
        if (parser_current_token_type(parser) == TOKEN_EOF)
        {
            return PARSE_INCOMPLETE;
        }
        
        return PARSE_ERROR;
    }

    /* Match any heredocs with their TOKEN_END_OF_HEREDOC content tokens */
    parse_status_t status = match_heredocs_in_simple_command(parser, node);
    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        return status;
    }

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * redirect_list    : redirect_list io_redirect
 *                  |                io_redirect
 *
 * After collecting all redirections, this function matches any G_IO_HERE
 * nodes with their corresponding TOKEN_END_OF_HEREDOC tokens. This is
 * necessary because multiple heredocs can appear on the same line
 * (e.g., "cat <<A <<-B"), and all their bodies appear after the command line.
 * ============================================================================
 */
parse_status_t gparse_redirect_list(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    gnode_t *node = g_node_create(G_REDIRECT_LIST);
    node->data.list = g_list_create();

    /* Parse first io_redirect */
    gnode_t *redir = NULL;
    parse_status_t status = gparse_io_redirect(parser, &redir);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        return status;
    }

    g_list_append(node->data.list, redir);

    /* Loop: io_redirect */
    while (true)
    {
        gnode_t *next = NULL;
        status = gparse_io_redirect(parser, &next);

        if (status != PARSE_OK)
            break;

        g_list_append(node->data.list, next);
    }

    /* Now match any G_IO_HERE nodes with their TOKEN_END_OF_HEREDOC tokens.
     * The heredoc bodies appear in the same order as the << operators. */
    for (int i = 0; i < node->data.list->size; i++)
    {
        gnode_t *redirect = node->data.list->nodes[i];
        if (redirect->type != G_IO_REDIRECT)
            continue;

        /* G_IO_REDIRECT has the actual io_file or io_here in multi.c */
        gnode_t *target = redirect->data.multi.c;
        if (!target || target->type != G_IO_HERE)
            continue;

        /* Skip any newlines to find the TOKEN_END_OF_HEREDOC */
        while (parser_current_token_type(parser) == TOKEN_NEWLINE)
            parser_advance(parser);

        if (parser_current_token_type(parser) != TOKEN_END_OF_HEREDOC)
        {
            parser_set_error(parser, "Expected heredoc content for delimiter '%s'",
                             string_cstr(target->data.io_here.here_end));
            g_node_destroy(&node);
            return PARSE_ERROR;
        }

        /* Clone the TOKEN_END_OF_HEREDOC and store it in the io_here node */
        const token_t *heredoc_tok = parser_current_token(parser);
        target->data.io_here.tok = token_clone(heredoc_tok);
        parser_advance(parser);
    }

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * io_redirect      : io_file
 *                  | IO_NUMBER io_file
 *                  | IO_LOCATION io_file
 *                  | io_here
 *                  | IO_NUMBER io_here
 *                  | IO_LOCATION io_here
 * ============================================================================
 */
parse_status_t gparse_io_redirect(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    gnode_t *io_number = NULL;
    gnode_t *io_location = NULL;

    token_type_t t = parser_current_token_type(parser);

    /* Optional IO_NUMBER or IO_LOCATION */
    if (t == TOKEN_IO_NUMBER)
    {
        io_number = g_node_create(G_IO_NUMBER_NODE);
        io_number->data.token = token_clone(parser_current_token(parser));
        parser_advance(parser);
        t = parser_current_token_type(parser);
    }
    else if (t == TOKEN_IO_LOCATION)
    {
        io_location = g_node_create(G_IO_LOCATION_NODE);
        io_location->data.token = token_clone(parser_current_token(parser));
        parser_advance(parser);
        t = parser_current_token_type(parser);
    }

    /* Try io_file */
    gnode_t *file = NULL;
    parse_status_t status = gparse_io_file(parser, &file);

    if (status == PARSE_OK)
    {
        gnode_t *node = g_node_create(G_IO_REDIRECT);
        node->data.multi.a = io_number;
        node->data.multi.b = io_location;
        node->data.multi.c = file;
        *out_node = node;
        return PARSE_OK;
    }

    /* Try io_here */
    gnode_t *here = NULL;
    status = gparse_io_here(parser, &here);

    if (status == PARSE_OK)
    {
        gnode_t *node = g_node_create(G_IO_REDIRECT);
        node->data.multi.a = io_number;
        node->data.multi.b = io_location;
        node->data.multi.c = here;
        *out_node = node;
        return PARSE_OK;
    }

    /* No redirect found */
    if (io_number)
        g_node_destroy(&io_number);
    if (io_location)
        g_node_destroy(&io_location);

    return PARSE_ERROR;
}

/* ============================================================================
 * io_file          : '<'       filename
 *                  | LESSAND   filename
 *                  | '>'       filename
 *                  | GREATAND  filename
 *                  | DGREAT    filename
 *                  | LESSGREAT filename
 *                  | CLOBBER   filename
 * ============================================================================
 */
parse_status_t gparse_io_file(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    token_type_t t = parser_current_token_type(parser);

    switch (t)
    {
    case TOKEN_LESS:
    case TOKEN_LESSAND:
    case TOKEN_GREATER:
    case TOKEN_GREATAND:
    case TOKEN_DGREAT:
    case TOKEN_LESSGREAT:
    case TOKEN_CLOBBER:
        break;
    default:
        return PARSE_ERROR;
    }

    gnode_t *node = g_node_create(G_IO_FILE);

    /* operator token */
    gnode_t *op = g_node_create(G_WORD_NODE);
    op->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    /* filename */
    gnode_t *fname = NULL;
    parse_status_t status = gparse_filename(parser, &fname);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        g_node_destroy(&op);
        return status;
    }

    node->data.multi.a = op;
    node->data.multi.b = fname;

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * filename         : WORD                      (Apply rule 2)
 * ============================================================================
 */
parse_status_t gparse_filename(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    if (parser_current_token_type(parser) != TOKEN_WORD)
        return PARSE_ERROR;

    gnode_t *node = g_node_create(G_FILENAME);
    node->data.token = token_clone(parser_current_token(parser));
    parser_advance(parser);

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * io_here          : DLESS     here_end
 *                  | DLESSDASH here_end
 *
 * NOTE: This only parses the operator and delimiter. The heredoc content
 * (TOKEN_END_OF_HEREDOC) is matched later in gparse_redirect_list() after
 * all redirections on the line have been parsed.
 * ============================================================================
 */
parse_status_t gparse_io_here(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    token_type_t t = parser_current_token_type(parser);

    if (t != TOKEN_DLESS && t != TOKEN_DLESSDASH)
        return PARSE_ERROR;

    gnode_t *node = g_node_create(G_IO_HERE);

    /* operator token */
    node->data.io_here.op = t;
    parser_advance(parser);

    /* here_end (delimiter) */
    if (parser_current_token_type(parser) != TOKEN_WORD)
    {
        g_node_destroy(&node);
        return PARSE_ERROR;
    }
    token_t *here_end_tok = token_clone(parser_current_token(parser));
    node->data.io_here.here_end = token_get_all_text(here_end_tok);
    node->data.io_here.tok = NULL; /* Will be filled in by gparse_redirect_list */
    parser_advance(parser);

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * here_end         : WORD                      (Apply rule 3)
 * ============================================================================
 */
parse_status_t gparse_here_end(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    if (parser_current_token_type(parser) != TOKEN_WORD)
        return PARSE_ERROR;

    gnode_t *node = g_node_create(G_HERE_END);
    token_t *here_end_tok = token_clone(parser_current_token(parser));
    node->data.string = token_get_all_text(here_end_tok);
    parser_advance(parser);

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * separator_op     : '&'
 *                  | ';'
 * ============================================================================
 */
parse_status_t gparse_separator_op(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    token_type_t t = parser_current_token_type(parser);

    if (t != TOKEN_AMPER && t != TOKEN_SEMI)
        return PARSE_ERROR; /* Not a separator_op */

    gnode_t *node = g_node_create(G_SEPARATOR_OP);
    node->data.token = token_clone(parser_current_token(parser));

    parser_advance(parser);

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * separator : separator_op linebreak | newline_list;
 * ============================================================================
 */
parse_status_t gparse_separator(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    token_type_t t = parser_current_token_type(parser);

    /* Case 1: separator_op linebreak */
    if (t == TOKEN_AMPER || t == TOKEN_SEMI)
    {
        gnode_t *sep = g_node_create(G_SEPARATOR);

        /* separator_op */
        gnode_t *op = g_node_create(G_SEPARATOR_OP);
        op->data.token = token_clone(parser_current_token(parser));
        parser_advance(parser);

        /* linebreak */
        parser_skip_newlines(parser);

        sep->data.child = op;
        *out_node = sep;
        return PARSE_OK;
    }

    /* Case 2: newline_list */
    if (t == TOKEN_NEWLINE)
    {
        gnode_t *sep = g_node_create(G_SEPARATOR);

        gnode_t *nl = g_node_create(G_NEWLINE_LIST);
        nl->data.list = g_list_create();

        while (parser_current_token_type(parser) == TOKEN_NEWLINE)
        {
            gnode_t *tok = g_node_create(G_WORD_NODE);
            tok->data.token = token_clone(parser_current_token(parser));
            parser_advance(parser);
            g_list_append(nl->data.list, tok);
        }

        sep->data.child = nl;
        *out_node = sep;
        return PARSE_OK;
    }

    return PARSE_ERROR;
}

/* ============================================================================
 * Test functions
 * ============================================================================ */

extern lex_status_t lex_cstr_to_tokens(const char *input, token_list_t *out_tokens);

parser_t *parser_create_from_string(const char* input)
{
    Expects_not_null(input);
    token_list_t *tokens = token_list_create();
    lex_status_t status = lex_cstr_to_tokens(input, tokens);
    if (status != LEX_OK)
    {
        token_list_destroy(&tokens);
        return NULL;
    }
    parser_t *parser = parser_create_with_tokens_move(&tokens);
    return parser;
}

parse_status_t parser_string_to_gnodes(const char* input, gnode_t** out_node)
{
    Expects_not_null(input);
    Expects_not_null(out_node);
    parser_t* parser = parser_create_from_string(input);
    if (!parser)
    {
        return PARSE_ERROR;
    }
    parse_status_t status = parser_parse_program(parser, out_node);
    if (status != PARSE_OK)
    {
        parser_destroy(&parser);
        return status;
    }
    /* Ensure we've consumed all tokens */
    if (parser_current_token_type(parser) != TOKEN_EOF)
    {
        parser_set_error(parser, "Unexpected tokens after end of input");
        g_node_destroy(out_node);
        *out_node = NULL;
        parser_destroy(&parser);
        return PARSE_ERROR;
    }
    parser_destroy(&parser);
    return PARSE_OK;
}

gnode_t *parser_parse_string(const char *input)
{
    Expects_not_null(input);
    gnode_t *root = NULL;
    parse_status_t status = parser_string_to_gnodes(input, &root);
    if (status != PARSE_OK)
    {
        return NULL;
    }
    return root;
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "gparse.h"
#include "logging.h"

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
    gnode_t *program = g_node_create(G_PROGRAM);

    /* Skip leading newlines (linebreak) */
    parser_skip_newlines(parser);

    /* Try to parse complete_commands */
    gnode_t *commands = NULL;
    parse_status_t status = gparse_complete_commands(parser, &commands);

    if (status == PARSE_OK)
    {
        /* Attach complete_commands as the child of program */
        program->data.child = commands;
    }
    else if (status == PARSE_EMPTY)
    {
        /* program → linebreak (empty program) */
        program->data.child = NULL;
    }
    else
    {
        /* Error or incomplete */
        g_node_destroy(&program);
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
    gnode_t *list = g_node_create(G_COMPLETE_COMMANDS);
    list->data.list = g_list_create();

    /* Parse the first complete_command */
    gnode_t *cmd = NULL;
    parse_status_t status = gparse_complete_command(parser, &cmd);

    if (status != PARSE_OK)
    {
        g_node_destroy(&list);
        return status;
    }

    g_list_append(list->data.list, cmd);

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

        if (status != PARSE_OK)
        {
            /* If incomplete or error, stop and return */
            g_node_destroy(&list);
            return status;
        }

        g_list_append(list->data.list, next);
    }

    *out_node = list;
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
        gnode_t *sep = NULL;
        status = gparse_separator_op(parser, &sep);

        if (status != PARSE_OK)
            break; /* No separator_op → end of list */

        /* Parse next and_or */
        gnode_t *next = NULL;
        status = gparse_and_or(parser, &next);

        if (status != PARSE_OK)
        {
            g_node_destroy(&sep);
            g_node_destroy(&list);
            return status;
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
        token_type_t t = parser_current_token_type(parser);

        if (t != TOKEN_AND_IF && t != TOKEN_OR_IF)
            break;

        /* Create operator node */
        gnode_t *op = g_node_create(G_AND_OR);
        op->data.token = parser_current_token(parser);

        parser_advance(parser);

        /* Skip linebreak */
        parser_skip_newlines(parser);

        /* Parse right-hand pipeline */
        gnode_t *right = NULL;
        status = gparse_pipeline(parser, &right);

        if (status != PARSE_OK)
        {
            g_node_destroy(&left);
            g_node_destroy(&op);
            return status;
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

    gnode_t *node = g_node_create(G_PIPELINE);
    node->data.list = g_list_create();

    token_type_t t = parser_current_token_type(parser);

    /* Optional Bang prefix */
    if (t == TOKEN_BANG)
    {
        gnode_t *bang_node = g_node_create(G_WORD_NODE); /* reuse token wrapper */
        bang_node->data.token = parser_current_token(parser);
        parser_advance(parser);
        g_list_append(node->data.list, bang_node);
    }

    /* Mandatory pipe_sequence */
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

    gnode_t *seq = g_node_create(G_PIPE_SEQUENCE);
    seq->data.list = g_list_create();

    /* First command */
    gnode_t *cmd = NULL;
    parse_status_t status = gparse_command(parser, &cmd);

    if (status != PARSE_OK)
    {
        g_node_destroy(&seq);
        return status;
    }

    g_list_append(seq->data.list, cmd);

    /* Loop: '|' linebreak command */
    while (parser_current_token_type(parser) == TOKEN_PIPE)
    {
        /* Wrap the '|' token */
        gnode_t *pipe_tok = g_node_create(G_WORD_NODE); /* token wrapper */
        pipe_tok->data.token = parser_current_token(parser);
        parser_advance(parser);

        g_list_append(seq->data.list, pipe_tok);

        /* linebreak */
        parser_skip_newlines(parser);

        /* next command */
        gnode_t *next_cmd = NULL;
        status = gparse_command(parser, &next_cmd);

        if (status != PARSE_OK)
        {
            g_node_destroy(&seq);
            return status;
        }

        g_list_append(seq->data.list, next_cmd);
    }

    *out_node = seq;
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

    token_t *tok0 = parser_current_token(parser);
    token_t *tok1 = parser_peek_token(parser, 1);
    token_t *tok2 = parser_peek_token(parser, 2);

    token_type_t t0 = token_get_type(tok0);
    token_type_t t1 = tok1 ? token_get_type(tok1) : TOKEN_INVALID;
    token_type_t t2 = tok2 ? token_get_type(tok2) : TOKEN_INVALID;

    /* ------------------------------------------------------------
     * 1. FUNCTION DEFINITION
     * ------------------------------------------------------------ */
    if (t0 == TOKEN_NAME && t1 == TOKEN_LPAREN && t2 == TOKEN_RPAREN)
    {
        return gparse_function_definition(parser, out_node);
    }

    /* ------------------------------------------------------------
     * 2. COMPOUND COMMAND
     * ------------------------------------------------------------ */
    switch (t0)
    {
    case TOKEN_LBRACE:
    case TOKEN_LPAREN:
    case TOKEN_IF:
    case TOKEN_WHILE:
    case TOKEN_UNTIL:
    case TOKEN_FOR:
    case TOKEN_CASE:
        return gparse_compound_command(parser, out_node);
    }

    /* ------------------------------------------------------------
     * 3. SIMPLE COMMAND (fallback)
     * ------------------------------------------------------------ */
    return gparse_simple_command(parser, out_node);
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

    token_type_t t = parser_current_token_type(parser);

    switch (t)
    {
    case TOKEN_LBRACE:
        return gparse_brace_group(parser, out_node);

    case TOKEN_LPAREN:
        return gparse_subshell(parser, out_node);

    case TOKEN_FOR:
        return gparse_for_clause(parser, out_node);

    case TOKEN_CASE:
        return gparse_case_clause(parser, out_node);

    case TOKEN_IF:
        return gparse_if_clause(parser, out_node);

    case TOKEN_WHILE:
        return gparse_while_clause(parser, out_node);

    case TOKEN_UNTIL:
        return gparse_until_clause(parser, out_node);

    default:
        parser_set_error(parser, "Expected compound command");
        return PARSE_ERROR;
    }
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

    /* Expect '(' */
    if (!parser_accept(parser, TOKEN_LPAREN))
    {
        parser_set_error(parser, "Expected '(' to start subshell");
        return PARSE_ERROR;
    }

    /* Parse compound_list */
    gnode_t *clist = NULL;
    parse_status_t status = gparse_compound_list(parser, &clist);

    if (status != PARSE_OK)
        return status;

    /* Expect ')' */
    if (!parser_accept(parser, TOKEN_RPAREN))
    {
        g_node_destroy(&clist);
        parser_set_error(parser, "Expected ')' to end subshell");
        return PARSE_ERROR;
    }

    /* Wrap into G_SUBSHELL node */
    gnode_t *node = g_node_create(G_SUBSHELL);
    node->data.child = clist;

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

    /* linebreak */
    parser_skip_newlines(parser);

    /* term */
    gnode_t *term = NULL;
    parse_status_t status = gparse_term(parser, &term);

    if (status != PARSE_OK)
        return status;

    gnode_t *clist = g_node_create(G_COMPOUND_LIST);
    clist->data.list = g_list_create();
    g_list_append(clist->data.list, term);

    /* Optional separator */
    gnode_t *sep = NULL;
    status = gparse_separator(parser, &sep);

    if (status == PARSE_OK)
        g_list_append(clist->data.list, sep);

    *out_node = clist;
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

    gnode_t *term = g_node_create(G_TERM);
    term->data.list = g_list_create();

    /* First and_or */
    gnode_t *first = NULL;
    parse_status_t status = gparse_and_or(parser, &first);

    if (status != PARSE_OK)
    {
        g_node_destroy(&term);
        return status;
    }

    g_list_append(term->data.list, first);

    /* Loop: separator and_or */
    while (true)
    {
        gnode_t *sep = NULL;
        status = gparse_separator(parser, &sep);

        if (status != PARSE_OK)
            break;

        g_list_append(term->data.list, sep);

        gnode_t *next = NULL;
        status = gparse_and_or(parser, &next);

        if (status != PARSE_OK)
        {
            g_node_destroy(&term);
            return status;
        }

        g_list_append(term->data.list, next);
    }

    *out_node = term;
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

    /* ------------------------------------------------------------
     * Expect 'for'
     * ------------------------------------------------------------ */
    if (!parser_accept(parser, TOKEN_FOR))
    {
        parser_set_error(parser, "Expected 'for'");
        return PARSE_ERROR;
    }

    /* ------------------------------------------------------------
     * Expect NAME
     * ------------------------------------------------------------ */
    if (parser_current_token_type(parser) != TOKEN_NAME)
    {
        parser_set_error(parser, "Expected name after 'for'");
        return PARSE_ERROR;
    }

    gnode_t *name = g_node_create(G_NAME_NODE);
    name->data.token = parser_current_token(parser);
    parser_advance(parser);

    /* ------------------------------------------------------------
     * linebreak
     * ------------------------------------------------------------ */
    parser_skip_newlines(parser);

    /* ------------------------------------------------------------
     * Optional: 'in' wordlist separator
     * ------------------------------------------------------------ */
    gnode_t *wordlist = NULL;
    parse_status_t status = gparse_in_clause(parser, &wordlist);

    if (status != PARSE_OK && status != PARSE_EMPTY)
    {
        g_node_destroy(&name);
        return status;
    }

    /* ------------------------------------------------------------
     * do_group
     * ------------------------------------------------------------ */
    gnode_t *do_group = NULL;
    status = gparse_do_group(parser, &do_group);

    if (status != PARSE_OK)
    {
        g_node_destroy(&name);
        g_node_destroy(&wordlist);
        return status;
    }

    /* ------------------------------------------------------------
     * Build G_FOR_CLAUSE
     * ------------------------------------------------------------ */
    gnode_t *node = g_node_create(G_FOR_CLAUSE);
    node->data.multi.a = name;
    node->data.multi.b = wordlist; /* may be NULL */
    node->data.multi.c = do_group;

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * in               : In                       (Apply rule 6)
 *
 * Rule 6: When parsing 'in', if not found, a WORD will result.
 * ============================================================================
 */
parse_status_t gparse_in_clause(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    if (parser_current_token_type(parser) != TOKEN_IN)
        return PARSE_EMPTY;

    parser_advance(parser);

    /* wordlist */
    gnode_t *wlist = NULL;
    parse_status_t status = gparse_wordlist(parser, &wlist);

    if (status != PARSE_OK)
        return status;

    /* separator */
    gnode_t *sep = NULL;
    status = gparse_separator(parser, &sep);

    if (status != PARSE_OK)
    {
        g_node_destroy(&wlist);
        return status;
    }

    /* Wrap into a G_WORDLIST node */
    gnode_t *node = g_node_create(G_WORDLIST);
    node->data.list = wlist->data.list; /* steal list */
    g_node_destroy(&wlist);

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

    gnode_t *list = g_node_create(G_WORDLIST);
    list->data.list = g_list_create();

    while (parser_current_token_type(parser) == TOKEN_WORD)
    {
        gnode_t *word = g_node_create(G_WORD_NODE);
        word->data.token = parser_current_token(parser);
        parser_advance(parser);
        g_list_append(list->data.list, word);
    }

    *out_node = list;
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

    /* 'case' */
    if (!parser_accept(parser, TOKEN_CASE))
    {
        parser_set_error(parser, "Expected 'case'");
        return PARSE_ERROR;
    }

    /* WORD (the subject) */
    if (parser_current_token_type(parser) != TOKEN_WORD)
    {
        parser_set_error(parser, "Expected WORD after 'case'");
        return PARSE_ERROR;
    }

    gnode_t *subject = g_node_create(G_WORD_NODE);
    subject->data.token = parser_current_token(parser);
    parser_advance(parser);

    /* linebreak */
    parser_skip_newlines(parser);

    /* 'in' */
    if (!parser_accept(parser, TOKEN_IN))
    {
        g_node_destroy(&subject);
        parser_set_error(parser, "Expected 'in' after case subject");
        return PARSE_ERROR;
    }

    /* linebreak */
    parser_skip_newlines(parser);

    /* Try case_list */
    gnode_t *list = NULL;
    parse_status_t status = gparse_case_list(parser, &list);

    if (status != PARSE_OK)
    {
        /* Try case_list_ns */
        status = gparse_case_list_ns(parser, &list);

        if (status != PARSE_OK)
        {
            /* No list at all → empty case */
            list = NULL;
        }
    }

    /* Expect 'esac' */
    if (!parser_accept(parser, TOKEN_ESAC))
    {
        g_node_destroy(&subject);
        g_node_destroy(&list);
        parser_set_error(parser, "Expected 'esac' to close case clause");
        return PARSE_ERROR;
    }

    /* Build G_CASE_CLAUSE */
    gnode_t *node = g_node_create(G_CASE_CLAUSE);
    node->data.multi.a = subject;
    node->data.multi.b = list;

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * case_list_ns     : case_list case_item_ns
 *                  |           case_item_ns
 * ============================================================================
 */
parse_status_t gparse_case_list_ns(parser_t *parser, gnode_t **out_node)
{
    *out_node = NULL;

    gnode_t *list = g_node_create(G_CASE_LIST_NS);
    list->data.list = g_list_create();

    while (true)
    {
        gnode_t *item = NULL;
        parse_status_t status = gparse_case_item_ns(parser, &item);

        if (status != PARSE_OK)
            break;

        g_list_append(list->data.list, item);
    }

    if (list->data.list->size == 0)
    {
        g_node_destroy(&list);
        return PARSE_ERROR;
    }

    *out_node = list;
    return PARSE_OK;
}

/* ============================================================================
 * case_list        : case_list case_item
 *                  |           case_item
 * ============================================================================
 */
parse_status_t gparse_case_list(parser_t *parser, gnode_t **out_node)
{
    *out_node = NULL;

    gnode_t *list = g_node_create(G_CASE_LIST);
    list->data.list = g_list_create();

    while (true)
    {
        gnode_t *item = NULL;
        parse_status_t status = gparse_case_item(parser, &item);

        if (status != PARSE_OK)
            break;

        g_list_append(list->data.list, item);
    }

    if (list->data.list->size == 0)
    {
        g_node_destroy(&list);
        return PARSE_ERROR;
    }

    *out_node = list;
    return PARSE_OK;
}

/* ============================================================================
 * case_item_ns     : pattern_list ')' linebreak
 *                  | pattern_list ')' compound_list
 * ============================================================================
 */
parse_status_t gparse_case_item_ns(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    /* pattern_list */
    gnode_t *patterns = NULL;
    parse_status_t status = gparse_pattern_list(parser, &patterns);
    if (status != PARSE_OK)
        return status;

    /* ')' */
    if (!parser_accept(parser, TOKEN_RPAREN))
    {
        g_node_destroy(&patterns);
        return PARSE_ERROR;
    }

    /* linebreak */
    parser_skip_newlines(parser);

    /* optional compound_list */
    gnode_t *body = NULL;
    status = gparse_compound_list(parser, &body);
    if (status != PARSE_OK)
        body = NULL;

    /* IMPORTANT: NO terminator allowed here */
    if (parser_current_token_type(parser) == TOKEN_DSEMI ||
        parser_current_token_type(parser) == TOKEN_SEMI_AND)
    {
        g_node_destroy(&patterns);
        g_node_destroy(&body);
        return PARSE_ERROR; /* POSIX forbids terminators here */
    }

    gnode_t *node = g_node_create(G_CASE_ITEM_NS);
    node->data.multi.a = patterns;
    node->data.multi.b = body;

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * case_item        : pattern_list ')' linebreak     DSEMI linebreak
 *                  | pattern_list ')' compound_list DSEMI linebreak
 *                  | pattern_list ')' linebreak     SEMI_AND linebreak
 *                  | pattern_list ')' compound_list SEMI_AND linebreak
 * ============================================================================
 */
parse_status_t gparse_case_item(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    /* ------------------------------------------------------------
     * pattern_list
     * ------------------------------------------------------------ */
    gnode_t *patterns = NULL;
    parse_status_t status = gparse_pattern_list(parser, &patterns);

    if (status != PARSE_OK)
        return status;

    /* ------------------------------------------------------------
     * ')'
     * ------------------------------------------------------------ */
    if (!parser_accept(parser, TOKEN_RPAREN))
    {
        g_node_destroy(&patterns);
        return PARSE_ERROR;
    }

    /* linebreak */
    parser_skip_newlines(parser);

    /* ------------------------------------------------------------
     * compound_list
     * ------------------------------------------------------------ */
    gnode_t *body = NULL;
    status = gparse_compound_list(parser, &body);

    if (status != PARSE_OK)
    {
        g_node_destroy(&patterns);
        return status;
    }

    /* ------------------------------------------------------------
     * Optional terminator: DSEMI ( ;; ) or SEMI_AND ( ;& )
     * ------------------------------------------------------------ */
    gnode_t *terminator = NULL;

    if (parser_accept(parser, TOKEN_DSEMI) || parser_accept(parser, TOKEN_SEMI_AND))
    {
        terminator = g_node_create(G_WORD_NODE);
        terminator->data.token = parser_previous_token(parser);
        parser_skip_newlines(parser);
    }

    /* ------------------------------------------------------------
     * Build G_CASE_ITEM
     * ------------------------------------------------------------ */
    gnode_t *node = g_node_create(G_CASE_ITEM);
    node->data.multi.a = patterns;
    node->data.multi.b = body;
    node->data.multi.c = terminator; /* may be NULL */

    *out_node = node;
    return PARSE_OK;
}


/* ============================================================================
 * pattern_list     :                  WORD    (Apply rule 4)
 *                  | '(' WORD                 (Do not apply rule 4)
 *                  | pattern_list '|' WORD    (Do not apply rule 4)
 * 
 * Rule 4: When the TOKEN is exactly the reserved word esac, the token
 *         identifier for esac shall result. Otherwise, the token WORD shal
 *         be returned.
 * ============================================================================
 */
parse_status_t gparse_pattern_list(parser_t *parser, gnode_t **out_node)
{
    *out_node = NULL;

    if (parser_current_token_type(parser) != TOKEN_WORD)
        return PARSE_ERROR;

    gnode_t *list = g_node_create(G_PATTERN_LIST);
    list->data.list = g_list_create();

    /* First WORD */
    gnode_t *word = g_node_create(G_WORD_NODE);
    word->data.token = parser_current_token(parser);
    parser_advance(parser);
    g_list_append(list->data.list, word);

    /* Loop: '|' WORD */
    while (parser_accept(parser, TOKEN_PIPE))
    {
        if (parser_current_token_type(parser) != TOKEN_WORD)
        {
            g_node_destroy(&list);
            return PARSE_ERROR;
        }

        gnode_t *w = g_node_create(G_WORD_NODE);
        w->data.token = parser_current_token(parser);
        parser_advance(parser);
        g_list_append(list->data.list, w);
    }

    *out_node = list;
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

    /* ------------------------------------------------------------
     * Expect 'if'
     * ------------------------------------------------------------ */
    if (!parser_accept(parser, TOKEN_IF))
    {
        parser_set_error(parser, "Expected 'if'");
        return PARSE_ERROR;
    }

    /* ------------------------------------------------------------
     * Parse first compound_list (the condition)
     * ------------------------------------------------------------ */
    gnode_t *cond = NULL;
    parse_status_t status = gparse_compound_list(parser, &cond);

    if (status != PARSE_OK)
        return status;

    /* ------------------------------------------------------------
     * Expect 'then'
     * ------------------------------------------------------------ */
    if (!parser_accept(parser, TOKEN_THEN))
    {
        g_node_destroy(&cond);
        parser_set_error(parser, "Expected 'then' after if-condition");
        return PARSE_ERROR;
    }

    /* ------------------------------------------------------------
     * Parse second compound_list (the then-body)
     * ------------------------------------------------------------ */
    gnode_t *then_body = NULL;
    status = gparse_compound_list(parser, &then_body);

    if (status != PARSE_OK)
    {
        g_node_destroy(&cond);
        return status;
    }

    /* ------------------------------------------------------------
     * Optional else_part
     * ------------------------------------------------------------ */
    gnode_t *else_part = NULL;
    status = gparse_else_part(parser, &else_part);

    if (status != PARSE_OK && status != PARSE_EMPTY)
    {
        g_node_destroy(&cond);
        g_node_destroy(&then_body);
        return status;
    }

    /* ------------------------------------------------------------
     * Expect 'fi'
     * ------------------------------------------------------------ */
    if (!parser_accept(parser, TOKEN_FI))
    {
        g_node_destroy(&cond);
        g_node_destroy(&then_body);
        g_node_destroy(&else_part);
        parser_set_error(parser, "Expected 'fi' to close if-clause");
        return PARSE_ERROR;
    }

    /* ------------------------------------------------------------
     * Build G_IF_CLAUSE node
     *
     * multi.a = cond
     * multi.b = then_body
     * multi.c = else_part (may be NULL)
     * ------------------------------------------------------------ */
    gnode_t *node = g_node_create(G_IF_CLAUSE);
    node->data.multi.a = cond;
    node->data.multi.b = then_body;
    node->data.multi.c = else_part;

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * else_part        : Elif compound_list Then compound_list
 *                  | Elif compound_list Then compound_list else_part
 *                  | Else compound_list
 * ============================================================================
 */
parse_status_t gparse_else_part(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    token_type_t t = parser_current_token_type(parser);

    /* ------------------------------------------------------------
     * Case 1: 'else' compound_list
     * ------------------------------------------------------------ */
    if (t == TOKEN_ELSE)
    {
        parser_advance(parser);

        gnode_t *body = NULL;
        parse_status_t status = gparse_compound_list(parser, &body);

        if (status != PARSE_OK)
            return status;

        gnode_t *node = g_node_create(G_ELSE_PART);
        node->data.multi.a = body;

        *out_node = node;
        return PARSE_OK;
    }

    /* ------------------------------------------------------------
     * Case 2: 'elif' compound_list 'then' compound_list else_part
     * ------------------------------------------------------------ */
    if (t == TOKEN_ELIF)
    {
        parser_advance(parser);

        /* condition */
        gnode_t *cond = NULL;
        parse_status_t status = gparse_compound_list(parser, &cond);

        if (status != PARSE_OK)
            return status;

        /* then */
        if (!parser_accept(parser, TOKEN_THEN))
        {
            g_node_destroy(&cond);
            parser_set_error(parser, "Expected 'then' after elif-condition");
            return PARSE_ERROR;
        }

        /* then-body */
        gnode_t *then_body = NULL;
        status = gparse_compound_list(parser, &then_body);

        if (status != PARSE_OK)
        {
            g_node_destroy(&cond);
            return status;
        }

        /* recursive else_part */
        gnode_t *next_else = NULL;
        status = gparse_else_part(parser, &next_else);

        if (status != PARSE_OK && status != PARSE_EMPTY)
        {
            g_node_destroy(&cond);
            g_node_destroy(&then_body);
            return status;
        }

        gnode_t *node = g_node_create(G_ELSE_PART);
        node->data.multi.a = cond;
        node->data.multi.b = then_body;
        node->data.multi.c = next_else;

        *out_node = node;
        return PARSE_OK;
    }

    /* No else_part present */
    return PARSE_EMPTY;
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

    /* ------------------------------------------------------------
     * Expect 'while'
     * ------------------------------------------------------------ */
    if (!parser_accept(parser, TOKEN_WHILE))
    {
        parser_set_error(parser, "Expected 'while'");
        return PARSE_ERROR;
    }

    /* ------------------------------------------------------------
     * Parse condition compound_list
     * ------------------------------------------------------------ */
    gnode_t *cond = NULL;
    parse_status_t status = gparse_compound_list(parser, &cond);

    if (status != PARSE_OK)
        return status;

    /* ------------------------------------------------------------
     * Parse do_group
     * ------------------------------------------------------------ */
    gnode_t *do_group = NULL;
    status = gparse_do_group(parser, &do_group);

    if (status != PARSE_OK)
    {
        g_node_destroy(&cond);
        return status;
    }

    /* ------------------------------------------------------------
     * Build G_WHILE_CLAUSE
     * ------------------------------------------------------------ */
    gnode_t *node = g_node_create(G_WHILE_CLAUSE);
    node->data.multi.a = cond;
    node->data.multi.b = do_group;

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

    /* ------------------------------------------------------------
     * Expect 'until'
     * ------------------------------------------------------------ */
    if (!parser_accept(parser, TOKEN_UNTIL))
    {
        parser_set_error(parser, "Expected 'until'");
        return PARSE_ERROR;
    }

    /* ------------------------------------------------------------
     * Parse condition compound_list
     * ------------------------------------------------------------ */
    gnode_t *cond = NULL;
    parse_status_t status = gparse_compound_list(parser, &cond);

    if (status != PARSE_OK)
        return status;

    /* ------------------------------------------------------------
     * Parse do_group
     * ------------------------------------------------------------ */
    gnode_t *do_group = NULL;
    status = gparse_do_group(parser, &do_group);

    if (status != PARSE_OK)
    {
        g_node_destroy(&cond);
        return status;
    }

    /* ------------------------------------------------------------
     * Build G_UNTIL_CLAUSE
     * ------------------------------------------------------------ */
    gnode_t *node = g_node_create(G_UNTIL_CLAUSE);
    node->data.multi.a = cond;
    node->data.multi.b = do_group;

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

    /* ------------------------------------------------------------
     * fname : NAME
     * ------------------------------------------------------------ */
    token_t *tok_name = parser_current_token(parser);
    if (token_get_type(tok_name) != TOKEN_NAME)
    {
        parser_set_error(parser, "Expected function name");
        return PARSE_ERROR;
    }

    gnode_t *fname = g_node_create(G_FNAME);
    fname->data.token = tok_name;
    parser_advance(parser);

    /* ------------------------------------------------------------
     * Expect '('
     * ------------------------------------------------------------ */
    if (parser_current_token_type(parser) != TOKEN_LPAREN)
    {
        g_node_destroy(&fname);
        parser_set_error(parser, "Expected '(' after function name");
        return PARSE_ERROR;
    }

    gnode_t *lparen = g_node_create(G_WORD_NODE);
    lparen->data.token = parser_current_token(parser);
    parser_advance(parser);

    /* ------------------------------------------------------------
     * Expect ')'
     * ------------------------------------------------------------ */
    if (parser_current_token_type(parser) != TOKEN_RPAREN)
    {
        g_node_destroy(&fname);
        g_node_destroy(&lparen);
        parser_set_error(parser, "Expected ')' after '(' in function definition");
        return PARSE_ERROR;
    }

    gnode_t *rparen = g_node_create(G_WORD_NODE);
    rparen->data.token = parser_current_token(parser);
    parser_advance(parser);

    /* ------------------------------------------------------------
     * linebreak
     * ------------------------------------------------------------ */
    parser_skip_newlines(parser);

    /* ------------------------------------------------------------
     * function_body :
     *      compound_command
     *    | compound_command redirect_list
     * ------------------------------------------------------------ */
    gnode_t *body = NULL;
    parse_status_t status = gparse_compound_command(parser, &body);

    if (status != PARSE_OK)
    {
        g_node_destroy(&fname);
        g_node_destroy(&lparen);
        g_node_destroy(&rparen);
        return status;
    }

    /* Optional redirect_list */
    gnode_t *redir_list = NULL;
    status = gparse_redirect_list(parser, &redir_list);

    if (status == PARSE_OK && redir_list != NULL)
    {
        /* Wrap body + redirections into a G_FUNCTION_BODY node */
        gnode_t *fb = g_node_create(G_FUNCTION_BODY);
        fb->data.multi.a = body;
        fb->data.multi.b = redir_list;
        body = fb;
    }

    /* ------------------------------------------------------------
     * Build final G_FUNCTION_DEFINITION node
     * ------------------------------------------------------------ */
    gnode_t *node = g_node_create(G_FUNCTION_DEFINITION);
    node->data.multi.a = fname;
    node->data.multi.b = lparen;
    node->data.multi.c = rparen;
    node->data.multi.d = body;

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * function_body    : compound_command                (Apply rule 9)
 *                  | compound_command redirect_list  (Apply rule 9)
 *
 * This is done inline
 * ============================================================================
 */

/* ============================================================================
 * fname            : NAME                            (Apply rule 8)
 * 
 * This is done inline
 * ============================================================================
 */


/* ============================================================================
 * brace_group      : Lbrace compound_list Rbrace
 * ============================================================================
 */
parse_status_t gparse_brace_group(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    /* Expect '{' */
    if (!parser_accept(parser, TOKEN_LBRACE))
    {
        parser_set_error(parser, "Expected '{' to start brace group");
        return PARSE_ERROR;
    }

    /* Parse compound_list */
    gnode_t *clist = NULL;
    parse_status_t status = gparse_compound_list(parser, &clist);

    if (status != PARSE_OK)
        return status;

    /* Expect '}' */
    if (!parser_accept(parser, TOKEN_RBRACE))
    {
        g_node_destroy(&clist);
        parser_set_error(parser, "Expected '}' to end brace group");
        return PARSE_ERROR;
    }

    /* Wrap into G_BRACE_GROUP node */
    gnode_t *node = g_node_create(G_BRACE_GROUP);
    node->data.child = clist;

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * do_group         : Do compound_list Done           (Apply rule 6)
 * ============================================================================
 */
parse_status_t gparse_do_group(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    /* Expect 'do' */
    if (!parser_accept(parser, TOKEN_DO))
    {
        parser_set_error(parser, "Expected 'do' in for-clause");
        return PARSE_ERROR;
    }

    /* compound_list */
    gnode_t *clist = NULL;
    parse_status_t status = gparse_compound_list(parser, &clist);

    if (status != PARSE_OK)
        return status;

    /* Expect 'done' */
    if (!parser_accept(parser, TOKEN_DONE))
    {
        g_node_destroy(&clist);
        parser_set_error(parser, "Expected 'done' to close do-group");
        return PARSE_ERROR;
    }

    /* Build G_DO_GROUP */
    gnode_t *node = g_node_create(G_DO_GROUP);
    node->data.child = clist;

    *out_node = node;
    return PARSE_OK;
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

    /* ------------------------------------------------------------
     * 1. Parse cmd_prefix (assignments + redirections)
     * ------------------------------------------------------------ */
    while (true)
    {
        token_type_t t = parser_current_token_type(parser);

        if (t == TOKEN_ASSIGNMENT_WORD)
        {
            gnode_t *assign = g_node_create(G_ASSIGNMENT_WORD);
            assign->data.token = parser_current_token(parser);
            parser_advance(parser);
            g_list_append(node->data.list, assign);
            continue;
        }

        /* Try io_redirect */
        gnode_t *redir = NULL;
        parse_status_t rstat = gparse_io_redirect(parser, &redir);

        if (rstat == PARSE_OK)
        {
            g_list_append(node->data.list, redir);
            continue;
        }

        break; /* no more prefix elements */
    }

    /* ------------------------------------------------------------
     * 2. Parse cmd_name or cmd_word (first WORD)
     * ------------------------------------------------------------ */
    token_type_t t = parser_current_token_type(parser);

    if (t == TOKEN_WORD)
    {
        gnode_t *cmdname = g_node_create(G_CMD_NAME);
        cmdname->data.token = parser_current_token(parser);
        parser_advance(parser);
        g_list_append(node->data.list, cmdname);
    }
    else
    {
        /* No WORD → simple_command = cmd_prefix only */
        *out_node = node;
        return PARSE_OK;
    }

    /* ------------------------------------------------------------
     * 3. Parse cmd_suffix (WORD or io_redirect)
     * ------------------------------------------------------------ */
    while (true)
    {
        token_type_t t2 = parser_current_token_type(parser);

        if (t2 == TOKEN_WORD)
        {
            gnode_t *word = g_node_create(G_CMD_WORD);
            word->data.token = parser_current_token(parser);
            parser_advance(parser);
            g_list_append(node->data.list, word);
            continue;
        }

        gnode_t *redir2 = NULL;
        parse_status_t rstat2 = gparse_io_redirect(parser, &redir2);

        if (rstat2 == PARSE_OK)
        {
            g_list_append(node->data.list, redir2);
            continue;
        }

        break; /* end of suffix */
    }

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * cmd_name         : WORD                   (Apply rule 7a)
 *
 * This is done inline
 * ============================================================================
 */

/* ============================================================================
 * cmd_word         : WORD                   (Apply rule 7b)
 *
 * This is done inline
 * ============================================================================
 */

/* ============================================================================
 * cmd_prefix       :            io_redirect
 *                  | cmd_prefix io_redirect
 *                  |            ASSIGNMENT_WORD
 *                  | cmd_prefix ASSIGNMENT_WORD
 *
 * This is done inline
 * ============================================================================
 */

/* ============================================================================
 * cmd_suffix       :            io_redirect
 *                  | cmd_suffix io_redirect
 *                  |            WORD
 *                  | cmd_suffix WORD
 *
 * This is done inline
 * ============================================================================
 */

/* ============================================================================
 * redirect_list    :               io_redirect
 *                  | redirect_list io_redirect
 * ============================================================================
 */
parse_status_t gparse_redirect_list(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    gnode_t *list = g_node_create(G_REDIRECT_LIST);
    list->data.list = g_list_create();

    while (true)
    {
        gnode_t *redir = NULL;
        parse_status_t status = gparse_io_redirect(parser, &redir);

        if (status != PARSE_OK)
            break;

        g_list_append(list->data.list, redir);
    }

    if (list->data.list->size == 0)
    {
        g_node_destroy(&list);
        return PARSE_ERROR;
    }

    *out_node = list;
    return PARSE_OK;
}


/* ============================================================================
 * io_redirect      :             io_file
 *                  | IO_NUMBER   io_file
 *                  | IO_LOCATION io_file
 *                  |             io_here
 *                  | IO_NUMBER   io_here
 *                  | IO_LOCATION io_here
 * ============================================================================
 */
parse_status_t gparse_io_redirect(parser_t *parser, gnode_t **out_node)
{
    Expects_not_null(parser);
    Expects_not_null(out_node);

    *out_node = NULL;

    token_type_t t = parser_current_token_type(parser);

    gnode_t *io_number = NULL;
    gnode_t *io_location = NULL;

    /* Optional IO_NUMBER or IO_LOCATION prefix */
    if (t == TOKEN_IO_NUMBER)
    {
        io_number = g_node_create(G_IO_NUMBER_NODE);
        io_number->data.token = parser_current_token(parser);
        parser_advance(parser);
        t = parser_current_token_type(parser);
    }
    else if (t == TOKEN_IO_LOCATION)
    {
        io_location = g_node_create(G_IO_LOCATION_NODE);
        io_location->data.token = parser_current_token(parser);
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
    op->data.token = parser_current_token(parser);
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
    node->data.token = parser_current_token(parser);
    parser_advance(parser);

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * io_here          : DLESS     here_end
 *                  | DLESSDASH here_end
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
    gnode_t *op = g_node_create(G_WORD_NODE);
    op->data.token = parser_current_token(parser);
    parser_advance(parser);

    /* here_end */
    gnode_t *end = NULL;
    parse_status_t status = gparse_filename(parser, &end);

    if (status != PARSE_OK)
    {
        g_node_destroy(&node);
        g_node_destroy(&op);
        return status;
    }

    node->data.multi.a = op;
    node->data.multi.b = end;

    *out_node = node;
    return PARSE_OK;
}

/* ============================================================================
 * here_end         : WORD                      (Apply rule 3)
 * 
 * This is done inline
 * ============================================================================
 */

/* ============================================================================
 * newline_list     :              NEWLINE
 *                  | newline_list NEWLINE
 *
 * This is done inline
 * ============================================================================
 */

/* ============================================================================
 * linebreak        : newline_list
 *                  | (empty)
 *
 * This is done inline
 * ============================================================================
 */

/* ============================================================================
 * separator_op     : '&'
 *                  | ';'
 *
 * This is done inline
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
    node->data.token = parser_current_token(parser);

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
        op->data.token = parser_current_token(parser);
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
            tok->data.token = parser_current_token(parser);
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
 * sequential_sep : ';' linebreak | newline_list;
 *
 * This is done inline
 * ============================================================================
 */




















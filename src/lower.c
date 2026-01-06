#include "lower.h"
#include "logging.h"
#include "token.h"
#include "gnode.h"
#include "string_t.h"

// Ignore warning 4061: enumerator in switch of enum is not explicitly handled by a case label
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4061)
#endif

/* Convenience macros for sanity checks */
#define EXPECT_TYPE(node, k)                                                                       \
    do                                                                                             \
    {                                                                                              \
        if (!(node) || (node)->type != (k))                                                        \
        {                                                                                          \
            log_error("ast_lower: expected type %s, got %d", #k,                                   \
                      (int)((node) ? (node)->type : G_UNSPECIFIED));	\
            return NULL;                                                                           \
        }                                                                                          \
    } while (0)

/* Forward declarations */
static ast_node_t *lower_program(const gnode_t *g);
static ast_node_t *lower_complete_commands(const gnode_t *g);
static ast_node_t *lower_complete_command(const gnode_t *g);
static ast_node_t *lower_list(const gnode_t *g);
static ast_node_t *lower_and_or(const gnode_t *g);
static ast_node_t *lower_pipeline(const gnode_t *g);
static ast_node_t *lower_pipe_sequence(const gnode_t *g, bool is_negated);
static ast_node_t *lower_command(const gnode_t *g);
static ast_node_t *lower_simple_command(const gnode_t *g);
static ast_node_t *lower_compound_command(const gnode_t *g);
static ast_node_t *lower_subshell(const gnode_t *g);
static ast_node_t *lower_brace_group(const gnode_t *g);
static ast_node_t *lower_if_clause(const gnode_t *g);
static ast_node_t *lower_else_part(const gnode_t *g);
static ast_node_t *lower_while_clause(const gnode_t *g);
static ast_node_t *lower_until_clause(const gnode_t *g);
static ast_node_t *lower_for_clause(const gnode_t *g);
static ast_node_t *lower_case_clause(const gnode_t *g);
static ast_node_t *lower_function_definition(const gnode_t *g);
static ast_node_t *lower_compound_list(const gnode_t *g);
static ast_node_t *lower_term_as_command_list(const gnode_t *g);
static ast_node_t *lower_redirect_list(const gnode_t *g);
static ast_node_t *lower_io_redirect(const gnode_t *g);
static ast_node_t *lower_case_item(const gnode_t *g);
static ast_node_t *lower_case_item_ns(const gnode_t *g);
static ast_node_t *lower_do_group(const gnode_t *g);


/* Small helpers */
static token_list_t *token_list_from_wordlist(const gnode_t *g);
static token_list_t *token_list_from_pattern_list(const gnode_t *g);
static redirection_type_t map_redir_type_from_io_file(const gnode_t *io_file);
static redir_operand_kind_t determine_operand_kind(token_type_t type,
                                                   token_t *target_tok, string_t **out_io_loc);
static cmd_separator_t separator_from_gseparator_op(const gnode_t *gsep);

/* Entry point */
ast_t *ast_lower(const gnode_t *root)
{
    EXPECT_TYPE(root, G_PROGRAM);
    return lower_program(root);
}

/* ============================================================================
 * program : linebreak complete_commands linebreak | linebreak
 * AST: PROGRAM -> COMMAND_LIST or NULL body
 * ============================================================================
 */
static ast_node_t *lower_program(const gnode_t *g)
{
    Expects_eq(g->type, G_PROGRAM);

    ast_node_t *prog = ast_create_program();

    const gnode_t *child = g->data.child;
    if (!child)
    {
        /* Empty program: body stays NULL */
        return prog;
    }

    ast_node_t *body = lower_complete_commands(child);
    if (!body)
    {
        ast_node_destroy(&prog);
        return NULL;
    }

    prog->data.program.body = body;
    return prog;
}

/* ============================================================================
 * complete_commands: complete_command (NEWLINE+ complete_command)*
 * AST: flatten to COMMAND_LIST
 * ============================================================================
 */
static ast_node_t *lower_complete_commands(const gnode_t *g)
{
    Expects_eq(g->type, G_COMPLETE_COMMANDS);

    ast_node_t *cl = ast_create_command_list();

    gnode_list_t *lst = g->data.list;
    for (int i = 0; i < lst->size; i++)
    {
        const gnode_t *gcmd = lst->nodes[i];
        Expects_eq(gcmd->type, G_COMPLETE_COMMAND);

        ast_node_t *item = lower_complete_command(gcmd);
        if (!item)
        {
            ast_node_destroy(&cl);
            return NULL;
        }

        ast_command_list_node_append_item(cl, item);

        /* For top-level complete_commands, treat as EOL-separated */
        ast_command_list_node_append_separator(cl, LIST_SEP_EOL);
    }

    return cl;
}

/* ============================================================================
 * complete_command : list [separator_op]
 * We ignore the final separator_op (it is structurally encoded already).
 * ============================================================================
 */
static ast_node_t *lower_complete_command(const gnode_t *g)
{
    Expects_eq(g->type, G_COMPLETE_COMMAND);

    const gnode_t *glist = g->data.multi.a;
    return lower_list(glist);
}

/* ============================================================================
 * list: and_or (separator_op and_or)*
 * AST: COMMAND_LIST
 *
 * Note: The parser may return G_PIPELINE directly (not wrapped in G_AND_OR)
 * when there are no && or || operators, so we handle both cases.
 * ============================================================================
 */
static ast_node_t *lower_list(const gnode_t *g)
{
    Expects_eq(g->type, G_LIST);

    ast_node_t *cl = ast_create_command_list();
    gnode_list_t *lst = g->data.list;

    /* list layout: [and_or, sep_op, and_or, sep_op, ...] */
    for (int i = 0; i < lst->size;)
    {
        const gnode_t *elem = lst->nodes[i];
        ast_node_t *node = NULL;

        /* The parser returns G_PIPELINE when there are no && or || operators */
        if (elem->type == G_AND_OR)
        {
            node = lower_and_or(elem);
        }
        else if (elem->type == G_PIPELINE)
        {
            /* Single pipeline without operators - lower it directly */
            node = lower_pipeline(elem);
        }
        else
        {
            log_error("lower_list: expected G_AND_OR or G_PIPELINE at index %zu, got %d",
                      i, (int)elem->type);
            ast_node_destroy(&cl);
            return NULL;
        }

        if (!node)
        {
            ast_node_destroy(&cl);
            return NULL;
        }
        ast_command_list_node_append_item(cl, node);

        i++;

        cmd_separator_t sep = LIST_SEP_EOL;

        if (i < lst->size)
        {
            const gnode_t *sep_node = lst->nodes[i];
            if (sep_node->type == G_SEPARATOR_OP)
            {
                sep = separator_from_gseparator_op(sep_node);
                i++;
            }
            else
            {
                /* No explicit separator → treat as EOL at this position */
                sep = LIST_SEP_EOL;
            }
        }
        else
        {
            sep = LIST_SEP_EOL;
        }

        ast_command_list_node_append_separator(cl, sep);
    }

    return cl;
}

/* ============================================================================
 * and_or : pipeline | and_or (&& or ||) pipeline
 * AST: binary AND_OR_LIST tree; pipeline alone collapses to its child.
 * ============================================================================
 */
static ast_node_t *lower_and_or(const gnode_t *g)
{
    Expects_eq(g->type, G_AND_OR);

    /* Leaf case: parser makes G_AND_OR only for composed expressions.
       Simple pipeline will not be a G_AND_OR node in your implementation
       (you build them as nested G_AND_OR). Handle both defensively. */

    if (g->data.multi.a == NULL && g->data.multi.b == NULL && g->data.multi.c == NULL)
    {
        /* Shouldn't happen, but return NULL to be safe */
        return NULL;
    }

    /* If this node was constructed as a binary structure:
       multi.a = left, multi.b = op-node, multi.c = right */
    if (g->data.multi.a && g->data.multi.b && g->data.multi.c)
    {
        ast_node_t *left = lower_and_or(g->data.multi.a);
        if (!left)
            return NULL;

        const gnode_t *op_node = g->data.multi.b;
        EXPECT_TYPE(op_node, G_AND_OR);
        token_t *tok = op_node->data.token;
        token_type_t t = token_get_type(tok);

        andor_operator_t op;
        if (t == TOKEN_AND_IF)
            op = ANDOR_OP_AND;
        else if (t == TOKEN_OR_IF)
            op = ANDOR_OP_OR;
        else
        {
            log_error("lower_and_or: unexpected operator token");
            ast_node_destroy(&left);
            return NULL;
        }

        ast_node_t *right = NULL;
        {
            const gnode_t *gp = g->data.multi.c;
            if (gp->type == G_PIPELINE)
                right = lower_pipeline(gp);
            else if (gp->type == G_AND_OR)
                right = lower_and_or(gp);
            else
            {
                log_error("lower_and_or: unexpected right kind %d", (int)gp->type);
                ast_node_destroy(&left);
                return NULL;
            }
        }

        if (!right)
        {
            ast_node_destroy(&left);
            return NULL;
        }

        return ast_create_andor_list(left, right, op);
    }

    /* Otherwise treat this as a single pipeline (degenerate form) */
    /* In your parser, a single pipeline is not wrapped in G_AND_OR.
       So this path might never be used, but kept for robustness. */
    const gnode_t *maybe_pipeline = g->data.multi.a ? g->data.multi.a : g->data.child;
    if (maybe_pipeline && maybe_pipeline->type == G_PIPELINE)
        return lower_pipeline(maybe_pipeline);

    log_error("lower_and_or: unexpected structure");
    return NULL;
}

/* ============================================================================
 * pipeline : [Bang] pipe_sequence
 * AST: PIPELINE or single command if only one element and no negation.
 * ============================================================================
 */
static ast_node_t *lower_pipeline(const gnode_t *g)
{
    Expects_eq(g->type, G_PIPELINE);

    gnode_list_t *lst = g->data.list;
    bool is_negated = false;
    const gnode_t *seq = NULL;

    if (lst->size == 1)
    {
        seq = lst->nodes[0];
    }
    else if (lst->size == 2)
    {
        /* Optional Bang + pipe_sequence */
        const gnode_t *first = lst->nodes[0];
        const gnode_t *second = lst->nodes[1];

        /* first is a WORD_NODE wrapping '!' in practice */
        if (first->type == G_WORD_NODE && token_get_type(first->data.token) == TOKEN_BANG)
        {
            is_negated = true;
            seq = second;
        }
        else
        {
            seq = lst->nodes[0]; /* be defensive */
        }
    }
    else
    {
        log_error("lower_pipeline: unexpected list size %zu", lst->size);
        return NULL;
    }

    EXPECT_TYPE(seq, G_PIPE_SEQUENCE);
    return lower_pipe_sequence(seq, is_negated);
}

/* ============================================================================
 * pipe_sequence: command ('|' command)*
 * AST: single command or PIPELINE(commands)
 * ============================================================================
 */
static ast_node_t *lower_pipe_sequence(const gnode_t *g, bool is_negated)
{
    Expects_eq(g->type, G_PIPE_SEQUENCE);

    ast_node_list_t *cmds = ast_node_list_create();
    gnode_list_t *lst = g->data.list;

    for (int i = 0; i < lst->size; i++)
    {
        const gnode_t *elem = lst->nodes[i];

        if (elem->type == G_PIPE_SEQUENCE || elem->type == G_COMMAND)
        {
            if (elem->type == G_COMMAND)
            {
                ast_node_t *cmd = lower_command(elem);
                if (!cmd)
                {
                    ast_node_list_destroy(&cmds);
                    return NULL;
                }
                ast_node_list_append(cmds, cmd);
            }
            /* G_WORD_NODE for '|' are ignored here, parser had separate pipe token nodes */
        }
    }

    int n = ast_node_list_size(cmds);
    if (n == 0)
    {
        ast_node_list_destroy(&cmds);
        return NULL;
    }

    if (n == 1 && !is_negated)
    {
        ast_node_t *only = ast_node_list_get(cmds, 0);
        /* Steal the single element, then free container */
        cmds->size = 0;
        ast_node_list_destroy(&cmds);
        return only;
    }

    return ast_create_pipeline(cmds, is_negated);
}

/* ============================================================================
 * command : simple_command
 *         | compound_command
 *         | compound_command redirect_list
 *         | function_definition
 * ============================================================================
 */
static ast_node_t *lower_command(const gnode_t *g)
{
    Expects_eq(g->type, G_COMMAND);

    /* G_COMMAND is a wrapper node - the actual command is in data.child or data.multi.a */
    const gnode_t *child = g->data.child;
    if (!child)
    {
        /* Try multi.a if child is NULL */
        child = g->data.multi.a;
    }

    if (!child)
    {
        log_error("lower_command: G_COMMAND wrapper has no child");
        return NULL;
    }

    /* Dispatch based on the actual command type */
    switch (child->type)
    {
    case G_SIMPLE_COMMAND:
        return lower_simple_command(child);
    case G_SUBSHELL:
    case G_BRACE_GROUP:
    case G_FOR_CLAUSE:
    case G_CASE_CLAUSE:
    case G_IF_CLAUSE:
    case G_WHILE_CLAUSE:
    case G_UNTIL_CLAUSE:
        return lower_compound_command(child);
    case G_FUNCTION_DEFINITION:
        return lower_function_definition(child);
    case G_COMPOUND_COMMAND:
        /* G_COMPOUND_COMMAND is itself a wrapper - recurse into it */
        return lower_compound_command(child);
    case G_PROGRAM:
    case G_UNSPECIFIED:
    default:
        break;
    }

    log_error("lower_command: unexpected child kind %d", (int)child->type);
    return NULL;
}

/* ============================================================================
 * simple_command
 * AST_SIMPLE_COMMAND(words, redirections, assignments)
 * ============================================================================
 */
static ast_node_t *lower_simple_command(const gnode_t *g)
{
    Expects_eq(g->type, G_SIMPLE_COMMAND);

    gnode_list_t *lst = g->data.list;
    token_list_t *assignments = token_list_create();
    token_list_t *words = token_list_create();
    ast_node_list_t *redirs = ast_node_list_create();

    for (int i = 0; i < lst->size; i++)
    {
        const gnode_t *elem = lst->nodes[i];

        /* Handle G_CMD_PREFIX and G_CMD_SUFFIX wrappers created by parser */
        if (elem->type == G_CMD_PREFIX)
        {
            /* G_CMD_PREFIX wraps a single assignment or redirect */
            elem = elem->data.child;
            if (!elem)
                continue;
        }
        else if (elem->type == G_CMD_SUFFIX)
        {
            /* G_CMD_SUFFIX contains a list of words and redirects */
            gnode_list_t *suffix_list = elem->data.list;
            for (int j = 0; j < suffix_list->size; j++)
            {
                const gnode_t *suffix_elem = suffix_list->nodes[j];

                if (suffix_elem->type == G_CMD_WORD || suffix_elem->type == G_WORD_NODE)
                {
                    /* Clone the token since gnode owns it and will destroy it */
                    token_list_append(words, token_clone(suffix_elem->data.token));
                }
                else if (suffix_elem->type == G_IO_REDIRECT)
                {
                    ast_node_t *r = lower_io_redirect(suffix_elem);
                    if (!r)
                    {
                        token_list_destroy(&assignments);
                        token_list_destroy(&words);
                        ast_node_list_destroy(&redirs);
                        return NULL;
                    }
                    ast_node_list_append(redirs, r);
                }
            }
            continue;
        }

        switch (elem->type)
        {
        case G_ASSIGNMENT_WORD:
            /* Clone the token since gnode owns it and will destroy it */
            token_list_append(assignments, token_clone(elem->data.token));
            break;

        case G_CMD_NAME:
        case G_CMD_WORD:
        case G_WORD_NODE:
            /* Clone the token since gnode owns it and will destroy it */
            token_list_append(words, token_clone(elem->data.token));
            break;

        case G_IO_REDIRECT: {
            ast_node_t *r = lower_io_redirect(elem);
            if (!r)
            {
                token_list_destroy(&assignments);
                token_list_destroy(&words);
                ast_node_list_destroy(&redirs);
                return NULL;
            }
            ast_node_list_append(redirs, r);
            break;
        }

        default:
            log_error("lower_simple_command: unexpected child kind %d", (int)elem->type);
            token_list_destroy(&assignments);
            token_list_destroy(&words);
            ast_node_list_destroy(&redirs);
            return NULL;
        }
    }

    return ast_create_simple_command(words, redirs, assignments);
}

/* ============================================================================
 * compound_command family
 * ============================================================================
 */
static ast_node_t *lower_compound_command(const gnode_t *g)
{
    switch (g->type)
    {
    case G_SUBSHELL:
        return lower_subshell(g);
    case G_BRACE_GROUP:
        return lower_brace_group(g);
    case G_FOR_CLAUSE:
        return lower_for_clause(g);
    case G_CASE_CLAUSE:
        return lower_case_clause(g);
    case G_IF_CLAUSE:
        return lower_if_clause(g);
    case G_WHILE_CLAUSE:
        return lower_while_clause(g);
    case G_UNTIL_CLAUSE:
        return lower_until_clause(g);
    default:
        log_error("lower_compound_command: unexpected kind %d", (int)g->type);
        return NULL;
    }
}

static ast_node_t *lower_subshell(const gnode_t *g)
{
    Expects_eq(g->type, G_SUBSHELL);
    const gnode_t *clist = g->data.child;
    ast_node_t *body = lower_compound_list(clist);
    if (!body)
        return NULL;
    return ast_create_subshell(body);
}

static ast_node_t *lower_brace_group(const gnode_t *g)
{
    Expects_eq(g->type, G_BRACE_GROUP);
    const gnode_t *clist = g->data.child;
    ast_node_t *body = lower_compound_list(clist);
    if (!body)
        return NULL;
    return ast_create_brace_group(body);
}

/* ============================================================================
 * compound_list : linebreak term [separator]
 * AST: COMMAND_LIST from term
 * ============================================================================
 */
static ast_node_t *lower_compound_list(const gnode_t *g)
{
    Expects_eq(g->type, G_COMPOUND_LIST);
    /* Your G_COMPOUND_LIST wraps a list whose first element is a G_TERM. */
    gnode_list_t *lst = g->data.list;
    if (lst->size == 0)
        return ast_create_command_list();
    const gnode_t *term = lst->nodes[0];
    EXPECT_TYPE(term, G_TERM);
    return lower_term_as_command_list(term);
}

/* term: and_or (separator and_or)* → COMMAND_LIST */
static ast_node_t *lower_term_as_command_list(const gnode_t *g)
{
    Expects_eq(g->type, G_TERM);

    ast_node_t *cl = ast_create_command_list();
    gnode_list_t *lst = g->data.list;

    for (int i = 0; i < lst->size;)
    {
        const gnode_t *elem = lst->nodes[i];

        if (elem->type != G_AND_OR)
        {
            log_error("lower_term: expected G_AND_OR");
            ast_node_destroy(&cl);
            return NULL;
        }

        ast_node_t *node = lower_and_or(elem);
        if (!node)
        {
            ast_node_destroy(&cl);
            return NULL;
        }

        ast_command_list_node_append_item(cl, node);
        i++;

        cmd_separator_t sep = LIST_SEP_EOL;

        if (i < lst->size)
        {
            const gnode_t *sep_node = lst->nodes[i];
            if (sep_node->type == G_SEPARATOR)
            {
                /* normalize to LIST_SEP_SEQUENTIAL for ; or newline,
                   LIST_SEP_BACKGROUND for &. For now treat all as SEQUENTIAL. */
                sep = LIST_SEP_SEQUENTIAL;
                i++;
            }
        }

        ast_command_list_node_append_separator(cl, sep);
    }

    return cl;
}

/* ============================================================================
 * if_clause / else_part
 * ============================================================================
 */
static ast_node_t *lower_if_clause(const gnode_t *g)
{
    Expects_eq(g->type, G_IF_CLAUSE);

    const gnode_t *gcond = g->data.multi.a;
    const gnode_t *gthen = g->data.multi.b;
    const gnode_t *gelse = g->data.multi.c; /* may be NULL */

    ast_node_t *cond = lower_compound_list(gcond);
    if (!cond)
        return NULL;

    ast_node_t *then_body = lower_compound_list(gthen);
    if (!then_body)
    {
        ast_node_destroy(&cond);
        return NULL;
    }

    ast_node_t *if_ast = ast_create_if_clause(cond, then_body);

    if (gelse)
    {
        ast_node_t *else_node = lower_else_part(gelse);
        if (!else_node)
        {
            ast_node_destroy(&if_ast);
            return NULL;
        }

        /* For now, treat else_node as else_body directly or as extra nesting;
           if you added AST_ELIF_CLAUSE, you’d populate if_clause.elif_list and
           if_clause.else_body here. Here we map a single else/elif chain body. */

        if_ast->data.if_clause.else_body = else_node;
    }

    return if_ast;
}

/* else_part: else compound_list | elif ... then ... [else_part] */
static ast_node_t *lower_else_part(const gnode_t *g)
{
    Expects_eq(g->type, G_ELSE_PART);

    /* You encoded else_part with multi.a, multi.b, multi.c. The parser code:
       - 'else' → multi.a = body
       - 'elif' → multi.a = cond, multi.b = then_body, multi.c = next_else
       For semantic AST, you may want to normalize to nested IFs for elif. */

    const gnode_t *a = g->data.multi.a;
    const gnode_t *b = g->data.multi.b;
    const gnode_t *c = g->data.multi.c;

    if (b == NULL && c == NULL)
    {
        /* else body */
        return lower_compound_list(a);
    }

    /* elif: treat as nested if */
    ast_node_t *cond = lower_compound_list(a);
    if (!cond)
        return NULL;

    ast_node_t *then_body = lower_compound_list(b);
    if (!then_body)
    {
        ast_node_destroy(&cond);
        return NULL;
    }

    ast_node_t *elif_if = ast_create_if_clause(cond, then_body);

    if (c)
    {
        ast_node_t *tail = lower_else_part(c);
        if (!tail)
        {
            ast_node_destroy(&elif_if);
            return NULL;
        }
        elif_if->data.if_clause.else_body = tail;
    }

    return elif_if;
}

/* ============================================================================
 * while / until
 * ============================================================================
 */
static ast_node_t *lower_while_clause(const gnode_t *g)
{
    Expects_eq(g->type, G_WHILE_CLAUSE);
    const gnode_t *gcond = g->data.multi.a;
    const gnode_t *gbody = g->data.multi.b;

    ast_node_t *cond = lower_compound_list(gcond);
    if (!cond)
        return NULL;

    ast_node_t *body = lower_do_group(gbody);
    if (!body)
    {
        ast_node_destroy(&cond);
        return NULL;
    }

    return ast_create_while_clause(cond, body);
}

static ast_node_t *lower_until_clause(const gnode_t *g)
{
    Expects_eq(g->type, G_UNTIL_CLAUSE);
    const gnode_t *gcond = g->data.multi.a;
    const gnode_t *gbody = g->data.multi.b;

    ast_node_t *cond = lower_compound_list(gcond);
    if (!cond)
        return NULL;

    ast_node_t *body = lower_do_group(gbody);
    if (!body)
    {
        ast_node_destroy(&cond);
        return NULL;
    }

    return ast_create_until_clause(cond, body);
}

/* do_group: Do compound_list Done */
static ast_node_t *lower_do_group(const gnode_t *g)
{
    Expects_eq(g->type, G_DO_GROUP);
    return lower_compound_list(g->data.child);
}

/* ============================================================================
 * for_clause
 * ============================================================================
 */
static ast_node_t *lower_for_clause(const gnode_t *g)
{
    Expects_eq(g->type, G_FOR_CLAUSE);

    const gnode_t *gname = g->data.multi.a;
    const gnode_t *gwlist = g->data.multi.b; /* may be NULL */
    const gnode_t *gdo = g->data.multi.c;

    EXPECT_TYPE(gname, G_NAME_NODE);

    string_t *var = token_get_all_text(gname->data.token);

    token_list_t *words = NULL;
    if (gwlist)
    {
        EXPECT_TYPE(gwlist, G_WORDLIST);
        words = token_list_from_wordlist(gwlist);
    }

    ast_node_t *body = lower_do_group(gdo);
    if (!body)
    {
        string_destroy(&var);
        if (words)
            token_list_destroy(&words);
        return NULL;
    }

    ast_node_t *node = ast_create_for_clause(var, words, body);
    string_destroy(&var); /* ast_create_for_clause clones variable */
    return node;
}

/* ============================================================================
 * case_clause
 * ============================================================================
 */
static ast_node_t *lower_case_clause(const gnode_t *g)
{
    Expects_eq(g->type, G_CASE_CLAUSE);

    const gnode_t *gword = g->data.multi.a;
    const gnode_t *glist = g->data.multi.b; /* may be NULL */

    EXPECT_TYPE(gword, G_WORD_NODE);
    token_t *subject = gword->data.token;

    ast_node_t *node = ast_create_case_clause(subject);

    if (glist)
    {
        gnode_list_t *lst = glist->data.list;
        for (int i = 0; i < lst->size; i++)
        {
            const gnode_t *item = lst->nodes[i];
            ast_node_t *ci = NULL;

            if (item->type == G_CASE_ITEM)
                ci = lower_case_item(item);
            else if (item->type == G_CASE_ITEM_NS)
                ci = lower_case_item_ns(item);
            else
            {
                log_error("lower_case_clause: unexpected item kind %d", (int)item->type);
                ast_node_destroy(&node);
                return NULL;
            }

            if (!ci)
            {
                ast_node_destroy(&node);
                return NULL;
            }

            ast_node_list_append(node->data.case_clause.case_items, ci);
        }
    }

    return node;
}

/* case_item: pattern_list ')' [compound_list] (DSEMI|SEMI_AND) */
static ast_node_t *lower_case_item(const gnode_t *g)
{
    Expects_eq(g->type, G_CASE_ITEM);

    const gnode_t *gpatterns = g->data.multi.a;
    const gnode_t *gbody = g->data.multi.b; /* may be NULL */
    const gnode_t *gterm = g->data.multi.c; /* terminator node */

    token_list_t *patterns = token_list_from_pattern_list(gpatterns);
    ast_node_t *body = NULL;

    if (gbody)
        body = lower_compound_list(gbody);

    case_terminator_t term = CASE_TERM_NONE;
    if (gterm)
    {
        token_type_t t = token_get_type(gterm->data.token);
        if (t == TOKEN_DSEMI)
            term = CASE_TERM_DSEMI;
        else if (t == TOKEN_SEMIAND)
            term = CASE_TERM_SEMI_AND;
    }

    ast_node_t *ci = ast_create_case_item(patterns, body);
    ci->data.case_item.terminator = term;
    return ci;
}

/* case_item_ns: pattern_list ')' [compound_list], no terminator */
static ast_node_t *lower_case_item_ns(const gnode_t *g)
{
    Expects_eq(g->type, G_CASE_ITEM_NS);

    const gnode_t *gpatterns = g->data.multi.a;
    const gnode_t *gbody = g->data.multi.b; /* may be NULL */

    token_list_t *patterns = token_list_from_pattern_list(gpatterns);
    ast_node_t *body = NULL;

    if (gbody)
        body = lower_compound_list(gbody);

    ast_node_t *ci = ast_create_case_item(patterns, body);
    ci->data.case_item.terminator = CASE_TERM_NONE;
    return ci;
}

/* ============================================================================
 * function_definition
 * ============================================================================
 */
static ast_node_t *lower_function_definition(const gnode_t *g)
{
    Expects_eq(g->type, G_FUNCTION_DEFINITION);

    const gnode_t *gfname = g->data.multi.a;
    const gnode_t *gbody = g->data.multi.d;

    EXPECT_TYPE(gfname, G_FNAME);

    string_t *name = token_get_all_text(gfname->data.token);

    ast_node_t *body = NULL;

    if (gbody->type == G_FUNCTION_BODY)
    {
        const gnode_t *gcmd = gbody->data.multi.a;
        const gnode_t *gredirs = gbody->data.multi.b;

        ast_node_t *cmd = lower_compound_command(gcmd);
        if (!cmd)
        {
            string_destroy(&name);
            return NULL;
        }

        ast_node_list_t *redirs = NULL;
        if (gredirs)
        {
            redirs = ast_node_list_create();
            gnode_list_t *lst = gredirs->data.list;
            for (int i = 0; i < lst->size; i++)
            {
                const gnode_t *gr = lst->nodes[i];
                ast_node_t *r = lower_io_redirect(gr);
                if (!r)
                {
                    ast_node_list_destroy(&redirs);
                    ast_node_destroy(&cmd);
                    string_destroy(&name);
                    return NULL;
                }
                ast_node_list_append(redirs, r);
            }
        }

        ast_node_t *fn = ast_create_function_def(name, cmd, redirs);
        string_destroy(&name); /* cloned */
        return fn;
    }

    /* If no G_FUNCTION_BODY wrapper, treat as plain compound_command */
    body = lower_compound_command(gbody);
    if (!body)
    {
        string_destroy(&name);
        return NULL;
    }

    ast_node_t *fn = ast_create_function_def(name, body, NULL);
    string_destroy(&name);
    return fn;
}

/* ============================================================================
 * redirect_list / io_redirect / io_file / io_here
 * ============================================================================
 */
static ast_node_t *lower_redirect_list(const gnode_t *g)
{
    Expects_eq(g->type, G_REDIRECT_LIST);

    ast_node_list_t *lst_ast = ast_node_list_create();
    gnode_list_t *lst = g->data.list;

    for (int i = 0; i < lst->size; i++)
    {
        const gnode_t *gr = lst->nodes[i];
        EXPECT_TYPE(gr, G_IO_REDIRECT);
        ast_node_t *r = lower_io_redirect(gr);
        if (!r)
        {
            ast_node_list_destroy(&lst_ast);
            return NULL;
        }
        ast_node_list_append(lst_ast, r);
    }

    /* The caller will wrap these into AST_REDIRECTED_COMMAND or similar. */
    ast_node_t *wrapper = ast_create_redirected_command(NULL, lst_ast);
    return wrapper;
}

static ast_node_t *lower_io_redirect(const gnode_t *g)
{
    Expects_eq(g->type, G_IO_REDIRECT);

    const gnode_t *gionum = g->data.multi.a;  /* IO_NUMBER_NODE or NULL */
    const gnode_t *gioloc = g->data.multi.b;  /* IO_LOCATION_NODE or NULL */
    const gnode_t *gtarget = g->data.multi.c; /* IO_FILE or IO_HERE */

    int io_number = -1;
    if (gionum)
    {
        EXPECT_TYPE(gionum, G_IO_NUMBER_NODE);
        // By the time we get to lowering, the token should have been promoted
        // to a number.
        io_number = gionum->data.token->io_number;
    }

    string_t *io_location = NULL;
    if (gioloc)
    {
        EXPECT_TYPE(gioloc, G_IO_LOCATION_NODE);
        io_location = string_create_from(gioloc->data.token->io_location);
    }

    redirection_type_t rtype;
    redir_operand_kind_t operand = REDIR_OPERAND_NONE;
    token_t *target_tok = NULL;
    string_t *heredoc_content = NULL;
    token_t *op_tok = NULL;
    token_type_t op_type;

    /* io_file */
    if (gtarget->type == G_IO_FILE)
    {
        const gnode_t *op_node = gtarget->data.multi.a;
        op_tok = op_node->data.token;
        op_type = token_get_type(op_tok);

        rtype = map_redir_type_from_io_file(gtarget);

        const gnode_t *gfname = gtarget->data.multi.b;
        EXPECT_TYPE(gfname, G_FILENAME);
        target_tok = gfname->data.token;

        operand = determine_operand_kind(op_type, target_tok, &io_location);
    }

    /* io_here */
    else if (gtarget->type == G_IO_HERE)
    {
        const gnode_t *op_node = gtarget->data.multi.a;
        op_tok = op_node->data.token;
        op_type = token_get_type(op_tok);

        if (op_type == TOKEN_DLESS)
            rtype = REDIR_HEREDOC;
        else
            rtype = REDIR_HEREDOC_STRIP;

        const gnode_t *gend = gtarget->data.multi.b;
        EXPECT_TYPE(gend, G_FILENAME);
        target_tok = gend->data.token;

        operand = REDIR_OPERAND_HEREDOC;
    }
    else
    {
        log_error("lower_io_redirect: unexpected target kind %d", (int)gtarget->type);
        if (io_location)
            string_destroy(&io_location);
        return NULL;
    }

    // Clone the token so the AST and GNode trees don't share ownership
    token_t *cloned_target = token_clone(target_tok);
    ast_node_t *node = ast_create_redirection(rtype, operand, io_number, io_location, cloned_target);
    node->data.redirection.operand = operand;
    node->data.redirection.heredoc_content = heredoc_content;

    return node;
}

/* ============================================================================
 * Helper: convert G_WORDLIST to token_list_t
 * ============================================================================
 */
static token_list_t *token_list_from_wordlist(const gnode_t *g)
{
    Expects_eq(g->type, G_WORDLIST);
    token_list_t *tl = token_list_create();
    gnode_list_t *lst = g->data.list;

    for (int i = 0; i < lst->size; i++)
    {
        const gnode_t *w = lst->nodes[i];
        EXPECT_TYPE(w, G_WORD_NODE);
        token_list_append(tl, w->data.token);
    }

    return tl;
}

/* pattern_list: list of WORD_NODE in data.list */
static token_list_t *token_list_from_pattern_list(const gnode_t *g)
{
    Expects_eq(g->type, G_PATTERN_LIST);
    token_list_t *tl = token_list_create();
    gnode_list_t *lst = g->data.list;

    for (int i = 0; i < lst->size; i++)
    {
        const gnode_t *w = lst->nodes[i];
        EXPECT_TYPE(w, G_WORD_NODE);
        token_list_append(tl, w->data.token);
    }

    return tl;
}

/* Map IO_FILE operator token to redirection_type_t */
static redirection_type_t map_redir_type_from_io_file(const gnode_t *io_file)
{
    Expects_eq(io_file->type, G_IO_FILE);
    const gnode_t *op = io_file->data.multi.a;
    Expects_eq(op->type, G_WORD_NODE);

    token_type_t t = token_get_type(op->data.token);
    switch (t)
    {
    case TOKEN_LESS:
        return REDIR_INPUT;
    case TOKEN_GREATER:
        return REDIR_OUTPUT;
    case TOKEN_DGREAT:
        return REDIR_APPEND;
    case TOKEN_LESSAND:
        return REDIR_DUP_INPUT;
    case TOKEN_GREATAND:
        return REDIR_DUP_OUTPUT;
    case TOKEN_LESSGREAT:
        return REDIR_READWRITE;
    case TOKEN_CLOBBER:
        return REDIR_CLOBBER;
    default:
        log_error("map_redir_type_from_io_file: unexpected token type %d", (int)t);
        return REDIR_INPUT;
    }
}

static redir_operand_kind_t determine_operand_kind(token_type_t op_type, token_t *target_tok,
                                                   string_t **out_io_loc)
{
    // FIXME: use out_io_loc if needed
    (void)out_io_loc;
    string_t *lx = token_get_all_text(target_tok);

    switch (op_type)
    {

    /* ------------------------------------------------------------
     * HEREDOC OPERATORS
     * ------------------------------------------------------------ */
    case TOKEN_DLESS:     /* << */
    case TOKEN_DLESSDASH: /* <<- */
        return REDIR_OPERAND_HEREDOC;

    /* ------------------------------------------------------------
     * FD DUPLICATION OPERATORS
     * ------------------------------------------------------------ */
    case TOKEN_LESSAND:  /* <& */
    case TOKEN_GREATAND: /* >& */
        if (string_compare_cstr(lx, "-") == 0)
            return REDIR_OPERAND_CLOSE;

        /* numeric? */
        if (string_length(lx) > 0 && string_find_first_not_of_cstr(lx, "0123456789") != -1)
        {
            string_destroy(&lx);
            return REDIR_OPERAND_FILENAME; /* unspecified → treat as filename */
        }

        return REDIR_OPERAND_FD;

    /* ------------------------------------------------------------
     * FILENAME OPERATORS
     * ------------------------------------------------------------ */
    case TOKEN_LESS:      /* < */
    case TOKEN_GREATER:   /* > */
    case TOKEN_DGREAT:    /* >> */
    case TOKEN_LESSGREAT: /* <> */
    case TOKEN_CLOBBER:   /* >| */
        return REDIR_OPERAND_FILENAME;

    default:
        return REDIR_OPERAND_FILENAME;
    }
}


/* separator_op: '&' or ';' → LIST_SEP_BACKGROUND/SEQUENTIAL */
static cmd_separator_t separator_from_gseparator_op(const gnode_t *gsep)
{
    Expects_eq(gsep->type, G_SEPARATOR_OP);
    token_type_t t = token_get_type(gsep->data.token);

    if (t == TOKEN_AMPER)
        return LIST_SEP_BACKGROUND;
    return LIST_SEP_SEQUENTIAL;
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

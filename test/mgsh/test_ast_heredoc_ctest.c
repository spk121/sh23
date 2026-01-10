#include "ctest.h"
#include "parser.h"
#include "ast.h"
#include "lexer.h"
#include "string_t.h"
#include "token.h"
#include "tokenizer.h"
#include "xalloc.h"
#include "lower.h"
#include "logging.h"

/* ============================================================================
 * Helpers
 * ============================================================================ */

static ast_node_t* parse_string(const char* input)
{
    gnode_t* top_node = parser_parse_string(input);

    if (top_node == NULL)
    {
        printf("Failed to parse: %s\n", input);
        return NULL;
    }

    ast_t* ast = ast_lower(top_node);

    g_node_destroy(&top_node);
    return ast;
}

/**
 * Helper to navigate from AST_PROGRAM to the first simple command.
 * Returns NULL if the structure doesn't match expectations.
 * Does NOT destroy ast on failure - caller is responsible.
 */
static ast_node_t* get_first_simple_command(CTest* ctest, ast_node_t* ast)
{
    CTEST_ASSERT_NOT_NULL(ctest, ast, "ast is not null");
    if (!ast) return NULL;

    ast_node_t* body = ast;
    CTEST_ASSERT_NOT_NULL(ctest, body, "program has body");
    if (!body) return NULL;

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(body), AST_COMMAND_LIST, "body is command list");
    if (ast_node_get_type(body) != AST_COMMAND_LIST) return NULL;

    CTEST_ASSERT_NOT_NULL(ctest, body->data.command_list.items, "command list has items");
    if (!body->data.command_list.items) return NULL;

    CTEST_ASSERT_GT(ctest, body->data.command_list.items->size, 0, "command list is not empty");
    if (body->data.command_list.items->size == 0) return NULL;

    ast_node_t* cmd = body->data.command_list.items->nodes[0];
    CTEST_ASSERT_NOT_NULL(ctest, cmd, "first command exists");
    if (!cmd) return NULL;

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(cmd), AST_SIMPLE_COMMAND, "first command is simple command");
    if (ast_node_get_type(cmd) != AST_SIMPLE_COMMAND) return NULL;

    return cmd;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

CTEST(test_parser_heredoc_basic)
{
    const char* src =
        "cat <<EOF\n"
        "hello\n"
        "EOF\n";

    ast_node_t* ast = parse_string(src);
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* cmd = get_first_simple_command(ctest, ast);
    if (!cmd)
    {
        ast_node_destroy(&ast);
        return;
    }

    CTEST_ASSERT_NOT_NULL(ctest, cmd->data.simple_command.redirections, "has redirections");
    if (!cmd->data.simple_command.redirections)
    {
        ast_node_destroy(&ast);
        return;
    }

    CTEST_ASSERT_EQ(ctest, cmd->data.simple_command.redirections->size, 1, "one redirection");
    if (cmd->data.simple_command.redirections->size != 1)
    {
        ast_node_destroy(&ast);
        return;
    }

    ast_node_t* redir = cmd->data.simple_command.redirections->nodes[0];
    CTEST_ASSERT_NOT_NULL(ctest, redir, "redirection exists");
    if (!redir)
    {
        ast_node_destroy(&ast);
        return;
    }

    CTEST_ASSERT_EQ(ctest, redir->data.redirection.redir_type, REDIR_FROM_BUFFER, "redir is heredoc");
    CTEST_ASSERT_NOT_NULL(ctest, redir->data.redirection.buffer, "has heredoc content");
    if (redir->data.redirection.buffer)
    {
        CTEST_ASSERT_STR_EQ(ctest, string_data(redir->data.redirection.buffer), "hello\n", "content matches");
    }

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_heredoc_quoted_delimiter)
{
    const char* src =
        "cat <<'EOF'\n"
        "$HOME \\` \\$ \\n stays\n"
        "EOF\n";

    ast_node_t* ast = parse_string(src);
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* cmd = get_first_simple_command(ctest, ast);
    if (!cmd)
    {
        ast_node_destroy(&ast);
        return;
    }

    CTEST_ASSERT_NOT_NULL(ctest, cmd->data.simple_command.redirections, "has redirections");
    if (!cmd->data.simple_command.redirections)
    {
        ast_node_destroy(&ast);
        return;
    }

    CTEST_ASSERT_GT(ctest, cmd->data.simple_command.redirections->size, 0, "has at least one redirection");
    if (cmd->data.simple_command.redirections->size == 0)
    {
        ast_node_destroy(&ast);
        return;
    }

    ast_node_t* redir = cmd->data.simple_command.redirections->nodes[0];
    CTEST_ASSERT_NOT_NULL(ctest, redir, "redirection exists");
    if (!redir)
    {
        ast_node_destroy(&ast);
        return;
    }

    CTEST_ASSERT_EQ(ctest, redir->data.redirection.redir_type, REDIR_FROM_BUFFER, "redir is heredoc");
    CTEST_ASSERT_NOT_NULL(ctest, redir->data.redirection.buffer, "has heredoc content");
    if (redir->data.redirection.buffer)
    {
        CTEST_ASSERT_STR_EQ(ctest, string_data(redir->data.redirection.buffer), "$HOME \\` \\$ \\n stays\n", "quoted content literal");
    }

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_heredoc_strip_tabs)
{
    const char* src =
        "cat <<-EOF\n"
        "\tline\n"
        "\tEOF\n";

    ast_node_t* ast = parse_string(src);
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* cmd = get_first_simple_command(ctest, ast);
    if (!cmd)
    {
        ast_node_destroy(&ast);
        return;
    }

    CTEST_ASSERT_NOT_NULL(ctest, cmd->data.simple_command.redirections, "has redirections");
    if (!cmd->data.simple_command.redirections)
    {
        ast_node_destroy(&ast);
        return;
    }

    CTEST_ASSERT_GT(ctest, cmd->data.simple_command.redirections->size, 0, "has at least one redirection");
    if (cmd->data.simple_command.redirections->size == 0)
    {
        ast_node_destroy(&ast);
        return;
    }

    ast_node_t* redir = cmd->data.simple_command.redirections->nodes[0];
    CTEST_ASSERT_NOT_NULL(ctest, redir, "redirection exists");
    if (!redir)
    {
        ast_node_destroy(&ast);
        return;
    }

    CTEST_ASSERT_EQ(ctest, redir->data.redirection.redir_type, REDIR_FROM_BUFFER_STRIP, "redir is heredoc strip");
    CTEST_ASSERT_NOT_NULL(ctest, redir->data.redirection.buffer, "has heredoc content");
    if (redir->data.redirection.buffer)
    {
        CTEST_ASSERT_STR_EQ(ctest, string_data(redir->data.redirection.buffer), "line\n", "tabs stripped in content");
    }

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_two_heredocs)
{
    const char* src =
        "cat <<A <<-B\n"
        "x\n"
        "A\n"
        "\ty\n"
        "B\n";

    ast_node_t* ast = parse_string(src);
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* cmd = get_first_simple_command(ctest, ast);
    if (!cmd)
    {
        ast_node_destroy(&ast);
        return;
    }

    CTEST_ASSERT_NOT_NULL(ctest, cmd->data.simple_command.redirections, "has redirections");
    if (!cmd->data.simple_command.redirections)
    {
        ast_node_destroy(&ast);
        return;
    }

    CTEST_ASSERT_EQ(ctest, cmd->data.simple_command.redirections->size, 2, "two redirections");
    if (cmd->data.simple_command.redirections->size != 2)
    {
        ast_node_destroy(&ast);
        return;
    }

    ast_node_t* r0 = cmd->data.simple_command.redirections->nodes[0];
    ast_node_t* r1 = cmd->data.simple_command.redirections->nodes[1];

    CTEST_ASSERT_NOT_NULL(ctest, r0, "first redirection exists");
    CTEST_ASSERT_NOT_NULL(ctest, r1, "second redirection exists");
    if (!r0 || !r1)
    {
        ast_node_destroy(&ast);
        return;
    }

    CTEST_ASSERT_EQ(ctest, r0->data.redirection.redir_type, REDIR_FROM_BUFFER, "first is <<");
    CTEST_ASSERT_EQ(ctest, r1->data.redirection.redir_type, REDIR_FROM_BUFFER_STRIP, "second is <<-");

    CTEST_ASSERT_NOT_NULL(ctest, r0->data.redirection.buffer, "first has buffer (heredoc) content");
    CTEST_ASSERT_NOT_NULL(ctest, r1->data.redirection.buffer, "second has buffer (heredoc) content");

    if (r0->data.redirection.buffer)
    {
        CTEST_ASSERT_STR_EQ(ctest, string_data(r0->data.redirection.buffer), "x\n", "first content");
    }
    if (r1->data.redirection.buffer)
    {
        CTEST_ASSERT_STR_EQ(ctest, string_data(r1->data.redirection.buffer), "y\n", "second content tab-stripped");
    }

    ast_node_destroy(&ast);
    (void)ctest;
}

int main(void)
{
    arena_start();
    log_init();

    CTestEntry* suite[] = {
        CTEST_ENTRY(test_parser_heredoc_basic),
        CTEST_ENTRY(test_parser_heredoc_quoted_delimiter),
        CTEST_ENTRY(test_parser_heredoc_strip_tabs),
        CTEST_ENTRY(test_parser_two_heredocs),
        NULL
    };

    int result = ctest_run_suite(suite);

    arena_end();
    return result;
}

#include "ctest.h"
#include "parser.h"
#include "ast.h"
#include "lexer.h"
#include "string_t.h"
#include "token.h"
#include "tokenizer.h"
#include "xalloc.h"
#include "logging.h"

/* ============================================================================
 * Helpers (copied from other AST tests for consistency)
 * ============================================================================ */
static token_list_t *lex_and_tokenize(const char *input)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, input);

    token_list_t *tokens = token_list_create();
    lex_status_t lex_status = lexer_tokenize(lx, tokens, NULL);

    lexer_destroy(&lx);

    if (lex_status != LEX_OK)
    {
        token_list_destroy(&tokens);
        return NULL;
    }

    tokenizer_t *tok = tokenizer_create(NULL);
    token_list_t *output = token_list_create();

    tok_status_t tok_status = tokenizer_process(tok, tokens, output);
    tokenizer_destroy(&tok);

    if (tok_status != TOK_OK)
    {
        token_list_destroy(&tokens);
        token_list_destroy(&output);
        return NULL;
    }

    token_list_destroy(&tokens);
    return output;
}

static ast_node_t *parse_string(const char *input)
{
    token_list_t *tokens = lex_and_tokenize(input);
    if (tokens == NULL)
    {
        printf("Failed to lex/tokenize: %s\n", input);
        return NULL;
    }

    parser_t *parser = parser_create();
    ast_node_t *ast = NULL;

    // FIXME: NEW API
    abort();
    // parse_status_t status = parser_parse(parser, tokens, &ast);
	parse_status_t status = PARSE_OK; // Placeholder

    if (status != PARSE_OK)
    {
        const char *err = parser_get_error(parser);
        printf("Parse error for input '%s': %s\n", input, err ? err : "unknown");
        parser_destroy(&parser);
        token_list_destroy(&tokens);
        return NULL;
    }

    parser_destroy(&parser);

    // AST now owns tokens
    token_list_release_tokens(tokens);
    xfree(tokens->tokens);
    xfree(tokens);

    return ast;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

CTEST(test_parser_heredoc_basic)
{
    const char *src =
        "cat <<EOF\n"
        "hello\n"
        "EOF\n";

    ast_node_t *ast = parse_string(src);
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");

    if (ast)
    {
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(ast), AST_COMMAND_LIST, "root is command list");
        CTEST_ASSERT(ctest, ast->data.command_list.items->size > 0, "has items");
        ast_node_t *cmd = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(cmd), AST_SIMPLE_COMMAND, "is simple command");
        CTEST_ASSERT_NOT_NULL(ctest, cmd->data.simple_command.redirections, "has redirections");
        CTEST_ASSERT_EQ(ctest, cmd->data.simple_command.redirections->size, 1, "one redirection");

        ast_node_t *redir = cmd->data.simple_command.redirections->nodes[0];
        CTEST_ASSERT_EQ(ctest, redir->data.redirection.redir_type, REDIR_HEREDOC, "redir is heredoc");
        CTEST_ASSERT_NOT_NULL(ctest, redir->data.redirection.heredoc_content, "has heredoc content");
        CTEST_ASSERT_STR_EQ(ctest, string_data(redir->data.redirection.heredoc_content), "hello\n", "content matches");

        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_heredoc_quoted_delimiter)
{
    const char *src =
        "cat <<'EOF'\n"
        "$HOME \\` \\$ \\n stays\n"
        "EOF\n";

    ast_node_t *ast = parse_string(src);
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (ast)
    {
        ast_node_t *cmd = ast->data.command_list.items->nodes[0];
        ast_node_t *redir = cmd->data.simple_command.redirections->nodes[0];
        CTEST_ASSERT_EQ(ctest, redir->data.redirection.redir_type, REDIR_HEREDOC, "redir is heredoc");
        CTEST_ASSERT_NOT_NULL(ctest, redir->data.redirection.heredoc_content, "has heredoc content");
        CTEST_ASSERT_STR_EQ(ctest, string_data(redir->data.redirection.heredoc_content), "$HOME \\` \\$ \\n stays\n", "quoted content literal");
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_heredoc_strip_tabs)
{
    const char *src =
        "cat <<-EOF\n"
        "\tline\n"
        "\tEOF\n";

    ast_node_t *ast = parse_string(src);
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (ast)
    {
        ast_node_t *cmd = ast->data.command_list.items->nodes[0];
        ast_node_t *redir = cmd->data.simple_command.redirections->nodes[0];
        CTEST_ASSERT_EQ(ctest, redir->data.redirection.redir_type, REDIR_HEREDOC_STRIP, "redir is heredoc strip");
        CTEST_ASSERT_STR_EQ(ctest, string_data(redir->data.redirection.heredoc_content), "line\n", "tabs stripped in content");
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_two_heredocs)
{
    const char *src =
        "cat <<A <<-B\n"
        "x\n"
        "A\n"
        "\ty\n"
        "B\n";

    ast_node_t *ast = parse_string(src);
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (ast)
    {
        ast_node_t *cmd = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_NOT_NULL(ctest, cmd->data.simple_command.redirections, "has redirs");
        CTEST_ASSERT_EQ(ctest, cmd->data.simple_command.redirections->size, 2, "two redirs");

        ast_node_t *r0 = cmd->data.simple_command.redirections->nodes[0];
        ast_node_t *r1 = cmd->data.simple_command.redirections->nodes[1];
        CTEST_ASSERT_EQ(ctest, r0->data.redirection.redir_type, REDIR_HEREDOC, "first is <<");
        CTEST_ASSERT_EQ(ctest, r1->data.redirection.redir_type, REDIR_HEREDOC_STRIP, "second is <<-");
        CTEST_ASSERT_STR_EQ(ctest, string_data(r0->data.redirection.heredoc_content), "x\n", "first content");
        CTEST_ASSERT_STR_EQ(ctest, string_data(r1->data.redirection.heredoc_content), "y\n", "second content tab-stripped");
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

int main(void)
{
    arena_start();
    log_init();

    CTestEntry *suite[] = {
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

#include "ctest.h"
#include "lexer.h"
#include "lexer_normal.h"
#include "lexer_heredoc.h"
#include "token.h"
#include "string_t.h"
#include "xalloc.h"

/* ============================================================================
 * Heredoc Tests
 * ============================================================================ */

// Test that heredoc processing can be initiated
CTEST(test_heredoc_mode_exists)
{
    lexer_t *lx = lexer_create();

    // Verify the lexer can be created and heredoc mode exists
    CTEST_ASSERT_NOT_NULL(ctest, lx, "lexer created successfully");
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.size, 0, "heredoc queue starts empty");

    lexer_destroy(&lx);
    (void)ctest;
}

// Test heredoc queue functionality
CTEST(test_heredoc_queue)
{
    lexer_t *lx = lexer_create();

    // Queue a heredoc
    string_t *delim = string_create_from_cstr("EOF");
    lexer_queue_heredoc(lx, delim, false, false);

    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.size, 1, "heredoc was queued");
    CTEST_ASSERT_NOT_NULL(ctest, lx->heredoc_queue.entries[0].delimiter, "delimiter was stored");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(lx->heredoc_queue.entries[0].delimiter), "EOF",
                        "delimiter is correct");
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.entries[0].strip_tabs, false, "strip_tabs is false");
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.entries[0].delimiter_quoted, false,
                    "delimiter_quoted is false");

    string_destroy(&delim);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test heredoc queue with strip_tabs
CTEST(test_heredoc_queue_strip_tabs)
{
    lexer_t *lx = lexer_create();

    string_t *delim = string_create_from_cstr("EOF");
    lexer_queue_heredoc(lx, delim, true, false);

    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.entries[0].strip_tabs, true, "strip_tabs is true");

    string_destroy(&delim);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test heredoc queue with quoted delimiter
CTEST(test_heredoc_queue_quoted)
{
    lexer_t *lx = lexer_create();

    string_t *delim = string_create_from_cstr("EOF");
    lexer_queue_heredoc(lx, delim, false, true);

    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.entries[0].delimiter_quoted, true,
                    "delimiter_quoted is true");

    string_destroy(&delim);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test multiple heredocs can be queued
CTEST(test_heredoc_multiple_queue)
{
    lexer_t *lx = lexer_create();

    string_t *delim1 = string_create_from_cstr("EOF1");
    string_t *delim2 = string_create_from_cstr("EOF2");

    lexer_queue_heredoc(lx, delim1, false, false);
    lexer_queue_heredoc(lx, delim2, true, true);

    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.size, 2, "two heredocs queued");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(lx->heredoc_queue.entries[0].delimiter), "EOF1",
                        "first delimiter is correct");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(lx->heredoc_queue.entries[1].delimiter), "EOF2",
                        "second delimiter is correct");
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.entries[1].strip_tabs, true,
                    "second heredoc has strip_tabs");
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.entries[1].delimiter_quoted, true,
                    "second heredoc has delimiter_quoted");

    string_destroy(&delim1);
    string_destroy(&delim2);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test heredoc body processing with simple content
CTEST(test_heredoc_body_simple)
{
    lexer_t *lx = lexer_create();

    // Setup: queue a heredoc and set up input with body
    string_t *delim = string_create_from_cstr("EOF");
    lexer_queue_heredoc(lx, delim, false, false);
    lexer_append_input_cstr(lx, "hello world\nEOF\n");

    // Set up the lexer state to read heredoc
    lx->reading_heredoc = true;
    lx->heredoc_index = 0;
    lexer_push_mode(lx, LEX_HEREDOC_BODY);

    // Try to process the heredoc body
    lex_status_t status = lexer_process_heredoc_body(lx);

    // Should complete successfully
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "heredoc body processed successfully");

    string_destroy(&delim);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test heredoc body processing with tabs to strip
CTEST(test_heredoc_body_strip_tabs)
{
    lexer_t *lx = lexer_create();

    // Queue a heredoc with strip_tabs enabled
    string_t *delim = string_create_from_cstr("EOF");
    lexer_queue_heredoc(lx, delim, true, false);
    lexer_append_input_cstr(lx, "\thello world\n\tEOF\n");

    lx->reading_heredoc = true;
    lx->heredoc_index = 0;
    lexer_push_mode(lx, LEX_HEREDOC_BODY);

    lex_status_t status = lexer_process_heredoc_body(lx);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "heredoc body with tabs processed");

    string_destroy(&delim);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test heredoc incomplete when delimiter not found
CTEST(test_heredoc_incomplete)
{
    lexer_t *lx = lexer_create();

    string_t *delim = string_create_from_cstr("EOF");
    lexer_queue_heredoc(lx, delim, false, false);
    lexer_append_input_cstr(lx, "hello world\n");

    lx->reading_heredoc = true;
    lx->heredoc_index = 0;
    lexer_push_mode(lx, LEX_HEREDOC_BODY);

    lex_status_t status = lexer_process_heredoc_body(lx);

    CTEST_ASSERT_EQ(ctest, status, LEX_INCOMPLETE, "returns INCOMPLETE when delimiter not found");

    string_destroy(&delim);
    lexer_destroy(&lx);
    (void)ctest;
}

// Integration test: heredoc with << operator and unquoted delimiter
CTEST(test_heredoc_integration_unquoted)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "cat <<EOF\nhello world\nEOF\n");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize completes successfully");
    CTEST_ASSERT_TRUE(ctest, token_list_size(tokens) >= 3, "at least 3 tokens produced");

    // Should have: WORD(cat), DLESS(<<), NEWLINE, and possibly more
    token_t *tok0 = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_get_type(tok0), TOKEN_WORD, "first token is WORD");

    token_t *tok1 = token_list_get(tokens, 1);
    CTEST_ASSERT_EQ(ctest, token_get_type(tok1), TOKEN_DLESS, "second token is DLESS");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Integration test: heredoc with <<- operator (tab stripping)
CTEST(test_heredoc_integration_strip_tabs)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "cat <<-EOF\n\thello\n\tEOF\n");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize completes successfully");

    // Should have: WORD(cat), DLESSDASH(<<-), NEWLINE
    token_t *tok1 = token_list_get(tokens, 1);
    CTEST_ASSERT_EQ(ctest, token_get_type(tok1), TOKEN_DLESSDASH, "second token is DLESSDASH");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Integration test: heredoc with quoted delimiter
CTEST(test_heredoc_integration_quoted_delimiter)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "cat <<'EOF'\nhello world\nEOF\n");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize completes successfully");
    CTEST_ASSERT_TRUE(ctest, token_list_size(tokens) >= 2, "at least 2 tokens produced");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Integration test: heredoc with double-quoted delimiter
CTEST(test_heredoc_integration_dquoted_delimiter)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "cat <<\"EOF\"\nhello world\nEOF\n");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize completes successfully");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    arena_start();

    CTestEntry *suite[] = {
        CTEST_ENTRY(test_heredoc_mode_exists),
        CTEST_ENTRY(test_heredoc_queue),
        CTEST_ENTRY(test_heredoc_queue_strip_tabs),
        CTEST_ENTRY(test_heredoc_queue_quoted),
        CTEST_ENTRY(test_heredoc_multiple_queue),
        CTEST_ENTRY(test_heredoc_body_simple),
        CTEST_ENTRY(test_heredoc_body_strip_tabs),
        CTEST_ENTRY(test_heredoc_incomplete),
        CTEST_ENTRY(test_heredoc_integration_unquoted),
        CTEST_ENTRY(test_heredoc_integration_strip_tabs),
        CTEST_ENTRY(test_heredoc_integration_quoted_delimiter),
        CTEST_ENTRY(test_heredoc_integration_dquoted_delimiter),
        NULL
    };

    int result = ctest_run_suite(suite);

    arena_end();

    return result;
}

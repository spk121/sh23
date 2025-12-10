#include "ctest.h"
#include "alias_store.h"
#include "lexer.h"
#include "string_t.h"
#include "token.h"
#include "tokenizer.h"
#include "xalloc.h"

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

// Helper to lex a string into tokens
static token_list_t *lex_string(const char *input)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, input);

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    lexer_destroy(lx);

    if (status != LEX_OK)
    {
        token_list_destroy(tokens);
        return NULL;
    }

    return tokens;
}

/* ============================================================================
 * Basic Tokenization Tests (no aliases)
 * ============================================================================ */

CTEST(test_tokenizer_create_destroy)
{
    tokenizer_t *tok = tokenizer_create(NULL);
    CTEST_ASSERT_NOT_NULL(ctest, tok, "tokenizer created");
    tokenizer_destroy(tok);
    (void)ctest;
}

CTEST(test_tokenizer_passthrough_no_aliases)
{
    // Test that tokenizer passes through tokens unchanged when no aliases
    token_list_t *input = lex_string("echo hello world");
    CTEST_ASSERT_NOT_NULL(ctest, input, "lexing succeeded");
    if (input == NULL)
        return;

    tokenizer_t *tok = tokenizer_create(NULL);
    token_list_t *output = token_list_create();

    tok_status_t status = tokenizer_process(tok, input, output);

    CTEST_ASSERT_EQ(ctest, status, TOK_OK, "tokenizer status is TOK_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(output), 3, "three tokens in output");

    token_list_destroy(input);
    token_list_destroy(output);
    tokenizer_destroy(tok);
    (void)ctest;
}

CTEST(test_tokenizer_empty_input)
{
    token_list_t *input = token_list_create();

    tokenizer_t *tok = tokenizer_create(NULL);
    token_list_t *output = token_list_create();

    tok_status_t status = tokenizer_process(tok, input, output);

    CTEST_ASSERT_EQ(ctest, status, TOK_OK, "tokenizer status is TOK_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(output), 0, "no tokens in output");

    token_list_destroy(input);
    token_list_destroy(output);
    tokenizer_destroy(tok);
    (void)ctest;
}

/* ============================================================================
 * Simple alias_t Expansion Tests
 * ============================================================================ */

CTEST(test_tokenizer_simple_alias)
{
    // Test simple alias: ll -> ls -l
    alias_store_t *aliases = alias_store_create();
    alias_store_add_cstr(aliases, "ll", "ls -l");

    token_list_t *input = lex_string("ll");
    CTEST_ASSERT_NOT_NULL(ctest, input, "lexing succeeded");
    if (input == NULL)
        return;

    tokenizer_t *tok = tokenizer_create(aliases);
    token_list_t *output = token_list_create();

    tok_status_t status = tokenizer_process(tok, input, output);

    CTEST_ASSERT_EQ(ctest, status, TOK_OK, "tokenizer status is TOK_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(output), 2, "two tokens in output (ls -l)");

    token_list_destroy(input);
    token_list_destroy(output);
    tokenizer_destroy(tok);
    alias_store_destroy(&aliases);
    (void)ctest;
}

CTEST(test_tokenizer_alias_with_args)
{
    // Test alias with arguments: ll file.txt -> ls -l file.txt
    alias_store_t *aliases = alias_store_create();
    alias_store_add_cstr(aliases, "ll", "ls -l");

    token_list_t *input = lex_string("ll file.txt");
    CTEST_ASSERT_NOT_NULL(ctest, input, "lexing succeeded");
    if (input == NULL)
        return;

    tokenizer_t *tok = tokenizer_create(aliases);
    token_list_t *output = token_list_create();

    tok_status_t status = tokenizer_process(tok, input, output);

    CTEST_ASSERT_EQ(ctest, status, TOK_OK, "tokenizer status is TOK_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(output), 3, "three tokens in output (ls -l file.txt)");

    token_list_destroy(input);
    token_list_destroy(output);
    tokenizer_destroy(tok);
    alias_store_destroy(&aliases);
    (void)ctest;
}

CTEST(test_tokenizer_no_alias_when_quoted)
{
    // Test that quoted words are not expanded
    alias_store_t *aliases = alias_store_create();
    alias_store_add_cstr(aliases, "ll", "ls -l");

    token_list_t *input = lex_string("'ll'");
    CTEST_ASSERT_NOT_NULL(ctest, input, "lexing succeeded");
    if (input == NULL)
        return;

    tokenizer_t *tok = tokenizer_create(aliases);
    token_list_t *output = token_list_create();

    tok_status_t status = tokenizer_process(tok, input, output);

    CTEST_ASSERT_EQ(ctest, status, TOK_OK, "tokenizer status is TOK_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(output), 1, "one token in output (ll not expanded)");

    token_list_destroy(input);
    token_list_destroy(output);
    tokenizer_destroy(tok);
    alias_store_destroy(&aliases);
    (void)ctest;
}

CTEST(test_tokenizer_no_alias_not_at_command)
{
    // Test that aliases are only expanded at command position
    alias_store_t *aliases = alias_store_create();
    alias_store_add_cstr(aliases, "file", "myfile.txt");

    token_list_t *input = lex_string("cat file");
    CTEST_ASSERT_NOT_NULL(ctest, input, "lexing succeeded");
    if (input == NULL)
        return;

    tokenizer_t *tok = tokenizer_create(aliases);
    token_list_t *output = token_list_create();

    tok_status_t status = tokenizer_process(tok, input, output);

    CTEST_ASSERT_EQ(ctest, status, TOK_OK, "tokenizer status is TOK_OK");
    // 'cat' is not expanded (not an alias), 'file' is not expanded (not at command position)
    CTEST_ASSERT_EQ(ctest, token_list_size(output), 2, "two tokens in output (cat file)");

    token_list_destroy(input);
    token_list_destroy(output);
    tokenizer_destroy(tok);
    alias_store_destroy(&aliases);
    (void)ctest;
}

/* ============================================================================
 * alias_t with Trailing Blank Tests
 * ============================================================================ */

CTEST(test_tokenizer_alias_trailing_blank)
{
    // Test alias with trailing blank: the next word should also be checked
    // Example: nohup -> nohup  (with trailing space)
    //          bg -> background_command
    // So "nohup bg" should expand to "nohup background_command"
    alias_store_t *aliases = alias_store_create();
    alias_store_add_cstr(aliases, "nohup", "nohup ");  // trailing space
    alias_store_add_cstr(aliases, "bg", "background_command");

    token_list_t *input = lex_string("nohup bg");
    CTEST_ASSERT_NOT_NULL(ctest, input, "lexing succeeded");
    if (input == NULL)
        return;

    tokenizer_t *tok = tokenizer_create(aliases);
    token_list_t *output = token_list_create();

    tok_status_t status = tokenizer_process(tok, input, output);

    CTEST_ASSERT_EQ(ctest, status, TOK_OK, "tokenizer status is TOK_OK");
    // Should expand to "nohup background_command"
    CTEST_ASSERT_EQ(ctest, token_list_size(output), 2, "two tokens in output");

    token_list_destroy(input);
    token_list_destroy(output);
    tokenizer_destroy(tok);
    alias_store_destroy(&aliases);
    (void)ctest;
}

/* ============================================================================
 * Recursive alias_t Prevention Tests
 * ============================================================================ */

CTEST(test_tokenizer_prevent_direct_recursion)
{
    // Test that direct recursion is prevented: ls -> ls -l
    alias_store_t *aliases = alias_store_create();
    alias_store_add_cstr(aliases, "ls", "ls -l");

    token_list_t *input = lex_string("ls");
    CTEST_ASSERT_NOT_NULL(ctest, input, "lexing succeeded");
    if (input == NULL)
        return;

    tokenizer_t *tok = tokenizer_create(aliases);
    token_list_t *output = token_list_create();

    tok_status_t status = tokenizer_process(tok, input, output);

    CTEST_ASSERT_EQ(ctest, status, TOK_OK, "tokenizer status is TOK_OK");
    // Should expand to "ls -l" but not recurse on the first 'ls'
    CTEST_ASSERT_EQ(ctest, token_list_size(output), 2, "two tokens in output");

    token_list_destroy(input);
    token_list_destroy(output);
    tokenizer_destroy(tok);
    alias_store_destroy(&aliases);
    (void)ctest;
}

CTEST(test_tokenizer_prevent_indirect_recursion)
{
    // Test that indirect recursion is prevented: a -> b, b -> a
    alias_store_t *aliases = alias_store_create();
    alias_store_add_cstr(aliases, "a", "b");
    alias_store_add_cstr(aliases, "b", "a");

    token_list_t *input = lex_string("a");
    CTEST_ASSERT_NOT_NULL(ctest, input, "lexing succeeded");
    if (input == NULL)
        return;

    tokenizer_t *tok = tokenizer_create(aliases);
    token_list_t *output = token_list_create();

    tok_status_t status = tokenizer_process(tok, input, output);

    CTEST_ASSERT_EQ(ctest, status, TOK_OK, "tokenizer status is TOK_OK");
    // Should expand a -> b, then b -> a, then stop (a already expanded)
    CTEST_ASSERT_EQ(ctest, token_list_size(output), 1, "one token in output");

    token_list_destroy(input);
    token_list_destroy(output);
    tokenizer_destroy(tok);
    alias_store_destroy(&aliases);
    (void)ctest;
}

/* ============================================================================
 * Multiple Commands Tests
 * ============================================================================ */

CTEST(test_tokenizer_multiple_commands)
{
    // Test that aliases are expanded separately for each command
    alias_store_t *aliases = alias_store_create();
    alias_store_add_cstr(aliases, "ll", "ls -l");

    token_list_t *input = lex_string("ll ; ll");
    CTEST_ASSERT_NOT_NULL(ctest, input, "lexing succeeded");
    if (input == NULL)
        return;

    tokenizer_t *tok = tokenizer_create(aliases);
    token_list_t *output = token_list_create();

    tok_status_t status = tokenizer_process(tok, input, output);

    CTEST_ASSERT_EQ(ctest, status, TOK_OK, "tokenizer status is TOK_OK");
    // Should expand to "ls -l ; ls -l" = 5 tokens (ls, -l, ;, ls, -l)
    CTEST_ASSERT_EQ(ctest, token_list_size(output), 5, "five tokens in output");

    token_list_destroy(input);
    token_list_destroy(output);
    tokenizer_destroy(tok);
    alias_store_destroy(&aliases);
    (void)ctest;
}

CTEST(test_tokenizer_alias_in_pipeline)
{
    // Test alias expansion in pipelines
    alias_store_t *aliases = alias_store_create();
    alias_store_add_cstr(aliases, "ll", "ls -l");

    token_list_t *input = lex_string("ll | grep txt");
    CTEST_ASSERT_NOT_NULL(ctest, input, "lexing succeeded");
    if (input == NULL)
        return;

    tokenizer_t *tok = tokenizer_create(aliases);
    token_list_t *output = token_list_create();

    tok_status_t status = tokenizer_process(tok, input, output);

    CTEST_ASSERT_EQ(ctest, status, TOK_OK, "tokenizer status is TOK_OK");
    // Should expand to "ls -l | grep txt" = 5 tokens
    CTEST_ASSERT_EQ(ctest, token_list_size(output), 5, "five tokens in output");

    token_list_destroy(input);
    token_list_destroy(output);
    tokenizer_destroy(tok);
    alias_store_destroy(&aliases);
    (void)ctest;
}

/* ============================================================================
 * Complex alias_t Tests
 * ============================================================================ */

CTEST(test_tokenizer_alias_to_multiple_commands)
{
    // Test alias that expands to multiple commands
    alias_store_t *aliases = alias_store_create();
    alias_store_add_cstr(aliases, "update", "apt update && apt upgrade");

    token_list_t *input = lex_string("update");
    CTEST_ASSERT_NOT_NULL(ctest, input, "lexing succeeded");
    if (input == NULL)
        return;

    tokenizer_t *tok = tokenizer_create(aliases);
    token_list_t *output = token_list_create();

    tok_status_t status = tokenizer_process(tok, input, output);

    CTEST_ASSERT_EQ(ctest, status, TOK_OK, "tokenizer status is TOK_OK");
    // Should expand to "apt update && apt upgrade" = 5 tokens
    CTEST_ASSERT_EQ(ctest, token_list_size(output), 5, "five tokens in output");

    token_list_destroy(input);
    token_list_destroy(output);
    tokenizer_destroy(tok);
    alias_store_destroy(&aliases);
    (void)ctest;
}

/* ============================================================================
 * Main Test Entry Point
 * ============================================================================ */

int main(void)
{
    arena_start();

    CTestEntry *suite[] = {
        // Basic tests
        CTEST_ENTRY(test_tokenizer_create_destroy),
        CTEST_ENTRY(test_tokenizer_passthrough_no_aliases),
        CTEST_ENTRY(test_tokenizer_empty_input),

        // Simple alias expansion
        CTEST_ENTRY(test_tokenizer_simple_alias),
        CTEST_ENTRY(test_tokenizer_alias_with_args),
        CTEST_ENTRY(test_tokenizer_no_alias_when_quoted),
        CTEST_ENTRY(test_tokenizer_no_alias_not_at_command),

        // Trailing blank
        CTEST_ENTRY(test_tokenizer_alias_trailing_blank),

        // Recursion prevention
        CTEST_ENTRY(test_tokenizer_prevent_direct_recursion),
        CTEST_ENTRY(test_tokenizer_prevent_indirect_recursion),

        // Multiple commands
        CTEST_ENTRY(test_tokenizer_multiple_commands),
        CTEST_ENTRY(test_tokenizer_alias_in_pipeline),

        // Complex aliases
        CTEST_ENTRY(test_tokenizer_alias_to_multiple_commands),

        NULL
    };

    int result = ctest_run_suite(suite);

    arena_end();

    return result;
}

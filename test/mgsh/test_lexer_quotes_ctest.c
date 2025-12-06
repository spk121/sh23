#include "ctest.h"
#include "lexer.h"
#include "lexer_normal.h"
#include "lexer_squote.h"
#include "lexer_dquote.h"
#include "token.h"
#include "string_t.h"
#include "xalloc.h"

/* ============================================================================
 * Single Quote Tests
 * ============================================================================ */

// Test basic single-quoted string
CTEST(test_squote_basic)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "'hello'");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_get_type(tok), TOKEN_WORD, "token is WORD");
    CTEST_ASSERT_TRUE(ctest, token_was_quoted(tok), "token was quoted");
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 1, "one part");
    
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_LITERAL, "part is literal");
    CTEST_ASSERT_TRUE(ctest, part_was_single_quoted(part), "part was single-quoted");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "hello", "text is 'hello'");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test single-quoted string with special characters (should be literal)
CTEST(test_squote_special_chars)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "'$VAR `cmd` \\n \"quoted\"'");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    const char *expected = "$VAR `cmd` \\n \"quoted\"";
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), expected, "special chars are literal");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test single-quoted string with newlines
CTEST(test_squote_with_newline)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "'line1\nline2'");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "line1\nline2", "newline is preserved");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test empty single-quoted string
CTEST(test_squote_empty)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "''");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_TRUE(ctest, token_was_quoted(tok), "token was quoted");
    // Empty quoted string should still produce a WORD token with empty part
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test unclosed single quote
CTEST(test_squote_unclosed)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "'hello");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_INCOMPLETE, "unclosed quote returns INCOMPLETE");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test single-quoted string followed by text
CTEST(test_squote_with_suffix)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "'hello'world");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced (word continues)");
    
    // Token should have two parts: quoted 'hello' and unquoted 'world'
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 2, "two parts");
    
    part_t *part1 = token_get_part(tok, 0);
    CTEST_ASSERT_TRUE(ctest, part_was_single_quoted(part1), "first part single-quoted");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part1)), "hello", "first part is 'hello'");
    
    part_t *part2 = token_get_part(tok, 1);
    CTEST_ASSERT_FALSE(ctest, part_was_single_quoted(part2), "second part not quoted");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part2)), "world", "second part is 'world'");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

/* ============================================================================
 * Double Quote Tests
 * ============================================================================ */

// Test basic double-quoted string
CTEST(test_dquote_basic)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "\"hello\"");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_get_type(tok), TOKEN_WORD, "token is WORD");
    CTEST_ASSERT_TRUE(ctest, token_was_quoted(tok), "token was quoted");
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 1, "one part");
    
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_LITERAL, "part is literal");
    CTEST_ASSERT_TRUE(ctest, part_was_double_quoted(part), "part was double-quoted");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "hello", "text is 'hello'");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test double-quoted string with escape sequences
CTEST(test_dquote_escapes)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "\"a\\$b\\`c\\\"d\\\\e\"");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    // \$ -> $, \` -> `, \" -> ", \\ -> \  (becomes a$b`c"d\e)
    const char *expected = "a$b`c\"d\\e";
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), expected, "escape sequences resolved");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test double-quoted string with non-escapable backslash
CTEST(test_dquote_literal_backslash)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "\"a\\nb\"");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    // \n is NOT escapable in double quotes, so both \ and n are kept
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "a\\nb", "backslash+n literal");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test double-quoted string with line continuation
CTEST(test_dquote_line_continuation)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "\"hello\\\nworld\"");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    // \<newline> is consumed entirely
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "helloworld", "line continuation removed");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test empty double-quoted string
CTEST(test_dquote_empty)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "\"\"");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_TRUE(ctest, token_was_quoted(tok), "token was quoted");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test unclosed double quote
CTEST(test_dquote_unclosed)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "\"hello");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_INCOMPLETE, "unclosed quote returns INCOMPLETE");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test double-quoted string with literal special chars
CTEST(test_dquote_literal_metachars)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "\"a|b;c&d\"");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    // Metacharacters are literal inside double quotes
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "a|b;c&d", "metacharacters are literal");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test double-quoted string with single quotes inside (literal)
CTEST(test_dquote_with_squote)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "\"it's\"");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "it's", "single quote literal in dquote");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

/* ============================================================================
 * Combined Tests
 * ============================================================================ */

// Test mixed quoting in same word
CTEST(test_mixed_quotes)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "'single'\"double\"");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one combined token");
    
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 2, "two parts");
    
    part_t *part1 = token_get_part(tok, 0);
    CTEST_ASSERT_TRUE(ctest, part_was_single_quoted(part1), "first part single-quoted");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part1)), "single", "first is 'single'");
    
    part_t *part2 = token_get_part(tok, 1);
    CTEST_ASSERT_TRUE(ctest, part_was_double_quoted(part2), "second part double-quoted");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part2)), "double", "second is 'double'");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test unquoted text between quoted sections
CTEST(test_quoted_unquoted_mix)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "pre'mid'post");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one combined token");
    
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 3, "three parts");
    
    part_t *part1 = token_get_part(tok, 0);
    CTEST_ASSERT_FALSE(ctest, part_was_single_quoted(part1), "first part not quoted");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part1)), "pre", "first is 'pre'");
    
    part_t *part2 = token_get_part(tok, 1);
    CTEST_ASSERT_TRUE(ctest, part_was_single_quoted(part2), "middle part single-quoted");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part2)), "mid", "middle is 'mid'");
    
    part_t *part3 = token_get_part(tok, 2);
    CTEST_ASSERT_FALSE(ctest, part_was_single_quoted(part3), "last part not quoted");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part3)), "post", "last is 'post'");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

int main()
{
    arena_start();
    
    CTestEntry *suite[] = {
        // Single quote tests
        CTEST_ENTRY(test_squote_basic),
        CTEST_ENTRY(test_squote_special_chars),
        CTEST_ENTRY(test_squote_with_newline),
        CTEST_ENTRY(test_squote_empty),
        CTEST_ENTRY(test_squote_unclosed),
        CTEST_ENTRY(test_squote_with_suffix),
        // Double quote tests
        CTEST_ENTRY(test_dquote_basic),
        CTEST_ENTRY(test_dquote_escapes),
        CTEST_ENTRY(test_dquote_literal_backslash),
        CTEST_ENTRY(test_dquote_line_continuation),
        CTEST_ENTRY(test_dquote_empty),
        CTEST_ENTRY(test_dquote_unclosed),
        CTEST_ENTRY(test_dquote_literal_metachars),
        CTEST_ENTRY(test_dquote_with_squote),
        // Combined tests
        CTEST_ENTRY(test_mixed_quotes),
        CTEST_ENTRY(test_quoted_unquoted_mix),
        NULL
    };
    
    int result = ctest_run_suite(suite);
    
    arena_end();
    
    return result;
}

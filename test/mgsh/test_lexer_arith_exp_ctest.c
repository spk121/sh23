#include "ctest.h"
#include "lexer.h"
#include "lexer_normal.h"
#include "lexer_arith_exp.h"
#include "token.h"
#include "string.h"
#include "xalloc.h"

/* ============================================================================
 * Basic Arithmetic Expansion Tests
 * ============================================================================ */

// Test basic arithmetic expansion
CTEST(test_arith_exp_basic)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$((1+2))");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_get_type(tok), TOKEN_WORD, "token is WORD");
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 1, "one part");
    
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_ARITHMETIC, "part is arithmetic");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "1+2", "expression text is correct");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test empty arithmetic expansion
CTEST(test_arith_exp_empty)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$(())");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_ARITHMETIC, "part is arithmetic");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test unclosed arithmetic expansion
CTEST(test_arith_exp_unclosed)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$((1+2");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_INCOMPLETE, "unclosed expansion returns INCOMPLETE");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test unclosed with single paren
CTEST(test_arith_exp_unclosed_single_paren)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$((1+2)");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_INCOMPLETE, "unclosed expansion with single ) returns INCOMPLETE");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test arithmetic expansion with spaces
CTEST(test_arith_exp_with_spaces)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$(( 1 + 2 ))");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_ARITHMETIC, "part is arithmetic");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), " 1 + 2 ", "expression text preserves spaces");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

/* ============================================================================
 * Nested Parentheses Tests
 * ============================================================================ */

// Test arithmetic expansion with nested parentheses for grouping
CTEST(test_arith_exp_nested_parens)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$(( (1+2)*3 ))");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_ARITHMETIC, "part is arithmetic");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), " (1+2)*3 ", "nested parens preserved");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test arithmetic expansion with deeply nested parentheses
CTEST(test_arith_exp_deeply_nested_parens)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$(( ((1+2)) ))");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_ARITHMETIC, "part is arithmetic");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), " ((1+2)) ", "deeply nested parens preserved");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

/* ============================================================================
 * Variable Reference Tests
 * ============================================================================ */

// Test arithmetic expansion with variable reference
CTEST(test_arith_exp_with_variable)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$((x+1))");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_ARITHMETIC, "part is arithmetic");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "x+1", "variable reference preserved");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test arithmetic expansion with $variable reference
CTEST(test_arith_exp_with_dollar_variable)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$(($x+1))");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_ARITHMETIC, "part is arithmetic");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "$x+1", "$variable reference preserved");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test arithmetic expansion with braced parameter expansion
CTEST(test_arith_exp_with_braced_param)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$((${x}+1))");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_ARITHMETIC, "part is arithmetic");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "${x}+1", "braced param preserved");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

/* ============================================================================
 * Operator Tests
 * ============================================================================ */

// Test arithmetic expansion with various operators
CTEST(test_arith_exp_operators)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$((1+2-3*4/5%6))");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "1+2-3*4/5%6", "operators preserved");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test arithmetic expansion with comparison operators
CTEST(test_arith_exp_comparison_operators)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$((x<y))");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "x<y", "comparison operators preserved");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test arithmetic expansion with ternary operator
CTEST(test_arith_exp_ternary_operator)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$((x?1:0))");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "x?1:0", "ternary operator preserved");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

/* ============================================================================
 * Context Tests
 * ============================================================================ */

// Test arithmetic expansion with text before and after
CTEST(test_arith_exp_in_word)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "prefix$((1+2))suffix");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 3, "three parts");
    
    part_t *part1 = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part1), PART_LITERAL, "first part is literal");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part1)), "prefix", "prefix is correct");
    
    part_t *part2 = token_get_part(tok, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(part2), PART_ARITHMETIC, "second part is arithmetic");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part2)), "1+2", "expression is correct");
    
    part_t *part3 = token_get_part(tok, 2);
    CTEST_ASSERT_EQ(ctest, part_get_type(part3), PART_LITERAL, "third part is literal");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part3)), "suffix", "suffix is correct");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test arithmetic expansion inside double quotes
CTEST(test_arith_exp_in_dquote)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "\"$((1+2))\"");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_TRUE(ctest, token_was_quoted(tok), "token was quoted");
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 1, "one part");
    
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_ARITHMETIC, "part is arithmetic");
    CTEST_ASSERT_TRUE(ctest, part_was_double_quoted(part), "part was double-quoted");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test multiple arithmetic expansions in one word
CTEST(test_arith_exp_multiple)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$((1))$((2))");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 2, "two parts");
    
    part_t *part1 = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part1), PART_ARITHMETIC, "first part is arithmetic");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part1)), "1", "first expression correct");
    
    part_t *part2 = token_get_part(tok, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(part2), PART_ARITHMETIC, "second part is arithmetic");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part2)), "2", "second expression correct");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

/* ============================================================================
 * Mixed Expansion Tests
 * ============================================================================ */

// Test mixing arithmetic and command substitution
CTEST(test_arith_exp_mixed_with_cmd_subst)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$((1+2))$(echo hello)");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 2, "two parts");
    
    part_t *part1 = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part1), PART_ARITHMETIC, "first part is arithmetic");
    
    part_t *part2 = token_get_part(tok, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(part2), PART_COMMAND_SUBST, "second part is command subst");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test mixing command substitution and arithmetic
CTEST(test_arith_exp_cmd_subst_then_arith)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$(echo x)$((1+2))");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 2, "two parts");
    
    part_t *part1 = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part1), PART_COMMAND_SUBST, "first part is command subst");
    
    part_t *part2 = token_get_part(tok, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(part2), PART_ARITHMETIC, "second part is arithmetic");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

/* ============================================================================
 * Edge Case Tests
 * ============================================================================ */

// Test arithmetic expansion with single quote inside
CTEST(test_arith_exp_with_squote)
{
    lexer_t *lx = lexer_create();
    // Single quotes inside arithmetic are valid but unusual
    lexer_append_input_cstr(lx, "$(('1'+2))");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_ARITHMETIC, "part is arithmetic");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "'1'+2", "single quotes preserved");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test arithmetic expansion with backslash
CTEST(test_arith_exp_with_backslash)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$((1\\+2))");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_ARITHMETIC, "part is arithmetic");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "1\\+2", "backslash preserved");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test that ) inside nested parens doesn't close arithmetic expansion early
CTEST(test_arith_exp_paren_not_closing)
{
    lexer_t *lx = lexer_create();
    // The inner ) should not close the arithmetic expansion
    lexer_append_input_cstr(lx, "$((1+(2)))");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_ARITHMETIC, "part is arithmetic");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "1+(2)", "nested parens handled correctly");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test unbalanced parentheses - single ) at depth 0 followed by non-) character
CTEST(test_arith_exp_unbalanced_paren)
{
    lexer_t *lx = lexer_create();
    // Single ) at depth 0 followed by 'x' - this is unbalanced
    lexer_append_input_cstr(lx, "$((1+2)x))");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_ERROR, "unbalanced parens returns ERROR");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test unbalanced parentheses - extra closing paren inside expression
CTEST(test_arith_exp_extra_close_paren)
{
    lexer_t *lx = lexer_create();
    // Arithmetic expansion with unbalanced parentheses where a single )
    // at depth 0 is followed by 'x' before the required ))
    lexer_append_input_cstr(lx, "$((1)x))");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    // Should error because we have unbalanced parens
    CTEST_ASSERT_EQ(ctest, status, LEX_ERROR, "unbalanced parens returns ERROR");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test single ) at depth 0 at end of input - should be INCOMPLETE
CTEST(test_arith_exp_single_paren_eof)
{
    lexer_t *lx = lexer_create();
    // Single ) at depth 0 at end of input
    lexer_append_input_cstr(lx, "$((1+2)");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    // Should be INCOMPLETE because we need more input
    CTEST_ASSERT_EQ(ctest, status, LEX_INCOMPLETE, "single ) at EOF returns INCOMPLETE");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

int main()
{
    arena_start();
    
    CTestEntry *suite[] = {
        // Basic tests
        CTEST_ENTRY(test_arith_exp_basic),
        CTEST_ENTRY(test_arith_exp_empty),
        CTEST_ENTRY(test_arith_exp_unclosed),
        CTEST_ENTRY(test_arith_exp_unclosed_single_paren),
        CTEST_ENTRY(test_arith_exp_with_spaces),
        // Nested parentheses tests
        CTEST_ENTRY(test_arith_exp_nested_parens),
        CTEST_ENTRY(test_arith_exp_deeply_nested_parens),
        // Variable reference tests
        CTEST_ENTRY(test_arith_exp_with_variable),
        CTEST_ENTRY(test_arith_exp_with_dollar_variable),
        CTEST_ENTRY(test_arith_exp_with_braced_param),
        // Operator tests
        CTEST_ENTRY(test_arith_exp_operators),
        CTEST_ENTRY(test_arith_exp_comparison_operators),
        CTEST_ENTRY(test_arith_exp_ternary_operator),
        // Context tests
        CTEST_ENTRY(test_arith_exp_in_word),
        CTEST_ENTRY(test_arith_exp_in_dquote),
        CTEST_ENTRY(test_arith_exp_multiple),
        // Mixed expansion tests
        CTEST_ENTRY(test_arith_exp_mixed_with_cmd_subst),
        CTEST_ENTRY(test_arith_exp_cmd_subst_then_arith),
        // Edge case tests
        CTEST_ENTRY(test_arith_exp_with_squote),
        CTEST_ENTRY(test_arith_exp_with_backslash),
        CTEST_ENTRY(test_arith_exp_paren_not_closing),
        // Unbalanced parentheses tests
        CTEST_ENTRY(test_arith_exp_unbalanced_paren),
        CTEST_ENTRY(test_arith_exp_extra_close_paren),
        CTEST_ENTRY(test_arith_exp_single_paren_eof),
        NULL
    };
    
    int result = ctest_run_suite(suite);
    
    arena_end();
    
    return result;
}

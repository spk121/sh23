#include "ctest.h"
#include "lexer.h"
#include "lexer_normal.h"
#include "lexer_cmd_subst.h"
#include "token.h"
#include "string.h"
#include "xalloc.h"

/* ============================================================================
 * $(...) Command Substitution Tests
 * ============================================================================ */

// Test basic parenthesized command substitution
CTEST(test_cmd_subst_paren_basic)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$(echo hello)");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_get_type(tok), TOKEN_WORD, "token is WORD");
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 1, "one part");
    
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_COMMAND_SUBST, "part is command substitution");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "echo hello", "command text is correct");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test empty command substitution
CTEST(test_cmd_subst_paren_empty)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$()");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_COMMAND_SUBST, "part is command substitution");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test unclosed parenthesized command substitution
CTEST(test_cmd_subst_paren_unclosed)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$(echo hello");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_INCOMPLETE, "unclosed substitution returns INCOMPLETE");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test command substitution with nested parentheses
CTEST(test_cmd_subst_paren_nested_parens)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$(echo (foo))");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_COMMAND_SUBST, "part is command substitution");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "echo (foo)", "nested parens preserved");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test command substitution with text before and after
CTEST(test_cmd_subst_paren_in_word)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "prefix$(cmd)suffix");
    
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
    CTEST_ASSERT_EQ(ctest, part_get_type(part2), PART_COMMAND_SUBST, "second part is command subst");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part2)), "cmd", "command is correct");
    
    part_t *part3 = token_get_part(tok, 2);
    CTEST_ASSERT_EQ(ctest, part_get_type(part3), PART_LITERAL, "third part is literal");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part3)), "suffix", "suffix is correct");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test command substitution inside double quotes
CTEST(test_cmd_subst_paren_in_dquote)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "\"$(echo hello)\"");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_TRUE(ctest, token_was_quoted(tok), "token was quoted");
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 1, "one part");
    
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_COMMAND_SUBST, "part is command substitution");
    CTEST_ASSERT_TRUE(ctest, part_was_double_quoted(part), "part was double-quoted");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test command substitution with single quotes inside command
CTEST(test_cmd_subst_paren_with_squotes)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$(echo 'hello world')");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "echo 'hello world'", "single quotes preserved");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

/* ============================================================================
 * Backtick Command Substitution Tests
 * ============================================================================ */

// Test basic backtick command substitution
CTEST(test_cmd_subst_backtick_basic)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "`echo hello`");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_get_type(tok), TOKEN_WORD, "token is WORD");
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 1, "one part");
    
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_COMMAND_SUBST, "part is command substitution");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "echo hello", "command text is correct");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test empty backtick command substitution
CTEST(test_cmd_subst_backtick_empty)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "``");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_COMMAND_SUBST, "part is command substitution");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test unclosed backtick command substitution
CTEST(test_cmd_subst_backtick_unclosed)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "`echo hello");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_INCOMPLETE, "unclosed substitution returns INCOMPLETE");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test backtick with escaped characters
CTEST(test_cmd_subst_backtick_escaped)
{
    lexer_t *lx = lexer_create();
    // In backticks, \$ becomes $, \` becomes `, and \\ becomes a single backslash
    lexer_append_input_cstr(lx, "`echo \\$VAR \\` \\\\`");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    // Backslash escapes $, `, and backslash: \$ -> $, \` -> `, \\ -> single backslash
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "echo $VAR ` \\", "escape sequences resolved");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test backtick with literal backslash (non-escapable char)
CTEST(test_cmd_subst_backtick_literal_backslash)
{
    lexer_t *lx = lexer_create();
    // \n is NOT escapable in backticks, so both \ and n are kept
    lexer_append_input_cstr(lx, "`echo \\n`");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    
    token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part)), "echo \\n", "backslash+n literal");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test backtick with text before and after
CTEST(test_cmd_subst_backtick_in_word)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "prefix`cmd`suffix");
    
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
    CTEST_ASSERT_EQ(ctest, part_get_type(part2), PART_COMMAND_SUBST, "second part is command subst");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part2)), "cmd", "command is correct");
    
    part_t *part3 = token_get_part(tok, 2);
    CTEST_ASSERT_EQ(ctest, part_get_type(part3), PART_LITERAL, "third part is literal");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part3)), "suffix", "suffix is correct");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test backtick inside double quotes
CTEST(test_cmd_subst_backtick_in_dquote)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "\"`echo hello`\"");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_TRUE(ctest, token_was_quoted(tok), "token was quoted");
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 1, "one part");
    
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_COMMAND_SUBST, "part is command substitution");
    CTEST_ASSERT_TRUE(ctest, part_was_double_quoted(part), "part was double-quoted");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

/* ============================================================================
 * Combined Tests
 * ============================================================================ */

// Test multiple command substitutions in one word
CTEST(test_cmd_subst_multiple)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$(cmd1)$(cmd2)");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 2, "two parts");
    
    part_t *part1 = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part1), PART_COMMAND_SUBST, "first part is command subst");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part1)), "cmd1", "first command correct");
    
    part_t *part2 = token_get_part(tok, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(part2), PART_COMMAND_SUBST, "second part is command subst");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(part2)), "cmd2", "second command correct");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

// Test mixing $(...) and `...` forms
CTEST(test_cmd_subst_mixed_forms)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$(cmd1)`cmd2`");
    
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");
    
    token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 2, "two parts");
    
    part_t *part1 = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part1), PART_COMMAND_SUBST, "first part is command subst");
    
    part_t *part2 = token_get_part(tok, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(part2), PART_COMMAND_SUBST, "second part is command subst");
    
    token_list_destroy(tokens);
    lexer_destroy(lx);
    (void)ctest;
}

int main()
{
    arena_start();
    
    CTestEntry *suite[] = {
        // $(...) tests
        CTEST_ENTRY(test_cmd_subst_paren_basic),
        CTEST_ENTRY(test_cmd_subst_paren_empty),
        CTEST_ENTRY(test_cmd_subst_paren_unclosed),
        CTEST_ENTRY(test_cmd_subst_paren_nested_parens),
        CTEST_ENTRY(test_cmd_subst_paren_in_word),
        CTEST_ENTRY(test_cmd_subst_paren_in_dquote),
        CTEST_ENTRY(test_cmd_subst_paren_with_squotes),
        // Backtick tests
        CTEST_ENTRY(test_cmd_subst_backtick_basic),
        CTEST_ENTRY(test_cmd_subst_backtick_empty),
        CTEST_ENTRY(test_cmd_subst_backtick_unclosed),
        CTEST_ENTRY(test_cmd_subst_backtick_escaped),
        CTEST_ENTRY(test_cmd_subst_backtick_literal_backslash),
        CTEST_ENTRY(test_cmd_subst_backtick_in_word),
        CTEST_ENTRY(test_cmd_subst_backtick_in_dquote),
        // Combined tests
        CTEST_ENTRY(test_cmd_subst_multiple),
        CTEST_ENTRY(test_cmd_subst_mixed_forms),
        NULL
    };
    
    int result = ctest_run_suite(suite);
    
    arena_end();
    
    return result;
}

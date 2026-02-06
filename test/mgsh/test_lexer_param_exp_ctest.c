#include "ctest.h"

#define LEXER_INTERNAL
#include "lexer.h"
#include "lexer_normal.h"
#include "lexer_param_exp.h"
#include "token.h"
#include "string_t.h"
#include "xalloc.h"

/* ============================================================================
 * Unbraced Parameter Expansion Tests
 * ============================================================================ */

// Test simple unbraced parameter: $var
CTEST(test_param_unbraced_simple)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$var");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");

    const token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_get_type(tok), TOKEN_WORD, "token is WORD");
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 1, "one part");

    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_PARAMETER, "part is PARAMETER");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part)), "var", "param name is 'var'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test unbraced parameter with underscore: $my_var
CTEST(test_param_unbraced_underscore)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$my_var");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part)), "my_var", "param name is 'my_var'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test unbraced parameter with numbers: $var123
CTEST(test_param_unbraced_with_digits)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$var123");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part)), "var123", "param name is 'var123'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test positional parameter: $1
CTEST(test_param_unbraced_positional)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$1");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part)), "1", "param name is '1'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test special parameter: $?
CTEST(test_param_unbraced_special_question)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$?");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part)), "?", "param name is '?'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test special parameter: $$
CTEST(test_param_unbraced_special_dollar)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$$");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part)), "$", "param name is '$'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test special parameter: $@
CTEST(test_param_unbraced_special_at)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$@");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part)), "@", "param name is '@'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test special parameter: $*
CTEST(test_param_unbraced_special_star)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$*");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part)), "*", "param name is '*'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test special parameter: $#
CTEST(test_param_unbraced_special_hash)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$#");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part)), "#", "param name is '#'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test unbraced parameter with suffix: $var.txt
CTEST(test_param_unbraced_with_suffix)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$var.txt");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");

    const token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 2, "two parts");

    part_t *part1 = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part1), PART_PARAMETER, "first part is PARAMETER");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part1)), "var", "param name is 'var'");

    part_t *part2 = token_get_part(tok, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(part2), PART_LITERAL, "second part is LITERAL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_text(part2)), ".txt", "literal is '.txt'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

/* ============================================================================
 * Braced Parameter Expansion Tests
 * ============================================================================ */

// Test simple braced parameter: ${var}
CTEST(test_param_braced_simple)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "${var}");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_PARAMETER, "part is PARAMETER");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part)), "var", "param name is 'var'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test braced parameter with suffix: ${var}suffix
CTEST(test_param_braced_with_suffix)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "${var}suffix");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");

    const token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 2, "two parts");

    part_t *part1 = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part1)), "var", "param name is 'var'");

    part_t *part2 = token_get_part(tok, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_text(part2)), "suffix", "literal is 'suffix'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test length operator: ${#var}
CTEST(test_param_braced_length)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "${#var}");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part)), "var", "param name is 'var'");
    CTEST_ASSERT_EQ(ctest, part->param_kind, PARAM_LENGTH, "kind is PARAM_LENGTH");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test special parameter ${#}
CTEST(test_param_braced_special_hash)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "${#}");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part)), "#", "param name is '#'");
    CTEST_ASSERT_EQ(ctest, part->param_kind, PARAM_PLAIN, "kind is PARAM_PLAIN");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test use default: ${var:-word}
CTEST(test_param_braced_use_default)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "${var:-default}");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part)), "var", "param name is 'var'");
    CTEST_ASSERT_EQ(ctest, part->param_kind, PARAM_USE_DEFAULT, "kind is PARAM_USE_DEFAULT");
    CTEST_ASSERT_NOT_NULL(ctest, part->word, "word is set");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part->word), "default", "word is 'default'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test assign default: ${var:=word}
CTEST(test_param_braced_assign_default)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "${var:=value}");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part->param_kind, PARAM_ASSIGN_DEFAULT, "kind is PARAM_ASSIGN_DEFAULT");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part->word), "value", "word is 'value'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test error if unset: ${var:?message}
CTEST(test_param_braced_error_if_unset)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "${var:?error}");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part->param_kind, PARAM_ERROR_IF_UNSET, "kind is PARAM_ERROR_IF_UNSET");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part->word), "error", "word is 'error'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test use alternate: ${var:+word}
CTEST(test_param_braced_use_alternate)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "${var:+alternate}");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part->param_kind, PARAM_USE_ALTERNATE, "kind is PARAM_USE_ALTERNATE");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part->word), "alternate", "word is 'alternate'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test remove small suffix: ${var%pattern}
CTEST(test_param_braced_remove_small_suffix)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "${var%*.txt}");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part->param_kind, PARAM_REMOVE_SMALL_SUFFIX, "kind is PARAM_REMOVE_SMALL_SUFFIX");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part->word), "*.txt", "word is '*.txt'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test remove large suffix: ${var%%pattern}
CTEST(test_param_braced_remove_large_suffix)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "${var%%*.txt}");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part->param_kind, PARAM_REMOVE_LARGE_SUFFIX, "kind is PARAM_REMOVE_LARGE_SUFFIX");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part->word), "*.txt", "word is '*.txt'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test remove small prefix: ${var#pattern}
CTEST(test_param_braced_remove_small_prefix)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "${var#*/}");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part->param_kind, PARAM_REMOVE_SMALL_PREFIX, "kind is PARAM_REMOVE_SMALL_PREFIX");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part->word), "*/", "word is '*/'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test remove large prefix: ${var##pattern}
CTEST(test_param_braced_remove_large_prefix)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "${var##*/}");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part->param_kind, PARAM_REMOVE_LARGE_PREFIX, "kind is PARAM_REMOVE_LARGE_PREFIX");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part->word), "*/", "word is '*/'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test unclosed braced parameter
CTEST(test_param_braced_unclosed)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "${var");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_INCOMPLETE, "unclosed brace returns INCOMPLETE");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test empty braced parameter (error)
CTEST(test_param_braced_empty)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "${}");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_ERROR, "empty braces returns ERROR");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

/* ============================================================================
 * Parameter Expansion in Double Quotes
 * ============================================================================ */

// Test parameter in double quotes: "$var"
CTEST(test_param_in_dquote)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "\"$var\"");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");

    const token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_TRUE(ctest, token_was_quoted(tok), "token was quoted");
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 1, "one part");

    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_PARAMETER, "part is PARAMETER");
    CTEST_ASSERT_TRUE(ctest, part_was_double_quoted(part), "part was double-quoted");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part)), "var", "param name is 'var'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test braced parameter in double quotes: "${var}"
CTEST(test_param_braced_in_dquote)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "\"${var}\"");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_TRUE(ctest, part_was_double_quoted(part), "part was double-quoted");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part)), "var", "param name is 'var'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test mixed content in double quotes: "prefix${var}suffix"
CTEST(test_param_mixed_in_dquote)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "\"prefix${var}suffix\"");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");

    const token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 3, "three parts");

    part_t *part1 = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part1), PART_LITERAL, "first part is LITERAL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_text(part1)), "prefix", "first part is 'prefix'");

    part_t *part2 = token_get_part(tok, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(part2), PART_PARAMETER, "second part is PARAMETER");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part2)), "var", "param name is 'var'");

    part_t *part3 = token_get_part(tok, 2);
    CTEST_ASSERT_EQ(ctest, part_get_type(part3), PART_LITERAL, "third part is LITERAL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_text(part3)), "suffix", "third part is 'suffix'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

/* ============================================================================
 * Parameter Boundary Tests
 * ============================================================================ */

// Test two separate braced params with space: ${foo} ${bar}
CTEST(test_param_two_braced_with_space)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "${foo} ${bar}");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 2, "two tokens produced");

    // First token: ${foo}
    const token_t *tok1 = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_get_type(tok1), TOKEN_WORD, "first token is WORD");
    CTEST_ASSERT_EQ(ctest, token_part_count(tok1), 1, "first token has one part");
    part_t *part1 = token_get_part(tok1, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part1), PART_PARAMETER, "first part is PARAMETER");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part1)), "foo", "param name is 'foo'");

    // Second token: ${bar}
    const token_t *tok2 = token_list_get(tokens, 1);
    CTEST_ASSERT_EQ(ctest, token_get_type(tok2), TOKEN_WORD, "second token is WORD");
    CTEST_ASSERT_EQ(ctest, token_part_count(tok2), 1, "second token has one part");
    part_t *part2 = token_get_part(tok2, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part2), PART_PARAMETER, "second part is PARAMETER");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part2)), "bar", "param name is 'bar'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test two consecutive braced params: ${foo}${bar}
CTEST(test_param_two_braced_consecutive)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "${foo}${bar}");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");

    const token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_get_type(tok), TOKEN_WORD, "token is WORD");
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 2, "two parts");

    part_t *part1 = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part1), PART_PARAMETER, "first part is PARAMETER");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part1)), "foo", "first param is 'foo'");

    part_t *part2 = token_get_part(tok, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(part2), PART_PARAMETER, "second part is PARAMETER");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part2)), "bar", "second param is 'bar'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test two consecutive unbraced params: $foo$bar
CTEST(test_param_two_unbraced_consecutive)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$foo$bar");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");

    const token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_get_type(tok), TOKEN_WORD, "token is WORD");
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 2, "two parts");

    const part_t *part1 = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part1), PART_PARAMETER, "first part is PARAMETER");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part1)), "foo", "first param is 'foo'");

    part_t *part2 = token_get_part(tok, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(part2), PART_PARAMETER, "second part is PARAMETER");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part2)), "bar", "second param is 'bar'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test mixed unbraced and braced: $foo${bar}
CTEST(test_param_unbraced_then_braced)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$foo${bar}");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");

    const token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_get_type(tok), TOKEN_WORD, "token is WORD");
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 2, "two parts");

    part_t *part1 = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part1), PART_PARAMETER, "first part is PARAMETER");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part1)), "foo", "first param is 'foo'");

    part_t *part2 = token_get_part(tok, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(part2), PART_PARAMETER, "second part is PARAMETER");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part2)), "bar", "second param is 'bar'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test param followed by single-quoted string: $foo'bar'
CTEST(test_param_followed_by_squote)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "$foo'bar'");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");

    const token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_get_type(tok), TOKEN_WORD, "token is WORD");
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 2, "two parts");

    part_t *part1 = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part1), PART_PARAMETER, "first part is PARAMETER");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part1)), "foo", "param is 'foo'");

    part_t *part2 = token_get_part(tok, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(part2), PART_LITERAL, "second part is LITERAL");
    CTEST_ASSERT_TRUE(ctest, part_was_single_quoted(part2), "second part was single-quoted");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_text(part2)), "bar", "literal is 'bar'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test single-quoted string followed by param: 'foo'$bar
CTEST(test_squote_followed_by_param)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "'foo'$bar");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");

    const token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_get_type(tok), TOKEN_WORD, "token is WORD");
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 2, "two parts");

    part_t *part1 = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part1), PART_LITERAL, "first part is LITERAL");
    CTEST_ASSERT_TRUE(ctest, part_was_single_quoted(part1), "first part was single-quoted");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_text(part1)), "foo", "literal is 'foo'");

    part_t *part2 = token_get_part(tok, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(part2), PART_PARAMETER, "second part is PARAMETER");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part2)), "bar", "param is 'bar'");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

// Test parameter as word in braced expansion: ${x#$HOME}
CTEST(test_param_in_word_of_braced)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, "${x#$HOME}");

    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);

    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "tokenize status is LEX_OK");
    CTEST_ASSERT_EQ(ctest, token_list_size(tokens), 1, "one token produced");

    const token_t *tok = token_list_get(tokens, 0);
    CTEST_ASSERT_EQ(ctest, token_get_type(tok), TOKEN_WORD, "token is WORD");
    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 1, "one part");

    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_PARAMETER, "part is PARAMETER");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part)), "x", "param name is 'x'");
    CTEST_ASSERT_EQ(ctest, part->param_kind, PARAM_REMOVE_SMALL_PREFIX, "kind is PARAM_REMOVE_SMALL_PREFIX");
    // The word part contains "$HOME" as a literal (not expanded at lex time)
    CTEST_ASSERT_NOT_NULL(ctest, part->word, "word is set");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part->word), "$HOME", "word is '$HOME'");

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
        // Unbraced parameter tests
        CTEST_ENTRY(test_param_unbraced_simple),
        CTEST_ENTRY(test_param_unbraced_underscore),
        CTEST_ENTRY(test_param_unbraced_with_digits),
        CTEST_ENTRY(test_param_unbraced_positional),
        CTEST_ENTRY(test_param_unbraced_special_question),
        CTEST_ENTRY(test_param_unbraced_special_dollar),
        CTEST_ENTRY(test_param_unbraced_special_at),
        CTEST_ENTRY(test_param_unbraced_special_star),
        CTEST_ENTRY(test_param_unbraced_special_hash),
        CTEST_ENTRY(test_param_unbraced_with_suffix),
        // Braced parameter tests
        CTEST_ENTRY(test_param_braced_simple),
        CTEST_ENTRY(test_param_braced_with_suffix),
        CTEST_ENTRY(test_param_braced_length),
        CTEST_ENTRY(test_param_braced_special_hash),
        CTEST_ENTRY(test_param_braced_use_default),
        CTEST_ENTRY(test_param_braced_assign_default),
        CTEST_ENTRY(test_param_braced_error_if_unset),
        CTEST_ENTRY(test_param_braced_use_alternate),
        CTEST_ENTRY(test_param_braced_remove_small_suffix),
        CTEST_ENTRY(test_param_braced_remove_large_suffix),
        CTEST_ENTRY(test_param_braced_remove_small_prefix),
        CTEST_ENTRY(test_param_braced_remove_large_prefix),
        CTEST_ENTRY(test_param_braced_unclosed),
        CTEST_ENTRY(test_param_braced_empty),
        // Parameter in double quotes
        CTEST_ENTRY(test_param_in_dquote),
        CTEST_ENTRY(test_param_braced_in_dquote),
        CTEST_ENTRY(test_param_mixed_in_dquote),
        // Parameter boundary tests
        CTEST_ENTRY(test_param_two_braced_with_space),
        CTEST_ENTRY(test_param_two_braced_consecutive),
        CTEST_ENTRY(test_param_two_unbraced_consecutive),
        CTEST_ENTRY(test_param_unbraced_then_braced),
        CTEST_ENTRY(test_param_followed_by_squote),
        CTEST_ENTRY(test_squote_followed_by_param),
        CTEST_ENTRY(test_param_in_word_of_braced),
        NULL
    };

    int result = ctest_run_suite(suite);

    arena_end();

    return result;
}

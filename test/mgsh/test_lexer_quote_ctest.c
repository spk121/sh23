#include "ctest.h"
#include "lexer_squote.h"
#include "lexer_dquote.h"
#include "string.h"
#include "token.h"
#include "xalloc.h"

/* ============================================================================
 * Single Quote Lexer Tests
 * ============================================================================ */

/* Test empty single quotes '' */
CTEST(test_squote_empty)
{
    string_t *input = string_create_from_cstr("'");  /* Just closing quote */
    lexer_squote_t lexer;
    lexer_squote_init(&lexer, input, 0, 1, 2);  /* pos after opening quote */

    part_t *part = NULL;
    lexer_squote_result_t result = lexer_squote_lex(&lexer, &part);

    CTEST_ASSERT_EQ(ctest, result, LEXER_SQUOTE_OK, "empty squote succeeds");
    CTEST_ASSERT_NOT_NULL(ctest, part, "part created");
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_LITERAL, "part is literal");
    CTEST_ASSERT_TRUE(ctest, part_was_single_quoted(part), "marked as single quoted");
    CTEST_ASSERT_FALSE(ctest, part_was_double_quoted(part), "not marked as double quoted");

    const string_t *text = part_get_text(part);
    CTEST_ASSERT_NOT_NULL(ctest, text, "text not null");
    CTEST_ASSERT_EQ(ctest, string_length(text), 0, "empty content");

    part_destroy(part);
    string_destroy(input);
    (void)ctest;
}

/* Test simple single-quoted string 'hello' */
CTEST(test_squote_simple)
{
    string_t *input = string_create_from_cstr("hello'");
    lexer_squote_t lexer;
    lexer_squote_init(&lexer, input, 0, 1, 2);

    part_t *part = NULL;
    lexer_squote_result_t result = lexer_squote_lex(&lexer, &part);

    CTEST_ASSERT_EQ(ctest, result, LEXER_SQUOTE_OK, "simple squote succeeds");
    CTEST_ASSERT_NOT_NULL(ctest, part, "part created");

    const string_t *text = part_get_text(part);
    CTEST_ASSERT_STR_EQ(ctest, string_data(text), "hello", "content is 'hello'");
    CTEST_ASSERT_EQ(ctest, lexer_squote_get_pos(&lexer), 6, "position after closing quote");

    part_destroy(part);
    string_destroy(input);
    (void)ctest;
}

/* Test that backslash has no special meaning in single quotes */
CTEST(test_squote_backslash_literal)
{
    string_t *input = string_create_from_cstr("a\\nb\\tc'");
    lexer_squote_t lexer;
    lexer_squote_init(&lexer, input, 0, 1, 2);

    part_t *part = NULL;
    lexer_squote_result_t result = lexer_squote_lex(&lexer, &part);

    CTEST_ASSERT_EQ(ctest, result, LEXER_SQUOTE_OK, "backslash squote succeeds");
    const string_t *text = part_get_text(part);
    CTEST_ASSERT_STR_EQ(ctest, string_data(text), "a\\nb\\tc", "backslash is literal");

    part_destroy(part);
    string_destroy(input);
    (void)ctest;
}

/* Test that dollar sign has no special meaning in single quotes */
CTEST(test_squote_dollar_literal)
{
    string_t *input = string_create_from_cstr("$HOME $var ${x}'");
    lexer_squote_t lexer;
    lexer_squote_init(&lexer, input, 0, 1, 2);

    part_t *part = NULL;
    lexer_squote_result_t result = lexer_squote_lex(&lexer, &part);

    CTEST_ASSERT_EQ(ctest, result, LEXER_SQUOTE_OK, "dollar squote succeeds");
    const string_t *text = part_get_text(part);
    CTEST_ASSERT_STR_EQ(ctest, string_data(text), "$HOME $var ${x}", "dollar is literal");

    part_destroy(part);
    string_destroy(input);
    (void)ctest;
}

/* Test that backtick has no special meaning in single quotes */
CTEST(test_squote_backtick_literal)
{
    string_t *input = string_create_from_cstr("`command`'");
    lexer_squote_t lexer;
    lexer_squote_init(&lexer, input, 0, 1, 2);

    part_t *part = NULL;
    lexer_squote_result_t result = lexer_squote_lex(&lexer, &part);

    CTEST_ASSERT_EQ(ctest, result, LEXER_SQUOTE_OK, "backtick squote succeeds");
    const string_t *text = part_get_text(part);
    CTEST_ASSERT_STR_EQ(ctest, string_data(text), "`command`", "backtick is literal");

    part_destroy(part);
    string_destroy(input);
    (void)ctest;
}

/* Test that newlines are preserved in single quotes */
CTEST(test_squote_newline_preserved)
{
    string_t *input = string_create_from_cstr("line1\nline2'");
    lexer_squote_t lexer;
    lexer_squote_init(&lexer, input, 0, 1, 2);

    part_t *part = NULL;
    lexer_squote_result_t result = lexer_squote_lex(&lexer, &part);

    CTEST_ASSERT_EQ(ctest, result, LEXER_SQUOTE_OK, "newline squote succeeds");
    const string_t *text = part_get_text(part);
    CTEST_ASSERT_STR_EQ(ctest, string_data(text), "line1\nline2", "newline preserved");
    CTEST_ASSERT_EQ(ctest, lexer_squote_get_line(&lexer), 2, "line incremented");

    part_destroy(part);
    string_destroy(input);
    (void)ctest;
}

/* Test unterminated single quote */
CTEST(test_squote_unterminated)
{
    string_t *input = string_create_from_cstr("no closing quote");
    lexer_squote_t lexer;
    lexer_squote_init(&lexer, input, 0, 1, 2);

    part_t *part = NULL;
    lexer_squote_result_t result = lexer_squote_lex(&lexer, &part);

    CTEST_ASSERT_EQ(ctest, result, LEXER_SQUOTE_UNTERMINATED, "unterminated detected");
    CTEST_ASSERT_NULL(ctest, part, "no part on error");

    string_destroy(input);
    (void)ctest;
}

/* Test double quotes inside single quotes are literal */
CTEST(test_squote_dquote_literal)
{
    string_t *input = string_create_from_cstr("a\"b\"c'");
    lexer_squote_t lexer;
    lexer_squote_init(&lexer, input, 0, 1, 2);

    part_t *part = NULL;
    lexer_squote_result_t result = lexer_squote_lex(&lexer, &part);

    CTEST_ASSERT_EQ(ctest, result, LEXER_SQUOTE_OK, "dquote in squote succeeds");
    const string_t *text = part_get_text(part);
    CTEST_ASSERT_STR_EQ(ctest, string_data(text), "a\"b\"c", "double quotes are literal");

    part_destroy(part);
    string_destroy(input);
    (void)ctest;
}

/* ============================================================================
 * Double Quote Lexer Tests
 * ============================================================================ */

/* Test empty double quotes "" */
CTEST(test_dquote_empty)
{
    string_t *input = string_create_from_cstr("\"");  /* Just closing quote */
    lexer_dquote_t lexer;
    lexer_dquote_init(&lexer, input, 0, 1, 2);

    part_list_t *parts = NULL;
    lexer_dquote_result_t result = lexer_dquote_lex(&lexer, &parts);

    CTEST_ASSERT_EQ(ctest, result, LEXER_DQUOTE_OK, "empty dquote succeeds");
    CTEST_ASSERT_NOT_NULL(ctest, parts, "parts created");
    CTEST_ASSERT_EQ(ctest, part_list_size(parts), 0, "no parts for empty string");

    part_list_destroy(parts);
    string_destroy(input);
    (void)ctest;
}

/* Test simple literal content */
CTEST(test_dquote_simple_literal)
{
    string_t *input = string_create_from_cstr("hello world\"");
    lexer_dquote_t lexer;
    lexer_dquote_init(&lexer, input, 0, 1, 2);

    part_list_t *parts = NULL;
    lexer_dquote_result_t result = lexer_dquote_lex(&lexer, &parts);

    CTEST_ASSERT_EQ(ctest, result, LEXER_DQUOTE_OK, "simple dquote succeeds");
    CTEST_ASSERT_EQ(ctest, part_list_size(parts), 1, "one part");

    part_t *part = part_list_get(parts, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(part), PART_LITERAL, "part is literal");
    CTEST_ASSERT_TRUE(ctest, part_was_double_quoted(part), "marked as double quoted");

    const string_t *text = part_get_text(part);
    CTEST_ASSERT_STR_EQ(ctest, string_data(text), "hello world", "content correct");

    part_list_destroy(parts);
    string_destroy(input);
    (void)ctest;
}

/* Test backslash escape for $ */
CTEST(test_dquote_escape_dollar)
{
    string_t *input = string_create_from_cstr("cost is \\$100\"");
    lexer_dquote_t lexer;
    lexer_dquote_init(&lexer, input, 0, 1, 2);

    part_list_t *parts = NULL;
    lexer_dquote_result_t result = lexer_dquote_lex(&lexer, &parts);

    CTEST_ASSERT_EQ(ctest, result, LEXER_DQUOTE_OK, "escaped dollar succeeds");
    CTEST_ASSERT_EQ(ctest, part_list_size(parts), 1, "one literal part");

    part_t *part = part_list_get(parts, 0);
    const string_t *text = part_get_text(part);
    CTEST_ASSERT_STR_EQ(ctest, string_data(text), "cost is $100", "dollar escaped correctly");

    part_list_destroy(parts);
    string_destroy(input);
    (void)ctest;
}

/* Test backslash escape for backslash */
CTEST(test_dquote_escape_backslash)
{
    string_t *input = string_create_from_cstr("path\\\\dir\"");
    lexer_dquote_t lexer;
    lexer_dquote_init(&lexer, input, 0, 1, 2);

    part_list_t *parts = NULL;
    lexer_dquote_result_t result = lexer_dquote_lex(&lexer, &parts);

    CTEST_ASSERT_EQ(ctest, result, LEXER_DQUOTE_OK, "escaped backslash succeeds");

    part_t *part = part_list_get(parts, 0);
    const string_t *text = part_get_text(part);
    CTEST_ASSERT_STR_EQ(ctest, string_data(text), "path\\dir", "backslash escaped correctly");

    part_list_destroy(parts);
    string_destroy(input);
    (void)ctest;
}

/* Test backslash escape for double quote */
CTEST(test_dquote_escape_quote)
{
    string_t *input = string_create_from_cstr("say \\\"hello\\\"\"");
    lexer_dquote_t lexer;
    lexer_dquote_init(&lexer, input, 0, 1, 2);

    part_list_t *parts = NULL;
    lexer_dquote_result_t result = lexer_dquote_lex(&lexer, &parts);

    CTEST_ASSERT_EQ(ctest, result, LEXER_DQUOTE_OK, "escaped quote succeeds");

    part_t *part = part_list_get(parts, 0);
    const string_t *text = part_get_text(part);
    CTEST_ASSERT_STR_EQ(ctest, string_data(text), "say \"hello\"", "quote escaped correctly");

    part_list_destroy(parts);
    string_destroy(input);
    (void)ctest;
}

/* Test backslash not special before regular char */
CTEST(test_dquote_backslash_literal)
{
    string_t *input = string_create_from_cstr("a\\bc\"");
    lexer_dquote_t lexer;
    lexer_dquote_init(&lexer, input, 0, 1, 2);

    part_list_t *parts = NULL;
    lexer_dquote_result_t result = lexer_dquote_lex(&lexer, &parts);

    CTEST_ASSERT_EQ(ctest, result, LEXER_DQUOTE_OK, "literal backslash succeeds");

    part_t *part = part_list_get(parts, 0);
    const string_t *text = part_get_text(part);
    /* Backslash before 'b' is literal per POSIX */
    CTEST_ASSERT_STR_EQ(ctest, string_data(text), "a\\bc", "backslash literal before 'b'");

    part_list_destroy(parts);
    string_destroy(input);
    (void)ctest;
}

/* Test simple parameter expansion $var */
CTEST(test_dquote_param_simple)
{
    string_t *input = string_create_from_cstr("hello $name world\"");
    lexer_dquote_t lexer;
    lexer_dquote_init(&lexer, input, 0, 1, 2);

    part_list_t *parts = NULL;
    lexer_dquote_result_t result = lexer_dquote_lex(&lexer, &parts);

    CTEST_ASSERT_EQ(ctest, result, LEXER_DQUOTE_OK, "param expansion succeeds");
    CTEST_ASSERT_EQ(ctest, part_list_size(parts), 3, "three parts: literal, param, literal");

    part_t *p0 = part_list_get(parts, 0);
    CTEST_ASSERT_EQ(ctest, part_get_type(p0), PART_LITERAL, "first part is literal");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(p0)), "hello ", "first literal correct");

    part_t *p1 = part_list_get(parts, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(p1), PART_PARAMETER, "second part is parameter");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_param_name(p1)), "name", "param name correct");
    CTEST_ASSERT_TRUE(ctest, part_was_double_quoted(p1), "param marked as double quoted");

    part_t *p2 = part_list_get(parts, 2);
    CTEST_ASSERT_EQ(ctest, part_get_type(p2), PART_LITERAL, "third part is literal");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_text(p2)), " world", "third literal correct");

    part_list_destroy(parts);
    string_destroy(input);
    (void)ctest;
}

/* Test special parameter $@ */
CTEST(test_dquote_param_special)
{
    string_t *input = string_create_from_cstr("args: $@\"");
    lexer_dquote_t lexer;
    lexer_dquote_init(&lexer, input, 0, 1, 2);

    part_list_t *parts = NULL;
    lexer_dquote_result_t result = lexer_dquote_lex(&lexer, &parts);

    CTEST_ASSERT_EQ(ctest, result, LEXER_DQUOTE_OK, "special param succeeds");
    CTEST_ASSERT_EQ(ctest, part_list_size(parts), 2, "two parts");

    part_t *p1 = part_list_get(parts, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(p1), PART_PARAMETER, "second part is parameter");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_param_name(p1)), "@", "param name is @");

    part_list_destroy(parts);
    string_destroy(input);
    (void)ctest;
}

/* Test braced parameter ${var} */
CTEST(test_dquote_param_braced)
{
    string_t *input = string_create_from_cstr("value: ${myvar}\"");
    lexer_dquote_t lexer;
    lexer_dquote_init(&lexer, input, 0, 1, 2);

    part_list_t *parts = NULL;
    lexer_dquote_result_t result = lexer_dquote_lex(&lexer, &parts);

    CTEST_ASSERT_EQ(ctest, result, LEXER_DQUOTE_OK, "braced param succeeds");
    CTEST_ASSERT_EQ(ctest, part_list_size(parts), 2, "two parts");

    part_t *p1 = part_list_get(parts, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(p1), PART_PARAMETER, "second part is parameter");
    CTEST_ASSERT_STR_EQ(ctest, string_data(part_get_param_name(p1)), "myvar", "braced param name");

    part_list_destroy(parts);
    string_destroy(input);
    (void)ctest;
}

/* Test command substitution $(...) */
CTEST(test_dquote_command_subst)
{
    string_t *input = string_create_from_cstr("date: $(date +%Y)\"");
    lexer_dquote_t lexer;
    lexer_dquote_init(&lexer, input, 0, 1, 2);

    part_list_t *parts = NULL;
    lexer_dquote_result_t result = lexer_dquote_lex(&lexer, &parts);

    CTEST_ASSERT_EQ(ctest, result, LEXER_DQUOTE_OK, "command subst succeeds");
    CTEST_ASSERT_EQ(ctest, part_list_size(parts), 2, "two parts");

    part_t *p1 = part_list_get(parts, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(p1), PART_COMMAND_SUBST, "second part is command subst");
    CTEST_ASSERT_TRUE(ctest, part_was_double_quoted(p1), "command subst marked as double quoted");

    part_list_destroy(parts);
    string_destroy(input);
    (void)ctest;
}

/* Test backtick command substitution */
CTEST(test_dquote_backtick_subst)
{
    string_t *input = string_create_from_cstr("date: `date`\"");
    lexer_dquote_t lexer;
    lexer_dquote_init(&lexer, input, 0, 1, 2);

    part_list_t *parts = NULL;
    lexer_dquote_result_t result = lexer_dquote_lex(&lexer, &parts);

    CTEST_ASSERT_EQ(ctest, result, LEXER_DQUOTE_OK, "backtick subst succeeds");
    CTEST_ASSERT_EQ(ctest, part_list_size(parts), 2, "two parts");

    part_t *p1 = part_list_get(parts, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(p1), PART_COMMAND_SUBST, "second part is command subst");

    part_list_destroy(parts);
    string_destroy(input);
    (void)ctest;
}

/* Test arithmetic expansion $((...)) */
CTEST(test_dquote_arithmetic)
{
    string_t *input = string_create_from_cstr("result: $((1+2))\"");
    lexer_dquote_t lexer;
    lexer_dquote_init(&lexer, input, 0, 1, 2);

    part_list_t *parts = NULL;
    lexer_dquote_result_t result = lexer_dquote_lex(&lexer, &parts);

    CTEST_ASSERT_EQ(ctest, result, LEXER_DQUOTE_OK, "arithmetic expansion succeeds");
    CTEST_ASSERT_EQ(ctest, part_list_size(parts), 2, "two parts");

    part_t *p1 = part_list_get(parts, 1);
    CTEST_ASSERT_EQ(ctest, part_get_type(p1), PART_ARITHMETIC, "second part is arithmetic");

    part_list_destroy(parts);
    string_destroy(input);
    (void)ctest;
}

/* Test single quotes are literal inside double quotes */
CTEST(test_dquote_squote_literal)
{
    string_t *input = string_create_from_cstr("it's a test\"");
    lexer_dquote_t lexer;
    lexer_dquote_init(&lexer, input, 0, 1, 2);

    part_list_t *parts = NULL;
    lexer_dquote_result_t result = lexer_dquote_lex(&lexer, &parts);

    CTEST_ASSERT_EQ(ctest, result, LEXER_DQUOTE_OK, "squote in dquote succeeds");
    CTEST_ASSERT_EQ(ctest, part_list_size(parts), 1, "one part");

    part_t *part = part_list_get(parts, 0);
    const string_t *text = part_get_text(part);
    CTEST_ASSERT_STR_EQ(ctest, string_data(text), "it's a test", "single quotes are literal");

    part_list_destroy(parts);
    string_destroy(input);
    (void)ctest;
}

/* Test unterminated double quote */
CTEST(test_dquote_unterminated)
{
    string_t *input = string_create_from_cstr("no closing quote");
    lexer_dquote_t lexer;
    lexer_dquote_init(&lexer, input, 0, 1, 2);

    part_list_t *parts = NULL;
    lexer_dquote_result_t result = lexer_dquote_lex(&lexer, &parts);

    CTEST_ASSERT_EQ(ctest, result, LEXER_DQUOTE_UNTERMINATED, "unterminated detected");
    CTEST_ASSERT_NULL(ctest, parts, "no parts on error");

    string_destroy(input);
    (void)ctest;
}

/* Test unterminated brace expansion */
CTEST(test_dquote_unterminated_brace)
{
    string_t *input = string_create_from_cstr("${unclosed");
    lexer_dquote_t lexer;
    lexer_dquote_init(&lexer, input, 0, 1, 2);

    part_list_t *parts = NULL;
    lexer_dquote_result_t result = lexer_dquote_lex(&lexer, &parts);

    CTEST_ASSERT_EQ(ctest, result, LEXER_DQUOTE_UNTERMINATED_EXPANSION, "unterminated brace detected");

    string_destroy(input);
    (void)ctest;
}

/* Test line continuation */
CTEST(test_dquote_line_continuation)
{
    string_t *input = string_create_from_cstr("hello \\\nworld\"");
    lexer_dquote_t lexer;
    lexer_dquote_init(&lexer, input, 0, 1, 2);

    part_list_t *parts = NULL;
    lexer_dquote_result_t result = lexer_dquote_lex(&lexer, &parts);

    CTEST_ASSERT_EQ(ctest, result, LEXER_DQUOTE_OK, "line continuation succeeds");

    part_t *part = part_list_get(parts, 0);
    const string_t *text = part_get_text(part);
    CTEST_ASSERT_STR_EQ(ctest, string_data(text), "hello world", "line continuation removed");

    part_list_destroy(parts);
    string_destroy(input);
    (void)ctest;
}

/* Test bare $ at end (not followed by expansion) */
CTEST(test_dquote_bare_dollar)
{
    string_t *input = string_create_from_cstr("cost is $\"");
    lexer_dquote_t lexer;
    lexer_dquote_init(&lexer, input, 0, 1, 2);

    part_list_t *parts = NULL;
    lexer_dquote_result_t result = lexer_dquote_lex(&lexer, &parts);

    CTEST_ASSERT_EQ(ctest, result, LEXER_DQUOTE_OK, "bare dollar succeeds");
    CTEST_ASSERT_EQ(ctest, part_list_size(parts), 1, "one part");

    part_t *part = part_list_get(parts, 0);
    const string_t *text = part_get_text(part);
    CTEST_ASSERT_STR_EQ(ctest, string_data(text), "cost is $", "bare dollar is literal");

    part_list_destroy(parts);
    string_destroy(input);
    (void)ctest;
}

int main(void)
{
    arena_start();

    CTestEntry *suite[] = {
        /* Single quote tests */
        CTEST_ENTRY(test_squote_empty),
        CTEST_ENTRY(test_squote_simple),
        CTEST_ENTRY(test_squote_backslash_literal),
        CTEST_ENTRY(test_squote_dollar_literal),
        CTEST_ENTRY(test_squote_backtick_literal),
        CTEST_ENTRY(test_squote_newline_preserved),
        CTEST_ENTRY(test_squote_unterminated),
        CTEST_ENTRY(test_squote_dquote_literal),
        /* Double quote tests */
        CTEST_ENTRY(test_dquote_empty),
        CTEST_ENTRY(test_dquote_simple_literal),
        CTEST_ENTRY(test_dquote_escape_dollar),
        CTEST_ENTRY(test_dquote_escape_backslash),
        CTEST_ENTRY(test_dquote_escape_quote),
        CTEST_ENTRY(test_dquote_backslash_literal),
        CTEST_ENTRY(test_dquote_param_simple),
        CTEST_ENTRY(test_dquote_param_special),
        CTEST_ENTRY(test_dquote_param_braced),
        CTEST_ENTRY(test_dquote_command_subst),
        CTEST_ENTRY(test_dquote_backtick_subst),
        CTEST_ENTRY(test_dquote_arithmetic),
        CTEST_ENTRY(test_dquote_squote_literal),
        CTEST_ENTRY(test_dquote_unterminated),
        CTEST_ENTRY(test_dquote_unterminated_brace),
        CTEST_ENTRY(test_dquote_line_continuation),
        CTEST_ENTRY(test_dquote_bare_dollar),
        NULL
    };

    int result = ctest_run_suite(suite);

    arena_end();

    return result;
}

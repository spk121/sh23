/*
 * test_token_ctest.c
 *
 * Unit tests for token.c - lexical tokens and token components
 */

#include "ctest.h"
#include "token.h"
#include "string_t.h"
#include "lib.h"
#include "xalloc.h"

// ============================================================================
// Token Lifecycle
// ============================================================================

CTEST(test_token_create_destroy)
{
    token_t *tok = token_create(TOKEN_WORD);
    CTEST_ASSERT_NOT_NULL(ctest, tok, "token created");
    CTEST_ASSERT_EQ(ctest, (int)token_get_type(tok), (int)TOKEN_WORD, "type is WORD");
    token_destroy(&tok);
    CTEST_ASSERT_NULL(ctest, tok, "token destroyed");
}

CTEST(test_token_create_word)
{
    token_t *tok = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, tok, "word token created");
    CTEST_ASSERT_EQ(ctest, (int)token_get_type(tok), (int)TOKEN_WORD, "type is WORD");
    CTEST_ASSERT_NOT_NULL(ctest, token_get_parts(tok), "parts list created");
    token_destroy(&tok);
}

CTEST(test_token_clone_word)
{
    token_t *orig = token_create_word();
    string_t *s = string_create_from_cstr("hello");
    token_add_literal_part(orig, s);
    string_destroy(&s);

    token_t *cloned = token_clone(orig);
    CTEST_ASSERT_NOT_NULL(ctest, cloned, "cloned token");
    CTEST_ASSERT_EQ(ctest, (int)token_get_type(cloned), (int)TOKEN_WORD, "type matches");
    CTEST_ASSERT_EQ(ctest, token_part_count(cloned), 1, "part count matches");

    token_destroy(&orig);
    token_destroy(&cloned);
}

// ============================================================================
// Token Types
// ============================================================================

CTEST(test_token_type_conversion)
{
    CTEST_ASSERT_EQ(ctest, token_type_to_cstr(TOKEN_EOF)[0], 'E', "EOF name starts with E");
    CTEST_ASSERT_EQ(ctest, token_type_to_cstr(TOKEN_WORD)[0], 'W', "WORD name starts with W");
    CTEST_ASSERT_EQ(ctest, token_type_to_cstr(TOKEN_IF)[0], 'i', "IF name starts with i");
    CTEST_ASSERT_EQ(ctest, token_type_to_cstr(TOKEN_AND_IF)[0], '&', "AND_IF name starts with &");
}

CTEST(test_token_is_reserved_word)
{
    CTEST_ASSERT_TRUE(ctest, token_is_reserved_word("if"), "if is reserved");
    CTEST_ASSERT_TRUE(ctest, token_is_reserved_word("then"), "then is reserved");
    CTEST_ASSERT_TRUE(ctest, token_is_reserved_word("else"), "else is reserved");
    CTEST_ASSERT_FALSE(ctest, token_is_reserved_word("foo"), "foo is not reserved");
    CTEST_ASSERT_FALSE(ctest, token_is_reserved_word("bar"), "bar is not reserved");
}

CTEST(test_token_string_to_reserved_word)
{
    CTEST_ASSERT_EQ(ctest, (int)token_string_to_reserved_word("if"), (int)TOKEN_IF, "if -> TOKEN_IF");
    CTEST_ASSERT_EQ(ctest, (int)token_string_to_reserved_word("do"), (int)TOKEN_DO, "do -> TOKEN_DO");
    CTEST_ASSERT_EQ(ctest, (int)token_string_to_reserved_word("done"), (int)TOKEN_DONE, "done -> TOKEN_DONE");
    CTEST_ASSERT_EQ(ctest, (int)token_string_to_reserved_word("foo"), (int)TOKEN_WORD, "foo -> TOKEN_WORD");
}

CTEST(test_token_is_operator)
{
    CTEST_ASSERT_TRUE(ctest, token_is_operator("&&"), "&& is operator");
    CTEST_ASSERT_TRUE(ctest, token_is_operator("||"), "|| is operator");
    CTEST_ASSERT_TRUE(ctest, token_is_operator("<<"), "<< is operator");
    CTEST_ASSERT_TRUE(ctest, token_is_operator(">"), "> is operator");
    CTEST_ASSERT_FALSE(ctest, token_is_operator("foo"), "foo is not operator");
}

CTEST(test_token_string_to_operator)
{
    CTEST_ASSERT_EQ(ctest, (int)token_string_to_operator("&&"), (int)TOKEN_AND_IF, "&& -> AND_IF");
    CTEST_ASSERT_EQ(ctest, (int)token_string_to_operator("||"), (int)TOKEN_OR_IF, "|| -> OR_IF");
    CTEST_ASSERT_EQ(ctest, (int)token_string_to_operator("<<"), (int)TOKEN_DLESS, "<< -> DLESS");
    CTEST_ASSERT_EQ(ctest, (int)token_string_to_operator(">|"), (int)TOKEN_CLOBBER, ">| -> CLOBBER");
    CTEST_ASSERT_EQ(ctest, (int)token_string_to_operator("foo"), (int)TOKEN_EOF, "foo -> EOF");
}

// ============================================================================
// Token Parts
// ============================================================================

CTEST(test_part_create_literal)
{
    string_t *text = string_create_from_cstr("hello");
    part_t *part = part_create_literal(text);
    string_destroy(&text);

    CTEST_ASSERT_NOT_NULL(ctest, part, "literal part created");
    CTEST_ASSERT_EQ(ctest, (int)part_get_type(part), (int)PART_LITERAL, "type is LITERAL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_text(part)), "hello", "text matches");

    part_destroy(&part);
}

CTEST(test_part_create_parameter)
{
    string_t *name = string_create_from_cstr("USER");
    part_t *part = part_create_parameter(name);
    string_destroy(&name);

    CTEST_ASSERT_NOT_NULL(ctest, part, "parameter part created");
    CTEST_ASSERT_EQ(ctest, (int)part_get_type(part), (int)PART_PARAMETER, "type is PARAMETER");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(part_get_param_name(part)), "USER", "param name matches");

    part_destroy(&part);
}

CTEST(test_token_add_literal_part)
{
    token_t *tok = token_create_word();
    string_t *text = string_create_from_cstr("literal");
    token_add_literal_part(tok, text);
    string_destroy(&text);

    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 1, "one part added");
    part_t *part = token_get_part(tok, 0);
    CTEST_ASSERT_EQ(ctest, (int)part_get_type(part), (int)PART_LITERAL, "part is literal");

    token_destroy(&tok);
}

CTEST(test_token_append_parameter)
{
    token_t *tok = token_create_word();
    string_t *name = string_create_from_cstr("foo");
    token_append_parameter(tok, name);
    string_destroy(&name);

    CTEST_ASSERT_EQ(ctest, token_part_count(tok), 1, "one part added");
    CTEST_ASSERT_TRUE(ctest, token_needs_expansion(tok), "needs expansion");

    token_destroy(&tok);
}

// ============================================================================
// Token Quote Tracking
// ============================================================================

CTEST(test_token_set_quoted)
{
    token_t *tok = token_create_word();
    CTEST_ASSERT_FALSE(ctest, token_was_quoted(tok), "not quoted initially");

    token_set_quoted(tok, true);
    CTEST_ASSERT_TRUE(ctest, token_was_quoted(tok), "quoted after set");

    token_destroy(&tok);
}

CTEST(test_part_quote_tracking)
{
    string_t *text = string_create_from_cstr("text");
    part_t *part = part_create_literal(text);
    string_destroy(&text);

    CTEST_ASSERT_FALSE(ctest, part_was_single_quoted(part), "not single quoted initially");
    CTEST_ASSERT_FALSE(ctest, part_was_double_quoted(part), "not double quoted initially");

    part_set_quoted(part, true, false);
    CTEST_ASSERT_TRUE(ctest, part_was_single_quoted(part), "single quoted after set");
    CTEST_ASSERT_FALSE(ctest, part_was_double_quoted(part), "not double quoted");

    part_destroy(&part);
}

// ============================================================================
// Token Expansion Flags
// ============================================================================

CTEST(test_token_expansion_flags_initial)
{
    token_t *tok = token_create_word();
    CTEST_ASSERT_FALSE(ctest, token_needs_expansion(tok), "no expansion initially");
    token_destroy(&tok);
}

CTEST(test_token_recompute_expansion_flags)
{
    token_t *tok = token_create_word();
    string_t *s = string_create_from_cstr("*.txt");
    token_add_literal_part(tok, s);
    string_destroy(&s);

    token_recompute_expansion_flags(tok);
    CTEST_ASSERT_TRUE(ctest, token_needs_pathname_expansion(tok), "glob pattern detected");

    token_destroy(&tok);
}

// ============================================================================
// Token Location Tracking
// ============================================================================

CTEST(test_token_set_location)
{
    token_t *tok = token_create(TOKEN_WORD);
    token_set_location(tok, 5, 10, 5, 15);

    CTEST_ASSERT_EQ(ctest, token_get_first_line(tok), 5, "line is 5");
    CTEST_ASSERT_EQ(ctest, token_get_first_column(tok), 10, "column is 10");

    token_destroy(&tok);
}

// ============================================================================
// Token IO Number/Location
// ============================================================================

CTEST(test_token_io_number)
{
    token_t *tok = token_create(TOKEN_IO_NUMBER);
    token_set_io_number(tok, 2);

    CTEST_ASSERT_EQ(ctest, token_get_io_number(tok), 2, "io_number is 2");

    token_destroy(&tok);
}

CTEST(test_token_io_location)
{
    token_t *tok = token_create(TOKEN_IO_LOCATION);
    string_t *loc = string_create_from_cstr("{2}");
    token_set_io_location(tok, loc);
    // loc ownership transferred

    const string_t *retrieved = token_get_io_location(tok);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(retrieved), "{2}", "io_location matches");

    token_destroy(&tok);
}

// ============================================================================
// Token Reserved Word Promotion
// ============================================================================

CTEST(test_token_try_promote_to_reserved_word)
{
    token_t *tok = token_create_word();
    string_t *s = string_create_from_cstr("if");
    token_add_literal_part(tok, s);
    string_destroy(&s);

    CTEST_ASSERT_TRUE(ctest, token_try_promote_to_reserved_word(tok, false), "promoted to reserved");
    CTEST_ASSERT_EQ(ctest, (int)token_get_type(tok), (int)TOKEN_IF, "type is TOKEN_IF");

    token_destroy(&tok);
}

CTEST(test_token_try_promote_to_bang)
{
    token_t *tok = token_create_word();
    string_t *s = string_create_from_cstr("!");
    token_add_literal_part(tok, s);
    string_destroy(&s);

    CTEST_ASSERT_TRUE(ctest, token_try_promote_to_bang(tok), "promoted to bang");
    CTEST_ASSERT_EQ(ctest, (int)token_get_type(tok), (int)TOKEN_BANG, "type is TOKEN_BANG");

    token_destroy(&tok);
}

CTEST(test_token_quoted_word_no_promote)
{
    token_t *tok = token_create_word();
    string_t *s = string_create_from_cstr("if");
    token_add_literal_part(tok, s);
    string_destroy(&s);
    token_set_quoted(tok, true);

    CTEST_ASSERT_FALSE(ctest, token_try_promote_to_reserved_word(tok, false), "quoted word not promoted");
    CTEST_ASSERT_EQ(ctest, (int)token_get_type(tok), (int)TOKEN_WORD, "still TOKEN_WORD");

    token_destroy(&tok);
}

// ============================================================================
// Token List Lifecycle
// ============================================================================

CTEST(test_token_list_create_destroy)
{
    token_list_t *list = token_list_create();
    CTEST_ASSERT_NOT_NULL(ctest, list, "list created");
    CTEST_ASSERT_EQ(ctest, token_list_size(list), 0, "size is 0");

    token_list_destroy(&list);
    CTEST_ASSERT_NULL(ctest, list, "list destroyed");
}

CTEST(test_token_list_append)
{
    token_list_t *list = token_list_create();
    token_t *tok1 = token_create_word();
    token_t *tok2 = token_create(TOKEN_IF);

    token_list_append(list, tok1);
    token_list_append(list, tok2);

    CTEST_ASSERT_EQ(ctest, token_list_size(list), 2, "size is 2");

    token_list_destroy(&list);
}

CTEST(test_token_list_get)
{
    token_list_t *list = token_list_create();
    token_t *tok = token_create(TOKEN_WORD);
    token_list_append(list, tok);

    token_t *retrieved = token_list_get(list, 0);
    CTEST_ASSERT_EQ(ctest, (int)token_get_type(retrieved), (int)TOKEN_WORD, "retrieved token type matches");

    token_list_destroy(&list);
}

CTEST(test_token_list_remove)
{
    token_list_t *list = token_list_create();
    token_list_append(list, token_create_word());
    token_list_append(list, token_create(TOKEN_IF));
    token_list_append(list, token_create(TOKEN_THEN));

    CTEST_ASSERT_EQ(ctest, token_list_size(list), 3, "size is 3");
    token_list_remove(list, 1);
    CTEST_ASSERT_EQ(ctest, token_list_size(list), 2, "size is 2 after remove");

    token_list_destroy(&list);
}

CTEST(test_token_list_clone)
{
    token_list_t *orig = token_list_create();
    token_list_append(orig, token_create_word());
    token_list_append(orig, token_create(TOKEN_IF));

    token_list_t *cloned = token_list_clone(orig);
    CTEST_ASSERT_EQ(ctest, token_list_size(cloned), 2, "cloned size matches");

    token_list_destroy(&orig);
    token_list_destroy(&cloned);
}

// ============================================================================
// Part List Lifecycle
// ============================================================================

CTEST(test_part_list_create_destroy)
{
    part_list_t *list = part_list_create();
    CTEST_ASSERT_NOT_NULL(ctest, list, "list created");
    CTEST_ASSERT_EQ(ctest, part_list_size(list), 0, "size is 0");

    part_list_destroy(&list);
}

CTEST(test_part_list_append)
{
    part_list_t *list = part_list_create();
    string_t *s = string_create_from_cstr("text");
    part_list_append(list, part_create_literal(s));
    string_destroy(&s);

    CTEST_ASSERT_EQ(ctest, part_list_size(list), 1, "size is 1");

    part_list_destroy(&list);
}

// ============================================================================
// Main test runner
// ============================================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    log_disable_abort();
    arena_start();

    CTestEntry *suite[] = {
        CTEST_ENTRY(test_token_create_destroy),
        CTEST_ENTRY(test_token_create_word),
        CTEST_ENTRY(test_token_clone_word),

        CTEST_ENTRY(test_token_type_conversion),
        CTEST_ENTRY(test_token_is_reserved_word),
        CTEST_ENTRY(test_token_string_to_reserved_word),
        CTEST_ENTRY(test_token_is_operator),
        CTEST_ENTRY(test_token_string_to_operator),

        CTEST_ENTRY(test_part_create_literal),
        CTEST_ENTRY(test_part_create_parameter),
        CTEST_ENTRY(test_token_add_literal_part),
        CTEST_ENTRY(test_token_append_parameter),

        CTEST_ENTRY(test_token_set_quoted),
        CTEST_ENTRY(test_part_quote_tracking),

        CTEST_ENTRY(test_token_expansion_flags_initial),
        CTEST_ENTRY(test_token_recompute_expansion_flags),

        CTEST_ENTRY(test_token_set_location),

        CTEST_ENTRY(test_token_io_number),
        CTEST_ENTRY(test_token_io_location),

        CTEST_ENTRY(test_token_try_promote_to_reserved_word),
        CTEST_ENTRY(test_token_try_promote_to_bang),
        CTEST_ENTRY(test_token_quoted_word_no_promote),

        CTEST_ENTRY(test_token_list_create_destroy),
        CTEST_ENTRY(test_token_list_append),
        CTEST_ENTRY(test_token_list_get),
        CTEST_ENTRY(test_token_list_remove),
        CTEST_ENTRY(test_token_list_clone),

        CTEST_ENTRY(test_part_list_create_destroy),
        CTEST_ENTRY(test_part_list_append),

        NULL
    };

    int result = ctest_run_suite(suite);

    arena_end();

    return result;
}

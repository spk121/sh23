/**
 * @file test_expander_ctest.c
 * @brief Tests for the expander module
 */

#include "ctest.h"
#include "expander.h"
#include "token.h"
#include "string_t.h"
#include "ast.h"
#include "variable_store.h"
#include "positional_params.h"
#include "xalloc.h"

// Test-specific pathname expansion callback that returns two fixed filenames
static string_list_t *test_pathname_expansion_callback(void *user_data, const string_t *pattern)
{
    (void)pattern;
    (void)user_data;
    string_list_t *lst = string_list_create();
    string_list_push_back(lst, string_create_from_cstr("foo.txt"));
    string_list_push_back(lst, string_create_from_cstr("bar.txt"));
    return lst;
}

/**
 * Test that we can create and destroy an expander
 */
CTEST(test_expander_create_destroy)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Pathname expansion callback: verify expander calls callback and replaces the word
 */
CTEST(test_expander_pathname_expansion_callback)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");

    // Register test-specific callback
    expander_set_glob(exp, test_pathname_expansion_callback);

    // Build a WORD token containing a literal with glob chars
    token_t *word = token_create_word();
    token_add_literal_part(word, string_create_from_cstr("*.txt"));

    // Expand the word
    string_list_t *res = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, res, "expansion returned list");
    CTEST_ASSERT_EQ(ctest, string_list_size(res), 2, "two matches returned");
    const string_t *s0 = string_list_at(res, 0);
    const string_t *s1 = string_list_at(res, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s0), "foo.txt", "first match is foo.txt");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s1), "bar.txt", "second match is bar.txt");

    // Cleanup
    string_list_destroy(&res);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Recursive parameter expansion: ${foo:=${bar}} assigns foo to expanded ${bar}
 */
CTEST(test_expander_recursive_param_assign_default)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");

    // Set bar=B; leave foo unset
    variable_store_add_cstr(vars, "bar", "B", false, false);

    // Build word token containing a PART_PARAMETER for ${foo:=${bar}}
    token_t *word = token_create_word();
    part_t *p = part_create_parameter(string_create_from_cstr("foo"));
    p->param_kind = PARAM_ASSIGN_DEFAULT;
    p->word = string_create_from_cstr("${bar}");
    token_add_part(word, p);

    string_list_t *res = expander_expand_word(exp, word);
    CTEST_ASSERT_EQ(ctest, string_list_size(res), 1, "one field produced");
    const string_t *out = string_list_at(res, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(out), "B", "assign default uses expanded ${bar}");

    // Verify foo was assigned to B in variable store
    CTEST_ASSERT_EQ(ctest, variable_store_has_name_cstr(vars, "foo"), 1, "foo present");
    const char *foo_val = variable_store_get_value_cstr(vars, "foo");
    CTEST_ASSERT_NOT_NULL(ctest, foo_val, "foo assigned");
    CTEST_ASSERT_STR_EQ(ctest, foo_val, "B", "foo == B");

    string_list_destroy(&res);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Recursive parameter expansion: ${foo:-${bar}} uses expanded ${bar} when foo unset
 */
CTEST(test_expander_recursive_param_use_default)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");

    variable_store_add_cstr(vars, "bar", "X", false, false);

    token_t *word = token_create_word();
    part_t *p = part_create_parameter(string_create_from_cstr("foo"));
    p->param_kind = PARAM_USE_DEFAULT;
    p->word = string_create_from_cstr("${bar}");
    token_add_part(word, p);

    string_list_t *res = expander_expand_word(exp, word);
    CTEST_ASSERT_EQ(ctest, string_list_size(res), 1, "one field produced");
    const string_t *out = string_list_at(res, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(out), "X", "use default expands ${bar}");

    string_list_destroy(&res);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Recursive parameter expansion: ${foo:+${bar}} returns expanded ${bar} only if foo is set/non-null
 */
CTEST(test_expander_recursive_param_use_alternate)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");

    // foo set to Y; bar to Z
    variable_store_add_cstr(vars, "foo", "Y", false, false);
    variable_store_add_cstr(vars, "bar", "Z", false, false);

    token_t *word = token_create_word();
    part_t *p = part_create_parameter(string_create_from_cstr("foo"));
    p->param_kind = PARAM_USE_ALTERNATE;
    p->word = string_create_from_cstr("${bar}");
    token_add_part(word, p);

    string_list_t *res = expander_expand_word(exp, word);
    CTEST_ASSERT_EQ(ctest, string_list_size(res), 1, "one field produced");
    const string_t *out = string_list_at(res, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(out), "Z", "use alternate expands ${bar}");

    string_list_destroy(&res);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test IFS getter and setter via variable store
 */
CTEST(test_expander_ifs)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Default IFS should be space, tab, newline (from getenv or default)
    // Since no IFS set, it uses default " \t\n"
    // But to test setting, set IFS in vars
    variable_store_add_cstr(vars, "IFS", ":", false, false);
    
    // For expansion, IFS is checked in expand_word
    // Create a word that needs splitting
    token_t *word = token_create_word();
    token_add_literal_part(word, string_create_from_cstr("a:b:c"));
    // Mark as needing field splitting (assume token_needs_expansion sets it)
    word->needs_field_splitting = true;
    
    string_list_t *res = expander_expand_word(exp, word);
    CTEST_ASSERT_EQ(ctest, string_list_size(res), 3, "IFS splits on :");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(string_list_at(res, 0)), "a", "first field");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(string_list_at(res, 1)), "b", "second field");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(string_list_at(res, 2)), "c", "third field");
    
    string_list_destroy(&res);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test expanding a simple literal word
 */
CTEST(test_expander_expand_simple_word)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Create a simple word token with one literal part
    token_t *word = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, word, "word token created");
    
    string_t *text = string_create_from_cstr("hello");
    token_add_literal_part(word, text);
    string_destroy(&text);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back a list with one string "hello"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "hello", "expanded string is 'hello'");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test expanding a word with multiple literal parts
 */
CTEST(test_expander_expand_concatenated_word)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Create a word token with multiple literal parts
    token_t *word = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, word, "word token created");
    
    string_t *text1 = string_create_from_cstr("hello");
    token_add_literal_part(word, text1);
    string_destroy(&text1);
    
    string_t *text2 = string_create_from_cstr("world");
    token_add_literal_part(word, text2);
    string_destroy(&text2);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back a list with one string "helloworld"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "helloworld", "expanded string is 'helloworld'");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test arithmetic expansion with simple expression (stub returns 42)
 */
CTEST(test_expander_arithmetic_simple)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Create a word token with an arithmetic part: $((1+2))
    token_t *word = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, word, "word token created");
    
    string_t *expr = string_create_from_cstr("1+2");
    token_append_arithmetic(word, expr);
    string_destroy(&expr);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Stub returns "42"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "42", "arithmetic stub returns '42'");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test arithmetic expansion with variable reference (stub returns 42)
 */
CTEST(test_expander_arithmetic_with_variable)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Create a variable store and set x=10
    variable_store_add_cstr(vars, "x", "10", false, false);
    
    // Create a word token with an arithmetic part: $(($x+5))
    token_t *word = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, word, "word token created");
    
    string_t *expr = string_create_from_cstr("$x+5");
    token_append_arithmetic(word, expr);
    string_destroy(&expr);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Stub returns "42"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "42", "arithmetic stub returns '42'");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test arithmetic expansion with multiple operations and variables (stub returns 42)
 */
CTEST(test_expander_arithmetic_complex)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Create a variable store and set x=10, y=5
    variable_store_add_cstr(vars, "x", "10", false, false);
    variable_store_add_cstr(vars, "y", "5", false, false);
    
    // Create a word token with an arithmetic part: $(($x+$y*3))
    token_t *word = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, word, "word token created");
    
    string_t *expr = string_create_from_cstr("$x+$y*3");
    token_append_arithmetic(word, expr);
    string_destroy(&expr);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Stub returns "42"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "42", "arithmetic stub returns '42'");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test arithmetic expansion with empty expression (stub returns 42)
 */
CTEST(test_expander_arithmetic_empty)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Create a word token with an empty arithmetic part: $(())
    token_t *word = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, word, "word token created");
    
    string_t *expr = string_create_from_cstr("");
    token_append_arithmetic(word, expr);
    string_destroy(&expr);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Stub returns "42"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "42", "arithmetic stub returns '42'");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test arithmetic expansion with nested arithmetic (stub returns 42)
 */
CTEST(test_expander_arithmetic_nested)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Create a word token with nested arithmetic: $((1 + $((1 + 1))))
    token_t *word = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, word, "word token created");
    
    string_t *expr = string_create_from_cstr("1 + $((1 + 1))");
    token_append_arithmetic(word, expr);
    string_destroy(&expr);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Stub returns "42"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "42", "arithmetic stub returns '42'");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test expansion of special parameter $? (exit status) via variable store
 */
CTEST(test_expander_special_param_exit_status)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Set exit status to 42 in variable store
    variable_store_add_cstr(vars, "?", "42", false, false);
    
    // Create a word token with $? parameter
    token_t *word = token_create_word();
    string_t *param = string_create_from_cstr("?");
    token_append_parameter(word, param);
    string_destroy(&param);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back "42"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "42", "expanded $? is '42'");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test expansion of $? with exit status 0
 */
CTEST(test_expander_special_param_exit_zero)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Default exit status is 0 (not set, so empty)
    // But since not set, it returns empty string
    
    // Create a word token with $? parameter
    token_t *word = token_create_word();
    string_t *param = string_create_from_cstr("?");
    token_append_parameter(word, param);
    string_destroy(&param);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back empty string
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "", "expanded $? is empty when not set");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test braced form ${?} expands to exit status
 */
CTEST(test_expander_special_param_braced)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Set exit status to 127
    variable_store_add_cstr(vars, "?", "127", false, false);
    
    // Create a word token with ${?} parameter
    token_t *word = token_create_word();
    string_t *param = string_create_from_cstr("?");
    token_append_parameter(word, param);
    string_destroy(&param);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back "127"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "127", "expanded $? is '127'");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test $$ special parameter expansion with PID set
 */
CTEST(test_expander_special_param_pid)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Set PID to 12345
    variable_store_add_cstr(vars, "$", "12345", false, false);
    
    // Create a word token with $$ parameter
    token_t *word = token_create_word();
    string_t *param = string_create_from_cstr("$");
    token_append_parameter(word, param);
    string_destroy(&param);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back "12345"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "12345", "expanded $$ is '12345'");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test $$ special parameter expansion with braced form
 */
CTEST(test_expander_special_param_pid_braced)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Set PID to 99999
    variable_store_add_cstr(vars, "$", "99999", false, false);
    
    // Create a word token with ${$} parameter
    token_t *word = token_create_word();
    string_t *param = string_create_from_cstr("$");
    token_append_parameter(word, param);
    string_destroy(&param);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back "99999"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "99999", "expanded ${$} is '99999'");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test $$ special parameter expansion when PID is not set (returns empty)
 */
CTEST(test_expander_special_param_pid_default)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Don't set PID
    
    // Create a word token with $$ parameter
    token_t *word = token_create_word();
    string_t *param = string_create_from_cstr("$");
    token_append_parameter(word, param);
    string_destroy(&param);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back empty string
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "", "expanded $$ is empty when not set");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test $! special parameter expansion with background PID set
 */
CTEST(test_expander_special_param_background_pid)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Set background PID to 54321
    variable_store_add_cstr(vars, "!", "54321", false, false);
    
    // Create a word token with $! parameter
    token_t *word = token_create_word();
    string_t *param = string_create_from_cstr("!");
    token_append_parameter(word, param);
    string_destroy(&param);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back "54321"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "54321", "expanded $! is '54321'");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test $! special parameter expansion with braced form
 */
CTEST(test_expander_special_param_background_pid_braced)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Set background PID to 11111
    variable_store_add_cstr(vars, "!", "11111", false, false);
    
    // Create a word token with ${!} parameter
    token_t *word = token_create_word();
    string_t *param = string_create_from_cstr("!");
    token_append_parameter(word, param);
    string_destroy(&param);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back "11111"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "11111", "expanded ${!} is '11111'");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test $! special parameter expansion when background PID is not set (returns empty)
 */
CTEST(test_expander_special_param_background_pid_default)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Don't set background PID
    
    // Create a word token with $! parameter
    token_t *word = token_create_word();
    string_t *param = string_create_from_cstr("!");
    token_append_parameter(word, param);
    string_destroy(&param);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back empty string
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "", "expanded $! is empty when not set");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test positional parameters: set argv and verify $#, $0, $1, $2
 */
CTEST(test_expander_positionals_basic)
{
    variable_store_t *vars = variable_store_create();
    
    // Set positional params
    string_t *parg0 = string_create_from_cstr("mgsh");
    string_t *p1 = string_create_from_cstr("one");
    string_t *p2 = string_create_from_cstr("two");
    string_t *pargs[] = { parg0, p1, p2 };
    positional_params_t *params = positional_params_create_from_array(pargs, 3);
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");

    // $# should be 2
    token_t *w_hash = token_create_word();
    string_t *ph = string_create_from_cstr("#");
    token_append_parameter(w_hash, ph);
    string_destroy(&ph);
    string_list_t *r_hash = expander_expand_word(exp, w_hash);
    CTEST_ASSERT_EQ(ctest, string_list_size(r_hash), 1, "one field for $#");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(string_list_at(r_hash,0)), "2", "$# == 2");
    string_list_destroy(&r_hash);
    token_destroy(&w_hash);

    // $0
    token_t *w0 = token_create_word();
    string_t *p0 = string_create_from_cstr("0");
    token_append_parameter(w0, p0);
    string_destroy(&p0);
    string_list_t *r0 = expander_expand_word(exp, w0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(string_list_at(r0,0)), "mgsh", "$0 == mgsh");
    string_list_destroy(&r0);
    token_destroy(&w0);

    // $1
    token_t *w1 = token_create_word();
    string_t *p1_str = string_create_from_cstr("1");
    token_append_parameter(w1, p1_str);
    string_destroy(&p1_str);
    string_list_t *r1 = expander_expand_word(exp, w1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(string_list_at(r1,0)), "one", "$1 == one");
    string_list_destroy(&r1);
    token_destroy(&w1);

    // $2
    token_t *w2 = token_create_word();
    string_t *p2_str = string_create_from_cstr("2");
    token_append_parameter(w2, p2_str);
    string_destroy(&p2_str);
    string_list_t *r2 = expander_expand_word(exp, w2);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(string_list_at(r2,0)), "two", "$2 == two");
    string_list_destroy(&r2);
    token_destroy(&w2);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/**
 * Test $@ and $* behavior
 */
CTEST(test_expander_positionals_at_star)
{
    variable_store_t *vars = variable_store_create();
    
    string_t *p0 = string_create_from_cstr("sh");
    string_t *pa = string_create_from_cstr("a");
    string_t *pb = string_create_from_cstr("b");
    string_t *pc = string_create_from_cstr("c");
    string_t *pargs[] = { p0, pa, pb, pc };
    positional_params_t *params = positional_params_create_from_array(pargs, 4);
    expander_t *exp = expander_create(vars, params);
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");

    // $@ unquoted -> separate fields a b c
    token_t *wat = token_create_word();
    string_t *pat = string_create_from_cstr("@");
    token_append_parameter(wat, pat);
    string_destroy(&pat);
    string_list_t *rat = expander_expand_word(exp, wat);
    CTEST_ASSERT_EQ(ctest, string_list_size(rat), 3, "$@ expands to 3 fields");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(string_list_at(rat,0)), "a", "first field");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(string_list_at(rat,1)), "b", "second field");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(string_list_at(rat,2)), "c", "third field");
    string_list_destroy(&rat);
    token_destroy(&wat);

    // $* unquoted -> single word joined by first IFS (space by default)
    token_t *wst = token_create_word();
    string_t *pst = string_create_from_cstr("*");
    token_append_parameter(wst, pst);
    string_destroy(&pst);
    string_list_t *rst = expander_expand_word(exp, wst);
    CTEST_ASSERT_EQ(ctest, string_list_size(rst), 1, "$* expands to single field");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(string_list_at(rst,0)), "a b c", "joined by space");
    string_list_destroy(&rst);
    token_destroy(&wst);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(void)
{
    arena_start();
    log_init();
    

    CTestEntry *suite[] = {
        CTEST_ENTRY(test_expander_create_destroy),
        CTEST_ENTRY(test_expander_ifs),
        CTEST_ENTRY(test_expander_expand_simple_word),
        CTEST_ENTRY(test_expander_expand_concatenated_word),
        CTEST_ENTRY(test_expander_arithmetic_simple),
        CTEST_ENTRY(test_expander_arithmetic_with_variable),
        CTEST_ENTRY(test_expander_arithmetic_complex),
        CTEST_ENTRY(test_expander_arithmetic_empty),
        CTEST_ENTRY(test_expander_arithmetic_nested),
        CTEST_ENTRY(test_expander_special_param_exit_status),
        CTEST_ENTRY(test_expander_special_param_exit_zero),
        CTEST_ENTRY(test_expander_special_param_braced),
        CTEST_ENTRY(test_expander_special_param_pid),
        CTEST_ENTRY(test_expander_special_param_pid_braced),
        CTEST_ENTRY(test_expander_special_param_pid_default),
        CTEST_ENTRY(test_expander_special_param_background_pid),
        CTEST_ENTRY(test_expander_special_param_background_pid_braced),
        CTEST_ENTRY(test_expander_special_param_background_pid_default),
        CTEST_ENTRY(test_expander_positionals_basic),
        CTEST_ENTRY(test_expander_positionals_at_star),
        CTEST_ENTRY(test_expander_recursive_param_assign_default),
        CTEST_ENTRY(test_expander_recursive_param_use_default),
        CTEST_ENTRY(test_expander_recursive_param_use_alternate),
        CTEST_ENTRY(test_expander_pathname_expansion_callback),
        NULL
    };

    int result = ctest_run_suite(suite);

    arena_end();
    return result;
}

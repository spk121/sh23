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
#include "xalloc.h"

/**
 * Test that we can create and destroy an expander
 */
CTEST(test_expander_create_destroy)
{
    expander_t *exp = expander_create();
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    expander_destroy(&exp);
    (void)ctest;
}

    /**
     * Recursive parameter expansion: ${foo:=${bar}} assigns foo to expanded ${bar}
     */
    CTEST(test_expander_recursive_param_assign_default)
    {
        expander_t *exp = expander_create();
        variable_store_t *vars = variable_store_create();
        expander_set_variable_store(exp, vars);

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
        variable_store_destroy(&vars);
        expander_destroy(&exp);
        (void)ctest;
    }

    /**
     * Recursive parameter expansion: ${foo:-${bar}} uses expanded ${bar} when foo unset
     */
    CTEST(test_expander_recursive_param_use_default)
    {
        expander_t *exp = expander_create();
        variable_store_t *vars = variable_store_create();
        expander_set_variable_store(exp, vars);

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
        variable_store_destroy(&vars);
        expander_destroy(&exp);
        (void)ctest;
    }

    /**
     * Recursive parameter expansion: ${foo:+${bar}} returns expanded ${bar} only if foo is set/non-null
     */
    CTEST(test_expander_recursive_param_use_alternate)
    {
        expander_t *exp = expander_create();
        variable_store_t *vars = variable_store_create();
        expander_set_variable_store(exp, vars);

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
        variable_store_destroy(&vars);
        expander_destroy(&exp);
        (void)ctest;
    }

/**
 * Test IFS getter and setter
 */
CTEST(test_expander_ifs)
{
    expander_t *exp = expander_create();
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Default IFS should be space, tab, newline
    const string_t *ifs = expander_get_ifs(exp);
    CTEST_ASSERT_NOT_NULL(ctest, ifs, "default IFS not NULL");
    CTEST_ASSERT_EQ(ctest, string_length(ifs), 3, "default IFS length is 3");
    
    // Set a custom IFS
    string_t *custom_ifs = string_create_from_cstr(":");
    expander_set_ifs(exp, custom_ifs);
    string_destroy(&custom_ifs);
    
    // Verify the IFS was updated
    ifs = expander_get_ifs(exp);
    CTEST_ASSERT_NOT_NULL(ctest, ifs, "IFS not NULL after set");
    CTEST_ASSERT_EQ(ctest, string_length(ifs), 1, "IFS length is 1");
    CTEST_ASSERT_EQ(ctest, string_at(ifs, 0), ':', "IFS is colon");
    
    expander_destroy(&exp);
    (void)ctest;
}

/**
 * Test expanding a simple literal word
 */
CTEST(test_expander_expand_simple_word)
{
    expander_t *exp = expander_create();
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
    (void)ctest;
}

/**
 * Test expanding a word with multiple literal parts
 */
CTEST(test_expander_expand_concatenated_word)
{
    expander_t *exp = expander_create();
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
    (void)ctest;
}

/**
 * Test that expander_expand_ast doesn't crash
 */
CTEST(test_expander_expand_ast_stub)
{
    expander_t *exp = expander_create();
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Create a simple AST node (we'll just use a word node for testing)
    // Since this is a stub, it should just return the node unchanged
    token_t *word = token_create_word();
    string_t *text = string_create_from_cstr("test");
    token_add_literal_part(word, text);
    string_destroy(&text);
    
    ast_node_t *node = ast_create_case_clause(word);
    CTEST_ASSERT_NOT_NULL(ctest, node, "AST node created");
    
    // Call the expansion function (stub should return it unchanged)
    ast_node_t *result = expander_expand_ast(exp, node);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    CTEST_ASSERT_EQ(ctest, result, node, "result is same node");
    
    ast_node_destroy(&node);
    expander_destroy(&exp);
    (void)ctest;
}

/**
 * Test arithmetic expansion with simple expression
 */
CTEST(test_expander_arithmetic_simple)
{
    expander_t *exp = expander_create();
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Create a variable store and set a variable
    variable_store_t *vars = variable_store_create();
    expander_set_variable_store(exp, vars);
    
    // Create a word token with an arithmetic part: $((1+2))
    token_t *word = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, word, "word token created");
    
    string_t *expr = string_create_from_cstr("1+2");
    token_append_arithmetic(word, expr);
    string_destroy(&expr);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back "3"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "3", "expanded string is '3'");
    
    string_list_destroy(&result);
    token_destroy(&word);
    variable_store_destroy(&vars);
    expander_destroy(&exp);
    (void)ctest;
}

/**
 * Test arithmetic expansion with variable reference
 */
CTEST(test_expander_arithmetic_with_variable)
{
    expander_t *exp = expander_create();
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Create a variable store and set x=10
    variable_store_t *vars = variable_store_create();
    variable_store_add_cstr(vars, "x", "10", false, false);
    expander_set_variable_store(exp, vars);
    
    // Create a word token with an arithmetic part: $(($x+5))
    token_t *word = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, word, "word token created");
    
    string_t *expr = string_create_from_cstr("$x+5");
    token_append_arithmetic(word, expr);
    string_destroy(&expr);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back "15"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "15", "expanded string is '15'");
    
    string_list_destroy(&result);
    token_destroy(&word);
    variable_store_destroy(&vars);
    expander_destroy(&exp);
    (void)ctest;
}

/**
 * Test arithmetic expansion with multiple operations and variables
 */
CTEST(test_expander_arithmetic_complex)
{
    expander_t *exp = expander_create();
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Create a variable store and set x=10, y=5
    variable_store_t *vars = variable_store_create();
    variable_store_add_cstr(vars, "x", "10", false, false);
    variable_store_add_cstr(vars, "y", "5", false, false);
    expander_set_variable_store(exp, vars);
    
    // Create a word token with an arithmetic part: $(($x+$y*3))
    // Should evaluate as: 10 + (5 * 3) = 10 + 15 = 25
    token_t *word = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, word, "word token created");
    
    string_t *expr = string_create_from_cstr("$x+$y*3");
    token_append_arithmetic(word, expr);
    string_destroy(&expr);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back "25"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "25", "expanded string is '25'");
    
    string_list_destroy(&result);
    token_destroy(&word);
    variable_store_destroy(&vars);
    expander_destroy(&exp);
    (void)ctest;
}

/**
 * Test arithmetic expansion with empty expression
 */
CTEST(test_expander_arithmetic_empty)
{
    expander_t *exp = expander_create();
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Create a variable store
    variable_store_t *vars = variable_store_create();
    expander_set_variable_store(exp, vars);
    
    // Create a word token with an empty arithmetic part: $(())
    token_t *word = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, word, "word token created");
    
    string_t *expr = string_create_from_cstr("");
    token_append_arithmetic(word, expr);
    string_destroy(&expr);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back "0" for empty expression
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "0", "empty expression evaluates to 0");
    
    string_list_destroy(&result);
    token_destroy(&word);
    variable_store_destroy(&vars);
    expander_destroy(&exp);
    (void)ctest;
}

/**
 * Test arithmetic expansion with nested arithmetic
 */
CTEST(test_expander_arithmetic_nested)
{
    expander_t *exp = expander_create();
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Create a variable store
    variable_store_t *vars = variable_store_create();
    expander_set_variable_store(exp, vars);
    
    // Create a word token with nested arithmetic: $((1 + $((1 + 1))))
    // The inner $((...)) should evaluate to 2, then 1 + 2 = 3
    token_t *word = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, word, "word token created");
    
    string_t *expr = string_create_from_cstr("1 + $((1 + 1))");
    token_append_arithmetic(word, expr);
    string_destroy(&expr);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back "3"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "3", "nested arithmetic evaluates correctly");
    
    string_list_destroy(&result);
    token_destroy(&word);
    variable_store_destroy(&vars);
    expander_destroy(&exp);
    (void)ctest;
}

/**
 * Test expansion of special parameter $? (exit status)
 */
CTEST(test_expander_special_param_exit_status)
{
    expander_t *exp = expander_create();
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Set exit status to 42
    expander_set_last_exit_status(exp, 42);
    CTEST_ASSERT_EQ(ctest, expander_get_last_exit_status(exp), 42, "exit status set");
    
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
    (void)ctest;
}

/**
 * Test expansion of $? with exit status 0
 */
CTEST(test_expander_special_param_exit_zero)
{
    expander_t *exp = expander_create();
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Default exit status is 0
    CTEST_ASSERT_EQ(ctest, expander_get_last_exit_status(exp), 0, "default exit status is 0");
    
    // Create a word token with $? parameter
    token_t *word = token_create_word();
    string_t *param = string_create_from_cstr("?");
    token_append_parameter(word, param);
    string_destroy(&param);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back "0"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "0", "expanded $? is '0'");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    (void)ctest;
}

/**
 * Test braced form ${?} expands to exit status
 */
CTEST(test_expander_special_param_braced)
{
    expander_t *exp = expander_create();
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Set exit status to 127
    expander_set_last_exit_status(exp, 127);
    
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
    (void)ctest;
}

/**
 * Test $$ special parameter expansion with PID set
 */
CTEST(test_expander_special_param_pid)
{
    expander_t *exp = expander_create();
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Set a test PID
    expander_set_pid(exp, 12345);
    
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
    (void)ctest;
}

/**
 * Test $$ special parameter expansion with braced form
 */
CTEST(test_expander_special_param_pid_braced)
{
    expander_t *exp = expander_create();
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Set a test PID
    expander_set_pid(exp, 99999);
    
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
    (void)ctest;
}

/**
 * Test $$ special parameter expansion when PID is not set (returns literal $$)
 */
CTEST(test_expander_special_param_pid_default)
{
    expander_t *exp = expander_create();
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Don't set a PID, should return literal "$$"
    
    // Create a word token with $$ parameter
    token_t *word = token_create_word();
    string_t *param = string_create_from_cstr("$");
    token_append_parameter(word, param);
    string_destroy(&param);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back "$$" (literal)
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "$$", "expanded $$ is '$$' when not set");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    (void)ctest;
}

/**
 * Test $! special parameter expansion with background PID set
 */
CTEST(test_expander_special_param_background_pid)
{
    expander_t *exp = expander_create();
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Set a test background PID
    expander_set_background_pid(exp, 54321);
    
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
    (void)ctest;
}

/**
 * Test $! special parameter expansion with braced form
 */
CTEST(test_expander_special_param_background_pid_braced)
{
    expander_t *exp = expander_create();
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Set a test background PID
    expander_set_background_pid(exp, 11111);
    
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
    (void)ctest;
}

/**
 * Test $! special parameter expansion when background PID is not set (returns literal $!)
 */
CTEST(test_expander_special_param_background_pid_default)
{
    expander_t *exp = expander_create();
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    
    // Don't set a background PID, should return literal "$!"
    
    // Create a word token with $! parameter
    token_t *word = token_create_word();
    string_t *param = string_create_from_cstr("!");
    token_append_parameter(word, param);
    string_destroy(&param);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back "$!" (literal)
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_at(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(expanded), "$!", "expanded $! is '$!' when not set");
    
    string_list_destroy(&result);
    token_destroy(&word);
    expander_destroy(&exp);
    (void)ctest;
}

/**
 * Test positional parameters: set argv and verify $#, $0, $1, $2
 */
CTEST(test_expander_positionals_basic)
{
    expander_t *exp = expander_create();
    CTEST_ASSERT_NOT_NULL(ctest, exp, "expander created");
    const char *argv[] = { "mgsh", "one", "two" };
    expander_set_positionals(exp, 3, argv);

    // $# should be 2 (excluding $0)
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
    string_t *p1 = string_create_from_cstr("1");
    token_append_parameter(w1, p1);
    string_destroy(&p1);
    string_list_t *r1 = expander_expand_word(exp, w1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(string_list_at(r1,0)), "one", "$1 == one");
    string_list_destroy(&r1);
    token_destroy(&w1);

    // $2
    token_t *w2 = token_create_word();
    string_t *p2 = string_create_from_cstr("2");
    token_append_parameter(w2, p2);
    string_destroy(&p2);
    string_list_t *r2 = expander_expand_word(exp, w2);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(string_list_at(r2,0)), "two", "$2 == two");
    string_list_destroy(&r2);
    token_destroy(&w2);

    expander_destroy(&exp);
    (void)ctest;
}

/**
 * Test $@ and $* behavior
 */
CTEST(test_expander_positionals_at_star)
{
    expander_t *exp = expander_create();
    const char *argv[] = { "mgsh", "a", "b", "c" };
    expander_set_positionals(exp, 4, argv);

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
    (void)ctest;
}
// Array of test entries
static CTestEntry test_entries[] = {
    { "test_expander_create_destroy", ctest_func_test_expander_create_destroy, NULL, NULL, false },
    { "test_expander_ifs", ctest_func_test_expander_ifs, NULL, NULL, false },
    { "test_expander_expand_simple_word", ctest_func_test_expander_expand_simple_word, NULL, NULL, false },
    { "test_expander_expand_concatenated_word", ctest_func_test_expander_expand_concatenated_word, NULL, NULL, false },
    { "test_expander_expand_ast_stub", ctest_func_test_expander_expand_ast_stub, NULL, NULL, false },
    { "test_expander_arithmetic_simple", ctest_func_test_expander_arithmetic_simple, NULL, NULL, false },
    { "test_expander_arithmetic_with_variable", ctest_func_test_expander_arithmetic_with_variable, NULL, NULL, false },
    { "test_expander_arithmetic_complex", ctest_func_test_expander_arithmetic_complex, NULL, NULL, false },
    { "test_expander_arithmetic_empty", ctest_func_test_expander_arithmetic_empty, NULL, NULL, false },
    { "test_expander_arithmetic_nested", ctest_func_test_expander_arithmetic_nested, NULL, NULL, false },
    { "test_expander_special_param_exit_status", ctest_func_test_expander_special_param_exit_status, NULL, NULL, false },
    { "test_expander_special_param_exit_zero", ctest_func_test_expander_special_param_exit_zero, NULL, NULL, false },
    { "test_expander_special_param_braced", ctest_func_test_expander_special_param_braced, NULL, NULL, false },
    { "test_expander_special_param_pid", ctest_func_test_expander_special_param_pid, NULL, NULL, false },
    { "test_expander_special_param_pid_braced", ctest_func_test_expander_special_param_pid_braced, NULL, NULL, false },
    { "test_expander_special_param_pid_default", ctest_func_test_expander_special_param_pid_default, NULL, NULL, false },
    { "test_expander_special_param_background_pid", ctest_func_test_expander_special_param_background_pid, NULL, NULL, false },
    { "test_expander_special_param_background_pid_braced", ctest_func_test_expander_special_param_background_pid_braced, NULL, NULL, false },
    { "test_expander_special_param_background_pid_default", ctest_func_test_expander_special_param_background_pid_default, NULL, NULL, false },
    { "test_expander_positionals_basic", ctest_func_test_expander_positionals_basic, NULL, NULL, false },
    { "test_expander_positionals_at_star", ctest_func_test_expander_positionals_at_star, NULL, NULL, false },
    { "test_expander_recursive_param_assign_default", ctest_func_test_expander_recursive_param_assign_default, NULL, NULL, false },
    { "test_expander_recursive_param_use_default", ctest_func_test_expander_recursive_param_use_default, NULL, NULL, false },
    { "test_expander_recursive_param_use_alternate", ctest_func_test_expander_recursive_param_use_alternate, NULL, NULL, false },
};

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    
    // Initialize the memory arena
    arena_init();
    
    CTest ctest = {0};
    int num_tests = sizeof(test_entries) / sizeof(test_entries[0]);
    
    printf("TAP version 14\n");
    printf("1..%d\n", num_tests);
    
    for (int i = 0; i < num_tests; i++)
    {
        CTestEntry *entry = &test_entries[i];
        ctest.current_test = entry->name;
        
        if (entry->setup)
            entry->setup(&ctest);
        
        entry->func(&ctest);
        
        if (entry->teardown)
            entry->teardown(&ctest);
        
        printf("ok %d - %s\n", i + 1, entry->name);
    }
    
    arena_end();
    return 0;
}

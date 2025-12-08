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
    
    expander_destroy(exp);
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
    string_destroy(custom_ifs);
    
    // Verify the IFS was updated
    ifs = expander_get_ifs(exp);
    CTEST_ASSERT_NOT_NULL(ctest, ifs, "IFS not NULL after set");
    CTEST_ASSERT_EQ(ctest, string_length(ifs), 1, "IFS length is 1");
    CTEST_ASSERT_EQ(ctest, string_char_at(ifs, 0), ':', "IFS is colon");
    
    expander_destroy(exp);
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
    string_destroy(text);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back a list with one string "hello"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_get(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_data(expanded), "hello", "expanded string is 'hello'");
    
    string_list_destroy(result);
    token_destroy(word);
    expander_destroy(exp);
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
    string_destroy(text1);
    
    string_t *text2 = string_create_from_cstr("world");
    token_add_literal_part(word, text2);
    string_destroy(text2);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back a list with one string "helloworld"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_get(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_data(expanded), "helloworld", "expanded string is 'helloworld'");
    
    string_list_destroy(result);
    token_destroy(word);
    expander_destroy(exp);
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
    string_destroy(text);
    
    ast_node_t *node = ast_create_word(word);
    CTEST_ASSERT_NOT_NULL(ctest, node, "AST node created");
    
    // Call the expansion function (stub should return it unchanged)
    ast_node_t *result = expander_expand_ast(exp, node);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    CTEST_ASSERT_EQ(ctest, result, node, "result is same node");
    
    ast_node_destroy(node);
    expander_destroy(exp);
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
    variable_store_t *vars = variable_store_create("test");
    expander_set_variable_store(exp, vars);
    
    // Create a word token with an arithmetic part: $((1+2))
    token_t *word = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, word, "word token created");
    
    string_t *expr = string_create_from_cstr("1+2");
    token_append_arithmetic(word, expr);
    string_destroy(expr);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back "3"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_get(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_data(expanded), "3", "expanded string is '3'");
    
    string_list_destroy(result);
    token_destroy(word);
    variable_store_destroy(vars);
    expander_destroy(exp);
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
    variable_store_t *vars = variable_store_create("test");
    variable_store_add_cstr(vars, "x", "10", false, false);
    expander_set_variable_store(exp, vars);
    
    // Create a word token with an arithmetic part: $(($x+5))
    token_t *word = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, word, "word token created");
    
    string_t *expr = string_create_from_cstr("$x+5");
    token_append_arithmetic(word, expr);
    string_destroy(expr);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back "15"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_get(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_data(expanded), "15", "expanded string is '15'");
    
    string_list_destroy(result);
    token_destroy(word);
    variable_store_destroy(vars);
    expander_destroy(exp);
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
    variable_store_t *vars = variable_store_create("test");
    variable_store_add_cstr(vars, "x", "10", false, false);
    variable_store_add_cstr(vars, "y", "5", false, false);
    expander_set_variable_store(exp, vars);
    
    // Create a word token with an arithmetic part: $(($x+$y*3))
    // Should evaluate as: 10 + (5 * 3) = 10 + 15 = 25
    token_t *word = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, word, "word token created");
    
    string_t *expr = string_create_from_cstr("$x+$y*3");
    token_append_arithmetic(word, expr);
    string_destroy(expr);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back "25"
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_get(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_data(expanded), "25", "expanded string is '25'");
    
    string_list_destroy(result);
    token_destroy(word);
    variable_store_destroy(vars);
    expander_destroy(exp);
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
    variable_store_t *vars = variable_store_create("test");
    expander_set_variable_store(exp, vars);
    
    // Create a word token with an empty arithmetic part: $(())
    token_t *word = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, word, "word token created");
    
    string_t *expr = string_create_from_cstr("");
    token_append_arithmetic(word, expr);
    string_destroy(expr);
    
    // Expand the word
    string_list_t *result = expander_expand_word(exp, word);
    CTEST_ASSERT_NOT_NULL(ctest, result, "expansion result not NULL");
    
    // Should get back "0" for empty expression
    CTEST_ASSERT_EQ(ctest, string_list_size(result), 1, "result has one string");
    const string_t *expanded = string_list_get(result, 0);
    CTEST_ASSERT_NOT_NULL(ctest, expanded, "expanded string not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_data(expanded), "0", "empty expression evaluates to 0");
    
    string_list_destroy(result);
    token_destroy(word);
    variable_store_destroy(vars);
    expander_destroy(exp);
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

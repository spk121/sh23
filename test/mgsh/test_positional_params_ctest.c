/*
 * test_positional_params_ctest.c
 *
 * Unit tests for positional_params.c - POSIX shell positional parameter storage
 *
 * Tests cover:
 * - Lifecycle management (create, destroy, clone)
 * - Parameter access ($0, $1-$N, $#)
 * - Special variable support ($@, $*)
 * - Modification operations (set, shift)
 * - Edge cases and limits
 */

#include "ctest.h"
#include "positional_params.h"
#include "lib.h"
#include "xalloc.h"
#include <string.h>

// ============================================================================
// Lifecycle Tests
// ============================================================================

CTEST(test_create_empty)
{
    positional_params_t *params = positional_params_create();
    CTEST_ASSERT_NOT_NULL(ctest, params, "params created");
    CTEST_ASSERT_EQ(ctest, 0, positional_params_count(params), "count is 0");
    CTEST_ASSERT_NULL(ctest, positional_params_get_arg0(params), "arg0 is NULL");
    positional_params_destroy(&params);
}

CTEST(test_create_from_array)
{
    string_t *args[3];
    args[0] = string_create_from_cstr("first");
    args[1] = string_create_from_cstr("second");
    args[2] = string_create_from_cstr("third");

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *params = positional_params_create_from_array(arg0, 3, (const string_t **)args);
    string_destroy(&arg0);
    CTEST_ASSERT_NOT_NULL(ctest, params, "params created");
    CTEST_ASSERT_EQ(ctest, 3, positional_params_count(params), "count is 3");

    const string_t *p1 = positional_params_get(params, 1);
    CTEST_ASSERT_NOT_NULL(ctest, p1, "param 1 exists");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(p1), "first", "param 1 is 'first'");

    positional_params_destroy(&params);
}

CTEST(test_create_from_string_list)
{
    string_list_t *list = string_list_create();
    string_list_push_back(list, string_create_from_cstr("alpha"));
    string_list_push_back(list, string_create_from_cstr("beta"));
    string_list_push_back(list, string_create_from_cstr("gamma"));

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *params = positional_params_create_from_string_list(arg0, list);
    string_destroy(&arg0);
    CTEST_ASSERT_NOT_NULL(ctest, params, "params created");
    CTEST_ASSERT_EQ(ctest, 3, positional_params_count(params), "count is 3");

    const string_t *p2 = positional_params_get(params, 2);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(p2), "beta", "param 2 is 'beta'");

    string_list_destroy(&list);
    positional_params_destroy(&params);
}

CTEST(test_create_from_argv)
{
    const char *argv[] = {"arg1", "arg2", "arg3"};

    positional_params_t *params = positional_params_create_from_argv("myshell", 3, argv);
    CTEST_ASSERT_NOT_NULL(ctest, params, "params created");

    // argv[0] becomes arg0
    const string_t *arg0 = positional_params_get_arg0(params);
    CTEST_ASSERT_NOT_NULL(ctest, arg0, "arg0 exists");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(arg0), "myshell", "arg0 is 'myshell'");

    // argv[1] becomes $1
    CTEST_ASSERT_EQ(ctest, 3, positional_params_count(params), "count is 3");
    const string_t *p1 = positional_params_get(params, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(p1), "arg1", "param 1 is 'arg1'");

    positional_params_destroy(&params);
}

CTEST(test_clone)
{
    string_t *args[2];
    args[0] = string_create_from_cstr("foo");
    args[1] = string_create_from_cstr("bar");

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *original = positional_params_create_from_array(arg0, 2, (const string_t **)args);
    string_destroy(&arg0);
    positional_params_set_arg0(original, string_create_from_cstr("test"));

    positional_params_t *copy = positional_params_clone(original);
    CTEST_ASSERT_NOT_NULL(ctest, copy, "copy created");
    CTEST_ASSERT_EQ(ctest, 2, positional_params_count(copy), "count is 2");

    // Verify arg0 was cloned
    const string_t *arg0_copy = positional_params_get_arg0(copy);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(arg0_copy), "test", "arg0 is 'test'");

    // Verify parameters were cloned
    const string_t *p1 = positional_params_get(copy, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(p1), "foo", "param 1 is 'foo'");

    positional_params_destroy(&original);
    positional_params_destroy(&copy);
}

CTEST(test_destroy_sets_to_null)
{
    positional_params_t *params = positional_params_create();
    positional_params_destroy(&params);
    CTEST_ASSERT_NULL(ctest, params, "params is NULL after destroy");
}

// ============================================================================
// Parameter Access Tests
// ============================================================================

CTEST(test_get_valid_parameter)
{
    string_t *args[3];
    args[0] = string_create_from_cstr("one");
    args[1] = string_create_from_cstr("two");
    args[2] = string_create_from_cstr("three");

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *params = positional_params_create_from_array(arg0, 3, (const string_t **)args);
    string_destroy(&arg0);

    const string_t *p1 = positional_params_get(params, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(p1), "one", "param 1 is 'one'");

    const string_t *p2 = positional_params_get(params, 2);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(p2), "two", "param 2 is 'two'");

    const string_t *p3 = positional_params_get(params, 3);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(p3), "three", "param 3 is 'three'");

    positional_params_destroy(&params);
}

CTEST(test_get_out_of_range_returns_null)
{
    string_t *args[2];
    args[0] = string_create_from_cstr("first");
    args[1] = string_create_from_cstr("second");

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *params = positional_params_create_from_array(arg0, 2, (const string_t **)args);
    string_destroy(&arg0);

    // Valid range is 1-2
    CTEST_ASSERT_NULL(ctest, positional_params_get(params, 3), "param 3 is NULL");
    CTEST_ASSERT_NULL(ctest, positional_params_get(params, 100), "param 100 is NULL");

    positional_params_destroy(&params);
}

CTEST(test_get_arg0)
{
    positional_params_t *params = positional_params_create();
    positional_params_set_arg0(params, string_create_from_cstr("mycommand"));

    const string_t *arg0 = positional_params_get_arg0(params);
    CTEST_ASSERT_NOT_NULL(ctest, arg0, "arg0 exists");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(arg0), "mycommand", "arg0 is 'mycommand'");

    positional_params_destroy(&params);
}

CTEST(test_count_returns_parameter_count)
{
    positional_params_t *empty = positional_params_create();
    CTEST_ASSERT_EQ(ctest, 0, positional_params_count(empty), "empty count is 0");
    positional_params_destroy(&empty);

    string_t *args[5];
    for (int i = 0; i < 5; i++)
        args[i] = string_create_from_cstr("param");

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *five = positional_params_create_from_array(arg0, 5, (const string_t **)args);
    string_destroy(&arg0);
    CTEST_ASSERT_EQ(ctest, 5, positional_params_count(five), "five count is 5");
    positional_params_destroy(&five);
}

// ============================================================================
// $@ and $* Support Tests
// ============================================================================

CTEST(test_get_all_returns_list_for_dollar_at)
{
    string_t *args[3];
    args[0] = string_create_from_cstr("alpha");
    args[1] = string_create_from_cstr("beta");
    args[2] = string_create_from_cstr("gamma");

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *params = positional_params_create_from_array(arg0, 3, (const string_t **)args);
    string_destroy(&arg0);

    // This would be used for "$@" expansion
    string_list_t *all = positional_params_get_all(params);
    CTEST_ASSERT_NOT_NULL(ctest, all, "list created");
    CTEST_ASSERT_EQ(ctest, 3, string_list_size(all), "list size is 3");

    const string_t *first = string_list_at(all, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(first), "alpha", "first is 'alpha'");

    const string_t *second = string_list_at(all, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(second), "beta", "second is 'beta'");

    string_list_destroy(&all);
    positional_params_destroy(&params);
}

CTEST(test_get_all_empty_returns_empty_list)
{
    positional_params_t *params = positional_params_create();

    string_list_t *all = positional_params_get_all(params);
    CTEST_ASSERT_NOT_NULL(ctest, all, "list created");
    CTEST_ASSERT_EQ(ctest, 0, string_list_size(all), "list size is 0");

    string_list_destroy(&all);
    positional_params_destroy(&params);
}

CTEST(test_get_all_joined_for_dollar_star)
{
    string_t *args[3];
    args[0] = string_create_from_cstr("one");
    args[1] = string_create_from_cstr("two");
    args[2] = string_create_from_cstr("three");

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *params = positional_params_create_from_array(arg0, 3, (const string_t **)args);
    string_destroy(&arg0);

    // This would be used for "$*" expansion with IFS=' '
    string_t *joined = positional_params_get_all_joined(params, ' ');
    CTEST_ASSERT_NOT_NULL(ctest, joined, "joined string created");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(joined), "one two three", "joined string correct");

    string_destroy(&joined);
    positional_params_destroy(&params);
}

CTEST(test_get_all_joined_with_custom_separator)
{
    string_t *args[3];
    args[0] = string_create_from_cstr("a");
    args[1] = string_create_from_cstr("b");
    args[2] = string_create_from_cstr("c");

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *params = positional_params_create_from_array(arg0, 3, (const string_t **)args);
    string_destroy(&arg0);

    // "$*" with IFS=':'
    string_t *joined = positional_params_get_all_joined(params, ':');
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(joined), "a:b:c", "joined with colon");

    string_destroy(&joined);
    positional_params_destroy(&params);
}

CTEST(test_get_all_joined_empty_returns_empty_string)
{
    positional_params_t *params = positional_params_create();

    string_t *joined = positional_params_get_all_joined(params, ' ');
    CTEST_ASSERT_NOT_NULL(ctest, joined, "joined string created");
    CTEST_ASSERT_EQ(ctest, 0, string_length(joined), "joined string is empty");

    string_destroy(&joined);
    positional_params_destroy(&params);
}

// ============================================================================
// Modification Tests (set and shift builtins)
// ============================================================================

CTEST(test_set_arg0)
{
    positional_params_t *params = positional_params_create();

    positional_params_set_arg0(params, string_create_from_cstr("first"));
    const string_t *arg0 = positional_params_get_arg0(params);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(arg0), "first", "arg0 is 'first'");

    // Replace arg0
    positional_params_set_arg0(params, string_create_from_cstr("second"));
    arg0 = positional_params_get_arg0(params);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(arg0), "second", "arg0 is 'second'");

    positional_params_destroy(&params);
}

CTEST(test_replace_implements_set_builtin)
{
    string_t *args[2];
    args[0] = string_create_from_cstr("old1");
    args[1] = string_create_from_cstr("old2");

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *params = positional_params_create_from_array(arg0, 2, (const string_t **)args);
    string_destroy(&arg0);
    CTEST_ASSERT_EQ(ctest, 2, positional_params_count(params), "count is 2");

    // Simulate: set new1 new2 new3
    string_t **new_args = xcalloc(3, sizeof(string_t *));
    new_args[0] = string_create_from_cstr("new1");
    new_args[1] = string_create_from_cstr("new2");
    new_args[2] = string_create_from_cstr("new3");

    bool result = positional_params_replace(params, new_args, 3);
    CTEST_ASSERT_TRUE(ctest, result, "replace succeeded");
    CTEST_ASSERT_EQ(ctest, 3, positional_params_count(params), "count is 3");

    const string_t *p1 = positional_params_get(params, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(p1), "new1", "param 1 is 'new1'");

    positional_params_destroy(&params);
}

CTEST(test_replace_with_empty_clears_parameters)
{
    string_t *args[2];
    args[0] = string_create_from_cstr("param1");
    args[1] = string_create_from_cstr("param2");

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *params = positional_params_create_from_array(arg0, 2, (const string_t **)args);
    string_destroy(&arg0);

    // Simulate: set --  (clear all parameters)
    bool result = positional_params_replace(params, NULL, 0);
    CTEST_ASSERT_TRUE(ctest, result, "replace succeeded");
    CTEST_ASSERT_EQ(ctest, 0, positional_params_count(params), "count is 0");

    positional_params_destroy(&params);
}

CTEST(test_shift_removes_first_parameter)
{
    string_t *args[3];
    args[0] = string_create_from_cstr("first");
    args[1] = string_create_from_cstr("second");
    args[2] = string_create_from_cstr("third");

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *params = positional_params_create_from_array(arg0, 3, (const string_t **)args);
    string_destroy(&arg0);

    // shift (removes $1, $2 becomes new $1)
    bool result = positional_params_shift(params, 1);
    CTEST_ASSERT_TRUE(ctest, result, "shift succeeded");
    CTEST_ASSERT_EQ(ctest, 2, positional_params_count(params), "count is 2");

    const string_t *p1 = positional_params_get(params, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(p1), "second", "param 1 is 'second'");

    const string_t *p2 = positional_params_get(params, 2);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(p2), "third", "param 2 is 'third'");

    positional_params_destroy(&params);
}

CTEST(test_shift_multiple_parameters)
{
    string_t *args[5];
    for (int i = 0; i < 5; i++)
    {
        char buf[10];
        sprintf(buf, "arg%d", i + 1);
        args[i] = string_create_from_cstr(buf);
    }

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *params = positional_params_create_from_array(arg0, 5, (const string_t **)args);
    string_destroy(&arg0);

    // shift 3
    bool result = positional_params_shift(params, 3);
    CTEST_ASSERT_TRUE(ctest, result, "shift succeeded");
    CTEST_ASSERT_EQ(ctest, 2, positional_params_count(params), "count is 2");

    const string_t *p1 = positional_params_get(params, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(p1), "arg4", "param 1 is 'arg4'");

    positional_params_destroy(&params);
}

CTEST(test_shift_all_parameters)
{
    string_t *args[3];
    args[0] = string_create_from_cstr("a");
    args[1] = string_create_from_cstr("b");
    args[2] = string_create_from_cstr("c");

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *params = positional_params_create_from_array(arg0, 3, (const string_t **)args);
    string_destroy(&arg0);

    bool result = positional_params_shift(params, 3);
    CTEST_ASSERT_TRUE(ctest, result, "shift succeeded");
    CTEST_ASSERT_EQ(ctest, 0, positional_params_count(params), "count is 0");

    positional_params_destroy(&params);
}

CTEST(test_shift_zero_is_noop)
{
    string_t *args[2];
    args[0] = string_create_from_cstr("first");
    args[1] = string_create_from_cstr("second");

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *params = positional_params_create_from_array(arg0, 2, (const string_t **)args);
    string_destroy(&arg0);

    bool result = positional_params_shift(params, 0);
    CTEST_ASSERT_TRUE(ctest, result, "shift succeeded");
    CTEST_ASSERT_EQ(ctest, 2, positional_params_count(params), "count is 2");

    positional_params_destroy(&params);
}

CTEST(test_shift_too_many_returns_false)
{
    string_t *args[2];
    args[0] = string_create_from_cstr("a");
    args[1] = string_create_from_cstr("b");

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *params = positional_params_create_from_array(arg0, 2, (const string_t **)args);
    string_destroy(&arg0);

    // Cannot shift 3 when only 2 exist
    bool result = positional_params_shift(params, 3);
    CTEST_ASSERT_FALSE(ctest, result, "shift failed as expected");
    CTEST_ASSERT_EQ(ctest, 2, positional_params_count(params), "count unchanged"); // Unchanged

    positional_params_destroy(&params);
}

// ============================================================================
// Configuration Tests
// ============================================================================

CTEST(test_get_max_returns_default_limit)
{
    positional_params_t *params = positional_params_create();

    int max = positional_params_get_max(params);
    CTEST_ASSERT_EQ(ctest, POSITIONAL_PARAMS_MAX, max, "max is default");

    positional_params_destroy(&params);
}

CTEST(test_set_max_changes_limit)
{
    positional_params_t *params = positional_params_create();

    positional_params_set_max(params, 100);
    CTEST_ASSERT_EQ(ctest, 100, positional_params_get_max(params), "max is 100");

    positional_params_destroy(&params);
}

CTEST(test_replace_exceeding_max_returns_false)
{
    positional_params_t *params = positional_params_create();
    positional_params_set_max(params, 5);

    // Try to set 10 parameters when max is 5
    string_t **args = xcalloc(10, sizeof(string_t *));
    for (int i = 0; i < 10; i++)
        args[i] = string_create_from_cstr("param");

    bool result = positional_params_replace(params, args, 10);
    CTEST_ASSERT_FALSE(ctest, result, "replace failed as expected");

    // Clean up the rejected parameters
    for (int i = 0; i < 10; i++)
        string_destroy(&args[i]);
    xfree(args);

    positional_params_destroy(&params);
}

// ============================================================================
// Edge Cases
// ============================================================================

CTEST(test_single_parameter)
{
    string_t *args[1];
    args[0] = string_create_from_cstr("only");

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *params = positional_params_create_from_array(arg0, 1, (const string_t **)args);
    string_destroy(&arg0);
    CTEST_ASSERT_EQ(ctest, 1, positional_params_count(params), "count is 1");

    const string_t *p1 = positional_params_get(params, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(p1), "only", "param 1 is 'only'");

    positional_params_destroy(&params);
}

CTEST(test_empty_string_parameter)
{
    string_t *args[2];
    args[0] = string_create_from_cstr("");
    args[1] = string_create_from_cstr("nonempty");

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *params = positional_params_create_from_array(arg0, 2, (const string_t **)args);
    string_destroy(&arg0);

    const string_t *p1 = positional_params_get(params, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(p1), "", "param 1 is empty");

    positional_params_destroy(&params);
}

CTEST(test_whitespace_in_parameters)
{
    string_t *args[2];
    args[0] = string_create_from_cstr("has spaces");
    args[1] = string_create_from_cstr("has\ttabs");

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *params = positional_params_create_from_array(arg0, 2, (const string_t **)args);
    string_destroy(&arg0);

    const string_t *p1 = positional_params_get(params, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(p1), "has spaces", "param 1 has spaces");

    const string_t *p2 = positional_params_get(params, 2);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(p2), "has\ttabs", "param 2 has tabs");

    positional_params_destroy(&params);
}

CTEST(test_special_characters_in_parameters)
{
    string_t *args[3];
    args[0] = string_create_from_cstr("$VAR");
    args[1] = string_create_from_cstr("*.txt");
    args[2] = string_create_from_cstr("foo|bar");

    string_t *arg0 = string_create_from_cstr("mgsh");
    positional_params_t *params = positional_params_create_from_array(arg0, 3, (const string_t **)args);
    string_destroy(&arg0);

    // Parameters should be stored as-is, no expansion
    const string_t *p1 = positional_params_get(params, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(p1), "$VAR", "param 1 is '$VAR'");

    const string_t *p2 = positional_params_get(params, 2);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(p2), "*.txt", "param 2 is '*.txt'");

    positional_params_destroy(&params);
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    arena_start();

    CTestEntry *suite[] = {
        /* Lifecycle */
        CTEST_ENTRY(test_create_empty),
        CTEST_ENTRY(test_create_from_array),
        CTEST_ENTRY(test_create_from_string_list),
        CTEST_ENTRY(test_create_from_argv),
        CTEST_ENTRY(test_clone),
        CTEST_ENTRY(test_destroy_sets_to_null),

        /* Parameter access */
        CTEST_ENTRY(test_get_valid_parameter),
        CTEST_ENTRY(test_get_out_of_range_returns_null),
        CTEST_ENTRY(test_get_arg0),
        CTEST_ENTRY(test_count_returns_parameter_count),

        /* $@ and $* */
        CTEST_ENTRY(test_get_all_returns_list_for_dollar_at),
        CTEST_ENTRY(test_get_all_empty_returns_empty_list),
        CTEST_ENTRY(test_get_all_joined_for_dollar_star),
        CTEST_ENTRY(test_get_all_joined_with_custom_separator),
        CTEST_ENTRY(test_get_all_joined_empty_returns_empty_string),

        /* Modification */
        CTEST_ENTRY(test_set_arg0),
        CTEST_ENTRY(test_replace_implements_set_builtin),
        CTEST_ENTRY(test_replace_with_empty_clears_parameters),
        CTEST_ENTRY(test_shift_removes_first_parameter),
        CTEST_ENTRY(test_shift_multiple_parameters),
        CTEST_ENTRY(test_shift_all_parameters),
        CTEST_ENTRY(test_shift_zero_is_noop),
        CTEST_ENTRY(test_shift_too_many_returns_false),

        /* Configuration */
        CTEST_ENTRY(test_get_max_returns_default_limit),
        CTEST_ENTRY(test_set_max_changes_limit),
        CTEST_ENTRY(test_replace_exceeding_max_returns_false),

        /* Edge cases */
        CTEST_ENTRY(test_single_parameter),
        CTEST_ENTRY(test_empty_string_parameter),
        CTEST_ENTRY(test_whitespace_in_parameters),
        CTEST_ENTRY(test_special_characters_in_parameters),

        NULL
    };

    int result = ctest_run_suite(suite);

    arena_end();

    return result;
}

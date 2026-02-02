/**
 * @file test_func_store_ctest.c
 * @brief Unit tests for function store (func_store.c)
 */

#include <string.h>
#include "ctest.h"
#include "func_store.h"
#include "ast.h"
#include "string_t.h"
#include "xalloc.h"

// ------------------------------------------------------------
// Helper functions for creating test AST nodes
// ------------------------------------------------------------

static ast_node_t *create_test_function_node(const char *name)
{
    // Create a simple function body (brace group with empty command list)
    ast_node_t *body = ast_create_brace_group(ast_create_command_list());

    // Create function definition
    string_t *func_name = string_create_from_cstr(name);
    ast_node_t *func_def = ast_create_function_def(func_name, body);
    string_destroy(&func_name);

    return func_def;
}

// ------------------------------------------------------------
// Creation and Destruction Tests
// ------------------------------------------------------------

CTEST(test_func_store_create)
{
    func_store_t *store = func_store_create();
    CTEST_ASSERT_NOT_NULL(ctest, store, "store created");
    CTEST_ASSERT_NOT_NULL(ctest, store->map, "store map created");
    CTEST_ASSERT_EQ(ctest, func_map_size(store->map), 0, "initial size is 0");
    func_store_destroy(&store);
    CTEST_ASSERT_NULL(ctest, store, "store is null after destroy");
}

CTEST(test_func_store_destroy_null)
{
    func_store_t *store = NULL;
    func_store_destroy(&store); // Should not crash
    CTEST_ASSERT_NULL(ctest, store, "null pointer handled");
}

// ------------------------------------------------------------
// Add Function Tests
// ------------------------------------------------------------

CTEST(test_func_store_add_basic)
{
    func_store_t *store = func_store_create();

    string_t *name = string_create_from_cstr("test_func");
    ast_node_t *func_def = create_test_function_node("test_func");

    func_store_error_t err = func_store_add(store, name, func_def);

    CTEST_ASSERT_EQ(ctest, err, FUNC_STORE_ERROR_NONE, "add succeeded");
    CTEST_ASSERT_EQ(ctest, func_map_size(store->map), 1, "size is 1");
    CTEST_ASSERT_TRUE(ctest, func_store_has_name(store, name), "function exists");

    ast_node_destroy(&func_def);
    string_destroy(&name);
    func_store_destroy(&store);
}

CTEST(test_func_store_add_cstr)
{
    func_store_t *store = func_store_create();

    ast_node_t *func_def = create_test_function_node("my_function");

    func_store_error_t err = func_store_add_cstr(store, "my_function", func_def);

    CTEST_ASSERT_EQ(ctest, err, FUNC_STORE_ERROR_NONE, "add_cstr succeeded");
    CTEST_ASSERT_TRUE(ctest, func_store_has_name_cstr(store, "my_function"), "function exists");

    ast_node_destroy(&func_def);
    func_store_destroy(&store);
}

CTEST(test_func_store_add_multiple)
{
    func_store_t *store = func_store_create();

    ast_node_t *func1 = create_test_function_node("func1");
    ast_node_t *func2 = create_test_function_node("func2");
    ast_node_t *func3 = create_test_function_node("func3");

    func_store_add_cstr(store, "func1", func1);
    func_store_add_cstr(store, "func2", func2);
    func_store_add_cstr(store, "func3", func3);

    CTEST_ASSERT_EQ(ctest, func_map_size(store->map), 3, "size is 3");
    CTEST_ASSERT_TRUE(ctest, func_store_has_name_cstr(store, "func1"), "func1 exists");
    CTEST_ASSERT_TRUE(ctest, func_store_has_name_cstr(store, "func2"), "func2 exists");
    CTEST_ASSERT_TRUE(ctest, func_store_has_name_cstr(store, "func3"), "func3 exists");

    ast_node_destroy(&func1);
    ast_node_destroy(&func2);
    ast_node_destroy(&func3);
    func_store_destroy(&store);
}

CTEST(test_func_store_add_overwrites_existing)
{
    func_store_t *store = func_store_create();

    ast_node_t *func1 = create_test_function_node("duplicate");
    ast_node_t *func2 = create_test_function_node("duplicate");

    func_store_add_cstr(store, "duplicate", func1);
    func_store_add_cstr(store, "duplicate", func2);

    CTEST_ASSERT_EQ(ctest, func_map_size(store->map), 1, "size is still 1 after overwrite");
    CTEST_ASSERT_TRUE(ctest, func_store_has_name_cstr(store, "duplicate"), "function exists");

    // Both original AST nodes should be freed by caller
    ast_node_destroy(&func1);
    ast_node_destroy(&func2);
    func_store_destroy(&store);
}

// ------------------------------------------------------------
// Add Extended Result Tests
// ------------------------------------------------------------

CTEST(test_func_store_add_ex_new_function)
{
    func_store_t *store = func_store_create();

    string_t *name = string_create_from_cstr("new_func");
    ast_node_t *func_def = create_test_function_node("new_func");

    func_store_insert_result_t result = func_store_add_ex(store, name, func_def);

    CTEST_ASSERT_EQ(ctest, result.error, FUNC_STORE_ERROR_NONE, "add succeeded");
    CTEST_ASSERT_TRUE(ctest, result.was_new, "was_new is true for new function");

    ast_node_destroy(&func_def);
    string_destroy(&name);
    func_store_destroy(&store);
}

CTEST(test_func_store_add_ex_replaces_existing)
{
    func_store_t *store = func_store_create();

    string_t *name = string_create_from_cstr("existing");
    ast_node_t *func1 = create_test_function_node("existing");
    ast_node_t *func2 = create_test_function_node("existing");

    func_store_insert_result_t result1 = func_store_add_ex(store, name, func1);
    CTEST_ASSERT_TRUE(ctest, result1.was_new, "first add was_new is true");

    func_store_insert_result_t result2 = func_store_add_ex(store, name, func2);
    CTEST_ASSERT_EQ(ctest, result2.error, FUNC_STORE_ERROR_NONE, "replace succeeded");
    CTEST_ASSERT_FALSE(ctest, result2.was_new, "was_new is false for replacement");

    ast_node_destroy(&func1);
    ast_node_destroy(&func2);
    string_destroy(&name);
    func_store_destroy(&store);
}

// ------------------------------------------------------------
// Name Validation Tests
// ------------------------------------------------------------

CTEST(test_func_store_add_empty_name)
{
    func_store_t *store = func_store_create();

    string_t *empty = string_create();
    ast_node_t *func_def = create_test_function_node("test");

    func_store_error_t err = func_store_add(store, empty, func_def);

    CTEST_ASSERT_EQ(ctest, err, FUNC_STORE_ERROR_EMPTY_NAME, "empty name rejected");

    ast_node_destroy(&func_def);
    string_destroy(&empty);
    func_store_destroy(&store);
}

CTEST(test_func_store_add_null_name)
{
    func_store_t *store = func_store_create();

    ast_node_t *func_def = create_test_function_node("test");

    func_store_error_t err = func_store_add(store, NULL, func_def);

    CTEST_ASSERT_EQ(ctest, err, FUNC_STORE_ERROR_EMPTY_NAME, "null name rejected");

    ast_node_destroy(&func_def);
    func_store_destroy(&store);
}

CTEST(test_func_store_add_invalid_name_space)
{
    func_store_t *store = func_store_create();

    ast_node_t *func_def = create_test_function_node("test");

    func_store_error_t err = func_store_add_cstr(store, "invalid name", func_def);

    CTEST_ASSERT_EQ(ctest, err, FUNC_STORE_ERROR_NAME_INVALID_CHARACTER, "name with space rejected");

    ast_node_destroy(&func_def);
    func_store_destroy(&store);
}

CTEST(test_func_store_add_invalid_name_special_char)
{
    func_store_t *store = func_store_create();

    ast_node_t *func_def = create_test_function_node("test");

    // Test various invalid characters
    CTEST_ASSERT_EQ(ctest, func_store_add_cstr(store, "func$name", func_def),
                    FUNC_STORE_ERROR_NAME_INVALID_CHARACTER, "$ rejected");
    CTEST_ASSERT_EQ(ctest, func_store_add_cstr(store, "func@name", func_def),
                    FUNC_STORE_ERROR_NAME_INVALID_CHARACTER, "@ rejected");
    CTEST_ASSERT_EQ(ctest, func_store_add_cstr(store, "func!name", func_def),
                    FUNC_STORE_ERROR_NAME_INVALID_CHARACTER, "! rejected");
    CTEST_ASSERT_EQ(ctest, func_store_add_cstr(store, "func-name", func_def),
                    FUNC_STORE_ERROR_NAME_INVALID_CHARACTER, "- rejected");

    ast_node_destroy(&func_def);
    func_store_destroy(&store);
}

CTEST(test_func_store_add_valid_names)
{
    func_store_t *store = func_store_create();

    ast_node_t *func1 = create_test_function_node("simple");
    ast_node_t *func2 = create_test_function_node("with_underscore");
    ast_node_t *func3 = create_test_function_node("_leading_underscore");
    ast_node_t *func4 = create_test_function_node("MixedCase");
    ast_node_t *func5 = create_test_function_node("name123");

    CTEST_ASSERT_EQ(ctest, func_store_add_cstr(store, "simple", func1),
                    FUNC_STORE_ERROR_NONE, "simple name accepted");
    CTEST_ASSERT_EQ(ctest, func_store_add_cstr(store, "with_underscore", func2),
                    FUNC_STORE_ERROR_NONE, "underscore accepted");
    CTEST_ASSERT_EQ(ctest, func_store_add_cstr(store, "_leading_underscore", func3),
                    FUNC_STORE_ERROR_NONE, "leading underscore accepted");
    CTEST_ASSERT_EQ(ctest, func_store_add_cstr(store, "MixedCase", func4),
                    FUNC_STORE_ERROR_NONE, "mixed case accepted");
    CTEST_ASSERT_EQ(ctest, func_store_add_cstr(store, "name123", func5),
                    FUNC_STORE_ERROR_NONE, "trailing digits accepted");

    ast_node_destroy(&func1);
    ast_node_destroy(&func2);
    ast_node_destroy(&func3);
    ast_node_destroy(&func4);
    ast_node_destroy(&func5);
    func_store_destroy(&store);
}

// ------------------------------------------------------------
// Remove Function Tests
// ------------------------------------------------------------

CTEST(test_func_store_remove_existing)
{
    func_store_t *store = func_store_create();

    ast_node_t *func_def = create_test_function_node("to_remove");
    func_store_add_cstr(store, "to_remove", func_def);

    string_t *name = string_create_from_cstr("to_remove");
    func_store_error_t err = func_store_remove(store, name);

    CTEST_ASSERT_EQ(ctest, err, FUNC_STORE_ERROR_NONE, "remove succeeded");
    CTEST_ASSERT_FALSE(ctest, func_store_has_name(store, name), "function no longer exists");
    CTEST_ASSERT_EQ(ctest, func_map_size(store->map), 0, "size is 0");

    ast_node_destroy(&func_def);
    string_destroy(&name);
    func_store_destroy(&store);
}

CTEST(test_func_store_remove_cstr)
{
    func_store_t *store = func_store_create();

    ast_node_t *func_def = create_test_function_node("test_remove");
    func_store_add_cstr(store, "test_remove", func_def);

    func_store_error_t err = func_store_remove_cstr(store, "test_remove");

    CTEST_ASSERT_EQ(ctest, err, FUNC_STORE_ERROR_NONE, "remove_cstr succeeded");
    CTEST_ASSERT_FALSE(ctest, func_store_has_name_cstr(store, "test_remove"), "function removed");

    ast_node_destroy(&func_def);
    func_store_destroy(&store);
}

CTEST(test_func_store_remove_nonexistent)
{
    func_store_t *store = func_store_create();

    func_store_error_t err = func_store_remove_cstr(store, "nonexistent");

    CTEST_ASSERT_EQ(ctest, err, FUNC_STORE_ERROR_NOT_FOUND, "remove nonexistent returns NOT_FOUND");

    func_store_destroy(&store);
}

CTEST(test_func_store_remove_one_of_many)
{
    func_store_t *store = func_store_create();

    ast_node_t *func1 = create_test_function_node("func1");
    ast_node_t *func2 = create_test_function_node("func2");
    ast_node_t *func3 = create_test_function_node("func3");

    func_store_add_cstr(store, "func1", func1);
    func_store_add_cstr(store, "func2", func2);
    func_store_add_cstr(store, "func3", func3);

    func_store_remove_cstr(store, "func2");

    CTEST_ASSERT_TRUE(ctest, func_store_has_name_cstr(store, "func1"), "func1 still exists");
    CTEST_ASSERT_FALSE(ctest, func_store_has_name_cstr(store, "func2"), "func2 removed");
    CTEST_ASSERT_TRUE(ctest, func_store_has_name_cstr(store, "func3"), "func3 still exists");
    CTEST_ASSERT_EQ(ctest, func_map_size(store->map), 2, "size is 2");

    ast_node_destroy(&func1);
    ast_node_destroy(&func2);
    ast_node_destroy(&func3);
    func_store_destroy(&store);
}

// ------------------------------------------------------------
// Has Name Tests
// ------------------------------------------------------------

CTEST(test_func_store_has_name)
{
    func_store_t *store = func_store_create();

    ast_node_t *func_def = create_test_function_node("exists");
    func_store_add_cstr(store, "exists", func_def);

    string_t *existing = string_create_from_cstr("exists");
    string_t *missing = string_create_from_cstr("missing");

    CTEST_ASSERT_TRUE(ctest, func_store_has_name(store, existing), "existing function found");
    CTEST_ASSERT_FALSE(ctest, func_store_has_name(store, missing), "missing function not found");

    ast_node_destroy(&func_def);
    string_destroy(&existing);
    string_destroy(&missing);
    func_store_destroy(&store);
}

CTEST(test_func_store_has_name_cstr)
{
    func_store_t *store = func_store_create();

    ast_node_t *func_def = create_test_function_node("my_func");
    func_store_add_cstr(store, "my_func", func_def);

    CTEST_ASSERT_TRUE(ctest, func_store_has_name_cstr(store, "my_func"), "function found");
    CTEST_ASSERT_FALSE(ctest, func_store_has_name_cstr(store, "other_func"), "other function not found");

    ast_node_destroy(&func_def);
    func_store_destroy(&store);
}

// ------------------------------------------------------------
// Get Definition Tests
// ------------------------------------------------------------

CTEST(test_func_store_get_def)
{
    func_store_t *store = func_store_create();

    ast_node_t *func_def = create_test_function_node("get_test");
    func_store_add_cstr(store, "get_test", func_def);

    string_t *name = string_create_from_cstr("get_test");
    const ast_node_t *retrieved = func_store_get_def(store, name);

    CTEST_ASSERT_NOT_NULL(ctest, retrieved, "function definition retrieved");
    CTEST_ASSERT_EQ(ctest, ast_node_get_type(retrieved), AST_FUNCTION_DEF, "correct node type");

    ast_node_destroy(&func_def);
    string_destroy(&name);
    func_store_destroy(&store);
}

CTEST(test_func_store_get_def_cstr)
{
    func_store_t *store = func_store_create();

    ast_node_t *func_def = create_test_function_node("cstr_test");
    func_store_add_cstr(store, "cstr_test", func_def);

    const ast_node_t *retrieved = func_store_get_def_cstr(store, "cstr_test");

    CTEST_ASSERT_NOT_NULL(ctest, retrieved, "function definition retrieved");
    CTEST_ASSERT_EQ(ctest, ast_node_get_type(retrieved), AST_FUNCTION_DEF, "correct node type");

    ast_node_destroy(&func_def);
    func_store_destroy(&store);
}

CTEST(test_func_store_get_def_nonexistent)
{
    func_store_t *store = func_store_create();

    const ast_node_t *retrieved = func_store_get_def_cstr(store, "nonexistent");

    CTEST_ASSERT_NULL(ctest, retrieved, "nonexistent function returns NULL");

    func_store_destroy(&store);
}

// ------------------------------------------------------------
// Clear Tests
// ------------------------------------------------------------

CTEST(test_func_store_clear)
{
    func_store_t *store = func_store_create();

    ast_node_t *func1 = create_test_function_node("func1");
    ast_node_t *func2 = create_test_function_node("func2");
    ast_node_t *func3 = create_test_function_node("func3");

    func_store_add_cstr(store, "func1", func1);
    func_store_add_cstr(store, "func2", func2);
    func_store_add_cstr(store, "func3", func3);

    CTEST_ASSERT_EQ(ctest, func_map_size(store->map), 3, "size is 3 before clear");

    func_store_clear(store);

    CTEST_ASSERT_EQ(ctest, func_map_size(store->map), 0, "size is 0 after clear");
    CTEST_ASSERT_FALSE(ctest, func_store_has_name_cstr(store, "func1"), "func1 removed");
    CTEST_ASSERT_FALSE(ctest, func_store_has_name_cstr(store, "func2"), "func2 removed");
    CTEST_ASSERT_FALSE(ctest, func_store_has_name_cstr(store, "func3"), "func3 removed");

    ast_node_destroy(&func1);
    ast_node_destroy(&func2);
    ast_node_destroy(&func3);
    func_store_destroy(&store);
}

// ------------------------------------------------------------
// Clone Tests
// ------------------------------------------------------------

CTEST(test_func_store_clone_empty)
{
    func_store_t *store = func_store_create();
    func_store_t *clone = func_store_clone(store);

    CTEST_ASSERT_NOT_NULL(ctest, clone, "clone created");
    CTEST_ASSERT_EQ(ctest, func_map_size(clone->map), 0, "clone size is 0");

    func_store_destroy(&store);
    func_store_destroy(&clone);
}

CTEST(test_func_store_clone_with_functions)
{
    func_store_t *store = func_store_create();

    ast_node_t *func1 = create_test_function_node("clone1");
    ast_node_t *func2 = create_test_function_node("clone2");

    func_store_add_cstr(store, "clone1", func1);
    func_store_add_cstr(store, "clone2", func2);

    func_store_t *clone = func_store_clone(store);

    CTEST_ASSERT_NOT_NULL(ctest, clone, "clone created");
    CTEST_ASSERT_EQ(ctest, func_map_size(clone->map), 2, "clone has same size");
    CTEST_ASSERT_TRUE(ctest, func_store_has_name_cstr(clone, "clone1"), "clone has clone1");
    CTEST_ASSERT_TRUE(ctest, func_store_has_name_cstr(clone, "clone2"), "clone has clone2");

    ast_node_destroy(&func1);
    ast_node_destroy(&func2);
    func_store_destroy(&store);
    func_store_destroy(&clone);
}

CTEST(test_func_store_clone_is_independent)
{
    func_store_t *store = func_store_create();

    ast_node_t *func1 = create_test_function_node("original");
    func_store_add_cstr(store, "original", func1);

    func_store_t *clone = func_store_clone(store);

    // Modify original
    ast_node_t *func2 = create_test_function_node("added");
    func_store_add_cstr(store, "added", func2);
    func_store_remove_cstr(store, "original");

    // Clone should be unaffected
    CTEST_ASSERT_EQ(ctest, func_map_size(clone->map), 1, "clone size unchanged");
    CTEST_ASSERT_TRUE(ctest, func_store_has_name_cstr(clone, "original"), "clone still has original");
    CTEST_ASSERT_FALSE(ctest, func_store_has_name_cstr(clone, "added"), "clone doesn't have added");

    // Modify clone
    ast_node_t *func3 = create_test_function_node("clone_only");
    func_store_add_cstr(clone, "clone_only", func3);

    // Original should be unaffected
    CTEST_ASSERT_FALSE(ctest, func_store_has_name_cstr(store, "clone_only"), "original doesn't have clone_only");

    ast_node_destroy(&func1);
    ast_node_destroy(&func2);
    ast_node_destroy(&func3);
    func_store_destroy(&store);
    func_store_destroy(&clone);
}

// ------------------------------------------------------------
// Foreach Tests
// ------------------------------------------------------------

typedef struct foreach_test_context_t
{
    int count;
    bool found_func1;
    bool found_func2;
    bool found_func3;
} foreach_test_context_t;

static void foreach_callback(const string_t *name, const ast_node_t *func, void *user_data)
{
    foreach_test_context_t *ctx = (foreach_test_context_t *)user_data;
    ctx->count++;

    if (string_compare_cstr(name, "func1") == 0)
        ctx->found_func1 = true;
    if (string_compare_cstr(name, "func2") == 0)
        ctx->found_func2 = true;
    if (string_compare_cstr(name, "func3") == 0)
        ctx->found_func3 = true;

    // Verify we receive valid AST nodes
    if (func && ast_node_get_type(func) != AST_FUNCTION_DEF)
    {
        ctx->count = -1; // Signal error
    }
}

CTEST(test_func_store_foreach)
{
    func_store_t *store = func_store_create();

    ast_node_t *func1 = create_test_function_node("func1");
    ast_node_t *func2 = create_test_function_node("func2");
    ast_node_t *func3 = create_test_function_node("func3");

    func_store_add_cstr(store, "func1", func1);
    func_store_add_cstr(store, "func2", func2);
    func_store_add_cstr(store, "func3", func3);

    foreach_test_context_t ctx = {0};
    func_store_foreach(store, foreach_callback, &ctx);

    CTEST_ASSERT_EQ(ctest, ctx.count, 3, "foreach called 3 times");
    CTEST_ASSERT_TRUE(ctest, ctx.found_func1, "func1 found");
    CTEST_ASSERT_TRUE(ctest, ctx.found_func2, "func2 found");
    CTEST_ASSERT_TRUE(ctest, ctx.found_func3, "func3 found");

    ast_node_destroy(&func1);
    ast_node_destroy(&func2);
    ast_node_destroy(&func3);
    func_store_destroy(&store);
}

CTEST(test_func_store_foreach_empty)
{
    func_store_t *store = func_store_create();

    foreach_test_context_t ctx = {0};
    func_store_foreach(store, foreach_callback, &ctx);

    CTEST_ASSERT_EQ(ctest, ctx.count, 0, "foreach not called on empty store");

    func_store_destroy(&store);
}

// ------------------------------------------------------------
// Ownership and Memory Tests
// ------------------------------------------------------------

CTEST(test_func_store_clones_ast_nodes)
{
    func_store_t *store = func_store_create();

    ast_node_t *original = create_test_function_node("ownership_test");
    func_store_add_cstr(store, "ownership_test", original);

    // We should be able to destroy the original
    ast_node_destroy(&original);

    // Store should still have a valid copy
    const ast_node_t *retrieved = func_store_get_def_cstr(store, "ownership_test");
    CTEST_ASSERT_NOT_NULL(ctest, retrieved, "function still accessible after original destroyed");
    CTEST_ASSERT_EQ(ctest, ast_node_get_type(retrieved), AST_FUNCTION_DEF, "node is still valid");

    func_store_destroy(&store);
}

// ------------------------------------------------------------
// Edge Cases and Error Handling
// ------------------------------------------------------------

CTEST(test_func_store_null_store_handling)
{
    func_store_t *null_store = NULL;
    ast_node_t *func_def = create_test_function_node("test");

    // All operations should handle NULL store gracefully
    CTEST_ASSERT_EQ(ctest, func_store_add(null_store, NULL, func_def),
                    FUNC_STORE_ERROR_STORAGE_FAILURE, "add with null store");
    CTEST_ASSERT_EQ(ctest, func_store_remove(null_store, NULL),
                    FUNC_STORE_ERROR_STORAGE_FAILURE, "remove with null store");
    CTEST_ASSERT_FALSE(ctest, func_store_has_name_cstr(null_store, "test"),
                       "has_name_cstr with null store");
    CTEST_ASSERT_NULL(ctest, func_store_get_def_cstr(null_store, "test"),
                      "get_def_cstr with null store");

    ast_node_destroy(&func_def);
}

CTEST(test_func_store_add_null_ast_node)
{
    func_store_t *store = func_store_create();

    func_store_error_t err = func_store_add_cstr(store, "test", NULL);

    CTEST_ASSERT_EQ(ctest, err, FUNC_STORE_ERROR_STORAGE_FAILURE, "null AST rejected");

    func_store_destroy(&store);
}

CTEST(test_func_store_large_number_of_functions)
{
    func_store_t *store = func_store_create();

    // Add 50 functions to test hash table growth
    for (int i = 0; i < 50; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "func_%d", i);
        ast_node_t *func_def = create_test_function_node(name);
        func_store_error_t err = func_store_add_cstr(store, name, func_def);
        CTEST_ASSERT_EQ(ctest, err, FUNC_STORE_ERROR_NONE, "add succeeded");
        ast_node_destroy(&func_def);
    }

    CTEST_ASSERT_EQ(ctest, func_map_size(store->map), 50, "all functions added");

    // Verify all functions are accessible
    for (int i = 0; i < 50; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "func_%d", i);
        CTEST_ASSERT_TRUE(ctest, func_store_has_name_cstr(store, name), "function accessible");
    }

    func_store_destroy(&store);
}

// ------------------------------------------------------------
// Test suite entry
// ------------------------------------------------------------

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    arena_start();

    CTestEntry *suite[] = {
        // Creation and destruction
        CTEST_ENTRY(test_func_store_create),
        CTEST_ENTRY(test_func_store_destroy_null),

        // Add function tests
        CTEST_ENTRY(test_func_store_add_basic),
        CTEST_ENTRY(test_func_store_add_cstr),
        CTEST_ENTRY(test_func_store_add_multiple),
        CTEST_ENTRY(test_func_store_add_overwrites_existing),

        // Add extended result tests
        CTEST_ENTRY(test_func_store_add_ex_new_function),
        CTEST_ENTRY(test_func_store_add_ex_replaces_existing),

        // Name validation tests
        CTEST_ENTRY(test_func_store_add_empty_name),
        CTEST_ENTRY(test_func_store_add_null_name),
        CTEST_ENTRY(test_func_store_add_invalid_name_space),
        CTEST_ENTRY(test_func_store_add_invalid_name_special_char),
        CTEST_ENTRY(test_func_store_add_valid_names),

        // Remove function tests
        CTEST_ENTRY(test_func_store_remove_existing),
        CTEST_ENTRY(test_func_store_remove_cstr),
        CTEST_ENTRY(test_func_store_remove_nonexistent),
        CTEST_ENTRY(test_func_store_remove_one_of_many),

        // Has name tests
        CTEST_ENTRY(test_func_store_has_name),
        CTEST_ENTRY(test_func_store_has_name_cstr),

        // Get definition tests
        CTEST_ENTRY(test_func_store_get_def),
        CTEST_ENTRY(test_func_store_get_def_cstr),
        CTEST_ENTRY(test_func_store_get_def_nonexistent),

        // Clear tests
        CTEST_ENTRY(test_func_store_clear),

        // Clone tests
        CTEST_ENTRY(test_func_store_clone_empty),
        CTEST_ENTRY(test_func_store_clone_with_functions),
        CTEST_ENTRY(test_func_store_clone_is_independent),

        // Foreach tests
        CTEST_ENTRY(test_func_store_foreach),
        CTEST_ENTRY(test_func_store_foreach_empty),

        // Ownership and memory tests
        CTEST_ENTRY(test_func_store_clones_ast_nodes),

        // Edge cases and error handling
        CTEST_ENTRY(test_func_store_null_store_handling),
        CTEST_ENTRY(test_func_store_add_null_ast_node),
        CTEST_ENTRY(test_func_store_large_number_of_functions),

        NULL
    };

    int result = ctest_run_suite(suite);

    arena_end();
    return result;
}

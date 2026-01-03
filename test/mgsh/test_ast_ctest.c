#include "ctest.h"
#include "ast.h"
#include "string_t.h"
#include "token.h"
#include "xalloc.h"
#include "logging.h"

/* ============================================================================
 * AST Node Creation Tests
 * ============================================================================ */

CTEST(test_ast_node_create)
{
    ast_node_t *node = ast_node_create(AST_SIMPLE_COMMAND);
    CTEST_ASSERT_NOT_NULL(ctest, node, "AST node created");
    CTEST_ASSERT_EQ(ctest, ast_node_get_type(node), AST_SIMPLE_COMMAND, "node type is correct");
    ast_node_destroy(&node);
    (void)ctest;
}

CTEST(test_ast_simple_command_create)
{
    token_list_t *words = token_list_create();
    ast_node_t *node = ast_create_simple_command(words, NULL, NULL);
    CTEST_ASSERT_NOT_NULL(ctest, node, "simple command created");
    CTEST_ASSERT_EQ(ctest, ast_node_get_type(node), AST_SIMPLE_COMMAND, "node type is correct");
    ast_node_destroy(&node);
    (void)ctest;
}

CTEST(test_ast_pipeline_create)
{
    ast_node_list_t *commands = ast_node_list_create();
    ast_node_t *node = ast_create_pipeline(commands, false);
    CTEST_ASSERT_NOT_NULL(ctest, node, "pipeline created");
    CTEST_ASSERT_EQ(ctest, ast_node_get_type(node), AST_PIPELINE, "node type is correct");
    ast_node_destroy(&node);
    (void)ctest;
}

CTEST(test_ast_if_clause_create)
{
    ast_node_t *condition = ast_create_command_list();
    ast_node_t *then_body = ast_create_command_list();
    ast_node_t *node = ast_create_if_clause(condition, then_body);
    CTEST_ASSERT_NOT_NULL(ctest, node, "if clause created");
    CTEST_ASSERT_EQ(ctest, ast_node_get_type(node), AST_IF_CLAUSE, "node type is correct");
    ast_node_destroy(&node);
    (void)ctest;
}

/* ============================================================================
 * AST Node List Tests
 * ============================================================================ */

CTEST(test_ast_node_list_create)
{
    ast_node_list_t *list = ast_node_list_create();
    CTEST_ASSERT_NOT_NULL(ctest, list, "node list created");
    CTEST_ASSERT_EQ(ctest, ast_node_list_size(list), 0, "list is initially empty");
    ast_node_list_destroy(&list);
    (void)ctest;
}

CTEST(test_ast_node_list_append)
{
    ast_node_list_t *list = ast_node_list_create();
    ast_node_t *node1 = ast_node_create(AST_SIMPLE_COMMAND);
    ast_node_t *node2 = ast_node_create(AST_PIPELINE);

    ast_node_list_append(list, node1);
    ast_node_list_append(list, node2);

    CTEST_ASSERT_EQ(ctest, ast_node_list_size(list), 2, "list has 2 nodes");
    CTEST_ASSERT_EQ(ctest, ast_node_get_type(ast_node_list_get(list, 0)), AST_SIMPLE_COMMAND, "first node type");
    CTEST_ASSERT_EQ(ctest, ast_node_get_type(ast_node_list_get(list, 1)), AST_PIPELINE, "second node type");

    ast_node_list_destroy(&list);
    (void)ctest;
}

/* ============================================================================
 * AST Utility Tests
 * ============================================================================ */

CTEST(test_ast_node_type_to_string)
{
    const char *str = ast_node_type_to_string(AST_SIMPLE_COMMAND);
    CTEST_ASSERT_NOT_NULL(ctest, str, "type to string works");
    CTEST_ASSERT_STR_EQ(ctest, str, "SIMPLE_COMMAND", "correct string");
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
        // AST Node Creation Tests
        CTEST_ENTRY(test_ast_node_create),
        CTEST_ENTRY(test_ast_simple_command_create),
        CTEST_ENTRY(test_ast_pipeline_create),
        CTEST_ENTRY(test_ast_if_clause_create),

        // AST Node List Tests
        CTEST_ENTRY(test_ast_node_list_create),
        CTEST_ENTRY(test_ast_node_list_append),

        // AST Utility Tests
        CTEST_ENTRY(test_ast_node_type_to_string),

        NULL
    };

    int result = ctest_run_suite(suite);

    arena_end();

    return result;
}

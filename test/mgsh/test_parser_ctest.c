#include <stdio.h>

#include "ctest.h"
#include "parser.h"

#include "ast.h"
#include "exec.h"
#include "gnode.h"
#include "logging.h"
#include "lower.h"
#include "string_t.h"
#include "token.h"
#include "xalloc.h"

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

// Helper to parse a string into an AST
static ast_node_t* parse_string(const char* input)
{
    gnode_t* top_node = parser_parse_string(input);

    if (top_node == NULL)
    {
        printf("Failed to parse: %s\n", input);
        return NULL;
    }

    ast_t* ast = ast_lower(top_node);

    g_node_destroy(&top_node);
    return ast;
}


/**
 * Helper to get the first command from an AST.
 * Returns NULL if the structure doesn't match expectations.
 */
static ast_node_t* get_first_command(CTest* ctest, ast_node_t* ast)
{
    ast_node_t* cmd_list = ast;
    if (!cmd_list) return NULL;

    /* Verify this is actually a command list before accessing the union */
    if (cmd_list->type != AST_COMMAND_LIST) {
        CTEST_ASSERT_EQ(ctest, cmd_list->type, AST_COMMAND_LIST, "ast is command list");
        return NULL;
    }

    CTEST_ASSERT_NOT_NULL(ctest, cmd_list->data.command_list.items, "command list has items");
    if (!cmd_list->data.command_list.items) return NULL;

    CTEST_ASSERT_GT(ctest, cmd_list->data.command_list.items->size, 0, "command list is not empty");
    if (cmd_list->data.command_list.items->size == 0) return NULL;

    ast_node_t* first = cmd_list->data.command_list.items->nodes[0];
    CTEST_ASSERT_NOT_NULL(ctest, first, "first command exists");

    return first;
}

/* ============================================================================
 * Parser Tests - Simple Commands
 * ============================================================================ */

CTEST(test_parser_create_destroy)
{
    parser_t* parser = parser_create();
    CTEST_ASSERT_NOT_NULL(ctest, parser, "parser created");
    parser_destroy(&parser);
    (void)ctest;
}

CTEST(test_parser_simple_command)
{
    ast_node_t* ast = parse_string("echo hello");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_SIMPLE_COMMAND, "first item is simple command");
    CTEST_ASSERT_EQ(ctest, token_list_size(first->data.simple_command.words), 2, "two words");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_simple_command_with_args)
{
    ast_node_t* ast = parse_string("ls -la /tmp");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_SIMPLE_COMMAND, "is simple command");
    CTEST_ASSERT_EQ(ctest, token_list_size(first->data.simple_command.words), 3, "three words");

    ast_node_destroy(&ast);
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - Pipelines
 * ============================================================================ */

CTEST(test_parser_pipeline)
{
    ast_node_t* ast = parse_string("ls | grep test");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_PIPELINE, "is pipeline");
    CTEST_ASSERT_EQ(ctest, first->data.pipeline.commands->size, 2, "two commands in pipeline");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_pipeline_negated)
{
    ast_node_t* ast = parse_string("! grep test file");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_PIPELINE, "is pipeline");
    CTEST_ASSERT(ctest, first->data.pipeline.is_negated, "pipeline is negated");

    ast_node_destroy(&ast);
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - And/Or Lists
 * ============================================================================ */

CTEST(test_parser_and_list)
{
    ast_node_t* ast = parse_string("true && echo success");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_AND_OR_LIST, "is and/or list");
    CTEST_ASSERT_EQ(ctest, first->data.andor_list.op, ANDOR_OP_AND, "operator is AND");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_or_list)
{
    ast_node_t* ast = parse_string("false || echo fallback");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_AND_OR_LIST, "is and/or list");
    CTEST_ASSERT_EQ(ctest, first->data.andor_list.op, ANDOR_OP_OR, "operator is OR");

    ast_node_destroy(&ast);
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - Command Lists
 * ============================================================================ */

CTEST(test_parser_sequential_commands)
{
    ast_node_t* ast = parse_string("echo one; echo two");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* cmd_list = ast;
    if (!cmd_list) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, cmd_list->data.command_list.items->size, 2, "two commands");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_background_command)
{
    ast_node_t* ast = parse_string("sleep 10 &");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* cmd_list = ast;
    if (!cmd_list) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT(ctest, ast_node_command_list_has_separators(cmd_list), "has separator");
    CTEST_ASSERT_EQ(ctest, ast_node_command_list_get_separator(cmd_list, 0), CMD_EXEC_BACKGROUND,
        "separator is background");

    ast_node_destroy(&ast);
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - If Clauses
 * ============================================================================ */

CTEST(test_parser_if_then_fi)
{
    ast_node_t* ast = parse_string("if true\nthen echo yes\nfi");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_IF_CLAUSE, "is if clause");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.if_clause.condition, "has condition");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.if_clause.then_body, "has then body");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_if_else)
{
    ast_node_t* ast = parse_string("if false\nthen echo yes\nelse echo no\nfi");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_IF_CLAUSE, "is if clause");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.if_clause.else_body, "has else body");

    ast_node_destroy(&ast);
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - While/Until Loops
 * ============================================================================ */

CTEST(test_parser_while_loop)
{
    ast_node_t* ast = parse_string("while true\ndo echo loop\ndone");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_WHILE_CLAUSE, "is while clause");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.loop_clause.condition, "has condition");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.loop_clause.body, "has body");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_until_loop)
{
    ast_node_t* ast = parse_string("until false\ndo echo loop\ndone");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_UNTIL_CLAUSE, "is until clause");

    ast_node_destroy(&ast);
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - For Loops
 * ============================================================================ */

CTEST(test_parser_for_loop)
{
    ast_node_t* ast = parse_string("for x in a b c\ndo echo $x\ndone");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_FOR_CLAUSE, "is for clause");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.for_clause.variable, "has variable");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.for_clause.body, "has body");

    ast_node_destroy(&ast);
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - Case Statements
 * ============================================================================ */

 // Re-enabled now that parser and ownership semantics are stable
CTEST(test_parser_case_statement)
{
    ast_node_t* ast = parse_string("case $x in\na ) echo a;;\nb ) echo b;;\nesac");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_CASE_CLAUSE, "is case clause");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.case_clause.word, "has word to match");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.case_clause.case_items, "has case items");
    CTEST_ASSERT(ctest, first->data.case_clause.case_items->size >= 2, "has at least 2 case items");

    ast_node_destroy(&ast);
    (void)ctest;
}

// Small case-specific test: optional leading '(' before pattern list
CTEST(test_parser_case_leading_paren)
{
    ast_node_t* ast = parse_string("case x in\n(a) echo a;;\n esac");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_CASE_CLAUSE, "is case clause");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.case_clause.case_items, "has case items");
    CTEST_ASSERT_EQ(ctest, first->data.case_clause.case_items->size, 1, "one case item");

    ast_node_t* item = first->data.case_clause.case_items->nodes[0];
    CTEST_ASSERT_NOT_NULL(ctest, item->data.case_item.patterns, "item has patterns");
    CTEST_ASSERT_EQ(ctest, token_list_size(item->data.case_item.patterns), 1, "one pattern");
    CTEST_ASSERT_NOT_NULL(ctest, item->data.case_item.body, "item has body");

    ast_node_destroy(&ast);
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - Function Definitions
 * ============================================================================ */

CTEST(test_parser_function_def)
{
    ast_node_t* ast = parse_string("myfunc() {\necho hello\n}");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_FUNCTION_DEF, "is function definition");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.function_def.name, "has function name");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.function_def.body, "has function body");
    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first->data.function_def.body), AST_BRACE_GROUP, "body is brace group");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_function_def_with_subshell)
{
    ast_node_t* ast = parse_string("myfunc() (echo hello)");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_FUNCTION_DEF, "is function definition");
    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first->data.function_def.body), AST_SUBSHELL, "body is subshell");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_function_def_with_redirections)
{
    ast_node_t* ast = parse_string("myfunc() { echo hello; } > output.txt");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_FUNCTION_DEF, "is function definition");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.function_def.redirections, "has redirections");
    if (first->data.function_def.redirections == NULL)
        return;
    CTEST_ASSERT_EQ(ctest, first->data.function_def.redirections->size, 1, "has one redirection");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_function_def_empty_body)
{
    ast_node_t* ast = parse_string("myfunc() { }");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_FUNCTION_DEF, "is function definition");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.function_def.body, "has function body");
    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first->data.function_def.body), AST_BRACE_GROUP, "body is brace group");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_function_def_missing_rbrace)
{
    ast_node_t* ast = parse_string("myfunc() { echo hello");
    CTEST_ASSERT_NULL(ctest, ast, "parsing failed for missing }");
    (void)ctest;
}

CTEST(test_parser_function_def_missing_lbrace)
{
    ast_node_t* ast = parse_string("myfunc() echo hello }");
    CTEST_ASSERT_NULL(ctest, ast, "parsing failed for missing {");
    (void)ctest;
}

CTEST(test_parser_function_def_reserved_word_name)
{
    ast_node_t* ast = parse_string("if() { echo hello }");
    CTEST_ASSERT_NULL(ctest, ast, "parsing failed for reserved word as function name");
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - Subshells and Brace Groups
 * ============================================================================ */

CTEST(test_parser_subshell)
{
    ast_node_t* ast = parse_string("(echo hello)");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_SUBSHELL, "is subshell");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.compound.body, "has body");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_brace_group)
{
    ast_node_t* ast = parse_string("{ echo hello; }");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_BRACE_GROUP, "is brace group");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.compound.body, "has body");

    ast_node_destroy(&ast);
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - Redirections
 * ============================================================================ */

CTEST(test_parser_output_redirection)
{
    ast_node_t* ast = parse_string("echo hello > file.txt");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_SIMPLE_COMMAND, "is simple command");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.simple_command.redirections, "has redirections");
    CTEST_ASSERT(ctest, first->data.simple_command.redirections->size > 0, "has at least one redirection");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_input_redirection)
{
    ast_node_t* ast = parse_string("cat < input.txt");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_SIMPLE_COMMAND, "is simple command");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.simple_command.redirections, "has redirections");
    CTEST_ASSERT(ctest, first->data.simple_command.redirections->size > 0, "has at least one redirection");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_append_redirection)
{
    ast_node_t* ast = parse_string("echo hello >> file.txt");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_SIMPLE_COMMAND, "is simple command");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.simple_command.redirections, "has redirections");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_braced_io_number_redirection)
{
    ast_node_t* ast = parse_string("{2}>out.txt");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_SIMPLE_COMMAND, "is simple command");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.simple_command.redirections, "has redirections");
    CTEST_ASSERT_EQ(ctest, first->data.simple_command.redirections->size, 1, "one redirection");

    ast_node_t* redir = first->data.simple_command.redirections->nodes[0];
    CTEST_ASSERT_EQ(ctest, redir->data.redirection.io_number, 2, "io number parsed");
    CTEST_ASSERT_NOT_NULL(ctest, redir->data.redirection.fd_string, "io location stored");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(redir->data.redirection.fd_string), "2", "io location inner text");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_braced_io_name_redirection)
{
    ast_node_t* ast = parse_string("{fd}>out.txt");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    if (!ast) return;

    ast_node_t* first = get_first_command(ctest, ast);
    if (!first) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_SIMPLE_COMMAND, "is simple command");
    CTEST_ASSERT_NOT_NULL(ctest, first->data.simple_command.redirections, "has redirections");
    CTEST_ASSERT_EQ(ctest, first->data.simple_command.redirections->size, 1, "one redirection");

    ast_node_t* redir = first->data.simple_command.redirections->nodes[0];
    CTEST_ASSERT_EQ(ctest, redir->data.redirection.io_number, -1, "io number defaults when name used");
    CTEST_ASSERT_NOT_NULL(ctest, redir->data.redirection.fd_string, "io location stored");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(redir->data.redirection.fd_string), "fd", "io location inner text");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_braced_io_invalid_redirection)
{
    ast_node_t* ast = parse_string("{2x}>out.txt");
    CTEST_ASSERT_NULL(ctest, ast, "parsing failed for invalid IO location");
    (void)ctest;
}

/* ============================================================================
 * Executor Tests
 * ============================================================================ */

CTEST(test_exec_create_destroy)
{
    exec_cfg_t cfg = { 0 };
    exec_t *executor = exec_create(&cfg);
    CTEST_ASSERT_NOT_NULL(ctest, executor, "executor created");
    CTEST_ASSERT_EQ(ctest, exec_get_exit_status(executor), 0, "initial exit status is 0");
    exec_destroy(&executor);
    (void)ctest;
}

/* ============================================================================
 * Visitor Pattern Tests
 * ============================================================================ */

static bool count_visitor(const ast_node_t* node, void* user_data)
{
    (void)node;
    int* count = (int*)user_data;
    (*count)++;
    return true;
}

CTEST(test_ast_traverse)
{
    ast_node_t* ast = parse_string("echo one; echo two");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");

    if (ast != NULL)
    {
        int count = 0;
        bool result = ast_traverse(ast, count_visitor, &count);

        CTEST_ASSERT(ctest, result, "traversal completed");
        CTEST_ASSERT(ctest, count > 0, "visited at least one node");

        ast_node_destroy(&ast);
    }
    (void)ctest;
}

/* ============================================================================
 * AST Utility Tests with Parser
 * ============================================================================ */

CTEST(test_ast_to_string)
{
    ast_node_t* ast = parse_string("echo hello");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");

    if (ast != NULL)
    {
        string_t* str = ast_node_to_string(ast);
        CTEST_ASSERT_NOT_NULL(ctest, str, "to_string works");
        CTEST_ASSERT(ctest, string_length(str) > 0, "string is not empty");

        string_destroy(&str);
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

/* ============================================================================
 * Advanced Parser Tests
 * ============================================================================ */

CTEST(test_parser_assignment_only)
{
    ast_node_t* ast = parse_string("VAR=value");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "assignment-only command parsed");
    if (!ast) return;

    ast_node_t* cmd_list = ast;
    if (!cmd_list) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_list_size(cmd_list->data.command_list.items), 1, "one command");

    ast_node_t* cmd = ast_node_list_get(cmd_list->data.command_list.items, 0);
    CTEST_ASSERT_EQ(ctest, cmd->type, AST_SIMPLE_COMMAND, "simple command");
    CTEST_ASSERT_EQ(ctest, token_list_size(cmd->data.simple_command.words), 0, "no words");
    CTEST_ASSERT_EQ(ctest, token_list_size(cmd->data.simple_command.assignments), 1, "one assignment");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_redirection_only)
{
    ast_node_t* ast = parse_string(">output.txt");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "redirection-only command parsed");
    if (!ast) return;

    ast_node_t* cmd_list = ast;
    if (!cmd_list) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_list_size(cmd_list->data.command_list.items), 1, "one command");

    ast_node_t* cmd = ast_node_list_get(cmd_list->data.command_list.items, 0);
    CTEST_ASSERT_EQ(ctest, cmd->type, AST_SIMPLE_COMMAND, "simple command");
    CTEST_ASSERT_EQ(ctest, token_list_size(cmd->data.simple_command.words), 0, "no words");
    CTEST_ASSERT_EQ(ctest, ast_node_list_size(cmd->data.simple_command.redirections), 1, "one redirection");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_command_with_assignment)
{
    ast_node_t* ast = parse_string("VAR=1 echo $VAR");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "command with assignment parsed");
    if (!ast) return;

    ast_node_t* cmd_list = ast;
    if (!cmd_list) { ast_node_destroy(&ast); return; }

    CTEST_ASSERT_EQ(ctest, ast_node_list_size(cmd_list->data.command_list.items), 1, "one command");

    ast_node_t* cmd = ast_node_list_get(cmd_list->data.command_list.items, 0);
    CTEST_ASSERT_EQ(ctest, cmd->type, AST_SIMPLE_COMMAND, "simple command");
    CTEST_ASSERT_EQ(ctest, token_list_size(cmd->data.simple_command.words), 2, "two words");
    CTEST_ASSERT_EQ(ctest, token_list_size(cmd->data.simple_command.assignments), 1, "one assignment");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_nested_if)
{
    const char* input = "if true; then\n"
        "  if false; then\n"
        "    echo no\n"
        "  else\n"
        "    echo yes\n"
        "  fi\n"
        "fi";
    ast_node_t* ast = parse_string(input);
    CTEST_ASSERT_NOT_NULL(ctest, ast, "nested if parsed");
    if (!ast) return;

    ast_node_t* cmd_list = ast;
    if (!cmd_list) { ast_node_destroy(&ast); return; }

    ast_node_t* outer_if = ast_node_list_get(cmd_list->data.command_list.items, 0);
    CTEST_ASSERT_EQ(ctest, outer_if->type, AST_IF_CLAUSE, "outer if clause");
    if (outer_if->type != AST_IF_CLAUSE)
        return;

    // Check that then_body contains another if
    ast_node_t* then_body = outer_if->data.if_clause.then_body;
    CTEST_ASSERT_NOT_NULL(ctest, then_body, "then body exists");
    CTEST_ASSERT_EQ(ctest, then_body->type, AST_COMMAND_LIST, "then body is command list");

    ast_node_t* inner_if = ast_node_list_get(then_body->data.command_list.items, 0);
    CTEST_ASSERT_EQ(ctest, inner_if->type, AST_IF_CLAUSE, "inner if clause");
    CTEST_ASSERT_NOT_NULL(ctest, inner_if->data.if_clause.else_body, "inner if has else");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_nested_loops)
{
    const char* input = "while true; do\n"
        "  for i in 1 2 3; do\n"
        "    echo $i\n"
        "  done\n"
        "done";
    ast_node_t* ast = parse_string(input);
    CTEST_ASSERT_NOT_NULL(ctest, ast, "nested loops parsed");
    if (!ast) return;

    ast_node_t* cmd_list = ast;
    if (!cmd_list) { ast_node_destroy(&ast); return; }

    ast_node_t* while_loop = ast_node_list_get(cmd_list->data.command_list.items, 0);
    CTEST_ASSERT_EQ(ctest, while_loop->type, AST_WHILE_CLAUSE, "while loop");

    // Check that body contains for loop
    ast_node_t* while_body = while_loop->data.loop_clause.body;
    CTEST_ASSERT_NOT_NULL(ctest, while_body, "while body exists");
    CTEST_ASSERT_EQ(ctest, while_body->type, AST_COMMAND_LIST, "while body is command list");
    if (while_body->type != AST_COMMAND_LIST)
        return;

    ast_node_t* for_loop = ast_node_list_get(while_body->data.command_list.items, 0);
    CTEST_ASSERT_EQ(ctest, for_loop->type, AST_FOR_CLAUSE, "for loop inside while");
    CTEST_ASSERT_NOT_NULL(ctest, for_loop->data.for_clause.words, "for loop has word list");

    ast_node_destroy(&ast);
    (void)ctest;
}

CTEST(test_parser_complex_case)
{
    const char* input = "case $x in\n"
        "  a|b) echo ab ;;\n"
        "  c) echo c ;;\n"
        "  *) echo other ;;\n"
        "esac";
    ast_node_t* ast = parse_string(input);
    CTEST_ASSERT_NOT_NULL(ctest, ast, "complex case parsed");
    if (!ast) return;

    ast_node_t* cmd_list = ast;
    if (!cmd_list) { ast_node_destroy(&ast); return; }

    ast_node_t* case_stmt = ast_node_list_get(cmd_list->data.command_list.items, 0);
    CTEST_ASSERT_EQ(ctest, case_stmt->type, AST_CASE_CLAUSE, "case statement");

    // Check that we have 3 case items
    CTEST_ASSERT_EQ(ctest, ast_node_list_size(case_stmt->data.case_clause.case_items), 3, "three case items");

    // First item should have 2 patterns (a|b)
    ast_node_t* first_item = ast_node_list_get(case_stmt->data.case_clause.case_items, 0);
    CTEST_ASSERT_EQ(ctest, first_item->type, AST_CASE_ITEM, "first case item");
    CTEST_ASSERT_EQ(ctest, token_list_size(first_item->data.case_item.patterns), 2, "two patterns in first item");

    ast_node_destroy(&ast);
    (void)ctest;
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(void)
{
    arena_start();
    log_init();


    CTestEntry* suite[] = {
        // Parser Tests - Simple Commands
        CTEST_ENTRY(test_parser_create_destroy),
        CTEST_ENTRY(test_parser_simple_command),
        CTEST_ENTRY(test_parser_simple_command_with_args),

        // Parser Tests - Pipelines
        CTEST_ENTRY(test_parser_pipeline),
        CTEST_ENTRY(test_parser_pipeline_negated),

        // Parser Tests - And/Or Lists
        CTEST_ENTRY(test_parser_and_list),
        CTEST_ENTRY(test_parser_or_list),

        // Parser Tests - Command Lists
        CTEST_ENTRY(test_parser_sequential_commands),
        CTEST_ENTRY(test_parser_background_command),

        // Parser Tests - If Clauses
        CTEST_ENTRY(test_parser_if_then_fi),
        CTEST_ENTRY(test_parser_if_else),

        // Parser Tests - While/Until Loops
        CTEST_ENTRY(test_parser_while_loop),
        CTEST_ENTRY(test_parser_until_loop),

        // Parser Tests - For Loops
        CTEST_ENTRY(test_parser_for_loop),

        // Parser Tests - Case Statements
        CTEST_ENTRY(test_parser_case_statement),
        CTEST_ENTRY(test_parser_case_leading_paren),

        // Parser Tests - Function Definitions
        CTEST_ENTRY(test_parser_function_def),
        CTEST_ENTRY(test_parser_function_def_with_subshell),
        CTEST_ENTRY(test_parser_function_def_with_redirections),
        CTEST_ENTRY(test_parser_function_def_empty_body),
        CTEST_ENTRY(test_parser_function_def_missing_rbrace),
        CTEST_ENTRY(test_parser_function_def_missing_lbrace),
        CTEST_ENTRY(test_parser_function_def_reserved_word_name),

        // Parser Tests - Subshells and Brace Groups
        CTEST_ENTRY(test_parser_subshell),
        CTEST_ENTRY(test_parser_brace_group),

        // Parser Tests - Redirections
        CTEST_ENTRY(test_parser_output_redirection),
        CTEST_ENTRY(test_parser_input_redirection),
        CTEST_ENTRY(test_parser_append_redirection),
        CTEST_ENTRY(test_parser_braced_io_number_redirection),
        CTEST_ENTRY(test_parser_braced_io_name_redirection),
        CTEST_ENTRY(test_parser_braced_io_invalid_redirection),

        // Executor Tests
        CTEST_ENTRY(test_exec_create_destroy),

        // Visitor Pattern Tests
        CTEST_ENTRY(test_ast_traverse),

        // AST Utility Tests with Parser
        CTEST_ENTRY(test_ast_to_string),

        // Advanced Parser Tests
        CTEST_ENTRY(test_parser_assignment_only),
        CTEST_ENTRY(test_parser_redirection_only),
        CTEST_ENTRY(test_parser_command_with_assignment),
        CTEST_ENTRY(test_parser_nested_if),
        CTEST_ENTRY(test_parser_nested_loops),
        CTEST_ENTRY(test_parser_complex_case),

        NULL
    };

    int result = ctest_run_suite(suite);

    printf("About to call arena_end...\n");
    fflush(stdout);
    arena_end();
    printf("arena_end completed\n");

    return result;
}

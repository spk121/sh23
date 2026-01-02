#include "ctest.h"
#include "parser.h"
#include "ast.h"
#include "exec.h"
#include "lexer.h"
#include "string_t.h"
#include "token.h"
#include "tokenizer.h"
#include "xalloc.h"
#include "logging.h"

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

// Helper to lex and tokenize a string
static token_list_t *lex_and_tokenize(const char *input)
{
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, input);

    token_list_t *tokens = token_list_create();
    lex_status_t lex_status = lexer_tokenize(lx, tokens, NULL);

    lexer_destroy(&lx);

    if (lex_status != LEX_OK)
    {
        token_list_destroy(&tokens);
        return NULL;
    }

    // Pass through tokenizer (for alias expansion, though we don't use aliases here)
    tokenizer_t *tok = tokenizer_create(NULL);
    token_list_t *output = token_list_create();
    
    tok_status_t tok_status = tokenizer_process(tok, tokens, output);
    
    tokenizer_destroy(&tok);

    if (tok_status != TOK_OK)
    {
        token_list_destroy(&tokens);
        token_list_destroy(&output);
        return NULL;
    }

    token_list_destroy(&tokens);
    return output;
}


// Helper to parse a string into an AST
static ast_node_t *parse_string(const char *input)
{
    token_list_t *tokens = lex_and_tokenize(input);
    if (tokens == NULL)
    {
        printf("Failed to lex/tokenize: %s\n", input);
        return NULL;
    }
    
    parser_t *parser = parser_create();
    ast_node_t *ast = NULL;
    
    // FIXME: NEW API
    abort();
    //parse_status_t status = parser_parse(parser, tokens, &ast);
	parse_status_t status = PARSE_OK; // Placeholder
    
    // Get error message before destroying parser
    const char *err = NULL;
    if (status != PARSE_OK)
    {
        err = parser_get_error(parser);
        if (err)
        {
            // Duplicate the error string since parser_destroy will free it
            size_t len = strlen(err);
            char *err_copy = xmalloc(len + 1);
            memcpy(err_copy, err, len + 1);
            err = err_copy;
        }
    }
    
    parser_destroy(&parser);
    
    // AST may have taken ownership of some tokens during parsing (even on error).
    // Release them from the list without destroying, then free the list structure.
    token_list_release_tokens(tokens);
    xfree(tokens->tokens);
    xfree(tokens);
    
    if (status != PARSE_OK)
    {
        printf("Parse error for input '%s': %s\n", input, err ? err : "unknown");
        if (err)
            xfree((void *)err);
        return NULL;
    }

    return ast;
}

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
 * Parser Tests - Simple Commands
 * ============================================================================ */

CTEST(test_parser_create_destroy)
{
    parser_t *parser = parser_create();
    CTEST_ASSERT_NOT_NULL(ctest, parser, "parser created");
    parser_destroy(&parser);
    (void)ctest;
}

CTEST(test_parser_simple_command)
{
    ast_node_t *ast = parse_string("echo hello");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(ast), AST_COMMAND_LIST, "root is command list");
        CTEST_ASSERT(ctest, ast->data.command_list.items->size > 0, "has items");
        
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_SIMPLE_COMMAND, "first item is simple command");
        CTEST_ASSERT_EQ(ctest, token_list_size(first->data.simple_command.words), 2, "two words");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_simple_command_with_args)
{
    ast_node_t *ast = parse_string("ls -la /tmp");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_SIMPLE_COMMAND, "is simple command");
        CTEST_ASSERT_EQ(ctest, token_list_size(first->data.simple_command.words), 3, "three words");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - Pipelines
 * ============================================================================ */

CTEST(test_parser_pipeline)
{
    ast_node_t *ast = parse_string("ls | grep test");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_PIPELINE, "is pipeline");
        CTEST_ASSERT_EQ(ctest, first->data.pipeline.commands->size, 2, "two commands in pipeline");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_pipeline_negated)
{
    ast_node_t *ast = parse_string("! grep test file");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_PIPELINE, "is pipeline");
        CTEST_ASSERT(ctest, first->data.pipeline.is_negated, "pipeline is negated");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - And/Or Lists
 * ============================================================================ */

CTEST(test_parser_and_list)
{
    ast_node_t *ast = parse_string("true && echo success");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_AND_OR_LIST, "is and/or list");
        CTEST_ASSERT_EQ(ctest, first->data.andor_list.op, ANDOR_OP_AND, "operator is AND");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_or_list)
{
    ast_node_t *ast = parse_string("false || echo fallback");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_AND_OR_LIST, "is and/or list");
        CTEST_ASSERT_EQ(ctest, first->data.andor_list.op, ANDOR_OP_OR, "operator is OR");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - Command Lists
 * ============================================================================ */

CTEST(test_parser_sequential_commands)
{
    ast_node_t *ast = parse_string("echo one; echo two");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(ast), AST_COMMAND_LIST, "is command list");
        CTEST_ASSERT_EQ(ctest, ast->data.command_list.items->size, 2, "two commands");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_background_command)
{
    ast_node_t *ast = parse_string("sleep 10 &");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(ast), AST_COMMAND_LIST, "is command list");
        CTEST_ASSERT(ctest, ast_node_command_list_has_separators(ast), "has separator");
        CTEST_ASSERT_EQ(ctest, ast_node_command_list_get_separator(ast, 0), LIST_SEP_BACKGROUND,
                       "separator is background");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - If Clauses
 * ============================================================================ */

CTEST(test_parser_if_then_fi)
{
    ast_node_t *ast = parse_string("if true\nthen echo yes\nfi");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_IF_CLAUSE, "is if clause");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.if_clause.condition, "has condition");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.if_clause.then_body, "has then body");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_if_else)
{
    ast_node_t *ast = parse_string("if false\nthen echo yes\nelse echo no\nfi");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_IF_CLAUSE, "is if clause");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.if_clause.else_body, "has else body");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - While/Until Loops
 * ============================================================================ */

CTEST(test_parser_while_loop)
{
    ast_node_t *ast = parse_string("while true\ndo echo loop\ndone");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_WHILE_CLAUSE, "is while clause");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.loop_clause.condition, "has condition");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.loop_clause.body, "has body");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_until_loop)
{
    ast_node_t *ast = parse_string("until false\ndo echo loop\ndone");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_UNTIL_CLAUSE, "is until clause");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - For Loops
 * ============================================================================ */

CTEST(test_parser_for_loop)
{
    ast_node_t *ast = parse_string("for x in a b c\ndo echo $x\ndone");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_FOR_CLAUSE, "is for clause");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.for_clause.variable, "has variable");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.for_clause.body, "has body");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - Case Statements
 * ============================================================================ */

// Re-enabled now that parser and ownership semantics are stable
CTEST(test_parser_case_statement)
{
    ast_node_t *ast = parse_string("case $x in\na ) echo a;;\nb ) echo b;;\nesac");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_CASE_CLAUSE, "is case clause");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.case_clause.word, "has word to match");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.case_clause.case_items, "has case items");
        CTEST_ASSERT(ctest, first->data.case_clause.case_items->size >= 2, "has at least 2 case items");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

// Small case-specific test: optional leading '(' before pattern list
CTEST(test_parser_case_leading_paren)
{
    ast_node_t *ast = parse_string("case x in\n(a) echo a;;\n esac");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");

    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_CASE_CLAUSE, "is case clause");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.case_clause.case_items, "has case items");
        CTEST_ASSERT_EQ(ctest, first->data.case_clause.case_items->size, 1, "one case item");
        
        ast_node_t *item = first->data.case_clause.case_items->nodes[0];
        CTEST_ASSERT_NOT_NULL(ctest, item->data.case_item.patterns, "item has patterns");
        CTEST_ASSERT_EQ(ctest, token_list_size(item->data.case_item.patterns), 1, "one pattern");
        CTEST_ASSERT_NOT_NULL(ctest, item->data.case_item.body, "item has body");

        ast_node_destroy(&ast);
    }
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - Function Definitions
 * ============================================================================ */

CTEST(test_parser_function_def)
{
    ast_node_t *ast = parse_string("myfunc() {\necho hello\n}");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_FUNCTION_DEF, "is function definition");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.function_def.name, "has function name");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.function_def.body, "has function body");
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first->data.function_def.body), AST_BRACE_GROUP, "body is brace group");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_function_def_with_subshell)
{
    ast_node_t *ast = parse_string("myfunc() (echo hello)");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_FUNCTION_DEF, "is function definition");
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first->data.function_def.body), AST_SUBSHELL, "body is subshell");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_function_def_with_redirections)
{
    ast_node_t *ast = parse_string("myfunc() { echo hello; } > output.txt");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_FUNCTION_DEF, "is function definition");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.function_def.redirections, "has redirections");
        CTEST_ASSERT_EQ(ctest, first->data.function_def.redirections->size, 1, "has one redirection");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_function_def_empty_body)
{
    ast_node_t *ast = parse_string("myfunc() { }");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_FUNCTION_DEF, "is function definition");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.function_def.body, "has function body");
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first->data.function_def.body), AST_BRACE_GROUP, "body is brace group");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_function_def_missing_rbrace)
{
    ast_node_t *ast = parse_string("myfunc() { echo hello");
    CTEST_ASSERT_NULL(ctest, ast, "parsing failed for missing }");
    (void)ctest;
}

CTEST(test_parser_function_def_missing_lbrace)
{
    ast_node_t *ast = parse_string("myfunc() echo hello }");
    CTEST_ASSERT_NULL(ctest, ast, "parsing failed for missing {");
    (void)ctest;
}

CTEST(test_parser_function_def_reserved_word_name)
{
    ast_node_t *ast = parse_string("if() { echo hello }");
    CTEST_ASSERT_NULL(ctest, ast, "parsing failed for reserved word as function name");
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - Subshells and Brace Groups
 * ============================================================================ */

CTEST(test_parser_subshell)
{
    ast_node_t *ast = parse_string("(echo hello)");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_SUBSHELL, "is subshell");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.compound.body, "has body");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_brace_group)
{
    ast_node_t *ast = parse_string("{ echo hello; }");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_BRACE_GROUP, "is brace group");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.compound.body, "has body");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

/* ============================================================================
 * Parser Tests - Redirections
 * ============================================================================ */

CTEST(test_parser_output_redirection)
{
    ast_node_t *ast = parse_string("echo hello > file.txt");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_SIMPLE_COMMAND, "is simple command");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.simple_command.redirections, "has redirections");
        CTEST_ASSERT(ctest, first->data.simple_command.redirections->size > 0, "has at least one redirection");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_input_redirection)
{
    ast_node_t *ast = parse_string("cat < input.txt");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_SIMPLE_COMMAND, "is simple command");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.simple_command.redirections, "has redirections");
        CTEST_ASSERT(ctest, first->data.simple_command.redirections->size > 0, "has at least one redirection");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_append_redirection)
{
    ast_node_t *ast = parse_string("echo hello >> file.txt");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_SIMPLE_COMMAND, "is simple command");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.simple_command.redirections, "has redirections");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_braced_io_number_redirection)
{
    ast_node_t *ast = parse_string("{2}>out.txt");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");

    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_SIMPLE_COMMAND, "is simple command");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.simple_command.redirections, "has redirections");
        CTEST_ASSERT_EQ(ctest, first->data.simple_command.redirections->size, 1, "one redirection");

        ast_node_t *redir = first->data.simple_command.redirections->nodes[0];
        CTEST_ASSERT_EQ(ctest, redir->data.redirection.io_number, 2, "io number parsed");
        CTEST_ASSERT_NOT_NULL(ctest, redir->data.redirection.io_location, "io location stored");
        CTEST_ASSERT_STR_EQ(ctest, string_cstr(redir->data.redirection.io_location), "2", "io location inner text");

        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_braced_io_name_redirection)
{
    ast_node_t *ast = parse_string("{fd}>out.txt");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");

    if (ast != NULL)
    {
        ast_node_t *first = ast->data.command_list.items->nodes[0];
        CTEST_ASSERT_EQ(ctest, ast_node_get_type(first), AST_SIMPLE_COMMAND, "is simple command");
        CTEST_ASSERT_NOT_NULL(ctest, first->data.simple_command.redirections, "has redirections");
        CTEST_ASSERT_EQ(ctest, first->data.simple_command.redirections->size, 1, "one redirection");

        ast_node_t *redir = first->data.simple_command.redirections->nodes[0];
        CTEST_ASSERT_EQ(ctest, redir->data.redirection.io_number, -1, "io number defaults when name used");
        CTEST_ASSERT_NOT_NULL(ctest, redir->data.redirection.io_location, "io location stored");
        CTEST_ASSERT_STR_EQ(ctest, string_cstr(redir->data.redirection.io_location), "fd", "io location inner text");

        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_braced_io_invalid_redirection)
{
    ast_node_t *ast = parse_string("{2x}>out.txt");
    CTEST_ASSERT_NULL(ctest, ast, "parsing failed for invalid IO location");
    (void)ctest;
}

/* ============================================================================
 * Executor Tests
 * ============================================================================ */

CTEST(test_exec_create_destroy)
{
    exec_t *executor = exec_create();
    CTEST_ASSERT_NOT_NULL(ctest, executor, "executor created");
    CTEST_ASSERT_EQ(ctest, exec_get_exit_status(executor), 0, "initial exit status is 0");
    exec_destroy(&executor);
    (void)ctest;
}

CTEST(test_exec_dry_run)
{
    ast_node_t *ast = parse_string("echo hello");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        exec_t *executor = exec_create();
        exec_set_dry_run(executor, true);
        
        exec_status_t status = exec_execute(executor, ast);
        CTEST_ASSERT_EQ(ctest, status, EXEC_OK, "dry run execution succeeded");
        
        exec_destroy(&executor);
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

/* ============================================================================
 * Visitor Pattern Tests
 * ============================================================================ */

static bool count_visitor(const ast_node_t *node, void *user_data)
{
    (void)node;
    int *count = (int *)user_data;
    (*count)++;
    return true;
}

CTEST(test_ast_traverse)
{
    ast_node_t *ast = parse_string("echo one; echo two");
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
 * AST Utility Tests
 * ============================================================================ */

CTEST(test_ast_node_type_to_string)
{
    const char *str = ast_node_type_to_string(AST_SIMPLE_COMMAND);
    CTEST_ASSERT_NOT_NULL(ctest, str, "type to string works");
    CTEST_ASSERT_STR_EQ(ctest, str, "SIMPLE_COMMAND", "correct string");
    (void)ctest;
}

CTEST(test_ast_to_string)
{
    ast_node_t *ast = parse_string("echo hello");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "parsing succeeded");
    
    if (ast != NULL)
    {
        string_t *str = ast_node_to_string(ast);
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
    ast_node_t *ast = parse_string("VAR=value");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "assignment-only command parsed");
    
    if (ast != NULL)
    {
        CTEST_ASSERT_EQ(ctest, ast->type, AST_COMMAND_LIST, "root is command list");
        CTEST_ASSERT_EQ(ctest, ast_node_list_size(ast->data.command_list.items), 1, "one command");
        
        ast_node_t *cmd = ast_node_list_get(ast->data.command_list.items, 0);
        CTEST_ASSERT_EQ(ctest, cmd->type, AST_SIMPLE_COMMAND, "simple command");
        CTEST_ASSERT_EQ(ctest, token_list_size(cmd->data.simple_command.words), 0, "no words");
        CTEST_ASSERT_EQ(ctest, token_list_size(cmd->data.simple_command.assignments), 1, "one assignment");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_redirection_only)
{
    ast_node_t *ast = parse_string(">output.txt");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "redirection-only command parsed");
    
    if (ast != NULL)
    {
        CTEST_ASSERT_EQ(ctest, ast->type, AST_COMMAND_LIST, "root is command list");
        CTEST_ASSERT_EQ(ctest, ast_node_list_size(ast->data.command_list.items), 1, "one command");
        
        ast_node_t *cmd = ast_node_list_get(ast->data.command_list.items, 0);
        CTEST_ASSERT_EQ(ctest, cmd->type, AST_SIMPLE_COMMAND, "simple command");
        CTEST_ASSERT_EQ(ctest, token_list_size(cmd->data.simple_command.words), 0, "no words");
        CTEST_ASSERT_EQ(ctest, ast_node_list_size(cmd->data.simple_command.redirections), 1, "one redirection");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_command_with_assignment)
{
    ast_node_t *ast = parse_string("VAR=1 echo $VAR");
    CTEST_ASSERT_NOT_NULL(ctest, ast, "command with assignment parsed");
    
    if (ast != NULL)
    {
        CTEST_ASSERT_EQ(ctest, ast->type, AST_COMMAND_LIST, "root is command list");
        CTEST_ASSERT_EQ(ctest, ast_node_list_size(ast->data.command_list.items), 1, "one command");
        
        ast_node_t *cmd = ast_node_list_get(ast->data.command_list.items, 0);
        CTEST_ASSERT_EQ(ctest, cmd->type, AST_SIMPLE_COMMAND, "simple command");
        CTEST_ASSERT_EQ(ctest, token_list_size(cmd->data.simple_command.words), 2, "two words");
        CTEST_ASSERT_EQ(ctest, token_list_size(cmd->data.simple_command.assignments), 1, "one assignment");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_nested_if)
{
    const char *input = "if true; then\n"
                       "  if false; then\n"
                       "    echo no\n"
                       "  else\n"
                       "    echo yes\n"
                       "  fi\n"
                       "fi";
    ast_node_t *ast = parse_string(input);
    CTEST_ASSERT_NOT_NULL(ctest, ast, "nested if parsed");
    
    if (ast != NULL)
    {
        CTEST_ASSERT_EQ(ctest, ast->type, AST_COMMAND_LIST, "root is command list");
        ast_node_t *outer_if = ast_node_list_get(ast->data.command_list.items, 0);
        CTEST_ASSERT_EQ(ctest, outer_if->type, AST_IF_CLAUSE, "outer if clause");
        
        // Check that then_body contains another if
        ast_node_t *then_body = outer_if->data.if_clause.then_body;
        CTEST_ASSERT_NOT_NULL(ctest, then_body, "then body exists");
        CTEST_ASSERT_EQ(ctest, then_body->type, AST_COMMAND_LIST, "then body is command list");
        
        ast_node_t *inner_if = ast_node_list_get(then_body->data.command_list.items, 0);
        CTEST_ASSERT_EQ(ctest, inner_if->type, AST_IF_CLAUSE, "inner if clause");
        CTEST_ASSERT_NOT_NULL(ctest, inner_if->data.if_clause.else_body, "inner if has else");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_nested_loops)
{
    const char *input = "while true; do\n"
                       "  for i in 1 2 3; do\n"
                       "    echo $i\n"
                       "  done\n"
                       "done";
    ast_node_t *ast = parse_string(input);
    CTEST_ASSERT_NOT_NULL(ctest, ast, "nested loops parsed");
    
    if (ast != NULL)
    {
        CTEST_ASSERT_EQ(ctest, ast->type, AST_COMMAND_LIST, "root is command list");
        ast_node_t *while_loop = ast_node_list_get(ast->data.command_list.items, 0);
        CTEST_ASSERT_EQ(ctest, while_loop->type, AST_WHILE_CLAUSE, "while loop");
        
        // Check that body contains for loop
        ast_node_t *while_body = while_loop->data.loop_clause.body;
        CTEST_ASSERT_NOT_NULL(ctest, while_body, "while body exists");
        CTEST_ASSERT_EQ(ctest, while_body->type, AST_COMMAND_LIST, "while body is command list");
        
        ast_node_t *for_loop = ast_node_list_get(while_body->data.command_list.items, 0);
        CTEST_ASSERT_EQ(ctest, for_loop->type, AST_FOR_CLAUSE, "for loop inside while");
        CTEST_ASSERT_NOT_NULL(ctest, for_loop->data.for_clause.words, "for loop has word list");
        
        ast_node_destroy(&ast);
    }
    (void)ctest;
}

CTEST(test_parser_complex_case)
{
    const char *input = "case $x in\n"
                       "  a|b) echo ab ;;\n"
                       "  c) echo c ;;\n"
                       "  *) echo other ;;\n"
                       "esac";
    ast_node_t *ast = parse_string(input);
    CTEST_ASSERT_NOT_NULL(ctest, ast, "complex case parsed");
    
    if (ast != NULL)
    {
        CTEST_ASSERT_EQ(ctest, ast->type, AST_COMMAND_LIST, "root is command list");
        ast_node_t *case_stmt = ast_node_list_get(ast->data.command_list.items, 0);
        CTEST_ASSERT_EQ(ctest, case_stmt->type, AST_CASE_CLAUSE, "case statement");
        
        // Check that we have 3 case items
        CTEST_ASSERT_EQ(ctest, ast_node_list_size(case_stmt->data.case_clause.case_items), 3, "three case items");
        
        // First item should have 2 patterns (a|b)
        ast_node_t *first_item = ast_node_list_get(case_stmt->data.case_clause.case_items, 0);
        CTEST_ASSERT_EQ(ctest, first_item->type, AST_CASE_ITEM, "first case item");
        CTEST_ASSERT_EQ(ctest, token_list_size(first_item->data.case_item.patterns), 2, "two patterns in first item");
        
        ast_node_destroy(&ast);
    }
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
        CTEST_ENTRY(test_exec_dry_run),

        // Visitor Pattern Tests
        CTEST_ENTRY(test_ast_traverse),

        // AST Utility Tests
        CTEST_ENTRY(test_ast_node_type_to_string),
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

    arena_end();

    return result;
}

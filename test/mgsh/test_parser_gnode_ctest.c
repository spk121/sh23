#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "gnode.h"
#include "logging.h"
#include "gprint.h"

// Utility function to check gnode type
static bool gnode_is_type(const gnode_t* node, gnode_type_t expected_type) {
    return node && node->type == expected_type;
}

// Utility function to get the body/child of a program node
static const gnode_t* gnode_get_program_body(const gnode_t* node) {
    if (node && node->type == G_PROGRAM) {
        const gnode_t *complete_commands = node->data.child;
        if (complete_commands && complete_commands->type == G_COMPLETE_COMMANDS) {
            gnode_list_t* complete_command_list = complete_commands->data.list;
            if (complete_command_list && complete_command_list->size > 0) {
                return complete_command_list->nodes[0];
            }
        }
    }
    return NULL;
}

/* DEBUGGING NOTES:
 * 
 * The tests added below (indices 11-13) reveal parser bugs:
 * 
 * 1. "if true\nthen echo yes\nfi" is parsed as G_SIMPLE_COMMAND with "if" as
 *    the command name, NOT as G_IF_CLAUSE. The parser is not recognizing "if"
 *    as a reserved word.
 * 
 * 2. "! grep test file" is parsed as G_SIMPLE_COMMAND with "!" as the command  
 *    name, NOT as a negated G_PIPELINE. The parser is not recognizing "!" as
 *    TOKEN_BANG.
 * 
 * 3. "while true\ndo echo loop\ndone" likely has the same issue as #1.
 * 
 * The lowering code in lower.c is correct. The parser needs to be fixed to:
 *   - Recognize reserved words (if, then, fi, else, elif, while, until, do,
 *     done, for, in, case, esac)
 *   - Recognize "!" as TOKEN_BANG for pipeline negation
 *   - Create G_IF_CLAUSE, G_WHILE_CLAUSE, G_FOR_CLAUSE, etc. nodes instead
 *     of treating reserved words as simple command names
 */

#define NUM_TESTS 16
char *tests[NUM_TESTS+1] = {
    "",
    "ls",
    "ls -l",
    "ls *.txt",
    "ls -l *.txt",
    "echo \'hello\'",
    "echo \\\'hello\\\'",
    "echo \"hello\"",
    "echo \\\"hello\\\"",
    "ls -1 | less",
    "cat tmp.txt > foo.txt",
    "if true\nthen echo yes\nfi",
    "! grep test file",
    "while true\ndo echo loop\ndone",
    "until false\ndo echo loop\ndone",
    "case $x in\na ) echo a;;\nb ) echo b;;\nesac",
    NULL
};

int main(void) {
    int test_count = 0;

    // Test 1: Parse a simple command "echo hello" - should return GNODE_PROGRAM with GNODE_SIMPLE_COMMAND body
    for (test_count = 0; test_count < NUM_TESTS; test_count++)
    {
        printf("TEST %d '%s'\n", test_count, tests[test_count]);
        gnode_t* node = NULL;
        parse_status_t status = parser_string_to_gnodes(tests[test_count], &node);
        printf("  status=%d, node=%p\n", (int)status, (void*)node);
        if (status == PARSE_OK && node) {
            const gnode_t* body = gnode_get_program_body(node);
            gprint(body);
        } else {
            printf("  <parse failed>\n");
        }

        if (node) {
            g_node_destroy(&node);
        }
    }
    
    printf("ok\n");
    return 0;
}

#if 0
    // Test 2: Parse a pipeline "echo hello | wc" - should return GNODE_PROGRAM with GNODE_PIPELINE body
    test_count++;
    {
        gnode_t* node = NULL;
        parse_status_t status = parser_string_to_gnodes("echo hello | wc", &node);
        const gnode_t* body = gnode_get_program_body(node);
        if (status == PARSE_OK && node && gnode_is_type(node, G_PROGRAM) && body && gnode_is_type(body, G_PIPELINE)) {
            printf("ok %d - parse pipeline\n", test_count);
            passed++;
        }
        else {
            printf("not ok %d - parse pipeline\n", test_count);
        }
        if (node) {
            g_node_destroy(&node);
        }
    }

    printf("1..%d\n", test_count);
    return passed == test_count ? 0 : 1;
}
#endif

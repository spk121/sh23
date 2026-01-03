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

char *tests[100] = {
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
};

int main(void) {
    int test_count = 0;

    // Test 1: Parse a simple command "echo hello" - should return GNODE_PROGRAM with GNODE_SIMPLE_COMMAND body
    for (test_count = 0; test_count < 10; test_count++)
    {
        printf("TEST %s\n", tests[test_count]);
        gnode_t* node = NULL;
        parse_status_t status = parser_string_to_gnodes(tests[test_count], &node);
        const gnode_t* body = gnode_get_program_body(node);
        gprint(body);

        if (node) {
            g_node_destroy(&node);
        }
    }
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

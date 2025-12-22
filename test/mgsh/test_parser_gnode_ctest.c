#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "gnode.h"
#include "logging.h"
#include "gprint.h"

// Utility function to check gnode type
bool gnode_is_type(const gnode_t* node, gnode_type_t expected_type) {
    return node && node->type == expected_type;
}

// Utility function to get the body/child of a program node
const gnode_t* gnode_get_program_body(const gnode_t* node) {
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

int main(void) {
    int test_count = 0;
    int passed = 0;

    // Test 1: Parse a simple command "echo hello" - should return GNODE_PROGRAM with GNODE_SIMPLE_COMMAND body
    test_count++;
    {
        gnode_t* node = NULL;
        parse_status_t status = parser_string_to_gnodes("echo hello | wc", &node);
        const gnode_t* body = gnode_get_program_body(node);
        gprint(body);
        if (status == PARSE_OK && node && gnode_is_type(node, G_PROGRAM) && body && gnode_is_type(body, G_SIMPLE_COMMAND)) {
            printf("ok %d - parse simple command\n", test_count);
            passed++;
        }
        else {
            printf("not ok %d - parse simple command\n", test_count);
        }
        if (node) {
            g_node_destroy(&node);
        }
    }

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

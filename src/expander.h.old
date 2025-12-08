#ifndef EXPANDER_H
#define EXPANDER_H

#include "ast.h"
#include "string_t.h"
typedef struct expander_t expander_t;

expander_t *expander_create(void);
void expander_destroy(expander_t *exp);

// Main entry point
// Takes a parsed AST, returns a new AST with all expansions done
// (or modifies in place — your choice)
ast_node_t *expander_expand_ast(expander_t *exp, ast_node_t *node);

// Or per-word (more modular):
string_list_t *expander_expand_word(expander_t *exp, token_t *word_token);

#endif

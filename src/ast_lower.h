#ifndef AST_LOWER_H
#define AST_LOWER_H

#include "ast.h"
#include "ast_grammar.h"

/* Lower a POSIX-precise grammar AST (gnode_t) into an execution AST (ast_t).
 * Expects root to be a G_PROGRAM node.
 */
ast_t *ast_lower(const gnode_t *root);

#endif

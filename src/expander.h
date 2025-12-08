#ifndef EXPANDER_H
#define EXPANDER_H

#include "ast.h"
#include "string_t.h"
#include "token.h"
#include <stdbool.h>

/**
 * @file expander.h
 * @brief Word expansion module for POSIX shell
 * 
 * The expander performs the word expansion steps according to POSIX:
 * 1. Tilde expansion
 * 2. Parameter expansion
 * 3. Command substitution
 * 4. Arithmetic expansion
 * 5. Field splitting
 * 6. Pathname expansion
 * 7. Quote removal
 * 
 * The expander operates on AST nodes or individual word tokens after parsing.
 */

// Opaque expander type
typedef struct expander_t expander_t;

/**
 * Create a new expander instance.
 * @return A new expander, or NULL on allocation failure.
 */
expander_t *expander_create(void);

/**
 * Destroy an expander instance and free associated resources.
 * Safe to call with NULL.
 * @param exp The expander to destroy
 */
void expander_destroy(expander_t *exp);

/**
 * Set the IFS (Internal Field Separator) value.
 * @param exp The expander instance
 * @param ifs The new IFS value (will be cloned)
 */
void expander_set_ifs(expander_t *exp, const string_t *ifs);

/**
 * Get the current IFS value.
 * @param exp The expander instance
 * @return The current IFS string (do not modify or free)
 */
const string_t *expander_get_ifs(const expander_t *exp);

/**
 * Expand an entire AST node tree.
 * This traverses the AST and expands all words in commands.
 * 
 * @param exp The expander instance
 * @param node The AST node to expand
 * @return The expanded AST (may be the same node or a modified copy)
 * 
 * @note This is a stub implementation that returns the node unchanged.
 */
ast_node_t *expander_expand_ast(expander_t *exp, ast_node_t *node);

/**
 * Expand a single word token into a list of strings.
 * 
 * This performs all expansion steps:
 * - Tilde expansion
 * - Parameter expansion
 * - Command substitution  
 * - Arithmetic expansion
 * - Field splitting
 * - Pathname expansion
 * - Quote removal
 * 
 * @param exp The expander instance
 * @param word_token The word token to expand
 * @return A list of expanded strings (caller must free with string_list_destroy)
 * 
 * @note This is a stub implementation that only handles literal parts.
 */
string_list_t *expander_expand_word(expander_t *exp, token_t *word_token);

#endif /* EXPANDER_H */

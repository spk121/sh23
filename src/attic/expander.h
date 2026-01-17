#ifndef EXPANDER_H
#define EXPANDER_H

#include <stdbool.h>
#include "positional_params.h"
#include "string_t.h"
#include "token.h"
#include "variable_store.h"

/* Forward declaration */
typedef struct exec_t exec_t;

/**
 * Opaque expander structure.
 * Handles shell word expansion: tilde, parameter, command substitution,
 * arithmetic, field splitting, and pathname expansion.
 */
typedef struct expander_t expander_t;

/**
 * Create a new expander.
 * 
 * @param executor Executor context (for callbacks to access shell state)
 * @return New expander instance, or NULL on allocation failure
 */
expander_t *expander_create(exec_t *executor);

/**
 * Destroy an expander and free its resources.
 * 
 * @param exp_ptr Pointer to expander pointer (set to NULL after destruction)
 */
void expander_destroy(expander_t **exp_ptr);

/**
 * Expand a single word token.
 * 
 * Performs all POSIX expansions in order:
 * 1. Tilde expansion
 * 2. Parameter expansion
 * 3. Command substitution
 * 4. Arithmetic expansion
 * 5. Field splitting
 * 6. Pathname expansion (globbing)
 * 
 * @param exp Expander instance
 * @param tok Token to expand (must be TOKEN_WORD)
 * @return List of expanded strings, or NULL on error
 */
string_list_t *exec_expand_word(expander_t *exp, const token_t *tok);

/**
 * Expand multiple word tokens.
 * 
 * @param exp Expander instance
 * @param tokens List of tokens to expand
 * @return List of all expanded strings, or NULL on error
 */
string_list_t *expander_expand_words(expander_t *exp, const token_list_t *tokens);

/**
 * Expand a redirection target.
 * 
 * Performs expansions without field splitting or pathname expansion.
 * 
 * @param exp Expander instance
 * @param tok Redirection target token
 * @return Expanded string, or NULL on error
 */
string_t *exec_expand_redirection_target(expander_t *exp, const token_t *tok);

/**
 * Expand an assignment value.
 * 
 * Performs expansions without field splitting or pathname expansion.
 * 
 * @param exp Expander instance
 * @param tok Assignment token (must be TOKEN_ASSIGNMENT_WORD)
 * @return Expanded value string, or NULL on error
 */
string_t *expander_expand_assignment_value(expander_t *exp, const token_t *tok);

/**
 * Expand a heredoc body.
 * 
 * @param exp Expander instance
 * @param body Heredoc body text
 * @param is_quoted Whether the heredoc delimiter was quoted
 * @return Expanded heredoc body, or NULL on error
 */
string_t *exec_expand_heredoc(expander_t *exp, const string_t *body, bool is_quoted);

#endif /* EXPANDER_H */

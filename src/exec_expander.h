#ifndef EXEC_EXPANDER_H
#define EXEC_EXPANDER_H

#include "string_t.h"
#include "token.h"

/* Forward declarations */
typedef struct exec_t exec_t;

/* ============================================================================
 * Expansion Callback Functions
 * ============================================================================ */

/**
 * Perform tilde expansion.
 * 
 * @param executor Executor context
 * @param text Text starting with tilde
 * @return Expanded path, or original text if expansion fails
 */
string_t *exec_expand_tilde(exec_t *executor, const string_t *text);

/* ============================================================================
 * Word Expansion Functions
 * ============================================================================ */

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
 * @param executor Executor context
 * @param tok Token to expand (must be TOKEN_WORD)
 * @return List of expanded strings, or NULL on error
 */
string_list_t *exec_expand_word(exec_t *executor, const token_t *tok);

/**
 * Expand multiple word tokens.
 * 
 * @param executor Executor context
 * @param tokens List of tokens to expand
 * @return List of all expanded strings, or NULL on error
 */
string_list_t *exec_expand_words(exec_t *executor, const token_list_t *tokens);

/**
 * Expand a redirection target.
 * 
 * Performs expansions without field splitting or pathname expansion.
 * 
 * @param executor Executor context
 * @param tok Redirection target token
 * @return Expanded string, or NULL on error
 */
string_t *exec_expand_redirection_target(exec_t *executor, const token_t *tok);

/**
 * Expand an assignment value.
 * 
 * Performs expansions without field splitting or pathname expansion.
 * 
 * @param executor Executor context
 * @param tok Assignment token (must be TOKEN_ASSIGNMENT_WORD)
 * @return Expanded value string, or NULL on error
 */
string_t *exec_expand_assignment_value(exec_t *executor, const token_t *tok);

/**
 * Expand a heredoc body.
 * 
 * @param executor Executor context
 * @param body Heredoc body text
 * @param is_quoted Whether the heredoc delimiter was quoted
 * @return Expanded heredoc body, or NULL on error
 */
string_t *exec_expand_heredoc(exec_t *executor, const string_t *body, bool is_quoted);

#endif /* EXEC_EXPANDER_H */
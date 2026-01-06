#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "alias_store.h"
#include "token.h"
#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * Tokenizer Status (return codes)
 * ============================================================================ */

typedef enum
{
    TOK_OK = 0,         // successful tokenization
    TOK_ERROR,          // error during tokenization
    TOK_INCOMPLETE,     // need more input tokens
    TOK_INTERNAL_ERROR, // an error due to bad programming logic
} tok_status_t;

/* ============================================================================
 * Tokenizer Context (main state structure)
 * ============================================================================ */

typedef struct tokenizer_t
{
    /* alias_t store for alias expansion */
    alias_store_t *aliases;

    /* Input tokens from lexer */
    token_list_t *input_tokens;
    int input_pos; // current position in input_tokens
    int padding1;

    /* Output tokens after tokenization */
    token_list_t *output_tokens;

    /* Expansion tracking to prevent infinite recursion */
    int expansion_depth;
    int max_expansion_depth;

    /* Track which aliases have been expanded in the current context
     * to prevent recursive expansion of the same alias */
    char **expanded_aliases;
    int expanded_aliases_count;
    int expanded_aliases_capacity;

    /* Error reporting */
    string_t *error_msg;

    /* Context flags */
    bool at_command_position; // true if next word could be eligible for alias expansion
    char padding2[7];

} tokenizer_t;

/* ============================================================================
 * Tokenizer Lifecycle Functions
 * ============================================================================ */

/**
 * Create a new tokenizer.
 * If aliases is NULL, no alias expansion will be performed.
 */
tokenizer_t *tokenizer_create(alias_store_t *aliases);

/**
 * Destroy a tokenizer and free all associated memory.
 * Safe to call with NULL.
 * Does not destroy the alias store (caller retains ownership).
 */
void tokenizer_destroy(tokenizer_t **tok);

/* ============================================================================
 * Main Tokenization Function
 * ============================================================================ */

/**
 * Tokenize the input token list and produce an output token list.
 * This performs alias expansion and re-lexing as needed.
 *
 * @param tok The tokenizer context
 * @param input_tokens The list of tokens from the lexer
 * @param output_tokens The list to append tokenized tokens to
 *
 * @return TOK_OK on success, TOK_ERROR on error
 *
 * The input_tokens are consumed and should not be reused.
 * The caller takes ownership of tokens in output_tokens.
 */
tok_status_t tokenizer_process(tokenizer_t *tok, token_list_t *input_tokens, token_list_t *output_tokens);

/**
 * Process a single token from the input.
 * Returns TOK_OK if a token was processed, TOK_ERROR on error,
 * or TOK_INCOMPLETE if more input is needed.
 */
tok_status_t tokenizer_process_one_token(tokenizer_t *tok);

/* ============================================================================
 * alias_t Expansion Functions
 * ============================================================================ */

/**
 * Check if a token is eligible for alias expansion.
 * A token is eligible if it's a WORD at a command position and
 * not quoted.
 */
bool tokenizer_is_alias_eligible(const tokenizer_t *tok, const token_t *token);

/**
 * Expand an alias for the given word token.
 * If the alias value contains text, it will be re-lexed and the
 * resulting tokens will be inserted into the input stream for re-processing.
 *
 * @param tok The tokenizer context
 * @param alias_name The name of the alias (extracted from the token)
 *
 * @return TOK_OK on success, TOK_ERROR on error
 */
tok_status_t tokenizer_expand_alias(tokenizer_t *tok, const char *alias_name);

/**
 * Check if an alias ends with a blank character (space or tab).
 * According to POSIX, when an alias ends with a blank, the next
 * word should also be checked for alias expansion.
 */
bool tokenizer_alias_ends_with_blank(const char *alias_value);

/**
 * Extract the word text from a token for alias lookup.
 * Returns a newly allocated string that must be freed by the caller.
 * Returns NULL if the token is not a simple word.
 */
char *tokenizer_extract_word_text(const token_t *token);

/**
 * Re-lex the given text and append the resulting tokens to the tokenizer.
 * This is used when an alias is expanded.
 *
 * @param tok The tokenizer context
 * @param text The text to re-lex
 *
 * @return TOK_OK on success, TOK_ERROR on error
 */
tok_status_t tokenizer_relex_text(tokenizer_t *tok, const char *text);

/* ============================================================================
 * Recursion Tracking Functions
 * ============================================================================ */

/**
 * Mark an alias as expanded in the current context.
 * This prevents infinite recursion.
 */
void tokenizer_mark_alias_expanded(tokenizer_t *tok, const char *alias_name);

/**
 * Check if an alias has already been expanded in the current context.
 */
bool tokenizer_is_alias_expanded(const tokenizer_t *tok, const char *alias_name);

/**
 * Clear the list of expanded aliases.
 * Called when entering a new expansion context.
 */
void tokenizer_clear_expanded_aliases(tokenizer_t *tok);

/* ============================================================================
 * Context Management Functions
 * ============================================================================ */

/**
 * Update the command position flag based on the token type.
 * After certain tokens (like newline, semicolon, etc.), the next
 * word is at a command position.
 */
void tokenizer_update_command_position(tokenizer_t *tok, const token_t *token);

/* ============================================================================
 * Error Handling Functions
 * ============================================================================ */

/**
 * Set an error message.
 */
void tokenizer_set_error(tokenizer_t *tok, const char *format, ...);

/**
 * Get the error message from the last failed operation.
 * Returns NULL if no error.
 */
const char *tokenizer_get_error(const tokenizer_t *tok);

/**
 * Clear the error state.
 */
void tokenizer_clear_error(tokenizer_t *tok);

#endif /* TOKENIZER_H */

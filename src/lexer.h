#ifndef LEXER_H
#define LEXER_H

/**
 * @file lexer.h
 * @brief Public API for the POSIX shell lexer
 *
 * This is the ONLY header that external modules should include.
 * The lexer_t struct is opaque; all interaction goes through the
 * functions declared below.
 *
 * Memory-safety contract (the "variable-store silo" rules):
 *
 *   1. Functions that feed data INTO the lexer accept non-immediate
 *      arguments as `const` pointers and deep-copy whatever they need.
 *      The caller retains full ownership of the originals.
 *
 *   2. Functions that return data FROM the lexer either:
 *      (a) return a `const` pointer into lexer-owned storage
 *          (the pointer is valid until the next mutating call), or
 *      (b) return a newly-allocated object whose ownership is
 *          transferred to the caller.
 *      When a returned struct contains pointer members, those members
 *      are themselves `const` (for case a) or caller-owned (for case b).
 *
 *   3. There are no public "move" functions.  (Internal code may use
 *      move semantics freely; see lexer_priv_t.h.)
 */

#include "lexer_t.h" /* lex_status_t, opaque lexer_t */
#include "token.h"   /* token_t, token_list_t         */

/* Forward-declare string_t so callers that only use the cstr
   overloads don't need to pull in string_t.h.                 */
typedef struct string_t string_t;

/* ============================================================================
 * Lexer Lifecycle
 * ============================================================================ */

/**
 * Create a new lexer in its initial state.
 * Returns a heap-allocated lexer; the caller must eventually pass it
 * to lexer_destroy().
 */
lexer_t *lexer_create(void);

/**
 * Reset the lexer to its initial state, clearing all buffered input,
 * tokens, heredocs, and error state.  The lexer object itself is
 * reusable after this call.
 */
void lexer_reset(lexer_t *lx);

/**
 * Destroy a lexer and free all associated memory.
 * Sets *lx to NULL.  Safe to call with a NULL pointer inside.
 */
void lexer_destroy(lexer_t **lx);

/* ============================================================================
 * Input — data flows INTO the lexer (deep-copied)
 * ============================================================================ */

/**
 * Append text to the lexer's input buffer.
 *
 * The lexer deep-copies the contents of @p input; the caller retains
 * ownership of the original string_t and may free it at any time.
 */
void lexer_append_input(lexer_t *lx, const string_t *input);

/**
 * Append a C string to the lexer's input buffer.
 *
 * The lexer deep-copies the contents of @p input.
 * @p input must be a valid, non-empty, NUL-terminated string.
 *
 * Returns the lexer pointer for chaining convenience.
 */
lexer_t *lexer_append_input_cstr(lexer_t *lx, const char *input);

/* ============================================================================
 * Tokenization — the main workhorse
 * ============================================================================ */

/**
 * Tokenize buffered input and append completed tokens to @p out_tokens.
 *
 * Ownership of every token appended to @p out_tokens is transferred to
 * the caller.  On error the lexer's error state is set (see
 * lexer_get_error()).
 *
 * @param lx              The lexer context.
 * @param out_tokens      Receives newly produced tokens (caller owns them).
 * @param num_tokens_read If non-NULL, receives the count of tokens added.
 * @return LEX_OK on success, LEX_ERROR on syntax error,
 *         LEX_INCOMPLETE if more input is needed.
 */
lex_status_t lexer_tokenize(lexer_t *lx, token_list_t *out_tokens, int *num_tokens_read);

/* ============================================================================
 * Error Reporting — data flows OUT of the lexer (const pointer)
 * ============================================================================ */

/**
 * Get the human-readable error message from the last failed operation.
 *
 * Returns a `const char *` that points into lexer-owned storage.
 * The pointer is valid until the next mutating lexer call.
 * Returns NULL if no error is pending.
 */
const char *lexer_get_error(const lexer_t *lx);

/* ============================================================================
 * Test / Convenience Helpers
 * ============================================================================ */

/**
 * Create a lexer pre-loaded with the given C string.
 * Equivalent to lexer_create() + lexer_append_input_cstr().
 * (Intended for unit tests.)
 */
lexer_t *lexer_create_with_input_cstr(const char *input);

/**
 * One-shot convenience: lex @p input and append all tokens to
 * @p out_tokens.  The caller owns the tokens.
 * (Intended for unit tests.)
 */
lex_status_t lex_cstr_to_tokens(const char *input, token_list_t *out_tokens);

#endif /* LEXER_H */

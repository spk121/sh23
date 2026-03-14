#ifndef PARSE_SESSION_H
#define PARSE_SESSION_H

/**
 * @file parse_session.h
 * @brief Unified parse session for the shell executor.
 *
 * An parse_session_t bundles all state needed to incrementally lex,
 * tokenize, and parse shell input:
 *
 *   - A lexer (tracks unclosed quotes, heredocs, etc.)
 *   - A tokenizer (alias expansion, compound-command buffering)
 *   - Accumulated tokens from an incomplete parse
 *   - Line-number tracking
 *   - Source-location metadata for error messages
 *   - An "incomplete" flag for the partial-execution API
 */

#include <stdbool.h>
#include <stddef.h>

#include "migash/string_t.h"

/* Forward declarations — concrete definitions live in their own headers. */
typedef struct lexer_t lexer_t;
typedef struct tokenizer_t tokenizer_t;
typedef struct token_list_t token_list_t;
typedef struct alias_store_t alias_store_t;

/* ============================================================================
 * Parse Session
 * ============================================================================ */

typedef struct parse_session_t
{
    /* Lexer — owns quote / heredoc continuation state. */
    lexer_t *lexer;

    bool own_aliases; // whether this session owns its alias store (and should destroy it)
    alias_store_t *aliases;

    /* Tokenizer — alias expansion, compound-command buffering. */
    tokenizer_t *tokenizer;

    /* Tokens accumulated across lines when the parser returns INCOMPLETE. */
    token_list_t *accumulated_tokens;

    /* Line counter (incremented by exec_frame_string_core on each chunk). */
    int line_num;

    /* Whether we are mid-parse (unclosed quote, compound command, etc.). */
    bool incomplete;

    /* Source filename for error messages (may be NULL). */
    string_t *filename;

    /* Source line number as seen by the caller (may differ from line_num
       when the caller supplies explicit line numbers). */
    size_t caller_line_number;

} parse_session_t;

/* ============================================================================
 * Lifecycle
 * ============================================================================ */

/**
 * Create a new parse session.
 * Uses a provided alias store for expansion if given, otherwise creates its own
 * independent alias store.
 *
 * @param aliases  Alias store for expansion (may be NULL).
 * @return A new session. Caller owns.
 */
parse_session_t *parse_session_create(alias_store_t *aliases);

/**
 * Destroy a parse session and free all associated memory.
 * Sets *session to NULL.  Safe to call with NULL or *session == NULL.
 */
void parse_session_destroy(parse_session_t **session);

/**
 * Reset a session after a command has been successfully executed.
 *
 * This clears the lexer, accumulated tokens, and resets the tokenizer
 * for the next command, but keeps the session allocated for reuse.
 * The filename and line counter are NOT reset (they keep incrementing).
 * The alias store is NOT reset (aliases persist across commands).
 */
void parse_session_reset(parse_session_t *session);

/**
 * Fully reset a session, including destroying and recreating the tokenizer.
 * If an alias store is provided, it will replace the existing one; otherwise the
 * existing alias store will be cleared if owned, or a new one will be created if not owned.
 *
 * Used after SIGINT or other hard interrupts where any buffered
 * compound-command state in the tokenizer must be discarded.
 *
 * @param session  The session.
 * @param aliases  Alias store for the new tokenizer (may be NULL).
 */
void parse_session_hard_reset(parse_session_t *session, alias_store_t *aliases);

/* ============================================================================
 * Opaque-size helper (for callers that cannot include this header)
 * ============================================================================ */

/**
 * Return sizeof(parse_session_t) so callers can allocate one
 * without including parse_session.h.
 */
size_t parse_session_size(void);

#endif /* PARSE_SESSION_H */

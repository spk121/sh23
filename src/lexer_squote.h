#ifndef LEXER_SQUOTE_H
#define LEXER_SQUOTE_H

#ifndef LEXER_INTERNAL
#error "This is a private header. Do not include it directly; include lexer.h instead."
#endif

#include "lexer_priv_t.h"

/**
 * @file lexer_squote.h
 * @brief Lexer module for POSIX shell single-quoted strings
 *
 * Single-quoted strings in POSIX shell treat all characters literally,
 * with no expansion. The only special character is the closing single
 * quote, which ends the quoted string.
 *
 * Key differences from normal mode:
 * - No parameter expansion ($var, ${var})
 * - No command substitution ($(cmd), `cmd`)
 * - No arithmetic expansion ($((...)))
 * - No backslash escaping (backslash is literal)
 * - Only the closing single quote ends the string
 */

/**
 * Process characters inside a single-quoted string.
 *
 * This function is called after the opening single quote has been consumed.
 * It reads characters and appends them literally to the current word until
 * it encounters the closing single quote.
 *
 * @param lx The lexer context
 * @return LEX_OK if the closing quote was found and processed,
 *         LEX_INCOMPLETE if more input is needed (unclosed quote)
 */
lex_status_t lexer_process_squote(lexer_t *lx);

#endif /* LEXER_SQUOTE_H */

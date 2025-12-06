#ifndef LEXER_DQUOTE_H
#define LEXER_DQUOTE_H

#include "lexer.h"

/**
 * @file lexer_dquote.h
 * @brief Lexer module for POSIX shell double-quoted strings
 *
 * Double-quoted strings in POSIX shell allow certain expansions and
 * escape sequences while still protecting most special characters.
 *
 * Key differences from single-quoted strings:
 * - Parameter expansion is performed ($var, ${var})
 * - Command substitution is performed ($(cmd), `cmd`)
 * - Arithmetic expansion is performed ($((...)))
 * - Backslash escapes the following characters: $ ` " \ newline
 * - Other backslash sequences are literal (both backslash and character kept)
 *
 * Key differences from normal mode:
 * - Most metacharacters are treated literally (|, &, ;, etc.)
 * - Field splitting is suppressed on expanded results
 * - Pathname expansion (globbing) is not performed
 */

/**
 * Process characters inside a double-quoted string.
 *
 * This function is called after the opening double quote has been consumed.
 * It reads characters, handling escape sequences and expansion triggers,
 * until it encounters the closing double quote.
 *
 * @param lx The lexer context
 * @return LEX_OK if the closing quote was found and processed,
 *         LEX_INCOMPLETE if more input is needed (unclosed quote, expansion pending),
 *         LEX_ERROR on syntax error
 */
lex_status_t lexer_process_dquote(lexer_t *lx);

#endif /* LEXER_DQUOTE_H */

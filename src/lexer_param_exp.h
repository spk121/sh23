#ifndef LEXER_PARAM_EXP_H
#define LEXER_PARAM_EXP_H

#include "lexer.h"

/**
 * @file lexer_param_exp.h
 * @brief Lexer module for POSIX shell parameter expansions
 *
 * This module handles lexing of parameter expansions, both braced (${...})
 * and unbraced ($var, $1, $?, etc.).
 *
 * Braced parameter expansions support various operators:
 * - ${parameter}           - simple braced expansion
 * - ${#parameter}          - string length
 * - ${parameter:-word}     - use default value
 * - ${parameter:=word}     - assign default value
 * - ${parameter:?word}     - error if unset
 * - ${parameter:+word}     - use alternate value
 * - ${parameter%pattern}   - remove small suffix
 * - ${parameter%%pattern}  - remove large suffix
 * - ${parameter#pattern}   - remove small prefix
 * - ${parameter##pattern}  - remove large prefix
 *
 * Unbraced parameter expansions:
 * - $name       - longest valid name (letters, digits, underscore starting with non-digit)
 * - $0-$9       - positional parameters
 * - $@, $*, $#, $?, $-, $$, $! - special parameters
 */

/**
 * Process a braced parameter expansion ${...}.
 *
 * This function is called after the ${ has been consumed.
 * It reads the parameter name and any operators/words,
 * until it encounters the closing }.
 *
 * @param lx The lexer context
 * @return LEX_OK if the closing brace was found and processed,
 *         LEX_INCOMPLETE if more input is needed,
 *         LEX_ERROR on syntax error
 */
lex_status_t lexer_process_param_exp_braced(lexer_t *lx);

/**
 * Process an unbraced parameter expansion $name or $special.
 *
 * This function is called after the $ has been consumed.
 * For a name, it reads the longest valid name (per POSIX 3.216).
 * For special parameters, it reads a single character.
 *
 * @param lx The lexer context
 * @return LEX_OK when the parameter name has been read,
 *         LEX_INCOMPLETE if more input is needed,
 *         LEX_ERROR on syntax error
 */
lex_status_t lexer_process_param_exp_unbraced(lexer_t *lx);

#endif /* LEXER_PARAM_EXP_H */

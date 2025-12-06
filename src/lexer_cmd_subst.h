#ifndef LEXER_CMD_SUBST_H
#define LEXER_CMD_SUBST_H

#include "lexer.h"

/**
 * @file lexer_cmd_subst.h
 * @brief Lexer module for POSIX shell command substitution
 *
 * This module handles lexing of command substitution in both forms:
 * - Modern form: $(command)
 * - Legacy/backtick form: `command`
 *
 * Command substitution allows the output of a command to replace the
 * command itself. The shell executes the command in a subshell environment
 * and substitutes the command substitution with the standard output of
 * the command, removing trailing newlines.
 *
 * Key differences between the two forms:
 * - In $(...), the string is parsed normally with proper nesting
 * - In `...`, backslash handling is special: backslash only escapes
 *   $, `, and \ characters. Also, when not inside double-quotes,
 *   backslash-newline handling differs.
 *
 * Both forms can be nested and can appear inside double quotes.
 */

/**
 * Process a parenthesized command substitution $(...).
 *
 * This function is called after the $( has been consumed.
 * It reads and tokenizes the embedded command(s) until
 * it encounters the matching closing parenthesis.
 *
 * The $(...) form uses normal shell parsing rules for the
 * embedded command, allowing proper nesting of quotes,
 * expansions, and even nested command substitutions.
 *
 * @param lx The lexer context
 * @return LEX_OK if the closing parenthesis was found and processed,
 *         LEX_INCOMPLETE if more input is needed (unclosed substitution),
 *         LEX_ERROR on syntax error
 */
lex_status_t lexer_process_cmd_subst_paren(lexer_t *lx);

/**
 * Process a backtick command substitution `...`.
 *
 * This function is called after the opening backtick has been consumed.
 * It reads the embedded command until it encounters the closing backtick.
 *
 * The backtick form has special backslash handling:
 * - Backslash only escapes $, `, \, and newline characters
 * - Other backslash sequences are preserved literally
 * - When inside double quotes, the context affects escape handling
 *
 * Note: POSIX recommends using $(...) over `...` because the
 * backtick form has irregular quoting rules for embedded quotes
 * and backslashes.
 *
 * @param lx The lexer context
 * @return LEX_OK if the closing backtick was found and processed,
 *         LEX_INCOMPLETE if more input is needed (unclosed substitution),
 *         LEX_ERROR on syntax error
 */
lex_status_t lexer_process_cmd_subst_backtick(lexer_t *lx);

#endif /* LEXER_CMD_SUBST_H */

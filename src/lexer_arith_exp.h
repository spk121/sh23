#ifndef LEXER_ARITH_EXP_H
#define LEXER_ARITH_EXP_H

#ifndef LEXER_INTERNAL
#error "This is a private header. Do not include it directly; include lexer.h instead."
#endif

#include "lexer_priv_t.h"

/**
 * @file lexer_arith_exp.h
 * @brief Lexer module for POSIX shell arithmetic expansion
 *
 * This module handles lexing of arithmetic expansion: $((...))
 *
 * Per POSIX specification, arithmetic expansion provides a mechanism
 * for evaluating an arithmetic expression and substituting its value.
 * The format is: $((expression))
 *
 * The expression is treated as if it were in double quotes, except that
 * a double quote inside the expression is not treated specially. The
 * shell expands all tokens in the expression for parameter expansion,
 * command substitution, and quote removal.
 *
 * The expression can contain:
 * - Integer constants
 * - Arithmetic operators: +, -, *, /, %, <<, >>, &, |, ^, ~, !, etc.
 * - Comparison operators: <, >, <=, >=, ==, !=
 * - Logical operators: &&, ||
 * - Assignment operators: =, +=, -=, *=, /=, %=, etc.
 * - Ternary operator: ?:
 * - Parentheses for grouping
 * - Variable references (with or without $)
 *
 * Nested parentheses within the arithmetic expression are supported.
 */

/**
 * Process an arithmetic expansion $((...)).
 *
 * This function is called after the $(( has been consumed.
 * It reads and tokenizes the arithmetic expression until
 * it encounters the matching closing )).
 *
 * The function handles:
 * - Nested parentheses for grouping in expressions like $(( (1+2)*3 ))
 * - Escape sequences with backslash
 * - Single and double quotes within the expression
 * - Nested command substitutions $(...) and `...`
 * - Nested parameter expansions ${...}
 *
 * @param lx The lexer context
 * @return LEX_OK if the closing )) was found and processed,
 *         LEX_INCOMPLETE if more input is needed (unclosed expansion),
 *         LEX_ERROR on syntax error
 */
lex_status_t lexer_process_arith_exp(lexer_t *lx);

#endif /* LEXER_ARITH_EXP_H */

#ifndef LEXER_HEREDOC_H
#define LEXER_HEREDOC_H

#ifndef LEXER_INTERNAL
#error "This is a private header. Do not include it directly; include lexer.h instead."
#endif

#include "lexer_priv_t.h"

/**
 * @file lexer_heredoc.h
 * @brief Lexer module for POSIX shell heredoc body processing
 *
 * This module handles lexing of heredoc bodies, which have special
 * processing rules per POSIX specification:
 *
 * Key heredoc rules:
 * - The heredoc body begins after the newline following the << or <<- operator
 * - Body continues until a line containing only the delimiter is found
 * - If delimiter was quoted (any quoting), the body is literal (no expansion)
 * - If delimiter was unquoted, the body undergoes expansion like double quotes
 * - Backslash-newline line continuation is removed during delimiter search
 * - For <<-, leading tabs are stripped from each line (but not spaces)
 * - Within unquoted heredoc, backslash behaves like inside double quotes
 * - Double quotes are literal except within $(), ``, or ${}
 */

/**
 * Process a heredoc body.
 *
 * This function is called when we need to read the body of a queued heredoc.
 * It reads lines until it finds the delimiter, handling expansion and
 * tab stripping as appropriate.
 *
 * @param lx The lexer context
 * @return LEX_OK if the heredoc body was successfully read,
 *         LEX_INCOMPLETE if more input is needed (delimiter not found yet),
 *         LEX_ERROR on syntax error
 */
lex_status_t lexer_process_heredoc_body(lexer_t *lx);

#endif /* LEXER_HEREDOC_H */

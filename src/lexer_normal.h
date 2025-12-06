#ifndef LEXER_NORMAL_H
#define LEXER_NORMAL_H
#include "lexer.h"

/**
 * Attempt to process one token in NORMAL mode, storing it internally.
 * Returns LEX_OK if a token was produced, LEX_ERROR on error,
 * or LEX_INCOMPLETE if more input is needed.
 */
lex_status_t lexer_process_one_normal_token(lexer_t *lx);

#endif
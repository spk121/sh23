#ifndef LEXER_NORMAL_H
#define LEXER_NORMAL_H

#ifndef LEXER_INTERNAL
#error "This is a private header. Do not include it directly; include lexer.h instead."
#endif

#include "lexer_priv_t.h"

/**
 * Attempt to process one token in NORMAL mode, storing it internally.
 * Returns LEX_OK if a token was produced, LEX_ERROR on error,
 * or LEX_INCOMPLETE if more input is needed.
 */
lex_status_t lexer_process_one_normal_token(lexer_t *lx);

#endif /* LEXER_NORMAL_H */

#ifndef LEXER_T_H
#define LEXER_T_H

/**
 * @file lexer_t.h
 * @brief Public types for the POSIX shell lexer
 *
 * This header defines the types visible to external consumers of the
 * lexer API.  The lexer_t struct is opaque — external code never sees
 * its internals and interacts with it exclusively through the functions
 * declared in lexer.h.
 */

#include "token.h"
#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * Lexer Status (return codes)  — public
 * ============================================================================ */

typedef enum
{
    LEX_OK = 0,         // successful tokenization
    LEX_ERROR,          // syntax error
    LEX_INCOMPLETE,     // need more input (e.g., unclosed quote)
    LEX_NEED_HEREDOC,   // parsed heredoc operator, need body next
    LEX_INTERNAL_ERROR, // an error due to bad programming logic
} lex_status_t;

/* ============================================================================
 * Opaque lexer handle — public
 * ============================================================================ */

/**
 * The lexer context is opaque to external consumers.
 * Use lexer_create() / lexer_destroy() to manage its lifetime.
 */
typedef struct lexer_t lexer_t;

#endif /* LEXER_T_H */

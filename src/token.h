#ifndef TOKEN_H
#define TOKEN_H

#include "string.h"

typedef enum {
    TOKEN_WORD,
    TOKEN_ASSIGNMENT_WORD,
    TOKEN_NAME,
    TOKEN_NEWLINE,
    TOKEN_IO_HERE,
    TOKEN_IO_NUMBER,
    TOKEN_IO_LOCATION,
    TOKEN_AND_IF,     // &&
    TOKEN_OR_IF,      // ||
    TOKEN_DSEMI,      // ;;
    TOKEN_SEMI_AND,   // ;&
    TOKEN_DLESS,      // <<
    TOKEN_DGREAT,     // >>
    TOKEN_LESSAND,    // <&
    TOKEN_GREATAND,   // >&
    TOKEN_LESSGREAT,  // <>
    TOKEN_DLESSDASH,  // <<-
    TOKEN_CLOBBER,    // <|
    TOKEN_IF,
    TOKEN_THEN,
    TOKEN_ELSE,
    TOKEN_ELIF,
    TOKEN_FI,
    TOKEN_DO,
    TOKEN_DONE,
    TOKEN_CASE,
    TOKEN_ESAC,
    TOKEN_WHILE,
    TOKEN_UNTIL,
    TOKEN_FOR,
    TOKEN_LBRACE,  // {
    TOKEN_RBRACE,  // }
    TOKEN_BANG,    // !
    TOKEN_IN
} TokenType;

typedef struct Token {
    TokenType type;
    String *str;
} Token;

typedef struct Tokenizer {
    TokenArray *tokens;
    bool io_here;   // Parse line as part of HEREDOC
} Tokenizer;

typedef enum TokenizerError {
    TOKENIZER_OK = 0,
    TOKENIZER_ERROR
} TokenizerError;

#endif

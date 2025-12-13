#ifndef TOKEN_H
#define TOKEN_H

#include "logging.h"
#include "string_t.h"
#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * Token Type Enumeration
 * ============================================================================ */

typedef enum
{
    TOKEN_EOF = 0, // end of input (must be 0 for easy testing)

    /* Basic word */
    TOKEN_WORD, // any word that may need expansion / splitting / globbing

    /* Operators – POSIX requires all of these to be recognized as single tokens */
    TOKEN_AND_IF,    // &&
    TOKEN_OR_IF,     // ||
    TOKEN_DSEMI,     // ;;
    TOKEN_DLESS,     // <<
    TOKEN_DGREAT,    // >>
    TOKEN_LESSAND,   // <&
    TOKEN_GREATAND,  // >&
    TOKEN_LESSGREAT, // <>
    TOKEN_DLESSDASH, // <<-
    TOKEN_CLOBBER,   // >|
    TOKEN_PIPE,      // |
    TOKEN_SEMI,      // ;
    TOKEN_AMPER,     // &
    TOKEN_LPAREN,    // (
    TOKEN_RPAREN,    // )
    TOKEN_GREATER,   // >
    TOKEN_LESS,      // <

    /* Reserved words – POSIX requires they be recognized as distinct tokens
       when they appear as standalone words (not quoted, not part of larger word) */
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
    TOKEN_IN,     // only a reserved word in "for name in" context
    TOKEN_BANG,   // ! (for pipelines)
    TOKEN_LBRACE, // {  (compound command)
    TOKEN_RBRACE, // }

    /* Special tokens used only internally by the lexer / parser */
    TOKEN_NEWLINE,         // logical newline
    TOKEN_IO_NUMBER,       // bare number before < or >, e.g. 2>file
    TOKEN_IO_LOCATION,     // {2} or {var} before < or >, e.g. {2}>file
    TOKEN_ASSIGNMENT_WORD, // name=value that appears where an assignment is allowed

    /* Optional but extremely useful for clean parser design */
    TOKEN_REDIRECT,       // internal pseudo-token when you want to group redirection
    TOKEN_END_OF_HEREDOC, // marks the end of a heredoc body (emitted when delimiter is seen)
    TOKEN_TYPE_COUNT      // total number of token types
} token_type_t;

/* ============================================================================
 * Part Type Enumeration (for TOKEN_WORD expansion components)
 * ============================================================================ */

typedef enum
{
    PART_LITERAL,       // "abc" or escaped characters
    PART_PARAMETER,     // $foo or ${foo}
    PART_COMMAND_SUBST, // $(...) or `...`
    PART_ARITHMETIC,    // $((...))
    PART_TILDE,         // ~/path or ~user/path
} part_type_t;

typedef enum
{
    PARAM_PLAIN,               // $var or ${var}
    PARAM_LENGTH,              // ${#var}
    PARAM_SUBSTRING,           // ${var:offset:length}
    PARAM_USE_DEFAULT,         // ${var:-word}
    PARAM_ASSIGN_DEFAULT,      // ${var:=word}
    PARAM_ERROR_IF_UNSET,      // ${var:?word}
    PARAM_USE_ALTERNATE,       // ${var:+word}
    PARAM_REMOVE_SMALL_PREFIX, // ${var%pattern}
    PARAM_REMOVE_LARGE_PREFIX, // ${var%%pattern}
    PARAM_REMOVE_SMALL_SUFFIX, // ${var#pattern}
    PARAM_REMOVE_LARGE_SUFFIX, // ${var##pattern}
    PARAM_INDIRECT,            // ${!var} or ${!prefix*}
} param_subtype_t;

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct token_t token_t;
typedef struct token_list_t token_list_t;
typedef struct part_t part_t;
typedef struct part_list_t part_list_t;

/* ============================================================================
 * Part Structure (components of TOKEN_WORD)
 * ============================================================================ */

struct part_t
{
    part_type_t type;

    /* For PART_LITERAL and PART_TILDE */
    string_t *text;

    /* For PART_PARAMETER */
    param_subtype_t param_kind;
    string_t *param_name;
    string_t *word; // the "word" in ${var:-word} (already parsed as nested tokens!)

    /* For nested expansions (COMMAND_SUBST, ARITHMETIC, complex ${...}) */
    token_list_t *nested;

    /* Quote tracking */
    bool was_single_quoted; // prevents all expansions
    bool was_double_quoted; // allows selective expansions
};

/* ============================================================================
 * Part List Structure
 * ============================================================================ */

struct part_list_t
{
    part_t **parts;
    int size;
    int capacity;
};

/* ============================================================================
 * Token Structure
 * ============================================================================ */

struct token_t
{
    token_type_t type;

    /* Location tracking for error messages */
    int first_line;
    int first_column;
    int last_line;
    int last_column;

    /* For TOKEN_WORD: list of parts that make up this word */
    part_list_t *parts;

    /* For TOKEN_IO_NUMBER: the actual number value (e.g., 2 in "2>file") */
    int io_number;

    /* For TOKEN_IO_LOCATION: the actual location string (e.g., "{2}>") */
    string_t *io_location;

    /* For heredoc handling */
    string_t *heredoc_delimiter; // the delimiter string
    string_t *heredoc_content;   // the body content
    bool heredoc_delim_quoted;   // <<'EOF' vs <<EOF

    /* For TOKEN_ASSIGNMENT_WORD */
    string_t *assignment_name;     // left side of =
    part_list_t *assignment_value; // right side (can contain expansions)

    /* Expansion control flags */
    bool needs_expansion;          // contains $, `, or other expansions
    bool needs_field_splitting;    // has unquoted expansions
    bool needs_pathname_expansion; // has unquoted glob characters
    bool was_quoted;               // entire word was quoted
    bool has_equals_before_quote;               // has an equals sign before a quoted character
};

/* ============================================================================
 * Token List Structure
 * ============================================================================ */

struct token_list_t
{
    token_t **tokens;
    int size;
    int capacity;
};

/* ============================================================================
 * Token Lifecycle Functions
 * ============================================================================ */

/**
 * Create a new token of the specified type.
 * Returns NULL on allocation failure.
 */
token_t *token_create(token_type_t type);

/**
 * Create a new TOKEN_WORD token (convenience function).
 * Returns NULL on allocation failure.
 */
token_t *token_create_word(void);

/**
 * Destroy a token and free all associated memory.
 * Safe to call with NULL.
 */
void token_destroy(token_t **token);

/* ============================================================================
 * Token Accessors
 * ============================================================================ */

/**
 * Get the type of a token.
 */
token_type_t token_get_type(const token_t *token);

/**
 * Set the type of a token.
 */
void token_set_type(token_t *token, token_type_t type);

/**
 * Get the part list of a TOKEN_WORD token.
 * Returns NULL for non-WORD tokens.
 */
part_list_t *token_get_parts(token_t *token);

/**
 * Get the part list of a TOKEN_WORD token (const version).
 */
const part_list_t *token_get_parts_const(const token_t *token);

/**
 * Get the number of parts in a TOKEN_WORD token.
 * Returns 0 for non-WORD tokens.
 */
int token_part_count(const token_t *token);

/**
 * Get a specific part by index.
 * Returns NULL if index is out of bounds or token is not a WORD.
 */
part_t *token_get_part(const token_t *token, int index);

/**
 * Check if the last part of a TOKEN_WORD is a PART_LITERAL.
 */
bool token_is_last_part_literal(const token_t *token);

/**
 * Check if a token was quoted (entire word).
 */
bool token_was_quoted(const token_t *token);

void token_set_quoted(token_t *token, bool was_quoted);
/* ============================================================================
 * Token Part Management
 * ============================================================================ */

/**
 * Add a part to a TOKEN_WORD token.
 */
void token_add_part(token_t *token, part_t *part);

/**
 * Convenience: append a literal string to a TOKEN_WORD.
 * Creates and adds a PART_LITERAL part.
 */
void token_add_literal_part(token_t *token, const string_t *text);

/**
 * Convenience: append a parameter expansion to a TOKEN_WORD.
 * Creates and adds a PART_PARAMETER part.
 */
void token_append_parameter(token_t *token, const string_t *param_name);

/**
 * Convenience: append a command substitution to a TOKEN_WORD.
 * Creates and adds a PART_COMMAND_SUBST part.
 */
void token_append_command_subst(token_t *token, const string_t *expr_text);

/**
 * Convenience: append an arithmetic expansion to a TOKEN_WORD.
 * Creates and adds a PART_ARITHMETIC part.
 */
void token_append_arithmetic(token_t *token, const string_t *expr_text);

/**
 * Convenience: append a tilde expansion to a TOKEN_WORD.
 * Creates and adds a PART_TILDE part.
 */
void token_append_tilde(token_t *token, const string_t *text);

/**
 * Synchronizes the token's top-level expansion, field splitting,
 * and pathname expansion state based on its parts.
 */
void token_recompute_expansion_flags(token_t *token);

/**
 * Append a character to the last PART_LITERAL of a TOKEN_WORD.
 */
void token_append_char_to_last_literal_part(token_t *token, char c);

/**
 * Append a C-string to the last PART_LITERAL of a TOKEN_WORD.
 */
void token_append_cstr_to_last_literal_part(token_t *token, const char *str);
/* ===========================================================================
 * Reserved Word Handling
 * ============================================================================ */

/**
 * Try to convert a TOKEN_WORD to a reserved word token type.
 * If allow_in is true, "in" can be converted to TOKEN_IN.
 * Returns true if conversion was done, false otherwise.
 */
bool token_try_promote_to_reserved_word(token_t *tok, bool allow_in);

/**
 * Try to convert a TOKEN_WORD to TOKEN_BANG.
 * Returns true if conversion was done, false otherwise.
 */
bool token_try_promote_to_bang(token_t *tok);

bool token_try_promote_to_lbrace(token_t *tok);

bool token_try_promote_to_elif(token_t *tok);
bool token_try_promote_to_else(token_t *tok);
bool token_try_promote_to_then(token_t *tok);
bool token_try_promote_to_fi(token_t *tok);
bool token_try_promote_to_else(token_t *tok);
bool token_try_promote_to_rbrace(token_t *tok);
bool token_try_promote_to_do(token_t *tok);
bool token_try_promote_to_done(token_t *tok);
bool token_try_promote_to_esac(token_t *tok);
bool token_try_promote_to_rbrace(token_t *tok);
bool token_try_promote_to_in(token_t *tok);

/* ============================================================================
 * Token Location Tracking
 * ============================================================================ */

/**
 * Set the location information for a token.
 */
void token_set_location(token_t *token, int first_line, int first_column, int last_line,
                        int last_column);

/**
 * Get the starting line of a token.
 */
int token_get_first_line(const token_t *token);

/**
 * Get the starting column of a token.
 */
int token_get_first_column(const token_t *token);

/* ============================================================================
 * Token Utility Functions
 * ============================================================================ */

/**
 * Convert a token type to a human-readable string.
 */
const char *token_type_to_string(token_type_t type);

/**
 * Create a debug string representation of a token.
 * Caller is responsible for freeing the returned string.
 */
string_t *token_to_string(const token_t *token);

/**
 * Check if a string is a POSIX reserved word.
 */
bool token_is_reserved_word(const char *word);

/**
 * Convert a string to its corresponding reserved word token type.
 * Returns TOKEN_WORD if the string is not a reserved word.
 */
token_type_t token_string_to_reserved_word(const char *word);

/**
 * Check if a string is an operator.
 */
bool token_is_operator(const char *str);

/**
 * Convert a string to its corresponding operator token type.
 * Returns TOKEN_EOF if the string is not an operator.
 */
token_type_t token_string_to_operator(const char *str);

/* ============================================================================
 * Part Lifecycle Functions
 * ============================================================================ */

/**
 * Create a literal part.
 * Returns NULL on allocation failure.
 */
part_t *part_create_literal(const string_t *text);

/**
 * Create a parameter expansion part.
 * Returns NULL on allocation failure.
 */
part_t *part_create_parameter(const string_t *param_name);

/**
 * Create a command substitution part.
 */
part_t *part_create_command_subst(const string_t *expr_text);

/**
 * Create an arithmetic expansion part.
 * The part does not take ownership of the expression text.
 */
part_t *part_create_arithmetic(const string_t *expr_text);

/**
 * Create a tilde expansion part.
 * Returns NULL on allocation failure.
 */
part_t *part_create_tilde(const string_t *text);

/**
 * Destroy a part and free all associated memory.
 * Safe to call with NULL.
 */
void part_destroy(part_t **part);

/* ============================================================================
 * Part Accessors
 * ============================================================================ */

/**
 * Get the type of a part.
 */
part_type_t part_get_type(const part_t *part);

/**
 * Get the text of a PART_LITERAL or PART_TILDE.
 * Returns NULL for other part types.
 */
const string_t *part_get_text(const part_t *part);

/**
 * Get the parameter name of a PART_PARAMETER.
 * Returns NULL for other part types.
 */
const string_t *part_get_param_name(const part_t *part);

/**
 * Get the nested token list of a PART_COMMAND_SUBST or PART_ARITHMETIC.
 * Returns NULL for other part types.
 */
token_list_t *part_get_nested(const part_t *part);

/**
 * Check if a part was single-quoted.
 */
bool part_was_single_quoted(const part_t *part);

/**
 * Check if a part was double-quoted.
 */
bool part_was_double_quoted(const part_t *part);

/**
 * Set quote status for a part.
 */
void part_set_quoted(part_t *part, bool single_quoted, bool double_quoted);

/* ============================================================================
 * Part Utility Functions
 * ============================================================================ */

/**
 * Convert a part type to a human-readable string.
 */
const char *part_type_to_string(part_type_t type);

/**
 * Create a debug string representation of a part.
 * Caller is responsible for freeing the returned string.
 */
string_t *part_to_string(const part_t *part);

/* ============================================================================
 * Part List Functions
 * ============================================================================ */

/**
 * Create a new part list.
 * Returns NULL on allocation failure.
 */
part_list_t *part_list_create(void);

/**
 * Destroy a part list and all contained parts.
 * Safe to call with NULL.
 */
void part_list_destroy(part_list_t **list);

/**
 * Append a part to a part list.
 * The list takes ownership of the part.
 * Returns 0 on success, -1 on failure.
 */
int part_list_append(part_list_t *list, part_t *part);

/**
 * Get the number of parts in a list.
 */
int part_list_size(const part_list_t *list);

/**
 * Get a part by index.
 * Returns NULL if index is out of bounds.
 */
part_t *part_list_get(const part_list_t *list, int index);

/**
 * Destroys the part at the given index,
 * shifting the remaining parts to fill the gap.
 */
int part_list_remove(part_list_t *list, int index);

/**
 * Destructively removes all parts and resets
 * the list to its initial capacity.
 */
void part_list_reinitialize(part_list_t *list);

/* ============================================================================
 * Token List Functions
 * ============================================================================ */

/**
 * Create a new token list.
 * Returns NULL on allocation failure.
 */
token_list_t *token_list_create(void);

/**
 * Destroy a token list and all contained tokens.
 * Safe to call with NULL.
 */
void token_list_destroy(token_list_t **list);

/**
 * Append a token to a token list.
 * The list takes ownership of the token.
 * Returns 0 on success, -1 on failure.
 */
int token_list_append(token_list_t *list, token_t *token);

/**
 * Get the number of tokens in a list.
 */
int token_list_size(const token_list_t *list);

/**
 * Get a token by index.
 * Returns NULL if index is out of bounds.
 */
token_t *token_list_get(const token_list_t *list, int index);

/**
 * Get the last token in a list.
 * Returns NULL if the list is empty.
 */
token_t *token_list_get_last(const token_list_t *list);

/**
 * Destroys the token at the given index, shifting the
 * other tokens to fill in the gap.
 */
int token_list_remove(token_list_t *list, int index);

/**
 * Destructively removes all tokens from a list.
 * Resets list to its initial capacity.
 */
void token_list_clear(token_list_t *list);

/**
 * Clears all token pointers from the list without destroying the tokens.
 * This transfers ownership of all tokens to the caller.
 * The list structure remains valid but empty.
 */
void token_list_release_tokens(token_list_t *list);

/**
 * Detaches the tokens array from the list and returns it.
 * The caller takes ownership of the array.
 * The list is left in a valid empty state.
 * Returns NULL if the list is empty.
 */
token_t **token_list_release(token_list_t *list, int *out_size);

/**
 * Ensure the list has at least the specified capacity.
 * Returns 0 on success, -1 on allocation failure.
 */
int token_list_ensure_capacity(token_list_t *list, int needed_capacity);

/**
 * Insert tokens at the specified position in the list.
 * The list takes ownership of the tokens.
 * Returns 0 on success, -1 on failure.
 */
int token_list_insert_range(token_list_t *list, int index, token_t **tokens, int count);

/**
 * Create a debug string representation of a token list.
 * Caller is responsible for freeing the returned string.
 */
string_t *token_list_to_string(const token_list_t *list);

#endif /* TOKEN_H */

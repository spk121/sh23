#ifndef LEXER_H
#define LEXER_H

#include "string.h"
#include "token.h"
#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

static const int LEXER_INITIAL_STACK_CAPACITY = 8;
static const int LEXER_INITIAL_HEREDOC_CAPACITY = 4;

/* ============================================================================
 * Lexer Modes (for mode stack)
 * ============================================================================ */

typedef enum
{
    LEX_NORMAL = 0,         // normal shell input
    LEX_SINGLE_QUOTE,       // inside '...'
    LEX_DOUBLE_QUOTE,       // inside "..."
    LEX_PARAM_EXP_BRACED,   // inside ${...}
    LEX_PARAM_EXP_UNBRACED, // inside $var (implicit, usually not stacked)
    LEX_CMD_SUBST_PAREN,    // inside $(...)
    LEX_CMD_SUBST_BACKTICK, // inside `...`
    LEX_ARITH_EXP,          // inside $((...))
    LEX_HEREDOC_BODY,       // reading heredoc body
} lex_mode_t;

/* ============================================================================
 * Lexer Status (return codes)
 * ============================================================================ */

typedef enum
{
    LEX_OK = 0,         // successful tokenization
    LEX_ERROR,          // syntax error
    LEX_INCOMPLETE,     // need more input (e.g., unclosed quote)
    LEX_NEED_HEREDOC,   // parsed heredoc operator, need body next
    LEX_INTERNAL_ERROR, // an error due to bad programming logic
} lex_status_t;

/*
 * Forward declarations
 */

typedef struct builder_stack_t builder_stack_t;
typedef struct builder_frame_t builder_frame_t;

/* ============================================================================
 * Mode Stack (for tracking nested contexts)
 * ============================================================================ */

typedef struct
{
    lex_mode_t *modes; // stack of modes
    int capacity;      // allocated capacity
    int size;          // current depth
} lex_mode_stack_t;

/* ============================================================================
 * Heredoc Queue (for pending heredoc bodies)
 * ============================================================================ */

typedef struct
{
    string_t *delimiter;   // the delimiter to look for
    bool strip_tabs;       // true for <<-, false for <<
    bool delimiter_quoted; // was delimiter quoted (affects expansion)
    int token_index;       // index in output tokens where this heredoc belongs
} heredoc_entry_t;

typedef struct
{
    heredoc_entry_t *entries; // array of pending heredocs
    int capacity;             // allocated capacity
    int size;                 // number of pending heredocs
} heredoc_queue_t;

/* ============================================================================
 * Lexer Context (main state structure)
 * ============================================================================ */

typedef struct lexer_t
{
    /* Input management */
    string_t *input; // input string (owned by lexer)
    int pos;         // current position in input

    /* Position tracking for error messages */
    int line_no;        // current line number (1-indexed)
    int col_no;         // current column number (1-indexed)
    int tok_start_line; // line where current token started
    int tok_start_col;  // column where current token started

    /* Mode stack for nested contexts */
    lex_mode_stack_t mode_stack;

    // builder_stack_t builder;

    /* Current token being built */
    token_t *current_token; // the token being constructed
    bool in_word;           // true if we're building a WORD token

    /* Output tokens */
    token_list_t *tokens; // list of completed tokens

    /* Heredoc handling */
    heredoc_queue_t heredoc_queue; // pending heredocs to read
    bool reading_heredoc;          // true when reading heredoc body
    int heredoc_index;             // which heredoc we're currently reading

    /* Character escape state */
    bool escaped; // next char is escaped by backslash

    /* Operator recognition */
    string_t *operator_buffer; // for multi-char operators like &&, <<, etc.

    /* Context for reserved word recognition */
    bool at_command_start; // true if next word could be a reserved word
    bool after_case_in;    // special context for case...in patterns

    /* Alias expansion state (if you implement aliases) */
    bool check_next_for_alias; // set when alias ends in blank

    /* Error reporting */
    string_t *error_msg; // detailed error message if LEX_ERROR
    int error_line;      // line number of error
    int error_col;       // column number of error

} lexer_t;

/* ============================================================================
 * Nested Expansion Builder Stack
 * ============================================================================ */

struct builder_frame_t
{
    token_t *owner_token;              // the TOKEN_WORD that owns this part
    part_list_t *target_parts;         // where new parts go
    token_list_t *nested_list;         // current nested token list (for $(...), $((...), ${...})
    param_subtype_t active_param_kind; // for ${var:...} forms
    bool in_param_word;                // are we parsing the "word" in ${var:-word}?
};

struct builder_stack_t
{
    struct builder_frame_t *stack;
    int capacity;
    int size;
};

/* ============================================================================
 * Lexer Lifecycle Functions
 * ============================================================================ */

/**
 * Create a new lexer.
 */
lexer_t *lexer_create();

/**
 * Append text to the lexer's input buffer.
 * The lexer does not take ownership of the input string.
 * The input must be a valid non-empty C string.
 */
lexer_t *lexer_append_input_cstr(lexer_t *lx, const char *input);

/**
 * Destroy a lexer and free all associated memory.
 * Safe to call with NULL.
 */
void lexer_destroy(lexer_t *lexer);

/* ============================================================================
 * Main Lexing Function
 * ============================================================================ */

/**
 * Tokenize the input and append tokens to the output list.
 * Returns the number of tokens read via num_tokens_read (if not NULL).
 * Returns the status of the last tokenization attempt:
 * LEX_OK on success, LEX_ERROR on syntax error, or LEX_INCOMPLETE
 * if more input is needed (e.g., unclosed quote).
 *
 * Ownership of any successfully processed tokens in the output list is transferred to the caller.
 * On error, the lexer's error_msg field contains details.
 */
lex_status_t lexer_tokenize(lexer_t *lexer, token_list_t *out_tokens, int *num_tokens_read);

/**
 * Process the next token from the input, storing it internally.
 * Returns LEX_OK if a token was processed, LEX_ERROR on error,
 * or LEX_INCOMPLETE if more input is needed.
 */
lex_status_t lexer_process_one_token(lexer_t *lexer);

/**
 * Pop the first completed token from the lexer's token list.
 * The caller takes ownership of the returned token.
 * Returns NULL if no tokens are available.
 */
token_t *lexer_pop_first_token(lexer_t *lexer);
/* ============================================================================
 * Mode Stack Functions
 * ============================================================================ */

/**
 * Push a new mode onto the mode stack.
 * Returns 0 on success, -1 on allocation failure.
 */
void lexer_push_mode(lexer_t *lexer, lex_mode_t mode);

/**
 * Pop the current mode from the stack.
 * Returns the popped mode, or LEX_NORMAL if stack is empty.
 */
lex_mode_t lexer_pop_mode(lexer_t *lexer);

/**
 * Get the current lexer mode (top of stack).
 * Returns LEX_NORMAL if stack is empty.
 */
lex_mode_t lexer_current_mode(const lexer_t *lexer);

/**
 * Check if we're at a specific depth in a specific mode.
 */
bool lexer_in_mode(const lexer_t *lexer, lex_mode_t mode);

int lexer_builder_push_word(lexer_t *lx, token_t *word);
int lexer_builder_push_nested(lexer_t *lx, part_type_t type); // for $(, $((, `
int lexer_builder_push_complex_param(lexer_t *lx, param_subtype_t kind, const string_t *param_name);
void lexer_builder_pop(lexer_t *lx); // call on ), }, `

/* ============================================================================
 * Character Access Functions
 * ============================================================================ */

/**
 * Get the current character without advancing.
 * Returns '\0' if at end of input.
 */
char lexer_peek(const lexer_t *lexer);

/**
 * Get the character at offset from current position.
 * Returns '\0' if beyond end of input.
 */
char lexer_peek_ahead(const lexer_t *lexer, int offset);

/**
 * Get the current character and advance position.
 * Returns '\0' if at end of input.
 */
char lexer_advance(lexer_t *lexer);

/**
 * Check if we're at the end of input.
 */
bool lexer_at_end(const lexer_t *lexer);

/**
 * Move back one character (used for lookahead correction).
 */
#if 0
void lexer_rewind_one(lexer_t *lexer);
#endif

/* ============================================================================
 * Token Building Functions
 * ============================================================================ */

/**
 * Start building a new WORD token.
 * Records the starting position for error messages.
 */
void lexer_start_word(lexer_t *lexer);

/**
 * Append a literal character to the current word token.
 */
void lexer_append_literal_char_to_word(lexer_t *lexer, char c);

/**
 * Append a literal string to the current word token.
 */
void lexer_append_literal_cstr_to_word(lexer_t *lexer, const char *str);

/**
 * Finalize the current WORD token and add it to the output list.
 */
void lexer_finalize_word(lexer_t *lexer);

/**
 * Emit a non-WORD token (operator, reserved word, etc.).
 */
void lexer_emit_token(lexer_t *lexer, token_type_t type);

/* ============================================================================
 * Operator Recognition Functions
 * ============================================================================ */

/**
 * Try to recognize an operator starting at the current position.
 * If successful, emits the operator token and returns true.
 * Otherwise returns false and position is unchanged.
 */
bool lexer_try_operator(lexer_t *lexer);

/**
 * Check if the current position starts with a specific operator.
 */
bool lexer_input_starts_with(const lexer_t *lexer, const char *str);

/* ============================================================================
 * Heredoc Functions
 * ============================================================================ */

/**
 * Queue a heredoc for later reading.
 * Called when we encounter << or <<- operator.
 */
void lexer_queue_heredoc(lexer_t *lexer, const string_t *delimiter, bool strip_tabs, bool delimiter_quoted);

/**
 * Read all queued heredoc bodies.
 * This is called after we've finished tokenizing the command line
 * and before returning the tokens to the parser.
 * Returns LEX_OK on success, LEX_ERROR or LEX_INCOMPLETE on failure.
 */
lex_status_t lexer_read_heredocs(lexer_t *lexer);

/* ============================================================================
 * Quote Processing Functions
 * ============================================================================ */

/**
 * Process characters inside single quotes.
 * Everything is literal until the closing quote.
 */
lex_status_t lexer_process_single_quote(lexer_t *lexer);

/**
 * Process characters inside double quotes.
 * Allows $, `, \, and nested expansions.
 */
lex_status_t lexer_process_double_quote(lexer_t *lexer);

/* ============================================================================
 * Expansion Processing Functions
 * ============================================================================ */

/**
 * Process parameter expansion: $var or ${...}
 */
lex_status_t lexer_process_parameter_expansion(lexer_t *lexer);

/**
 * Process command substitution: $(...) or `...`
 */
lex_status_t lexer_process_command_substitution(lexer_t *lexer, bool backtick);

/**
 * Process arithmetic expansion: $((...))
 */
lex_status_t lexer_process_arithmetic_expansion(lexer_t *lexer);

/**
 * Process tilde expansion: ~ or ~user
 */
lex_status_t lexer_process_tilde_expansion(lexer_t *lexer);

/* ============================================================================
 * Whitespace and Delimiter Handling
 * ============================================================================ */

/**
 * Skip whitespace (spaces and tabs).
 * Returns the number of whitespace characters skipped.
 */
int lexer_skip_whitespace(lexer_t *lexer);

/**
 * Check if the current character is a word delimiter.
 * Delimiters are: space, tab, newline, operators, etc.
 */
bool lexer_is_delimiter(const lexer_t *lexer, char c);

/**
 * Check if the current character can start a word.
 */
bool lexer_can_start_word(const lexer_t *lexer, char c);

/* ============================================================================
 * Error Handling
 * ============================================================================ */

/**
 * Set an error message with the current position.
 */
void lexer_set_error(lexer_t *lexer, const char *format, ...);

/**
 * Get the error message from the last failed operation.
 * Returns NULL if no error.
 */
const char *lexer_get_error(const lexer_t *lexer);

/**
 * Clear the error state.
 */
void lexer_clear_error(lexer_t *lexer);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Check if a character is a shell metacharacter.
 */
bool lexer_is_metachar(char c);

/**
 * Check if a character is a quote character (' or ").
 */
bool lexer_is_quote(char c);

/**
 * Check if we're in a quoted context (single or double quotes).
 */
bool lexer_in_quotes(const lexer_t *lexer);

/**
 * Get a debug string representation of the lexer state.
 * Useful for debugging and testing.
 */
string_t *lexer_debug_string(const lexer_t *lexer);

// Implemented in lexer_normal.c
// lex_status_t lexer_process_normal(lexer_t *lx, token_t **out_token);

#endif /* LEXER_H */

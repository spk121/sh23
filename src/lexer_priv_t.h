#ifndef LEXER_PRIV_T_H
#define LEXER_PRIV_T_H

#ifndef LEXER_INTERNAL
#error "This is a private header. Do not include it directly; include lexer.h instead."
#endif

/**
 * @file lexer_priv_t.h
 * @brief Private types and internal API for the POSIX shell lexer
 *
 * This header is for the INTERNAL use of lexer.c and the lexer_*.c
 * sub-modules ONLY.  External code must never include this file;
 * it should include lexer.h instead.
 *
 * Because this is internal, the functions declared here are free to
 * manipulate lexer_t and its member structures directly — no const /
 * deep-copy discipline is required at this layer.
 */

#include "lexer_t.h" /* lex_status_t, forward decl of lexer_t */
#include "string_t.h"
#include "token.h"
#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * Constants (internal)
 * ============================================================================ */

static const int LEXER_INITIAL_STACK_CAPACITY = 8;
static const int LEXER_INITIAL_HEREDOC_CAPACITY = 4;

/**
 * When dropping processed input, consider reallocating
 * if the input string has a large amount of unused capacity.
 */
static const int LEXER_LARGE_UNUSED_INPUT_THRESHOLD = 8192;

/**
 * When resizing down the input string, add some
 * padding to avoid frequent reallocations.
 */
static const int LEXER_INPUT_RESIZE_PADDING = 1024;

/* ============================================================================
 * Lexer Modes (for mode stack) — internal
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
 * Mode Stack (for tracking nested contexts) — internal
 * ============================================================================ */

typedef struct
{
    lex_mode_t *modes; // stack of modes
    int capacity;      // allocated capacity
    int size;          // current depth
} lex_mode_stack_t;

/* ============================================================================
 * Heredoc Queue (for pending heredoc bodies) — internal
 * ============================================================================ */

typedef struct
{
    string_t *delimiter;   // the delimiter to look for
    int token_index;       // index in output tokens where this heredoc belongs
    bool strip_tabs;       // true for <<-, false for <<
    bool delimiter_quoted; // was delimiter quoted (affects expansion)
} heredoc_entry_t;

typedef struct
{
    heredoc_entry_t *entries; // array of pending heredocs
    int capacity;             // allocated capacity
    int size;                 // number of pending heredocs
} heredoc_queue_t;

/* ============================================================================
 * Forward declarations — internal
 * ============================================================================ */

typedef struct builder_stack_t builder_stack_t;
typedef struct builder_frame_t builder_frame_t;

/* ============================================================================
 * Nested Expansion Builder Stack — internal
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
 * Lexer Context (full struct definition) — internal
 * ============================================================================ */

struct lexer_t
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
    int heredoc_index;             // which heredoc we're currently reading
    bool reading_heredoc;          // true when reading heredoc body

    /* Character escape state */
    bool escaped; // next char is escaped by backslash

    /* Operator recognition */
    string_t *operator_buffer; // for multi-char operators like &&, <<, etc.

    /* Context for reserved word recognition */
    bool at_command_start; // true if next word could be a reserved word
    bool after_case_in;    // special context for case...in patterns

    /* alias_t expansion state (if you implement aliases) */
    bool check_next_for_alias; // set when alias ends in blank

    /* Error reporting */
    string_t *error_msg; // detailed error message if LEX_ERROR
    int error_line;      // line number of error
    int error_col;       // column number of error
};

/* ============================================================================
 * Internal Lifecycle / Input Helpers
 * ============================================================================ */

void lexer_set_line_no(lexer_t *lx, int line_no);
void lexer_drop_processed_input(lexer_t *lx);

/* ============================================================================
 * Internal: Main Lexing Dispatch
 * ============================================================================ */

/**
 * Process the next token from the input, storing it internally.
 * Returns LEX_OK if a token was processed, LEX_ERROR on error,
 * or LEX_INCOMPLETE if more input is needed.
 */
lex_status_t lexer_process_one_token(lexer_t *lx);

/**
 * Pop the first completed token from the lexer's token list.
 * The caller takes ownership of the returned token.
 * Returns NULL if no tokens are available.
 */
token_t *lexer_pop_first_token(lexer_t *lx);

/* ============================================================================
 * Internal: Mode Stack
 * ============================================================================ */

void lexer_push_mode(lexer_t *lx, lex_mode_t mode);
lex_mode_t lexer_pop_mode(lexer_t *lx);
lex_mode_t lexer_current_mode(const lexer_t *lx);
bool lexer_in_mode(const lexer_t *lx, lex_mode_t mode);

/* ============================================================================
 * Internal: Builder Stack
 * ============================================================================ */

int lexer_builder_push_word(lexer_t *lx, token_t *word);
int lexer_builder_push_nested(lexer_t *lx, part_type_t type);
int lexer_builder_push_complex_param(lexer_t *lx, param_subtype_t kind, const string_t *param_name);
void lexer_builder_pop(lexer_t *lx);

/* ============================================================================
 * Internal: Character Access
 * ============================================================================ */

char lexer_peek(const lexer_t *lx);
char lexer_peek_ahead(const lexer_t *lx, int offset);
bool lexer_input_starts_with(const lexer_t *lx, const char *str);
bool lexer_input_has_substring_at(const lexer_t *lx, const char *str, int position);
bool lexer_input_starts_with_integer(const lexer_t *lx);
int lexer_peek_integer(const lexer_t *lx, int *digit_count);
char lexer_advance(lexer_t *lx);
void lexer_advance_n_chars(lexer_t *lx, int n);
bool lexer_at_end(const lexer_t *lx);

/* ============================================================================
 * Internal: Token Building
 * ============================================================================ */

void lexer_start_word(lexer_t *lx);
void lexer_append_literal_char_to_word(lexer_t *lx, char c);
void lexer_append_literal_cstr_to_word(lexer_t *lx, const char *str);
void lexer_finalize_word(lexer_t *lx);
void lexer_emit_token(lexer_t *lx, token_type_t type);
void lexer_emit_io_number_token(lexer_t *lx, int io_number);
void lexer_emit_io_location_token(lexer_t *lx, const char *io_location);

/* ============================================================================
 * Internal: Operator Recognition
 * ============================================================================ */

bool lexer_try_operator(lexer_t *lx);

/* ============================================================================
 * Internal: Heredoc
 * ============================================================================ */

bool lexer_previous_token_was_newline(const lexer_t *lx);
void lexer_queue_heredoc(lexer_t *lx, const string_t *delimiter, bool strip_tabs,
                         bool delimiter_quoted);
void lexer_empty_heredoc_queue(lexer_t *lx);
lex_status_t lexer_read_heredocs(lexer_t *lx);

/* ============================================================================
 * Internal: Quote Processing
 * ============================================================================ */

lex_status_t lexer_process_single_quote(lexer_t *lx);
lex_status_t lexer_process_double_quote(lexer_t *lx);

/* ============================================================================
 * Internal: Expansion Processing
 * ============================================================================ */

lex_status_t lexer_process_parameter_expansion(lexer_t *lx);
lex_status_t lexer_process_command_substitution(lexer_t *lx, bool backtick);
lex_status_t lexer_process_arithmetic_expansion(lexer_t *lx);
lex_status_t lexer_process_tilde_expansion(lexer_t *lx);

/* ============================================================================
 * Internal: Whitespace and Delimiter Handling
 * ============================================================================ */

int lexer_skip_whitespace(lexer_t *lx);
bool lexer_is_delimiter(const lexer_t *lx, char c);
bool lexer_can_start_word(const lexer_t *lx, char c);

/* ============================================================================
 * Internal: Error Handling
 * ============================================================================ */

void lexer_set_error(lexer_t *lx, const char *format, ...);
bool lexer_has_error(const lexer_t *lx);
void lexer_clear_error(lexer_t *lx);

/* ============================================================================
 * Internal: Utility
 * ============================================================================ */

bool lexer_is_metachar(char c);
bool lexer_is_quote(char c);
bool lexer_in_quotes(const lexer_t *lx);
string_t *lexer_debug_string(const lexer_t *lx);

#endif /* LEXER_PRIV_T_H */

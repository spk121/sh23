#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>

#include "lexer.h"

#define LEXER_INTERNAL
#include "lexer_arith_exp.h"
#include "lexer_cmd_subst.h"
#include "lexer_dquote.h"
#include "lexer_heredoc.h"
#include "lexer_normal.h"
#include "lexer_param_exp.h"
#include "lexer_squote.h"
#include "logging.h"
#include "string_t.h"
#include "token.h"
#include "xalloc.h"

/* ============================================================================
 * Lexer Lifecycle Functions
 * ============================================================================ */

lexer_t *lexer_create(void)
{
    lexer_t *lx = xcalloc(1, sizeof(lexer_t));

    lx->input = string_create();
    lx->pos = 0;
    lx->line_no = 1;
    lx->col_no = 1;
    lx->tok_start_line = 1;
    lx->tok_start_col = 1;

    lx->mode_stack.modes = xcalloc(LEXER_INITIAL_STACK_CAPACITY, sizeof(lex_mode_t));
    lx->mode_stack.capacity = LEXER_INITIAL_STACK_CAPACITY;
    lx->mode_stack.size = 0;

    lx->current_token = NULL;
    lx->in_word = false;
    lx->tokens = token_list_create();

    lx->heredoc_queue.entries = xcalloc(LEXER_INITIAL_HEREDOC_CAPACITY, sizeof(heredoc_entry_t));
    lx->heredoc_queue.capacity = LEXER_INITIAL_HEREDOC_CAPACITY;
    lx->heredoc_queue.size = 0;
    lx->reading_heredoc = false;
    lx->heredoc_index = 0;

    lx->escaped = false;
    lx->operator_buffer = string_create();

    lx->at_command_start = true;
    lx->after_case_in = false;
    lx->check_next_for_alias = false;

    lx->error_msg = NULL;
    lx->error_line = 0;
    lx->error_col = 0;

    return lx;
}

void lexer_reset(lexer_t *lx)
{
    Expects_not_null(lx);
    Expects_not_null(lx->input);

    string_clear(lx->input);
    lx->pos = 0;
    lx->line_no = 1;
    lx->col_no = 1;
    lx->tok_start_line = 1;
    lx->tok_start_col = 1;

    while (lx->mode_stack.size > 0)
        lexer_pop_mode(lx);

    if (lx->tokens)
        token_list_clear(lx->tokens);

    if (lx->heredoc_queue.size > 0)
    {
        for (int i = 0; i < lx->heredoc_queue.size; i++)
        {
            if (lx->heredoc_queue.entries[i].delimiter)
                string_destroy(&lx->heredoc_queue.entries[i].delimiter);
        }
        lx->heredoc_queue.size = 0;
    }

    // Destroy any in-progress token so it isn't leaked if reset is called
    // mid-tokenization (e.g. on a syntax error or interactive line discard).
    if (lx->current_token)
    {
        token_destroy(&lx->current_token);
        lx->current_token = NULL;
    }

    lx->in_word = false;
    lx->escaped = false;
    lx->at_command_start = true;
    lx->after_case_in = false;
    lx->check_next_for_alias = false;

    if (lx->operator_buffer)
        string_clear(lx->operator_buffer);

    if (lx->error_msg)
        string_clear(lx->error_msg);
    lx->error_line = 0;
    lx->error_col = 0;

    log_debug("lexer_reset: Lexer %p reset to initial state.", (void *)lx);
    // FIXME: consider shrinking the queues and buffers
    // if the capacities are too large.
}

void lexer_append_input(lexer_t *lx, const string_t *input)
{
    Expects_not_null(lx);
    Expects_not_null(input);

    string_append(lx->input, input);
}

lexer_t *lexer_append_input_cstr(lexer_t *lx, const char *input)
{
    Expects_not_null(lx);
    Expects_not_null(input);

    string_append_cstr(lx->input, input);
    return lx;
}

void lexer_set_line_no(lexer_t *lx, int line_no)
{
    Expects_not_null(lx);
    lx->line_no = line_no;
    lx->col_no = 1;
}

void lexer_drop_processed_input(lexer_t *lx)
{
    Expects_not_null(lx);
    Expects_not_null(lx->input);

    if (lx->pos > 0)
    {
        string_t *new_input = string_substring(lx->input, lx->pos, string_length(lx->input));
        string_move(lx->input, new_input);
        string_destroy(&new_input);
        lx->pos = 0;

        if (string_capacity(lx->input) - string_length(lx->input) >
            LEXER_LARGE_UNUSED_INPUT_THRESHOLD)
        {
            // If there's a lot of unused capacity, shrink the string
            string_resize(lx->input, string_length(lx->input) + LEXER_INPUT_RESIZE_PADDING);
        }
    }
}

void lexer_destroy(lexer_t **lx)
{
    Expects_not_null(lx);
    lexer_t *l = *lx;

    if (!l)
        return;

    if (l->input)
    {
        string_destroy(&l->input);
        l->input = NULL;
    }
    if (l->mode_stack.modes)
        xfree(l->mode_stack.modes);

    // Destroy any in-progress token that was never finalized
    if (l->current_token)
    {
        token_destroy(&l->current_token);
        l->current_token = NULL;
    }

    if (l->tokens)
        token_list_destroy(&l->tokens);

    // Free the delimiter strings inside each heredoc entry before freeing
    // the entries array. Previously the array was freed directly, leaking all
    // delimiter string_t allocations still in the queue.
    if (l->heredoc_queue.entries)
    {
        lexer_empty_heredoc_queue(l);
        xfree(l->heredoc_queue.entries);
    }

    if (l->operator_buffer)
        string_destroy(&l->operator_buffer);
    if (l->error_msg)
        string_destroy(&l->error_msg);
    xfree(l);
    *lx = NULL;
}

/* ============================================================================
 * Main Lexing Functions
 * ============================================================================ */

lex_status_t lexer_tokenize(lexer_t *lx, token_list_t *out_tokens, int *num_tokens_read)
{
    return_val_if_null(lx, LEX_INTERNAL_ERROR);
    return_val_if_null(out_tokens, LEX_INTERNAL_ERROR);

    if (num_tokens_read)
        *num_tokens_read = 0;
    lex_status_t status;
    while ((status = lexer_process_one_token(lx)) == LEX_OK)
    {
        token_t *tok;
        while ((tok = lexer_pop_first_token(lx)) != NULL)
        {
            if (token_get_type(tok) == TOKEN_EOF)
            {
                // TOKEN_EOF is popped (ownership transferred to tok) but
                // was previously neither appended to out_tokens nor destroyed,
                // leaking the allocation. Destroy it here before returning.
                token_destroy(&tok);
                return LEX_OK;
            }
            token_list_append(out_tokens, tok);
            if (num_tokens_read)
                (*num_tokens_read)++;
        }
    }
    return status;
}

/* ============================================================================
 * Mode Stack Functions
 * ============================================================================ */

void lexer_push_mode(lexer_t *lx, lex_mode_t mode)
{
    Expects_not_null(lx);

    if (lx->mode_stack.size >= lx->mode_stack.capacity)
    {
        int newcap = lx->mode_stack.capacity * 2;
        lex_mode_t *newmodes = xrealloc(lx->mode_stack.modes, newcap * sizeof(lex_mode_t));
        lx->mode_stack.modes = newmodes;
        lx->mode_stack.capacity = newcap;
    }
    lx->mode_stack.modes[lx->mode_stack.size++] = mode;
}

lex_mode_t lexer_pop_mode(lexer_t *lx)
{
    Expects_not_null(lx);

    // Underflow returns LEX_NORMAL silently. This prevents crashes but masks
    // push/pop mismatches; a log_warn here aids debugging during development.
    if (lx->mode_stack.size == 0)
    {
        log_warn("lexer_pop_mode: pop on empty mode stack — possible push/pop mismatch");
        return LEX_NORMAL;
    }
    return lx->mode_stack.modes[--lx->mode_stack.size];
}

lex_mode_t lexer_current_mode(const lexer_t *lx)
{
    Expects_not_null(lx);

    if (lx->mode_stack.size == 0)
        return LEX_NORMAL;
    return lx->mode_stack.modes[lx->mode_stack.size - 1];
}

bool lexer_in_mode(const lexer_t *lx, lex_mode_t mode)
{
    Expects_not_null(lx);

    for (int i = 0; i < lx->mode_stack.size; i++)
    {
        if (lx->mode_stack.modes[i] == mode)
            return true;
    }
    return false;
}

/* ============================================================================
 * Character Access Functions
 * ============================================================================ */

char lexer_peek(const lexer_t *lx)
{
    Expects_not_null(lx);
    Expects_not_null(lx->input);

    if (lx->pos >= string_length(lx->input))
        return '\0';
    return string_at(lx->input, lx->pos);
}

char lexer_peek_ahead(const lexer_t *lx, int offset)
{
    Expects_not_null(lx);
    Expects_not_null(lx->input);

    if (lx->pos + offset >= string_length(lx->input))
        return '\0';
    return string_at(lx->input, lx->pos + offset);
}

bool lexer_input_starts_with(const lexer_t *lx, const char *str)
{
    Expects_not_null(lx);
    Expects_not_null(str);
    Expects_lt(strlen(str), (size_t)INT_MAX);

    int len = (int)strlen(str);
    if (lx->pos + len > string_length(lx->input))
        return false;
    return strncmp(&string_data(lx->input)[lx->pos], str, len) == 0;
}

bool lexer_input_has_substring_at(const lexer_t *lx, const char *str, int position)
{
    Expects_not_null(lx);
    Expects_not_null(str);
    Expects_ge(position, 0);
    Expects_gt(strlen(str), 0);
    Expects_lt(strlen(str), (size_t)INT_MAX);

    int len = (int)strlen(str);
    if (lx->pos + position + len > string_length(lx->input))
        return false;
    const char *input_data = string_data(lx->input) + lx->pos + position;
    return (strncmp(input_data, str, len) == 0);
}

bool lexer_input_starts_with_integer(const lexer_t *lx)
{
    Expects_not_null(lx);
    Expects_not_null(lx->input);

    if (lx->pos >= string_length(lx->input))
        return false;
    char c = string_at(lx->input, lx->pos);
    return isdigit(c);
}

int lexer_peek_integer(const lexer_t *lx, int *digit_count)
{
    Expects_not_null(lx);
    Expects_not_null(lx->input);
    Expects_not_null(digit_count);

    int value = 0;
    int count = 0;
    int pos = lx->pos;
    while (pos < string_length(lx->input))
    {
        char c = string_at(lx->input, pos);
        if (!isdigit(c))
            break;
        if (value > (INT_MAX - (c - '0')) / 10)
            break; // prevent overflow
        value = value * 10 + (c - '0');
        count++;
        pos++;
    }
    *digit_count = count;
    return value;
}

char lexer_advance(lexer_t *lx)
{
    Expects_not_null(lx);
    Expects_not_null(lx->input);
    Expects_lt(lx->pos, string_length(lx->input));

    char c = string_at(lx->input, lx->pos++);
    // lexer_advance is the single authoritative place for
    // line/column tracking. All sub-modules (dquote, heredoc, arith_exp) that
    // previously did `lx->line_no++; lx->col_no = 1` after calling
    // lexer_advance on a '\n' were double-counting the newline. Those manual
    // increments have been removed from those modules; this function is the
    // only place that updates line_no/col_no.
    if (c == '\n')
    {
        lx->line_no++;
        lx->col_no = 1;
    }
    else
        lx->col_no++;
    return c;
}

void lexer_advance_n_chars(lexer_t *lx, int n)
{
    Expects_not_null(lx);
    Expects_not_null(lx->input);
    Expects_ge(n, 0);
    Expects_le(lx->pos + n, string_length(lx->input));

    for (int i = 0; i < n; i++)
    {
        lexer_advance(lx);
    }
}

bool lexer_at_end(const lexer_t *lx)
{
    Expects_not_null(lx);
    Expects_not_null(lx->input);

    return lx->pos >= string_length(lx->input);
}

#if 0
// FIXME: the logic for rewinding line/col is not correct for multi-line input
void lexer_rewind_one(lexer_t *lx) {
    if (!lx || lx->pos == 0) return;
    lx->pos--;
    lx->col_no--;
}
#endif

/* ============================================================================
 * Token Building Functions
 * ============================================================================ */

void lexer_start_word(lexer_t *lx)
{
    Expects_not_null(lx);
    Expects_eq(lx->current_token, NULL);
    Expects(!lx->in_word);

    lx->current_token = token_create_word();
    lx->tok_start_line = lx->line_no;
    lx->tok_start_col = lx->col_no;
    lx->in_word = true;
}

/**
 * Check if the last part of the current token is an unquoted literal.
 * Used to determine whether to append to existing part or create new one.
 */
static bool lexer_last_part_is_unquoted_literal(lexer_t *lx)
{
    Expects_not_null(lx);
    Expects_not_null(lx->current_token);

    if (lx->current_token->parts->size == 0)
        return false;

    part_t *last_part = token_get_part(lx->current_token, lx->current_token->parts->size - 1);
    return (part_get_type(last_part) == PART_LITERAL && !part_was_single_quoted(last_part) &&
            !part_was_double_quoted(last_part));
}

void lexer_append_literal_char_to_word(lexer_t *lx, char c)
{
    Expects_not_null(lx);
    Expects_not_null(lx->current_token);

    // Only append to existing part if it's an unquoted literal
    if (lexer_last_part_is_unquoted_literal(lx))
        token_append_char_to_last_literal_part(lx->current_token, c);
    else
    {
        // Create new literal part (unquoted)
        char buf[2] = {c, '\0'};
        string_t *s = string_create_from_cstr(buf);
        token_add_literal_part(lx->current_token, s);
        string_destroy(&s);
    }
}

void lexer_append_literal_cstr_to_word(lexer_t *lx, const char *str)
{
    Expects_not_null(lx);
    Expects_not_null(str);
    Expects_gt(strlen(str), 0);
    Expects_not_null(lx->current_token);

    // Only append to existing part if it's an unquoted literal
    if (lexer_last_part_is_unquoted_literal(lx))
    {
        token_append_cstr_to_last_literal_part(lx->current_token, str);
    }
    else
    {
        // Create new literal part (unquoted)
        string_t *s = string_create_from_cstr(str);
        token_add_literal_part(lx->current_token, s);
        string_destroy(&s);
    }
}

// Gotta search this word for assignments. There's an assignment
// if
// - the first part is a literal that starts with a valid name
// - has an equals sign in a non-initial position which was found before
//   an escape had been found
// - the equals is followed by more text in the literal part or there
//   are other parts that follow.
//
// If that is true,
// - The current word is promoted to an ASSIGNMENT_WORD
// - The text before the equals is moved to the token->assignment_name
// - The text that follows the equals is moved into a separate LITERAL
//   part placed at the beginning of the token->assignment_parts
// - The remaining parts are appended to the token->assignment_value
static bool try_promote_to_assignment(token_t *tok)
{
    if (!tok->has_equals_before_quote)
        return false;
    if (part_list_size(tok->parts) == 0)
        return false;
    part_list_t *parts = tok->parts;
    part_t *first_part = part_list_get(parts, 0);
    if (first_part->type != PART_LITERAL)
        return false;
    int idx;
    idx = string_find_cstr(first_part->text, "=");
    if (idx < 1)
        return false;
    bool equals_at_end = (idx == string_length(first_part->text) - 1);
    if (equals_at_end && part_list_size(parts) == 1)
        return false;

    // OK, can promote to assignment.
    tok->type = TOKEN_ASSIGNMENT_WORD;
    tok->assignment_name = string_substring(first_part->text, 0, idx);
    tok->assignment_value = part_list_create();
    if (!equals_at_end)
    {
        string_t *after_eq =
            string_substring(first_part->text, idx + 1, string_length(first_part->text));
        part_t *part_after_eq = part_create_literal(after_eq);
        part_list_append(tok->assignment_value, part_after_eq);
        string_destroy(&after_eq);
    }

    // Transfer parts 1..n to assignment_value. The pointers are moved out of
    // tok->parts before it is cleared, so there is no double-ownership.
    for (int i = 1; i < part_list_size(parts); ++i)
    {
        part_t *p = part_list_get(parts, i);
        // NULL out the slot in the original list before we clear it so that
        // part_list_destroy (if ever called on tok->parts) cannot reach them.
        parts->parts[i] = NULL;
        part_list_append(tok->assignment_value, p);
    }

    // First_part (index 0) was never destroyed after its text was
    // consumed into assignment_name and (optionally) part_after_eq.
    // Destroy it now before clearing the list.
    part_destroy(&first_part);
    parts->parts[0] = NULL;

    // Clear the original parts list. All pointers have been nulled above so
    // nothing is freed twice when the list is eventually destroyed.
    tok->parts->size = 0;
    return true;
}

void lexer_finalize_word(lexer_t *lx)
{
    Expects_not_null(lx);
    Expects_not_null(lx->current_token);

    // FIXME: Not for the lexer to decide whether expansions are needed.
#if 0
    if (lx->at_command_start)
    {
        token_try_promote_to_reserved_word(lx->current_token, lx->after_case_in);
    }
#endif
    try_promote_to_assignment(lx->current_token);

    // Recompute expansion flags based on the parts' quoted flags
    token_recompute_expansion_flags(lx->current_token);

    token_set_location(lx->current_token, lx->tok_start_line, lx->tok_start_col, lx->line_no,
                       lx->col_no);

    // Transfer ownership of the current token to the token list
    token_list_append(lx->tokens, lx->current_token);
    lx->current_token = NULL;
    lx->in_word = false;
    lx->at_command_start = false;
}

void lexer_emit_token(lexer_t *lx, token_type_t type)
{
    Expects_not_null(lx);
    Expects_eq(lx->current_token, NULL);
    Expects_ne(type, TOKEN_WORD);
    Expects_ne(type, TOKEN_IO_NUMBER);

    token_t *tok = token_create(type);
    token_set_location(tok, lx->line_no, lx->col_no, lx->line_no, lx->col_no);
    token_list_append(lx->tokens, tok);

    // Is this the logical place for this?
    lx->at_command_start = (type == TOKEN_SEMI || type == TOKEN_NEWLINE || type == TOKEN_AND_IF ||
                            type == TOKEN_OR_IF || type == TOKEN_PIPE);
}

void lexer_emit_io_number_token(lexer_t *lx, int io_number)
{
    Expects_not_null(lx);
    Expects_eq(lx->current_token, NULL);
    Expects_ge(io_number, 0);

    token_t *tok = token_create(TOKEN_IO_NUMBER);
    token_set_io_number(tok, io_number);
    token_set_location(tok, lx->line_no, lx->col_no, lx->line_no, lx->col_no);
    token_list_append(lx->tokens, tok);
}

void lexer_emit_io_location_token(lexer_t *lx, const char *io_location)
{
    Expects_not_null(lx);
    Expects_not_null(io_location);

    token_t *tok = token_create(TOKEN_IO_LOCATION);
    tok->io_location = string_create_from_cstr(io_location);
    token_set_location(tok, lx->line_no, lx->col_no, lx->line_no, lx->col_no);
    token_list_append(lx->tokens, tok);
}

/* ============================================================================
 * Operator Recognition Functions
 * ============================================================================ */

#if 0
bool lexer_try_operator(lexer_t *lx) {
    // Stub: real implementation in operator module
    return false;
}
#endif

/* ============================================================================
 * Heredoc Functions
 * ============================================================================ */

void lexer_queue_heredoc(lexer_t *lx, const string_t *delimiter, bool strip_tabs,
                         bool delimiter_quoted)
{
    Expects_not_null(lx);
    Expects_not_null(delimiter);

    if (lx->heredoc_queue.size >= lx->heredoc_queue.capacity)
    {
        int newcap = lx->heredoc_queue.capacity * 2;
        heredoc_entry_t *newentries =
            xrealloc(lx->heredoc_queue.entries, newcap * sizeof(heredoc_entry_t));
        lx->heredoc_queue.entries = newentries;
        lx->heredoc_queue.capacity = newcap;
    }

    heredoc_entry_t *entry = &lx->heredoc_queue.entries[lx->heredoc_queue.size++];
    entry->delimiter = string_create_from(delimiter);
    entry->strip_tabs = strip_tabs;
    entry->delimiter_quoted = delimiter_quoted;
    entry->token_index = token_list_size(lx->tokens);
}

void lexer_empty_heredoc_queue(lexer_t *lx)
{
    Expects_not_null(lx);

    for (int i = 0; i < lx->heredoc_queue.size; i++)
    {
        heredoc_entry_t *entry = &lx->heredoc_queue.entries[i];
        if (entry->delimiter)
        {
            string_destroy(&entry->delimiter);
            entry->delimiter = NULL;
        }
    }
    lx->heredoc_queue.size = 0;
}

/* ============================================================================
 * Expansion Processing Functions
 * ============================================================================ */

#if 0
lex_status_t lexer_process_parameter_expansion(lexer_t *lx) {
    // Stub
    return LEX_INCOMPLETE;
}

lex_status_t lexer_process_command_substitution(lexer_t *lx, bool backtick) {
    // Stub
    return LEX_INCOMPLETE;
}

lex_status_t lexer_process_arithmetic_expansion(lexer_t *lx) {
    // Stub
    return LEX_INCOMPLETE;
}

lex_status_t lexer_process_tilde_expansion(lexer_t *lx) {
    // Stub
    return LEX_INCOMPLETE;
}
#endif

/* ============================================================================
 * Whitespace and Delimiter Handling
 * ============================================================================ */

int lexer_skip_whitespace(lexer_t *lx)
{
    Expects_not_null(lx);

    int skipped = 0;
    while (!lexer_at_end(lx))
    {
        char c = lexer_peek(lx);
        if (c == ' ' || c == '\t')
        {
            lexer_advance(lx);
            skipped++;
        }
        else
        {
            break;
        }
    }
    return skipped;
}

bool lexer_is_delimiter(const lexer_t *lx, char c)
{
    (void)lx;
    return (c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '&' || c == '|' || c == '<' ||
            c == '>' || c == '(' || c == ')');
}

/* ============================================================================
 * Error Handling
 * ============================================================================ */

void lexer_set_error(lexer_t *lx, const char *format, ...)
{
    Expects_not_null(lx);
    Expects_not_null(format);

    if (lx->error_msg)
    {
        string_destroy(&lx->error_msg);
        lx->error_msg = NULL;
    }
    va_list args;
    va_start(args, format);
    lx->error_msg = string_create();
    string_vprintf(lx->error_msg, format, args);
    va_end(args);
    lx->error_line = lx->line_no;
    lx->error_col = lx->col_no;
}

bool lexer_has_error(const lexer_t *lx)
{
    Expects_not_null(lx);

    return lx->error_msg != NULL;
}

const char *lexer_get_error(const lexer_t *lx)
{
    Expects_not_null(lx);

    if (!lx->error_msg)
        return NULL;
    return string_data(lx->error_msg);
}

void lexer_clear_error(lexer_t *lx)
{
    Expects_not_null(lx);

    if (lx->error_msg)
    {
        string_destroy(&lx->error_msg);
        lx->error_msg = NULL;
    }
    lx->error_line = 0;
    lx->error_col = 0;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

bool lexer_is_metachar(char c)
{
    return (c == '|' || c == '&' || c == ';' || c == '(' || c == ')' || c == '<' || c == '>');
}

bool lexer_is_quote(char c)
{
    return (c == '\'' || c == '"');
}

bool lexer_in_quotes(const lexer_t *lx)
{
    Expects_not_null(lx);

    return lexer_in_mode(lx, LEX_SINGLE_QUOTE) || lexer_in_mode(lx, LEX_DOUBLE_QUOTE);
}

string_t *lexer_debug_string(const lexer_t *lx)
{
    Expects_not_null(lx);

    string_t *dbg = string_create();
    string_printf(dbg, "Lexer(pos=%d, line=%d, col=%d, mode=", (int)lx->pos, lx->line_no,
                  lx->col_no);

    switch (lexer_current_mode(lx))
    {
    case LEX_NORMAL:
        string_append_cstr(dbg, "NORMAL");
        break;
    case LEX_SINGLE_QUOTE:
        string_append_cstr(dbg, "SINGLE_QUOTE");
        break;
    case LEX_DOUBLE_QUOTE:
        string_append_cstr(dbg, "DOUBLE_QUOTE");
        break;
    case LEX_PARAM_EXP_BRACED:
        string_append_cstr(dbg, "PARAM_BRACED");
        break;
    case LEX_PARAM_EXP_UNBRACED:
        string_append_cstr(dbg, "PARAM_UNBRACED");
        break;
    case LEX_CMD_SUBST_PAREN:
        string_append_cstr(dbg, "CMD_SUBST_PAREN");
        break;
    case LEX_CMD_SUBST_BACKTICK:
        string_append_cstr(dbg, "CMD_SUBST_BACKTICK");
        break;
    case LEX_ARITH_EXP:
        string_append_cstr(dbg, "ARITH_EXP");
        break;
    case LEX_HEREDOC_BODY:
        string_append_cstr(dbg, "HEREDOC_BODY");
        break;
    default:
        string_append_cstr(dbg, "UNKNOWN");
        break;
    }

    string_append_cstr(dbg, ")");
    return dbg;
}

/* ============================================================================
 * Builder Stack Functions (stubs for now)
 * ============================================================================ */

int lexer_builder_push_word(lexer_t *lx, token_t *word)
{
    (void)lx;
    (void)word;
    return 0;
}

int lexer_builder_push_nested(lexer_t *lx, part_type_t type)
{
    (void)lx;
    (void)type;
    return 0;
}

int lexer_builder_push_complex_param(lexer_t *lx, param_subtype_t kind, const string_t *param_name)
{
    (void)lx;
    (void)kind;
    (void)param_name;
    return 0;
}

void lexer_builder_pop(lexer_t *lx)
{
    (void)lx;
}

lex_status_t lexer_process_one_token(lexer_t *lx)
{
    Expects_not_null(lx);

    lex_status_t status;
    int initial_token_count = token_list_size(lx->tokens);

    // Loop until we produce a token, need more input, or encounter an error
    while (1)
    {
        switch (lexer_current_mode(lx))
        {
        case LEX_NORMAL:
            status = lexer_process_one_normal_token(lx);
            break;
        case LEX_SINGLE_QUOTE:
            status = lexer_process_squote(lx);
            break;
        case LEX_DOUBLE_QUOTE:
            status = lexer_process_dquote(lx);
            break;
        case LEX_PARAM_EXP_BRACED:
            status = lexer_process_param_exp_braced(lx);
            break;
        case LEX_PARAM_EXP_UNBRACED:
            status = lexer_process_param_exp_unbraced(lx);
            break;
        case LEX_CMD_SUBST_PAREN:
            status = lexer_process_cmd_subst_paren(lx);
            break;
        case LEX_CMD_SUBST_BACKTICK:
            status = lexer_process_cmd_subst_backtick(lx);
            break;
        case LEX_ARITH_EXP:
            status = lexer_process_arith_exp(lx);
            break;
        case LEX_HEREDOC_BODY:
            status = lexer_process_heredoc_body(lx);
            break;
        default:
            lexer_set_error(lx, "Unknown lexer mode");
            return LEX_ERROR;
        }

        // If we got an error, return immediately
        if (status == LEX_ERROR || status == LEX_INTERNAL_ERROR)
        {
            return status;
        }

        // If we got OK:
        // - Check if we've produced tokens - if yes, we're done
        // - If we're back in normal mode, continue to finalize word or find next token
        if (status == LEX_OK)
        {
            if (token_list_size(lx->tokens) > initial_token_count)
            {
                return LEX_OK;
            }

            // If we're in any mode other than NORMAL, we should continue
            // processing (e.g., after param expansion inside double quotes)
            if (lexer_current_mode(lx) != LEX_NORMAL)
            {
                continue;
            }

            // If we're back in normal mode and have an in-progress word,
            // we need to continue to finalize it (even at end of input)
            if (lx->in_word)
            {
                continue;
            }

            // No tokens and no word in progress - return OK
            return LEX_OK;
        }

        // LEX_INCOMPLETE: could mean mode switch or truly need more input
        if (status == LEX_INCOMPLETE)
        {
            // If we've produced any tokens, that counts as success
            if (token_list_size(lx->tokens) > initial_token_count)
            {
                return LEX_OK;
            }

            // If we're at end of input and still incomplete, need more input
            if (lexer_at_end(lx))
            {
                return LEX_INCOMPLETE;
            }

            // Otherwise, continue the loop (mode switch happened)
            continue;
        }
    }
}

token_t *lexer_pop_first_token(lexer_t *lx)
{
    Expects_not_null(lx);
    Expects_not_null(lx->tokens);

    if (lx->tokens->size == 0)
    {
        return NULL;
    }

    token_t *first_token = lx->tokens->tokens[0];
    // Shift remaining tokens forward
    for (int i = 1; i < lx->tokens->size; i++)
    {
        lx->tokens->tokens[i - 1] = lx->tokens->tokens[i];
    }
    lx->tokens->size--;
    return first_token;
}

/* ============================================================================
 * Test functions
 * ============================================================================ */

lexer_t *lexer_create_with_input_cstr(const char *input)
{
    Expects_not_null(input);
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, input);
    return lx;
}

lex_status_t lex_cstr_to_tokens(const char *input, token_list_t *out_tokens)
{
    Expects_not_null(input);
    Expects_not_null(out_tokens);
    lexer_t *lx = lexer_create_with_input_cstr(input);
    if (!lx)
    {
        return LEX_INTERNAL_ERROR;
    }
    lex_status_t status = lexer_tokenize(lx, out_tokens, NULL);
    lexer_destroy(&lx);
    return status;
}

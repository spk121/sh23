#include "lexer.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "string.h"
#include "token.h"
#include "logging.h"
#include "lexer_normal.h"
#include "xalloc.h"

/* ============================================================================ 
 * Lexer Lifecycle Functions
 * ============================================================================ */

lexer_t *lexer_create()
{
    lexer_t *lx = xcalloc(1,sizeof(lexer_t));

    lx->input = string_create_empty(0);
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
    lx->operator_buffer = NULL;

    lx->at_command_start = true;
    lx->after_case_in = false;
    lx->check_next_for_alias = false;

    lx->error_msg = NULL;
    lx->error_line = 0;
    lx->error_col = 0;

    return lx;
}

lexer_t *lexer_append_input_cstr(lexer_t *lx, const char *input)
{
    Expects_not_null(lx);
    Expects_not_null(input);

    string_append_cstr(lx->input, input);
    return lx;
}

void lexer_destroy(lexer_t *lx) {
    Expects_not_null(lx);

    if (lx->input) {
        string_destroy(lx->input);
        lx->input = NULL;
    }
    if (lx->mode_stack.modes) xfree(lx->mode_stack.modes);
    if (lx->tokens) token_list_destroy(lx->tokens);
    if (lx->heredoc_queue.entries) xfree(lx->heredoc_queue.entries);
    if (lx->operator_buffer) string_destroy(lx->operator_buffer);
    if (lx->error_msg) string_destroy(lx->error_msg);
    xfree(lx);
}

/* ============================================================================ 
 * Main Lexing Functions
 * ============================================================================ */

lex_status_t lexer_tokenize(lexer_t *lx, token_list_t *out_tokens, int *num_tokens_read) {
    return_val_if_null(lx, LEX_INTERNAL_ERROR);
    return_val_if_null(out_tokens, LEX_INTERNAL_ERROR);

    if (num_tokens_read) *num_tokens_read = 0;
    lex_status_t status;
    while ((status = lexer_process_one_token(lx)) == LEX_OK) {
        token_t *tok = lexer_pop_first_token(lx);
        if (token_get_type(tok) == TOKEN_EOF) {
            token_destroy(tok);
            break;
        }
        token_list_append(out_tokens, tok);
        if (num_tokens_read) (*num_tokens_read)++;
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

    if (lx->mode_stack.size == 0)
        return LEX_NORMAL;
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
    return string_char_at(lx->input, lx->pos);
}

char lexer_peek_ahead(const lexer_t *lx, int offset)
{
    Expects_not_null(lx);
    Expects_not_null(lx->input);

    if (lx->pos + offset >= string_length(lx->input))
        return '\0';
    return string_char_at(lx->input, lx->pos + offset);
}

char lexer_advance(lexer_t *lx)
{
    Expects_not_null(lx);
    Expects_not_null(lx->input);
    Expects_lt(lx->pos, string_length(lx->input));

    char c = string_char_at(lx->input, lx->pos++);
    if (c == '\n')
    {
        lx->line_no++;
        lx->col_no = 1;
    }
    else lx->col_no++;
    return c;
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

void lexer_start_word(lexer_t *lx) {
    Expects_not_null(lx);
    Expects_eq(lx->current_token, NULL);
    Expects(!lx->in_word);

    lx->current_token = token_create_word();
    lx->tok_start_line = lx->line_no;
    lx->tok_start_col = lx->col_no;
    lx->in_word = true;
}

void lexer_append_literal_char_to_word(lexer_t *lx, char c) {
    Expects_not_null(lx);
    Expects_not_null(lx->current_token);

    if (token_is_last_part_literal(lx->current_token))
        token_append_char_to_last_literal_part(lx->current_token, c);
    else {
        // Create new literal part
        char buf[2] = {c, '\0'};
        string_t *s = string_create_from_cstr(buf);
        token_add_literal_part(lx->current_token, s);
        string_destroy(s);
    }
}

void lexer_append_literal_cstr_to_word(lexer_t *lx, const char *str) {
    Expects_not_null(lx);
    Expects_not_null(str);
    Expects_gt(strlen(str), 0);
    Expects_not_null(lx->current_token);

    if (token_is_last_part_literal(lx->current_token)) {
        token_append_cstr_to_last_literal_part(lx->current_token, str);
    }
    else {
        // Create new literal part
        string_t *s = string_create_from_cstr(str);
        token_add_literal_part(lx->current_token, s);
        string_destroy(s);
    }
}

void lexer_finalize_word(lexer_t *lx) {
    Expects_not_null(lx);
    Expects_not_null(lx->current_token);

    if(lx->at_command_start) {
        token_try_promote_to_reserved_word(lx->current_token, lx->after_case_in);
    }

    // FIXME: there is probably some logic missing for 'case' 'in' patterns

    token_set_location(lx->current_token, lx->tok_start_line, lx->tok_start_col,
                       lx->line_no, lx->col_no);
    token_list_append(lx->tokens, lx->current_token);
    lx->current_token = NULL;
    lx->in_word = false;
    lx->at_command_start = false;
}

void lexer_emit_token(lexer_t *lx, token_type_t type) {
    Expects_not_null(lx);
    Expects_eq(lx->current_token, NULL);

    token_t *tok = token_create(type);
    token_set_location(tok, lx->line_no, lx->col_no, lx->line_no, lx->col_no);
    token_list_append(lx->tokens, tok);

    // Is this the logical place for this?
    lx->at_command_start = (type == TOKEN_SEMI || type == TOKEN_NEWLINE ||
                            type == TOKEN_AND_IF || type == TOKEN_OR_IF ||
                            type == TOKEN_PIPE);
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

bool lexer_input_starts_with(const lexer_t *lx, const char *str) {
    Expects_not_null(lx);
    Expects_not_null(str);
    Expects_gt(strlen(str), 0);

    int len = strlen(str);
    if (lx->pos + len > string_length(lx->input))
        return false;
    return strncmp(&string_data(lx->input)[lx->pos], str, len) == 0;
}


/* ============================================================================ 
 * Heredoc Functions
 * ============================================================================ */

void lexer_queue_heredoc(lexer_t *lx, const string_t *delimiter,
                        bool strip_tabs, bool delimiter_quoted)
{
    Expects_not_null(lx);
    Expects_not_null(delimiter);

    if (lx->heredoc_queue.size >= lx->heredoc_queue.capacity)
    {
        int newcap = lx->heredoc_queue.capacity * 2;
        heredoc_entry_t *newentries = xrealloc(lx->heredoc_queue.entries,
                                             newcap * sizeof(heredoc_entry_t));
        lx->heredoc_queue.entries = newentries;
        lx->heredoc_queue.capacity = newcap;
    }

    heredoc_entry_t *entry = &lx->heredoc_queue.entries[lx->heredoc_queue.size++];
    entry->delimiter = string_clone(delimiter);
    entry->strip_tabs = strip_tabs;
    entry->delimiter_quoted = delimiter_quoted;
    entry->token_index = token_list_size(lx->tokens);
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
    while (!lexer_at_end(lx)) {
        char c = lexer_peek(lx);
        if (c == ' ' || c == '\t') {
            lexer_advance(lx);
            skipped++;
        } else {
            break;
        }
    }
    return skipped;
}

bool lexer_is_delimiter(const lexer_t *lx, char c) {
    (void)lx;
    return (c == ' ' || c == '\t' || c == '\n' ||
            c == ';' || c == '&' || c == '|' ||
            c == '<' || c == '>' || c == '(' || c == ')');
}


/* ============================================================================ 
 * Error Handling
 * ============================================================================ */

void lexer_set_error(lexer_t *lx, const char *format, ...) {
    Expects_not_null(lx);
    Expects_not_null(format);

    if (lx->error_msg) {
        string_destroy(lx->error_msg);
        lx->error_msg = NULL;
    }
    va_list args;
    va_start(args, format);
    lx->error_msg = string_vcreate(format, args);
    va_end(args);
    lx->error_line = lx->line_no;
    lx->error_col = lx->col_no;
}

bool lexer_has_error(const lexer_t *lx) {
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

    if (lx->error_msg) {
        string_destroy(lx->error_msg);
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
    return (c == '|' || c == '&' || c == ';' ||
            c == '(' || c == ')' || c == '<' || c == '>');
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

    string_t *dbg = string_create_from_format("Lexer(pos=%d, line=%d, col=%d, mode=",
                                   (int)lx->pos, lx->line_no, lx->col_no);

    switch (lexer_current_mode(lx))
    {
        case LEX_NORMAL: string_append_cstr(dbg, "NORMAL"); break;
        case LEX_SINGLE_QUOTE: string_append_cstr(dbg, "SINGLE_QUOTE"); break;
        case LEX_DOUBLE_QUOTE: string_append_cstr(dbg, "DOUBLE_QUOTE"); break;
        case LEX_PARAM_EXP_BRACED: string_append_cstr(dbg, "PARAM_BRACED"); break;
        case LEX_PARAM_EXP_UNBRACED: string_append_cstr(dbg, "PARAM_UNBRACED"); break;
        case LEX_CMD_SUBST_PAREN: string_append_cstr(dbg, "CMD_SUBST_PAREN"); break;
        case LEX_CMD_SUBST_BACKTICK: string_append_cstr(dbg, "CMD_SUBST_BACKTICK"); break;
        case LEX_ARITH_EXP: string_append_cstr(dbg, "ARITH_EXP"); break;
        case LEX_HEREDOC_BODY: string_append_cstr(dbg, "HEREDOC_BODY"); break;
        default: string_append_cstr(dbg, "UNKNOWN"); break;
    }

    string_append_cstr(dbg, ")");
    return dbg;
}

/* ============================================================================ 
 * Builder Stack Functions (stubs for now)
 * ============================================================================ */

int lexer_builder_push_word(lexer_t *lx, token_t *word) {
    (void)lx; (void)word;
    return 0;
}

int lexer_builder_push_nested(lexer_t *lx, part_type_t type) {
    (void)lx; (void)type;
    return 0;
}

int lexer_builder_push_complex_param(lexer_t *lx, param_subtype_t kind,
                                     const string_t *param_name) {
    (void)lx; (void)kind; (void)param_name;
    return 0;
}

void lexer_builder_pop(lexer_t *lx) {
    (void)lx;
}

lex_status_t lexer_process_one_token(lexer_t *lx)
{
    Expects_not_null(lx);

    switch (lexer_current_mode(lx)) {
        case LEX_NORMAL:
            return lexer_process_one_normal_token(lx);
#if 0            
        case LEX_SINGLE_QUOTE:
            return lexer_process_single_quote(lx, out_token);
        case LEX_DOUBLE_QUOTE:
            return lexer_process_double_quote(lx, out_token);
        case LEX_PARAM_EXP_BRACED:
            return lexer_process_param_braced(lx, out_token);
        case LEX_PARAM_EXP_UNBRACED:
            return lexer_process_param_unbraced(lx, out_token);
        case LEX_CMD_SUBST_PAREN:
            return lexer_process_cmd_subst_paren(lx, out_token);
        case LEX_CMD_SUBST_BACKTICK:
            return lexer_process_cmd_subst_backtick(lx, out_token);
        case LEX_ARITH_EXP:
            return lexer_process_arith_exp(lx, out_token);
        case LEX_HEREDOC_BODY:
            return lexer_process_heredoc_body(lx, out_token);
#endif            
        default:
            lexer_set_error(lx, "Unknown lexer mode");
            return LEX_ERROR;
    }
}

token_t *lexer_pop_first_token(lexer_t *lx)
{
    Expects_not_null(lx);
    Expects_not_null(lx->tokens);

    if(lx->tokens->size == 0) {
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

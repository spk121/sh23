#include "tokenizer.h"

#include "lexer.h"
#include "logging.h"
#include "string_t.h"
#include "token.h"
#include "xalloc.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

static const int TOKENIZER_MAX_EXPANSION_DEPTH = 32;
static const int TOKENIZER_INITIAL_EXPANDED_ALIASES_CAPACITY = 8;

/* ============================================================================
 * Tokenizer Lifecycle Functions
 * ============================================================================ */

tokenizer_t *tokenizer_create(AliasStore *aliases)
{
    tokenizer_t *tok = xcalloc(1, sizeof(tokenizer_t));

    tok->aliases = aliases; // does not take ownership
    tok->input_tokens = NULL;
    tok->input_pos = 0;
    tok->output_tokens = NULL;

    tok->expansion_depth = 0;
    tok->max_expansion_depth = TOKENIZER_MAX_EXPANSION_DEPTH;

    tok->expanded_aliases = xcalloc(TOKENIZER_INITIAL_EXPANDED_ALIASES_CAPACITY, sizeof(char *));
    tok->expanded_aliases_count = 0;
    tok->expanded_aliases_capacity = TOKENIZER_INITIAL_EXPANDED_ALIASES_CAPACITY;

    tok->error_msg = NULL;
    tok->error_line = 0;
    tok->error_col = 0;

    tok->at_command_position = true; // start at command position

    return tok;
}

void tokenizer_destroy(tokenizer_t *tok)
{
    if (tok == NULL)
        return;

    if (tok->error_msg)
    {
        string_destroy(tok->error_msg);
        tok->error_msg = NULL;
    }

    // Free the expanded_aliases tracking array
    if (tok->expanded_aliases)
    {
        for (int i = 0; i < tok->expanded_aliases_count; i++)
        {
            xfree(tok->expanded_aliases[i]);
        }
        xfree(tok->expanded_aliases);
    }

    xfree(tok);
}

/* ============================================================================
 * Error Handling Functions
 * ============================================================================ */

void tokenizer_set_error(tokenizer_t *tok, const char *format, ...)
{
    Expects_not_null(tok);
    Expects_not_null(format);

    if (tok->error_msg == NULL)
    {
        tok->error_msg = string_create_empty(0);
    }
    else
    {
        string_clear(tok->error_msg);
    }

    va_list args;
    va_start(args, format);
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    string_append_cstr(tok->error_msg, buffer);
}

const char *tokenizer_get_error(const tokenizer_t *tok)
{
    if (tok == NULL || tok->error_msg == NULL)
        return NULL;
    return string_data(tok->error_msg);
}

void tokenizer_clear_error(tokenizer_t *tok)
{
    if (tok == NULL)
        return;
    if (tok->error_msg)
    {
        string_clear(tok->error_msg);
    }
}

/* ============================================================================
 * Recursion Tracking Functions
 * ============================================================================ */

void tokenizer_mark_alias_expanded(tokenizer_t *tok, const char *alias_name)
{
    Expects_not_null(tok);
    Expects_not_null(alias_name);

    // Check if we need to grow the array
    if (tok->expanded_aliases_count >= tok->expanded_aliases_capacity)
    {
        tok->expanded_aliases_capacity *= 2;
        tok->expanded_aliases = xrealloc(tok->expanded_aliases, tok->expanded_aliases_capacity * sizeof(char *));
    }

    // Add the alias name
    tok->expanded_aliases[tok->expanded_aliases_count] = xstrdup(alias_name);
    tok->expanded_aliases_count++;
}

bool tokenizer_is_alias_expanded(const tokenizer_t *tok, const char *alias_name)
{
    Expects_not_null(tok);
    Expects_not_null(alias_name);

    for (int i = 0; i < tok->expanded_aliases_count; i++)
    {
        if (strcmp(tok->expanded_aliases[i], alias_name) == 0)
        {
            return true;
        }
    }
    return false;
}

void tokenizer_clear_expanded_aliases(tokenizer_t *tok)
{
    Expects_not_null(tok);

    for (int i = 0; i < tok->expanded_aliases_count; i++)
    {
        xfree(tok->expanded_aliases[i]);
        tok->expanded_aliases[i] = NULL;
    }
    tok->expanded_aliases_count = 0;
}

/* ============================================================================
 * Alias Expansion Helper Functions
 * ============================================================================ */

bool tokenizer_alias_ends_with_blank(const char *alias_value)
{
    if (alias_value == NULL || *alias_value == '\0')
        return false;

    // Find the last character
    size_t len = strlen(alias_value);
    char last = alias_value[len - 1];

    return (last == ' ' || last == '\t');
}

char *tokenizer_extract_word_text(const token_t *token)
{
    if (token == NULL || token_get_type(token) != TOKEN_WORD)
        return NULL;

    // Only handle simple literal words (no expansions)
    if (token_part_count(token) != 1)
        return NULL;

    part_t *part = token_get_part(token, 0);
    if (part_get_type(part) != PART_LITERAL)
        return NULL;

    const string_t *text = part_get_text(part);
    if (text == NULL)
        return NULL;

    return xstrdup(string_data(text));
}

/* ============================================================================
 * Context Management Functions
 * ============================================================================ */

void tokenizer_update_command_position(tokenizer_t *tok, const token_t *token)
{
    Expects_not_null(tok);
    Expects_not_null(token);

    token_type_t type = token_get_type(token);

    // After these tokens, the next word is at a command position
    switch (type)
    {
    case TOKEN_NEWLINE:
    case TOKEN_SEMI:
    case TOKEN_AMPER:
    case TOKEN_PIPE:
    case TOKEN_AND_IF:
    case TOKEN_OR_IF:
    case TOKEN_LPAREN:
    case TOKEN_DSEMI:
    case TOKEN_IF:
    case TOKEN_THEN:
    case TOKEN_ELSE:
    case TOKEN_ELIF:
    case TOKEN_DO:
    case TOKEN_WHILE:
    case TOKEN_UNTIL:
    case TOKEN_FOR:
    case TOKEN_CASE:
    case TOKEN_LBRACE:
        tok->at_command_position = true;
        // Clear expanded aliases - we're starting a new command
        tokenizer_clear_expanded_aliases(tok);
        break;

    default:
        // After a word or other token, we're no longer at command position
        // (unless the alias ends with a blank, which is handled separately)
        tok->at_command_position = false;
        break;
    }
}

/* ============================================================================
 * Alias Expansion Functions
 * ============================================================================ */

bool tokenizer_is_alias_eligible(const tokenizer_t *tok, const token_t *token)
{
    if (tok == NULL || token == NULL)
        return false;

    // Only expand at command positions
    if (!tok->at_command_position)
        return false;

    // Only expand WORD tokens
    if (token_get_type(token) != TOKEN_WORD)
        return false;

    // Don't expand quoted words
    if (token_was_quoted(token))
        return false;

    // Don't expand if the token has complex parts (expansions)
    if (token_part_count(token) != 1)
        return false;

    part_t *part = token_get_part(token, 0);
    if (part_get_type(part) != PART_LITERAL)
        return false;

    // Don't expand if it was quoted at the part level
    if (part_was_single_quoted(part) || part_was_double_quoted(part))
        return false;

    return true;
}

tok_status_t tokenizer_relex_text(tokenizer_t *tok, const char *text)
{
    Expects_not_null(tok);
    Expects_not_null(text);

    // Create a lexer for the alias expansion text
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, text);

    token_list_t *relexed_tokens = token_list_create();
    int num_tokens_read = 0;
    lex_status_t status = lexer_tokenize(lx, relexed_tokens, &num_tokens_read);

    if (status != LEX_OK)
    {
        tokenizer_set_error(tok, "Failed to re-lex alias expansion: %s", lexer_get_error(lx));
        token_list_destroy(relexed_tokens);
        lexer_destroy(lx);
        return TOK_ERROR;
    }

    // Insert the re-lexed tokens into our input stream at the current position
    // We need to grow the input_tokens array and shift existing tokens
    int num_new_tokens = token_list_size(relexed_tokens);
    
    if (num_new_tokens > 0)
    {
        // Ensure we have enough capacity
        int total_needed = token_list_size(tok->input_tokens) + num_new_tokens;
        if (total_needed > tok->input_tokens->capacity)
        {
            int new_capacity = tok->input_tokens->capacity * 2;
            while (new_capacity < total_needed)
                new_capacity *= 2;
            tok->input_tokens->tokens = xrealloc(tok->input_tokens->tokens, new_capacity * sizeof(token_t *));
            tok->input_tokens->capacity = new_capacity;
        }

        // Shift existing tokens to make room
        for (int i = tok->input_tokens->size - 1; i >= tok->input_pos; i--)
        {
            tok->input_tokens->tokens[i + num_new_tokens] = tok->input_tokens->tokens[i];
        }

        // Insert the new tokens
        for (int i = 0; i < num_new_tokens; i++)
        {
            tok->input_tokens->tokens[tok->input_pos + i] = token_list_get(relexed_tokens, i);
        }
        tok->input_tokens->size += num_new_tokens;

        // Clean up the relexed_tokens list structure (but not the tokens themselves)
        xfree(relexed_tokens->tokens);
        xfree(relexed_tokens);
    }
    else
    {
        token_list_destroy(relexed_tokens);
    }

    lexer_destroy(lx);

    return TOK_OK;
}

tok_status_t tokenizer_expand_alias(tokenizer_t *tok, const char *alias_name)
{
    Expects_not_null(tok);
    Expects_not_null(alias_name);

    // Check expansion depth
    if (tok->expansion_depth >= tok->max_expansion_depth)
    {
        tokenizer_set_error(tok, "Maximum alias expansion depth exceeded");
        return TOK_ERROR;
    }

    // Look up the alias value
    const char *alias_value = alias_store_get_value_cstr(tok->aliases, alias_name);
    if (alias_value == NULL)
    {
        // No alias found - this shouldn't happen if called correctly
        return TOK_OK;
    }

    // Mark this alias as expanded
    tokenizer_mark_alias_expanded(tok, alias_name);
    tok->expansion_depth++;

    // Check if the alias ends with a blank
    bool check_next = tokenizer_alias_ends_with_blank(alias_value);

    // Re-lex the alias value
    tok_status_t status = tokenizer_relex_text(tok, alias_value);

    tok->expansion_depth--;

    if (status != TOK_OK)
    {
        return status;
    }

    // If the alias ends with a blank, the next word should be checked for alias expansion
    if (check_next)
    {
        tok->at_command_position = true;
    }

    return TOK_OK;
}

/* ============================================================================
 * Main Tokenization Functions
 * ============================================================================ */

tok_status_t tokenizer_process_one_token(tokenizer_t *tok)
{
    Expects_not_null(tok);

    // Check if we have input
    if (tok->input_tokens == NULL || tok->input_pos >= token_list_size(tok->input_tokens))
    {
        return TOK_INCOMPLETE;
    }

    // Get the next input token
    token_t *token = token_list_get(tok->input_tokens, tok->input_pos);

    // Check if this token is eligible for alias expansion
    if (tok->aliases != NULL && tokenizer_is_alias_eligible(tok, token))
    {
        // Extract the word text
        char *word_text = tokenizer_extract_word_text(token);
        if (word_text != NULL)
        {
            // Check if there's an alias for this word
            if (alias_store_has_name_cstr(tok->aliases, word_text))
            {
                // Check if already expanded (recursion prevention)
                if (!tokenizer_is_alias_expanded(tok, word_text))
                {
                    // Before expanding, we need to remove this token from input
                    // since it will be replaced by the expansion
                    // token_list_remove will destroy the token
                    token_list_remove(tok->input_tokens, tok->input_pos);

                    // Expand the alias (this will insert new tokens at input_pos)
                    tok_status_t status = tokenizer_expand_alias(tok, word_text);
                    xfree(word_text);

                    if (status != TOK_OK)
                    {
                        return status;
                    }

                    // Don't increment input_pos - continue processing from the same position
                    // which now contains the expanded tokens
                    return TOK_OK;
                }
                // else: alias already expanded, treat as normal word (fall through)
            }
            xfree(word_text);
        }
    }

    // No alias expansion - add token to output and move to next
    tok->input_pos++;
    token_list_append(tok->output_tokens, token);

    // Update command position for next token
    tokenizer_update_command_position(tok, token);

    return TOK_OK;
}

tok_status_t tokenizer_process(tokenizer_t *tok, token_list_t *input_tokens, token_list_t *output_tokens)
{
    return_val_if_null(tok, TOK_INTERNAL_ERROR);
    return_val_if_null(input_tokens, TOK_INTERNAL_ERROR);
    return_val_if_null(output_tokens, TOK_INTERNAL_ERROR);

    // Set up the context
    tok->input_tokens = input_tokens;
    tok->input_pos = 0;
    tok->output_tokens = output_tokens;
    tok->at_command_position = true;
    tokenizer_clear_expanded_aliases(tok);
    tokenizer_clear_error(tok);

    // Process all tokens
    tok_status_t status;
    while (tok->input_pos < token_list_size(tok->input_tokens))
    {
        status = tokenizer_process_one_token(tok);
        if (status != TOK_OK)
        {
            return status;
        }
    }

    // Clear the input tokens array without destroying the tokens
    // (they've been transferred to output)
    // This prevents double-free when the caller destroys input_tokens
    for (int i = 0; i < input_tokens->size; i++)
    {
        input_tokens->tokens[i] = NULL;
    }
    input_tokens->size = 0;

    // Clean up context
    tok->input_tokens = NULL;
    tok->output_tokens = NULL;
    tok->input_pos = 0;

    return TOK_OK;
}

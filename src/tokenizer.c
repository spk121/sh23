#include "tokenizer.h"

#include "lexer.h"
#include "logging.h"
#include "string_t.h"
#include "token.h"
#include "xalloc.h"
#include <ctype.h>
#include <limits.h>
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

tokenizer_t *tokenizer_create(alias_store_t *aliases)
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

    tok->at_command_position = true; // start at command position

    return tok;
}

void tokenizer_destroy(tokenizer_t **tok)
{
    Expects_not_null(tok);
    tokenizer_t *t = *tok;
    Expects_not_null(t);

    if (t->error_msg)
        string_destroy(&t->error_msg);

    // Free the expanded_aliases tracking array
    if (t->expanded_aliases)
    {
        for (int i = 0; i < t->expanded_aliases_count; i++)
        {
            xfree(t->expanded_aliases[i]);
        }
        xfree(t->expanded_aliases);
    }

    xfree(t);
    *tok = NULL;
}

/* ============================================================================
 * Error Handling Functions
 * ============================================================================ */

void tokenizer_set_error(tokenizer_t *tok, const char *format, ...)
{
    Expects_not_null(tok);
    Expects_not_null(format);

    if (tok->error_msg != NULL)
    {
        string_clear(tok->error_msg);
    }

    va_list args;
    va_start(args, format);
    tok->error_msg = string_create();
    string_vprintf(tok->error_msg, format, args);
    va_end(args);
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
        int new_capacity;
        // Check for overflow before doubling
        // We can safely double if capacity <= INT_MAX/2
        if (tok->expanded_aliases_capacity <= INT_MAX / 2)
        {
            new_capacity = tok->expanded_aliases_capacity * 2;
        }
        else
        {
            // Can't double, set to max or fail
            new_capacity = INT_MAX;
            if (tok->expanded_aliases_count >= new_capacity)
            {
                // Can't expand further
                return;
            }
        }
        
        tok->expanded_aliases = xrealloc(tok->expanded_aliases, new_capacity * sizeof(char *));
        tok->expanded_aliases_capacity = new_capacity;
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
 * alias_t Expansion Helper Functions
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

    return xstrdup(string_cstr(text));
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
 * alias_t Expansion Functions
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
        token_list_destroy(&relexed_tokens);
        lexer_destroy(&lx);
        return TOK_ERROR;
    }

    // Insert the re-lexed tokens into our input stream at the current position
    int num_new_tokens = token_list_size(relexed_tokens);
    
    if (num_new_tokens > 0)
    {
        // Detach tokens from relexed_tokens list
        int detached_size;
        token_t **detached_tokens = token_list_release(relexed_tokens, &detached_size);
        
        // Insert them into input_tokens at current position
        int result = token_list_insert_range(tok->input_tokens, tok->input_pos, 
                                              detached_tokens, detached_size);
        
        // Free the detached array (tokens are now owned by input_tokens)
        xfree(detached_tokens);
        
        if (result != 0)
        {
            tokenizer_set_error(tok, "Failed to insert re-lexed tokens");
            token_list_destroy(&relexed_tokens);
            lexer_destroy(&lx);
            return TOK_ERROR;
        }
    }
    
    token_list_destroy(&relexed_tokens);
    lexer_destroy(&lx);

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
        // On error, remove the alias from the expanded list to allow retry
        if (tok->expanded_aliases_count > 0)
        {
            // Remove the last added alias (which should be this one)
            xfree(tok->expanded_aliases[tok->expanded_aliases_count - 1]);
            tok->expanded_aliases[tok->expanded_aliases_count - 1] = NULL;
            tok->expanded_aliases_count--;
        }
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

    // No alias expansion - check if this WORD token should be converted to TOKEN_IO_NUMBER
    // A WORD that consists only of digits and is immediately followed by a 
    // redirection operator should be an IO_NUMBER
    if (token_get_type(token) == TOKEN_WORD)
    {
        // Check if next token is a redirection operator
        if (tok->input_pos + 1 < token_list_size(tok->input_tokens))
        {
            token_t *next_token = token_list_get(tok->input_tokens, tok->input_pos + 1);
            token_type_t next_type = token_get_type(next_token);
            
            // List of redirection operators
            bool is_redir = (next_type == TOKEN_LESS || 
                           next_type == TOKEN_GREATER ||
                           next_type == TOKEN_DGREAT ||
                           next_type == TOKEN_DLESS ||
                           next_type == TOKEN_DLESSDASH ||
                           next_type == TOKEN_LESSAND ||
                           next_type == TOKEN_GREATAND ||
                           next_type == TOKEN_LESSGREAT ||
                           next_type == TOKEN_CLOBBER);
            
            if (is_redir)
            {
                // Check if the word is all digits
                char *word_text = tokenizer_extract_word_text(token);
                if (word_text != NULL && strlen(word_text) > 0)
                {
                    bool all_digits = true;
                    for (int i = 0; word_text[i] != '\0'; i++)
                    {
                        if (!isdigit((unsigned char)word_text[i]))
                        {
                            all_digits = false;
                            break;
                        }
                    }
                    
                    if (all_digits)
                    {
                        // Convert to TOKEN_IO_NUMBER
                        int io_num = atoi(word_text);
                        token_set_type(token, TOKEN_IO_NUMBER);
                        token_set_io_number(token, io_num);
                    }
                }
                xfree(word_text);
            }
        }
    }

    // Add token to output and move to next
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
    token_list_release_tokens(input_tokens);

    // Clean up context
    tok->input_tokens = NULL;
    tok->output_tokens = NULL;
    tok->input_pos = 0;

    return TOK_OK;
}

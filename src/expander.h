#ifndef EXPANDER_H
#define EXPANDER_H

#include "positional_params.h"
#include "string_t.h"
#include "token.h"
#include "variable_store.h"

/*
 * Forward declaration
 */
typedef struct expander_t expander_t;

/*
 * Function pointer types for injected system operations.
 *
 * userdata is typically an exec_t*, but the expander does not
 * know or care what it actually is.
 */

// typedef const char *(*expander_getenv_fn)(void *userdata, const char *name);
typedef string_t *(*expander_getenv_fn)(void *userdata, const string_t *name);

typedef string_t *(*expander_tilde_expand_fn)(void *userdata, const string_t *input);

typedef string_list_t *(*expander_glob_fn)(void *userdata, const string_t *pattern);

typedef string_t *(*expander_command_subst_fn)(void *userdata, const string_t *command);

/*
 * Create a new expander.
 *
 * vars   - variable store (persistent or temporary)
 * params - positional parameters (not owned by expander)
 *
 * The expander does NOT take ownership of vars or params.
 */
expander_t *expander_create(variable_store_t *vars, positional_params_t *params);

/*
 * Destroy an expander.
 */
void expander_destroy(expander_t **exp);

/* ============================================================================
 * Expansion entry points
 * ============================================================================
 */

/*
 * Expand a single WORD token into zero or more fields.
 * Full expansion pipeline: tilde, param, command, arithmetic,
 * field splitting, pathname expansion, quote removal.
 */
string_list_t *expander_expand_word(expander_t *exp, const token_t *tok);

/*
 * Expand a list of WORD tokens (e.g., command arguments).
 */
string_list_t *expander_expand_words(expander_t *exp, const token_list_t *tokens);

/*
 * Expand a redirection target.
 * No field splitting or pathname expansion.
 * Must produce exactly one string.
 */
string_t *expander_expand_redirection_target(expander_t *exp, const token_t *tok);

/*
 * Expand the RHS of an assignment word.
 * No field splitting or pathname expansion.
 * Must produce exactly one string.
 */
string_t *expander_expand_assignment_value(expander_t *exp, const token_t *tok);

/*
 * Expand a here-doc body.
 * If quoted, no expansions occur.
 * If unquoted, perform param, command, arithmetic expansions.
 */
string_t *expander_expand_heredoc(expander_t *exp, const string_t *body, bool is_quoted);

/* ============================================================================
 * System interaction hooks
 * ============================================================================
 */
void expander_set_getenv(expander_t *exp, expander_getenv_fn fn);
void expander_set_tilde_expand(expander_t *exp, expander_tilde_expand_fn fn);
void expander_set_glob(expander_t *exp, expander_glob_fn fn);
void expander_set_command_substitute(expander_t *exp, expander_command_subst_fn fn);
void expander_set_userdata(expander_t *exp, void *userdata);

#endif /* EXPANDER_H */

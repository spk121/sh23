#ifndef EXPANDER_H
#define EXPANDER_H

#include "ast.h"
#include "string_t.h"
#include "token.h"
#include "variable_store.h"
#include <stdbool.h>

/**
 * @file expander.h
 * @brief Word expansion module for POSIX shell
 * 
 * The expander performs the word expansion steps according to POSIX:
 * 1. Tilde expansion - expand ~ to home directory
 * 2. Parameter expansion - expand $VAR and ${VAR} to variable values
 * 3. Command substitution - execute $(cmd) and replace with output (stub)
 * 4. Arithmetic expansion - evaluate $((expr)) and replace with result (stub)
 * 5. Field splitting - split unquoted expansions on IFS characters
 * 6. Pathname expansion - glob patterns (not yet implemented)
 * 7. Quote removal - handled during parsing phase
 * 
 * The expander operates on AST nodes or individual word tokens after parsing.
 * 
 * ## Implementation Status
 * 
 * - **Tilde expansion**: ✓ Implemented
 *   - `~` expands to HOME
 *   - `~+` expands to PWD
 *   - `~-` expands to OLDPWD
 *   - `~user` expands to user's home directory
 * 
 * - **Parameter expansion**: ✓ Basic implementation
 *   - `$VAR` and `${VAR}` expand to environment variables
 *   - Advanced forms (${var:-default}, etc.) not yet implemented
 * 
 * - **Command substitution**: ⚠ Stub implementation (returns empty)
 * 
 * - **Arithmetic expansion**: ✓ Implemented
 *   - Evaluates arithmetic expressions using the arithmetic evaluator
 *   - Supports parameter expansion within arithmetic expressions
 * 
 * - **Field splitting**: ✓ Implemented
 *   - Splits unquoted expansions on IFS characters
 *   - Respects IFS whitespace vs. non-whitespace behavior
 * 
 * - **Pathname expansion**: ✗ Not yet implemented
 */

// Opaque expander type
typedef struct expander_t expander_t;

/**
 * Command substitution callback function type.
 * 
 * @param command The command string to execute
 * @param user_data User-provided context data
 * @return The output of the command as a newly allocated string_t (caller must free),
 *         or NULL on error
 */
typedef string_t *(*command_subst_callback_t)(const string_t *command, void *user_data);

/**
 * Create a new expander instance.
 * @return A new expander, or NULL on allocation failure.
 */
expander_t *expander_create(void);

/**
 * Destroy an expander instance and free associated resources.
 * Safe to call with NULL.
 * @param exp The expander to destroy
 */
void expander_destroy(expander_t **exp);

/**
 * Set the IFS (Internal Field Separator) value.
 * @param exp The expander instance
 * @param ifs The new IFS value (will be cloned)
 */
void expander_set_ifs(expander_t *exp, const string_t *ifs);

/**
 * Get the current IFS value.
 * @param exp The expander instance
 * @return The current IFS string (do not modify or free)
 */
const string_t *expander_get_ifs(const expander_t *exp);

/**
 * Set the variable store for the expander.
 * The expander does not take ownership of the variable store.
 * @param exp The expander instance
 * @param vars The variable store to use (can be NULL)
 */
void expander_set_variable_store(expander_t *exp, variable_store_t *vars);

/**
 * Get the variable store from the expander.
 * @param exp The expander instance
 * @return The variable store (can be NULL)
 */
variable_store_t *expander_get_variable_store(const expander_t *exp);

/**
 * Set the last exit status available to parameter expansion ($?).
 */
void expander_set_last_exit_status(expander_t *exp, int status);

/**
 * Get the last exit status tracked by the expander.
 */
int expander_get_last_exit_status(const expander_t *exp);

/**
 * Set the process ID available to parameter expansion ($$).
 * @param exp The expander instance
 * @param pid The process ID of the shell
 */
#ifdef POSIX_API
void expander_set_pid(expander_t *exp, pid_t pid);
#else
void expander_set_pid(expander_t *exp, int pid);
#endif

/**
 * Get the process ID tracked by the expander (for $$ expansion).
 * @param exp The expander instance
 * @return The process ID of the shell
 */
#ifdef POSIX_API
pid_t expander_get_pid(const expander_t *exp);
#else
int expander_get_pid(const expander_t *exp);
#endif

/**
 * Set the background process ID available to parameter expansion ($!).
 * @param exp The expander instance
 * @param pid The process ID of the last background command
 */
#ifdef POSIX_API
void expander_set_background_pid(expander_t *exp, pid_t pid);
#else
void expander_set_background_pid(expander_t *exp, int pid);
#endif

/**
 * Get the background process ID tracked by the expander (for $! expansion).
 * @param exp The expander instance
 * @return The process ID of the last background command
 */
#ifdef POSIX_API
pid_t expander_get_background_pid(const expander_t *exp);
#else
int expander_get_background_pid(const expander_t *exp);
#endif

/**
 * Set the command substitution callback function.
 * This callback is invoked when the expander needs to execute a command substitution.
 * @param exp The expander instance
 * @param callback The callback function pointer (can be NULL to disable command substitution)
 * @param user_data User-provided context data to pass to the callback
 */
void expander_set_command_subst_callback(expander_t *exp, command_subst_callback_t callback, void *user_data);

/**
 * Get the current command substitution callback.
 * @param exp The expander instance
 * @return The current callback function pointer (may be NULL)
 */
command_subst_callback_t expander_get_command_subst_callback(const expander_t *exp);

/**
 * Expand an entire AST node tree.
 * This traverses the AST and expands all words in commands.
 * 
 * @param exp The expander instance
 * @param node The AST node to expand
 * @return The expanded AST (currently returns the node unchanged)
 * 
 * @note This is currently a stub implementation that returns the node unchanged.
 *       Full AST traversal and in-place expansion is planned for future development.
 */
ast_node_t *expander_expand_ast(expander_t *exp, ast_node_t *node);

/**
 * Expand a single word token into a list of strings.
 * 
 * This performs the following expansion steps in order:
 * 1. Tilde expansion (if first part and unquoted)
 * 2. Parameter expansion ($VAR or ${VAR})
 * 3. Command substitution ($(cmd) or `cmd`) - stub
 * 4. Arithmetic expansion ($((expr)))
 * 5. Field splitting (on unquoted expansions using IFS)
 * 6. Pathname expansion (globbing) - not yet implemented
 * 
 * @param exp The expander instance
 * @param word_token The word token to expand (must be TOKEN_WORD type)
 * @return A list of expanded strings (caller must free with string_list_destroy)
 *         Returns empty list for non-WORD tokens.
 * 
 * @note Quoting is respected:
 *       - Single quotes prevent all expansions
 *       - Double quotes allow expansions but suppress field splitting
 *       - Unquoted words may be split into multiple fields according to IFS
 */
string_list_t *expander_expand_word(expander_t *exp, token_t *word_token);

/**
 * Expand a string directly (for arithmetic evaluation).
 * 
 * This performs parameter expansion on a raw string,
 * without field splitting, command substitution, or pathname expansion.
 * This is used by the arithmetic evaluator to expand variable references
 * within arithmetic expressions.
 * 
 * Note: This is a basic implementation that only handles simple parameter
 * expansion ($VAR and ${VAR}). It does not support special parameters
 * ($?, $#, $$, etc.) or recursive expansion.
 * 
 * @param exp The expander instance (currently unused, reserved for future use)
 * @param vars The variable store to use for parameter expansion
 * @param input The input string to expand
 * @return A newly allocated string with expansions applied (caller must free),
 *         or NULL on error (e.g., unclosed braced expansion).
 */
char *expand_string(expander_t *exp, variable_store_t *vars, const char *input);

/**
 * Set positional parameters (argv-like) on the expander.
 * The expander will clone the provided array.
 * @param exp The expander instance
 * @param argc Number of arguments
 * @param argv Array of C strings (size >= argc)
 */
void expander_set_positionals(expander_t *exp, int argc, const char **argv);

/**
 * Clear positional parameters.
 * @param exp The expander instance
 */
void expander_clear_positionals(expander_t *exp);


#endif /* EXPANDER_H */

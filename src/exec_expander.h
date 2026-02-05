#ifndef EXEC_EXPANDER_H
#define EXEC_EXPANDER_H

/**
 * exec_expander.h - Word expansion for shell execution
 *
 * This module performs POSIX word expansion in the context of an execution frame.
 * Expansions are performed in this order:
 * 1. Tilde expansion
 * 2. Parameter expansion
 * 3. Command substitution
 * 4. Arithmetic expansion
 * 5. Field splitting (IFS-based)
 * 6. Pathname expansion (globbing)
 *
 * The expander uses the frame's variable store, positional parameters, and
 * other context to resolve expansions. This is important for proper handling
 * of local variables in functions.
 */

#include "exec_internal.h"
#include "exec.h"
#include "string_list.h"
#include "string_t.h"
#include "token.h"
#include <stdbool.h>

/* Forward declarations */
typedef struct exec_t exec_t;
typedef struct exec_frame_t exec_frame_t;

/* ============================================================================
 * Expansion Context
 * ============================================================================ */

/**
 * Expansion flags to control which expansions are performed.
 */
typedef enum expand_flags_t
{
    EXPAND_NONE = 0,
    EXPAND_TILDE = (1 << 0),
    EXPAND_PARAMETER = (1 << 1),
    EXPAND_COMMAND_SUBST = (1 << 2),
    EXPAND_ARITHMETIC = (1 << 3),
    EXPAND_FIELD_SPLIT = (1 << 4),
    EXPAND_PATHNAME = (1 << 5),

    /* Common combinations */
    EXPAND_ALL = (EXPAND_TILDE | EXPAND_PARAMETER | EXPAND_COMMAND_SUBST | EXPAND_ARITHMETIC |
                  EXPAND_FIELD_SPLIT | EXPAND_PATHNAME),

    /* For assignments and redirections: no field splitting or globbing */
    EXPAND_NO_SPLIT_GLOB =
        (EXPAND_TILDE | EXPAND_PARAMETER | EXPAND_COMMAND_SUBST | EXPAND_ARITHMETIC),

    /* For here-documents: parameter, command, arithmetic only */
    EXPAND_HEREDOC = (EXPAND_PARAMETER | EXPAND_COMMAND_SUBST | EXPAND_ARITHMETIC)
} expand_flags_t;

/* ============================================================================
 * Frame-Based Expansion Functions
 * ============================================================================
 * These functions perform expansion in the context of an execution frame.
 * They should be used by the executor during command execution.
 */

/**
 * Expand a single word token in the context of a frame.
 *
 * Performs all applicable POSIX expansions based on the token's flags.
 *
 * @param frame  The execution frame (provides variables, positional params, etc.)
 * @param tok    Token to expand (must be TOKEN_WORD)
 * @return       List of expanded strings, or NULL on error
 */
string_list_t *expand_word(const exec_frame_t *frame, const token_t *tok);

/**
 * Expand multiple word tokens.
 *
 * @param frame   The execution frame
 * @param tokens  List of tokens to expand
 * @return        List of all expanded strings, or NULL on error
 */
string_list_t *expand_words(const exec_frame_t *frame, const token_list_t *tokens);

/**
 * Expand a string with specified expansion flags.
 *
 * This is a lower-level function for expanding arbitrary strings.
 *
 * @param frame  The execution frame
 * @param text   Text to expand
 * @param flags  Which expansions to perform
 * @return       Expanded string, or NULL on error
 */
string_t *expand_string(exec_frame_t *frame, const string_t *text, expand_flags_t flags);

/**
 * Expand a redirection target.
 *
 * Performs expansions without field splitting or pathname expansion.
 *
 * @param frame  The execution frame
 * @param tok    Redirection target token
 * @return       Expanded string, or NULL on error
 */
string_t *expand_redirection_target(exec_frame_t *frame, const token_t *tok);

/**
 * Expand an assignment value.
 *
 * Performs expansions without field splitting or pathname expansion.
 *
 * @param frame  The execution frame
 * @param tok    Assignment token (must be TOKEN_ASSIGNMENT_WORD)
 * @return       Expanded value string, or NULL on error
 */
string_t *expand_assignment_value(exec_frame_t *frame, const token_t *tok);

/**
 * Expand a heredoc body.
 *
 * If the delimiter was quoted, no expansion is performed.
 * Otherwise, parameter, command, and arithmetic expansions are performed.
 *
 * @param frame      The execution frame
 * @param body       Heredoc body text
 * @param is_quoted  Whether the heredoc delimiter was quoted
 * @return           Expanded heredoc body, or NULL on error
 */
string_t *expand_heredoc(exec_frame_t *frame, const string_t *body, bool is_quoted);

/* ============================================================================
 * Specific Expansion Functions
 * ============================================================================ */

/**
 * Perform tilde expansion.
 *
 * Expands ~ to $HOME, ~user to user's home directory.
 *
 * @param frame     The execution frame (may be NULL for standalone use)
 * @param username  Username after tilde (empty string for current user)
 * @return          Expanded path, or NULL if expansion fails
 */
string_t *expand_tilde(exec_frame_t *frame, const string_t *username);

/**
 * Perform parameter expansion.
 *
 * Expands $name, ${name}, ${name:-default}, etc.
 *
 * @param frame  The execution frame
 * @param name   Parameter name
 * @return       Expanded value (empty string if unset)
 */
string_t *expand_parameter(exec_frame_t *frame, const string_t *name);

/**
 * Perform command substitution.
 *
 * Executes the command and returns its stdout.
 *
 * @param frame    The execution frame
 * @param command  Command to execute
 * @return         Command output with trailing newlines stripped
 */
string_t *expand_command_subst(exec_frame_t *frame, const string_t *command);

/**
 * Perform arithmetic expansion.
 *
 * Evaluates the arithmetic expression and returns the result as a string.
 *
 * @param frame       The execution frame
 * @param expression  Arithmetic expression
 * @return            Result as decimal string
 */
string_t *expand_arithmetic(exec_frame_t *frame, const string_t *expression);

/**
 * Perform field splitting on a string.
 *
 * Splits the string based on IFS.
 *
 * @param frame  The execution frame (for IFS lookup)
 * @param text   Text to split
 * @return       List of fields
 */
string_list_t *expand_field_split(exec_frame_t *frame, const string_t *text);

/**
 * Perform pathname expansion (globbing).
 *
 * @param frame    The execution frame
 * @param pattern  Glob pattern
 * @return         List of matching pathnames, or single-element list with
 *                 original pattern if no matches
 */
string_list_t *expand_pathname(exec_frame_t *frame, const string_t *pattern);

/* ============================================================================
 * Special Parameter Access
 * ============================================================================ */

/**
 * Get the value of a special parameter.
 *
 * Special parameters: $?, $$, $!, $#, $@, $*, $0, $1, $2, ...
 *
 * @param frame  The execution frame
 * @param name   Parameter name (without $)
 * @return       Parameter value, or NULL if not a special parameter
 */
string_t *expand_special_param(exec_frame_t *frame, const string_t *name);

/**
 * Check if a name is a special parameter.
 *
 * @param name  Parameter name (without $)
 * @return      true if it's a special parameter
 */
bool is_special_param(const string_t *name);

/* ============================================================================
 * Legacy/Compatibility Functions
 * ============================================================================
 * These functions take exec_t* for backward compatibility with code that
 * hasn't been updated to use frames. They delegate to the frame-based
 * functions using the executor's current frame.
 */

/**
 * @deprecated Use expand_word() with frame instead.
 */
string_list_t *exec_expand_word(exec_t *executor, const token_t *tok);

/**
 * @deprecated Use expand_words() with frame instead.
 */
string_list_t *exec_expand_words(exec_t *executor, const token_list_t *tokens);

/**
 * @deprecated Use expand_redirection_target() with frame instead.
 */
string_t *exec_expand_redirection_target(exec_t *executor, const token_t *tok);

/**
 * @deprecated Use expand_assignment_value() with frame instead.
 */
string_t *exec_expand_assignment_value(exec_t *executor, const token_t *tok);

/**
 * @deprecated Use expand_heredoc() with frame instead.
 */
string_t *exec_expand_heredoc(exec_t *executor, const string_t *body, bool is_quoted);

/**
 * @deprecated Use expand_tilde() with frame instead.
 */
string_t *exec_expand_tilde(exec_t *executor, const string_t *text);

#endif /* EXEC_EXPANDER_H */

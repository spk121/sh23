#pragma once

#include "string_t.h"

/* Forward declaration */
struct exec_t;
struct expander_t;

/* ============================================================================
 * Expander Callback Functions
 * ============================================================================ */

/**
 * Command substitution callback for the expander.
 * Executes a command and returns its output.
 *
 * @param userdata Pointer to the executor context
 * @param command The command string to execute
 * @return The output of the command as a newly allocated string_t (caller must free),
 *         or an empty string on error
 */
string_t *exec_command_subst_callback(void *userdata, const string_t *command);

/**
 * Pathname expansion (glob) callback for the expander.
 * Platform behavior:
 * - POSIX_API: uses POSIX wordexp() for pathname expansion.
 * - UCRT_API: uses _findfirst/_findnext from <io.h> for wildcard matching.
 * - ISO_C: returns the literal pattern (no glob available).
 *
 * @param user_data Pointer to the shell_t context (currently unused)
 * @param pattern The glob pattern to expand
 * @return On success with matches: a newly allocated list of filenames
 *         (caller must free with string_list_destroy). On no matches or error:
 *         returns NULL, signaling the expander to leave the pattern unexpanded.
 */
string_list_t *exec_pathname_expansion_callback(void *user_data, const string_t *pattern);

/**
 * Environment variable lookup callback for the expander.
 * Looks up a variable in the executor's variable store.
 *
 * @param userdata Pointer to the executor context
 * @param name The variable name to look up
 * @return The variable value as a newly allocated string_t, or NULL if not found
 */
string_t *exec_getenv_callback(void *userdata, const string_t *name);

/**
 * Tilde expansion callback for the expander.
 * Expands ~ and ~user to home directories.
 *
 * @param userdata Pointer to the executor context (currently unused)
 * @param username The username after ~, or NULL for current user
 * @return The home directory path as a newly allocated string_t, or NULL if not found
 */
string_t *exec_tilde_expand_callback(void *userdata, const string_t *username);
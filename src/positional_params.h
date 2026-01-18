/**
 * @file positional_params.h
 * @brief Positional parameters ($1, $2, ..., $#) for a single executor context
 *
 * This module manages the positional parameters for a single exec_t instance.
 * Each executor has its own independent set of positional parameters.
 * When creating subshells or calling functions, a new exec_t is created with
 * its own positional_params_t copied or created from scratch.
 */

#ifndef POSITIONAL_PARAMS_H
#define POSITIONAL_PARAMS_H

#include "string_t.h"
#include <stdbool.h>
#include <limits.h>

// Maximum number of positional parameters allowed
#define POSITIONAL_PARAMS_MAX 4096
#if POSITIONAL_PARAMS_MAX <= 0 || POSITIONAL_PARAMS_MAX >= INT_MAX
#error "POSITIONAL_PARAMS_MAX must be a positive integer less than INT_MAX"
#endif

/**
 * @brief Positional parameters for a single executor context
 *
 * Contains the positional parameters ($1, $2, ...) for one execution context.
 * $0 is stored separately in the executor itself since it represents the
 * shell/script name and doesn't change with function calls.
 */
typedef struct positional_params_t
{
    string_t *arg0;     ///< Command name or argv[0]
    string_t **params;  ///< Array of parameters (params[0] is $1)
    int count;          ///< Number of parameters not including arg0
    int max_params;     ///< Maximum allowed (for validation)
} positional_params_t;

// ============================================================================
// Lifecycle Management
// ============================================================================

/**
 * @brief Create a new empty positional parameters set
 *
 * Creates a parameter set with no parameters ($# = 0).
 *
 * @return New positional_params_t, or NULL on allocation failure
 */
positional_params_t *positional_params_create(void);

/**
 * @brief Create positional parameters from an argv-style array
 *
 * @param arg0 Zeroth parameter
 * @param count Count of parameters not including arg0
 * @param params Array of string_t* (params[0] is $1)
 * @return New positional_params_t, or NULL if count exceeds maximum
 */
positional_params_t *positional_params_create_from_array(const string_t *arg0, int count, const string_t **params);
positional_params_t *positional_params_create_from_string_list(const string_t *arg0,
                                                               const string_list_t *params);

/**
 * @brief Create positional parameters from C-style argv
 *
 * Creates string_t copies of the C strings.
 *
 * @param arg0 Zeroth parameter
 * @param argc Number of arguments not including arg0
 * @param argv Array of C strings
 * @return New positional_params_t, or NULL on allocation failure
 */
positional_params_t *positional_params_create_from_argv(const char *arg0, int argc, const char **argv);

/**
 * @brief Copy positional parameters
 *
 * Creates a deep copy of all parameter strings.
 *
 * @param src Source parameters to copy
 * @return New positional_params_t copy, or NULL on allocation failure
 */
positional_params_t *positional_params_copy(const positional_params_t *src);

/**
 * @brief Destroy positional parameters
 *
 * Frees all parameter strings and the structure itself.
 *
 * @param params Pointer to positional_params_t pointer (set to NULL after free)
 */
void positional_params_destroy(positional_params_t **params);

// ============================================================================
// Parameter Access
// ============================================================================

/**
 * @brief Get a specific positional parameter
 *
 * @param params The positional parameters
 * @param n Parameter number (1-based: 1 is $1, 2 is $2, etc.)
 * @return The parameter value, or NULL if n is out of range
 */
const string_t *positional_params_get(const positional_params_t *params, int n);

/**
 * @brief Get the count of positional parameters (for $#)
 *
 * @param params The positional parameters
 * @return Number of positional parameters
 */
int positional_params_count(const positional_params_t *params);

/**
 * @brief Get all positional parameters as a list (for $@ and $*)
 *
 * Returns a newly allocated string_list_t (caller must free).
 *
 * @param params The positional parameters
 * @return List of all positional parameters (caller must free)
 */
string_list_t *positional_params_get_all(const positional_params_t *params);

/**
 * @brief Get all positional parameters joined by a separator (for "$*")
 *
 * @param params The positional parameters
 * @param sep Separator character (typically IFS[0])
 * @return Newly allocated string with all parameters joined (caller must free)
 */
string_t *positional_params_get_all_joined(const positional_params_t *params, char sep);

// ============================================================================
// Parameter Modification (for 'set' and 'shift' builtins)
// ============================================================================

/**
 * @brief Replace all positional parameters (for 'set' builtin)
 *
 * Destroys existing parameters and takes ownership of the new array.
 *
 * @param params The positional parameters to modify
 * @param new_params Array of new parameters (params[0] is $1), takes ownership
 * @param count Number of parameters
 * @return true if successful, false if count exceeds maximum
 */
bool positional_params_replace(positional_params_t *params,
                               string_t **new_params, int count);

/**
 * @brief Shift positional parameters (for 'shift' builtin)
 *
 * Removes the first n parameters. $1 is deleted, $2 becomes $1, etc.
 *
 * @param params The positional parameters to modify
 * @param n Number of parameters to shift (typically 1)
 * @return true if successful, false if n > parameter count
 */
bool positional_params_shift(positional_params_t *params, int n);

// ============================================================================
// Configuration
// ============================================================================

/**
 * @brief Set the maximum number of positional parameters allowed
 *
 * @param params The positional parameters
 * @param max_params Maximum number (must be > 0 and <= POSITIONAL_PARAMS_MAX)
 */
void positional_params_set_max(positional_params_t *params, int max_params);

/**
 * @brief Get the maximum number of positional parameters allowed
 *
 * @param params The positional parameters
 * @return Maximum number of positional parameters
 */
int positional_params_get_max(const positional_params_t *params);

#endif

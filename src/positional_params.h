// positional_params.h
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

struct positional_params_t
{
    string_t **params; // params[0] is $1
    int count;         // number of params
};

struct positional_params_stack_t
{
    struct positional_params_t **frames;
    int depth;
    int capacity;
    string_t *zero; // $0 stored separately
    int max_params; // Maximum number of positional parameters allowed
};

typedef struct positional_params_t positional_params_t;
typedef struct positional_params_stack_t positional_params_stack_t;

// ============================================================================
// Stack Lifecycle
// ============================================================================

/**
 * Create a new positional parameter stack.
 * Initializes with an empty parameter set.
 */
positional_params_stack_t *positional_params_stack_create(void);

/**
 * Destroy the stack and free all parameter sets.
 */
void positional_params_stack_destroy(positional_params_stack_t **stack);

// ============================================================================
// Stack Operations (for function calls)
// ============================================================================

/**
 * Push a new set of positional parameters onto the stack.
 * Takes ownership of the parameter array.
 * 
 * @param stack The parameter stack
 * @param params Array of parameters (params[0] is $1)
 * @param count Number of parameters
 * @return true if successful, false if count exceeds maximum allowed
 */
bool positional_params_push(positional_params_stack_t *stack, 
                            string_t **params, int count);

/**
 * Pop the top parameter set from the stack.
 * Restores the previous parameter set.
 * 
 * @param stack The parameter stack
 */
void positional_params_pop(positional_params_stack_t *stack);

/**
 * Get the current stack depth (for debugging).
 */
int positional_params_stack_depth(const positional_params_stack_t *stack);

// ============================================================================
// Parameter Access
// ============================================================================

/**
 * Get a specific positional parameter.
 * 
 * @param stack The parameter stack
 * @param n The parameter number (1-based: 1 is $1, 2 is $2, etc.)
 * @return The parameter value, or NULL if n is out of range
 * 
 * Note: Does not return $0 (use positional_params_get_zero separately)
 */
const string_t *positional_params_get(const positional_params_stack_t *stack, int n);

/**
 * Get the count of positional parameters (for $#).
 */
int positional_params_count(const positional_params_stack_t *stack);

/**
 * Get all positional parameters as a list (for $@ and $*).
 * Returns a newly allocated string_list_t (caller must free).
 * 
 * @param stack The parameter stack
 * @return List of all positional parameters
 */
string_list_t *positional_params_get_all(const positional_params_stack_t *stack);

/**
 * Get all positional parameters joined by a separator (for "$*").
 * 
 * @param stack The parameter stack
 * @param sep The separator character (typically IFS[0])
 * @return A newly allocated string with all parameters joined
 */
string_t *positional_params_get_all_joined(const positional_params_stack_t *stack, 
                                           char sep);

// ============================================================================
// Parameter Modification (for 'set' and 'shift' builtins)
// ============================================================================

/**
 * Replace the current positional parameters (for 'set' builtin).
 * This modifies the current frame, it does NOT push a new frame.
 * Takes ownership of the parameter array.
 * 
 * @param stack The parameter stack
 * @param params Array of new parameters (params[0] is $1)
 * @param count Number of parameters
 * @return true if successful, false if count exceeds maximum allowed
 */
bool positional_params_replace(positional_params_stack_t *stack,
                               string_t **params, int count);

/**
 * Shift positional parameters (for 'shift' builtin).
 * Removes the first n parameters from the current set.
 * 
 * @param stack The parameter stack
 * @param n Number of parameters to shift (default 1)
 * @return true if successful, false if n > parameter count
 */
bool positional_params_shift(positional_params_stack_t *stack, int n);

// ============================================================================
// Maximum Parameter Limit
// ============================================================================

/**
 * Set the maximum number of positional parameters allowed.
 * 
 * @param stack The parameter stack
 * @param max_params Maximum number of positional parameters (must be > 0)
 */
void positional_params_set_max(positional_params_stack_t *stack, int max_params);

/**
 * Get the maximum number of positional parameters allowed.
 * 
 * @param stack The parameter stack
 * @return Maximum number of positional parameters
 */
int positional_params_get_max(const positional_params_stack_t *stack);

// ============================================================================
// $0 Management (separate from positional parameters)
// ============================================================================

/**
 * Set $0 (script/shell name).
 * This is set once at shell initialization and never changes.
 */
void positional_params_set_zero(positional_params_stack_t *stack, 
                                const string_t *name);

/**
 * Check if $0 is set.
 */
bool positional_params_has_zero(const positional_params_stack_t *stack);

/**
 * Get $0 (script/shell name).
 * Will return $0. Caller must first check positional_params_has_zero().
 */
const string_t *positional_params_get_zero(const positional_params_stack_t *stack);

#endif

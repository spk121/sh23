#ifndef ARITHMETIC_H
#define ARITHMETIC_H

#include "frame.h"
#include "string_t.h"

/**
 * Result of an arithmetic expression evaluation.
 * 
 * The caller owns this struct and must call arithmetic_result_free() to clean up.
 * The error field (if non-NULL) is owned by the caller and will be freed by
 * arithmetic_result_free().
 */
typedef struct {
    long value;      // Result value if successful
    int failed;      // 1 if error occurred, 0 if successful
    string_t *error; // Error message if failed (owned by caller, freed by arithmetic_result_free)
} ArithmeticResult;

/**
 * Evaluate an arithmetic expression with full POSIX semantics.
 * 
 * This function performs parameter expansion, command substitution, and arithmetic
 * evaluation according to POSIX shell arithmetic rules.
 * 
 * @param frame The execution frame (provides executor, variables, etc.)
 *              The frame's variable store may be modified for assignment operations like x=5.
 * @param expression The arithmetic expression to evaluate (not modified, deep copied internally)
 * 
 * @return ArithmeticResult struct containing the result or error. Caller must call
 *         arithmetic_result_free() to clean up, even on success (though it's a no-op
 *         if no error occurred).
 */
ArithmeticResult arithmetic_evaluate(exec_frame_t *frame, const string_t *expression);

/**
 * Free resources associated with an ArithmeticResult.
 * 
 * This function frees the error string if present and resets the result struct.
 * It is safe to call even if the result was successful (no error).
 * After calling this function, the result struct is left in a clean state.
 * 
 * @param result The result to free (must not be NULL)
 */
void arithmetic_result_free(ArithmeticResult *result);

#endif

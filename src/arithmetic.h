#ifndef ARITHMETIC_H
#define ARITHMETIC_H

#include "expander.h"
#include "variable_store.h"

typedef struct {
    long value;      // Result if success
    int failed;      // 1 if error, 0 if success
    string_t *error; // Error message if failed (caller frees)
} ArithmeticResult;

// Evaluate an arithmetic expression, handling parameter expansion and command substitution
ArithmeticResult arithmetic_evaluate(expander_t *exp, variable_store_t *vars, const string_t *expression);

// Free an ArithmeticResult
void arithmetic_result_free(ArithmeticResult *result);

#endif

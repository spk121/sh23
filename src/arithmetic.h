#ifndef ARITHMETIC_H
#define ARITHMETIC_H

#include "executor.h"

typedef struct {
    long value;       // Result if success
    char *error;      // Error message if failed (caller frees)
    int failed;       // 1 if error, 0 if success
} ArithmeticResult;

// Evaluate an arithmetic expression, handling parameter expansion and command substitution
ArithmeticResult arithmetic_evaluate(Executor *exec, const char *expression);

// Free an ArithmeticResult
void arithmetic_result_free(ArithmeticResult *result);

#endif

/**
 * @file test_arithmetic_ctest.c
 * @brief Tests for the POSIX shell arithmetic evaluator
 */

#include "ctest.h"
#include "arithmetic.h"
#include "string_t.h"
#include "variable_store.h"
#include "positional_params.h"
#include "exec_expander.h"
#include "xalloc.h"

/* Helper to evaluate an arithmetic expression and check the result */
static ArithmeticResult eval_expr(expander_t *exp, variable_store_t *vars, const char *expr_cstr)
{
    string_t *expr = string_create_from_cstr(expr_cstr);
    ArithmeticResult result = arithmetic_evaluate(exp, vars, expr);
    string_destroy(&expr);
    return result;
}

/* ============================================================================
 * Basic Arithmetic Operations
 * ============================================================================ */

CTEST(test_arithmetic_addition)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "2+3");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 5, "2+3 == 5");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_subtraction)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "10-4");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 6, "10-4 == 6");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_multiplication)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "6*7");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 42, "6*7 == 42");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_division)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "20/4");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 5, "20/4 == 5");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_modulo)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "17%5");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 2, "17%5 == 2");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_division_by_zero)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "5/0");
    CTEST_ASSERT_EQ(ctest, r.failed, 1, "division by zero fails");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_modulo_by_zero)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "5%0");
    CTEST_ASSERT_EQ(ctest, r.failed, 1, "modulo by zero fails");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/* ============================================================================
 * Unary Operators
 * ============================================================================ */

CTEST(test_arithmetic_unary_plus)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "+5");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 5, "+5 == 5");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_unary_minus)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "-5");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, -5, "-5 == -5");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_bitwise_not)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "~0");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, -1, "~0 == -1");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_logical_not)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r1 = eval_expr(exp, vars, "!0");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 1, "!0 == 1");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, vars, "!5");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 0, "!5 == 0");
    arithmetic_result_free(&r2);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/* ============================================================================
 * Comparison Operators
 * ============================================================================ */

CTEST(test_arithmetic_less_than)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r1 = eval_expr(exp, vars, "3<5");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 1, "3<5 == 1");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, vars, "5<3");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 0, "5<3 == 0");
    arithmetic_result_free(&r2);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_greater_than)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r1 = eval_expr(exp, vars, "5>3");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 1, "5>3 == 1");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, vars, "3>5");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 0, "3>5 == 0");
    arithmetic_result_free(&r2);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_less_equal)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r1 = eval_expr(exp, vars, "3<=5");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 1, "3<=5 == 1");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, vars, "5<=5");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 1, "5<=5 == 1");
    arithmetic_result_free(&r2);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_greater_equal)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r1 = eval_expr(exp, vars, "5>=3");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 1, "5>=3 == 1");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, vars, "5>=5");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 1, "5>=5 == 1");
    arithmetic_result_free(&r2);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_equality)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r1 = eval_expr(exp, vars, "5==5");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 1, "5==5 == 1");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, vars, "5==3");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 0, "5==3 == 0");
    arithmetic_result_free(&r2);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_not_equal)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r1 = eval_expr(exp, vars, "5!=3");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 1, "5!=3 == 1");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, vars, "5!=5");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 0, "5!=5 == 0");
    arithmetic_result_free(&r2);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/* ============================================================================
 * Bitwise Operators
 * ============================================================================ */

CTEST(test_arithmetic_bitwise_and)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "12&10");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 8, "12&10 == 8");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_bitwise_or)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "12|10");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 14, "12|10 == 14");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_bitwise_xor)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "12^10");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 6, "12^10 == 6");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_left_shift)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "1<<4");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 16, "1<<4 == 16");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_right_shift)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "16>>2");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 4, "16>>2 == 4");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/* ============================================================================
 * Logical Operators
 * ============================================================================ */

CTEST(test_arithmetic_logical_and)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r1 = eval_expr(exp, vars, "1&&1");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 1, "1&&1 == 1");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, vars, "1&&0");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 0, "1&&0 == 0");
    arithmetic_result_free(&r2);

    ArithmeticResult r3 = eval_expr(exp, vars, "0&&1");
    CTEST_ASSERT_EQ(ctest, r3.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r3.value, 0, "0&&1 == 0");
    arithmetic_result_free(&r3);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_logical_or)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r1 = eval_expr(exp, vars, "0||0");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 0, "0||0 == 0");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, vars, "1||0");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 1, "1||0 == 1");
    arithmetic_result_free(&r2);

    ArithmeticResult r3 = eval_expr(exp, vars, "0||1");
    CTEST_ASSERT_EQ(ctest, r3.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r3.value, 1, "0||1 == 1");
    arithmetic_result_free(&r3);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/* ============================================================================
 * Ternary Operator
 * ============================================================================ */

CTEST(test_arithmetic_ternary)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r1 = eval_expr(exp, vars, "1?10:20");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 10, "1?10:20 == 10");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, vars, "0?10:20");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 20, "0?10:20 == 20");
    arithmetic_result_free(&r2);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/* ============================================================================
 * Parentheses and Precedence
 * ============================================================================ */

CTEST(test_arithmetic_parentheses)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r1 = eval_expr(exp, vars, "2+3*4");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 14, "2+3*4 == 14 (precedence)");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, vars, "(2+3)*4");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 20, "(2+3)*4 == 20");
    arithmetic_result_free(&r2);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/* ============================================================================
 * Octal and Hexadecimal Constants (POSIX requirement)
 * ============================================================================ */

CTEST(test_arithmetic_octal)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r1 = eval_expr(exp, vars, "010");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 8, "010 (octal) == 8");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, vars, "0777");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 511, "0777 (octal) == 511");
    arithmetic_result_free(&r2);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_hexadecimal)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r1 = eval_expr(exp, vars, "0x10");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 16, "0x10 == 16");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, vars, "0xFF");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 255, "0xFF == 255");
    arithmetic_result_free(&r2);

    ArithmeticResult r3 = eval_expr(exp, vars, "0XAB");
    CTEST_ASSERT_EQ(ctest, r3.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r3.value, 171, "0XAB == 171");
    arithmetic_result_free(&r3);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_zero)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "0");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 0, "0 == 0");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/* ============================================================================
 * Variables
 * ============================================================================ */

CTEST(test_arithmetic_variable)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    variable_store_add_cstr(vars, "x", "10", false, false);
    variable_store_add_cstr(vars, "y", "5", false, false);

    ArithmeticResult r = eval_expr(exp, vars, "x+y");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 15, "x+y == 15");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_variable_with_digits)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    variable_store_add_cstr(vars, "var1", "100", false, false);
    variable_store_add_cstr(vars, "count2", "50", false, false);

    ArithmeticResult r = eval_expr(exp, vars, "var1+count2");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 150, "var1+count2 == 150");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_unset_variable)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "unset_var+5");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 5, "unset_var+5 == 5 (unset treated as 0)");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/* ============================================================================
 * Assignment Operators
 * ============================================================================ */

CTEST(test_arithmetic_assignment)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "x=42");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 42, "x=42 returns 42");
    arithmetic_result_free(&r);

    const char *val = variable_store_get_value_cstr(vars, "x");
    CTEST_ASSERT_NOT_NULL(ctest, val, "x is set");
    CTEST_ASSERT_STR_EQ(ctest, val, "42", "x == 42");

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

CTEST(test_arithmetic_plus_assign)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    variable_store_add_cstr(vars, "x", "10", false, false);

    ArithmeticResult r = eval_expr(exp, vars, "x+=5");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    arithmetic_result_free(&r);

    const char *val = variable_store_get_value_cstr(vars, "x");
    CTEST_ASSERT_NOT_NULL(ctest, val, "x is set");
    CTEST_ASSERT_STR_EQ(ctest, val, "15", "x == 15 after x+=5");

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/* ============================================================================
 * Comma Operator
 * ============================================================================ */

CTEST(test_arithmetic_comma)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    ArithmeticResult r = eval_expr(exp, vars, "1,2,3");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 1, "1,2,3 returns first value");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/* ============================================================================
 * Complex Expressions
 * ============================================================================ */

CTEST(test_arithmetic_complex_expression)
{
    variable_store_t *vars = variable_store_create();
    positional_params_t *params = positional_params_create();
    expander_t *exp = expander_create(vars, params);

    variable_store_add_cstr(vars, "a", "2", false, false);
    variable_store_add_cstr(vars, "b", "3", false, false);

    ArithmeticResult r = eval_expr(exp, vars, "(a+b)*4-2");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 18, "(2+3)*4-2 == 18");
    arithmetic_result_free(&r);

    expander_destroy(&exp);
    positional_params_destroy(&params);
    variable_store_destroy(&vars);
    (void)ctest;
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(void)
{
    arena_start();
    log_init();

    CTestEntry *suite[] = {
        /* Basic operations */
        CTEST_ENTRY(test_arithmetic_addition),
        CTEST_ENTRY(test_arithmetic_subtraction),
        CTEST_ENTRY(test_arithmetic_multiplication),
        CTEST_ENTRY(test_arithmetic_division),
        CTEST_ENTRY(test_arithmetic_modulo),
        CTEST_ENTRY(test_arithmetic_division_by_zero),
        CTEST_ENTRY(test_arithmetic_modulo_by_zero),

        /* Unary operators */
        CTEST_ENTRY(test_arithmetic_unary_plus),
        CTEST_ENTRY(test_arithmetic_unary_minus),
        CTEST_ENTRY(test_arithmetic_bitwise_not),
        CTEST_ENTRY(test_arithmetic_logical_not),

        /* Comparison operators */
        CTEST_ENTRY(test_arithmetic_less_than),
        CTEST_ENTRY(test_arithmetic_greater_than),
        CTEST_ENTRY(test_arithmetic_less_equal),
        CTEST_ENTRY(test_arithmetic_greater_equal),
        CTEST_ENTRY(test_arithmetic_equality),
        CTEST_ENTRY(test_arithmetic_not_equal),

        /* Bitwise operators */
        CTEST_ENTRY(test_arithmetic_bitwise_and),
        CTEST_ENTRY(test_arithmetic_bitwise_or),
        CTEST_ENTRY(test_arithmetic_bitwise_xor),
        CTEST_ENTRY(test_arithmetic_left_shift),
        CTEST_ENTRY(test_arithmetic_right_shift),

        /* Logical operators */
        CTEST_ENTRY(test_arithmetic_logical_and),
        CTEST_ENTRY(test_arithmetic_logical_or),

        /* Ternary operator */
        CTEST_ENTRY(test_arithmetic_ternary),

        /* Parentheses and precedence */
        CTEST_ENTRY(test_arithmetic_parentheses),

        /* Octal and hexadecimal (POSIX) */
        CTEST_ENTRY(test_arithmetic_octal),
        CTEST_ENTRY(test_arithmetic_hexadecimal),
        CTEST_ENTRY(test_arithmetic_zero),

        /* Variables */
        CTEST_ENTRY(test_arithmetic_variable),
        CTEST_ENTRY(test_arithmetic_variable_with_digits),
        CTEST_ENTRY(test_arithmetic_unset_variable),

        /* Assignment operators */
        CTEST_ENTRY(test_arithmetic_assignment),
        CTEST_ENTRY(test_arithmetic_plus_assign),

        /* Comma operator */
        CTEST_ENTRY(test_arithmetic_comma),

        /* Complex expressions */
        CTEST_ENTRY(test_arithmetic_complex_expression),

        NULL
    };

    int result = ctest_run_suite(suite);

    arena_end();
    return result;
}

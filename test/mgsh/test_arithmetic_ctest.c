/**
 * @file test_arithmetic_ctest.c
 * @brief Tests for the POSIX shell arithmetic evaluator
 */

#include "ctest.h"
#include "arithmetic.h"
#include "string_t.h"
#include "variable_store.h"
#include "positional_params.h"
#include "exec.h"
#include "exec_frame.h"
#include "xalloc.h"

/* Helper to evaluate an arithmetic expression and check the result */
static ArithmeticResult eval_expr(exec_t *exp, const char *expr_cstr)
{
    string_t *expr = string_create_from_cstr(expr_cstr);
    ArithmeticResult result = arithmetic_evaluate(exp->current_frame, expr);
    string_destroy(&expr);
    return result;
}

/* ============================================================================
 * Basic Arithmetic Operations
 * ============================================================================ */

CTEST(test_arithmetic_addition)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "2+3");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 5, "2+3 == 5");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_subtraction)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "10-4");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 6, "10-4 == 6");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_multiplication)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "6*7");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 42, "6*7 == 42");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_division)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "20/4");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 5, "20/4 == 5");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_modulo)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "17%5");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 2, "17%5 == 2");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_division_by_zero)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "5/0");
    CTEST_ASSERT_EQ(ctest, r.failed, 1, "division by zero fails");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_modulo_by_zero)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "5%0");
    CTEST_ASSERT_EQ(ctest, r.failed, 1, "modulo by zero fails");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

/* ============================================================================
 * Unary Operators
 * ============================================================================ */

CTEST(test_arithmetic_unary_plus)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "+5");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 5, "+5 == 5");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_unary_minus)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "-5");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, -5, "-5 == -5");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_bitwise_not)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "~0");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, -1, "~0 == -1");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_logical_not)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r1 = eval_expr(exp, "!0");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 1, "!0 == 1");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, "!5");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 0, "!5 == 0");
    arithmetic_result_free(&r2);

    exec_destroy(&exp);
    (void)ctest;
}

/* ============================================================================
 * Comparison Operators
 * ============================================================================ */

CTEST(test_arithmetic_less_than)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r1 = eval_expr(exp, "3<5");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 1, "3<5 == 1");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, "5<3");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 0, "5<3 == 0");
    arithmetic_result_free(&r2);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_greater_than)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r1 = eval_expr(exp, "5>3");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 1, "5>3 == 1");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, "3>5");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 0, "3>5 == 0");
    arithmetic_result_free(&r2);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_less_equal)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r1 = eval_expr(exp, "3<=5");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 1, "3<=5 == 1");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, "5<=5");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 1, "5<=5 == 1");
    arithmetic_result_free(&r2);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_greater_equal)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r1 = eval_expr(exp, "5>=3");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 1, "5>=3 == 1");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, "5>=5");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 1, "5>=5 == 1");
    arithmetic_result_free(&r2);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_equality)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r1 = eval_expr(exp, "5==5");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 1, "5==5 == 1");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, "5==3");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 0, "5==3 == 0");
    arithmetic_result_free(&r2);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_not_equal)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r1 = eval_expr(exp, "5!=3");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 1, "5!=3 == 1");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, "5!=5");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 0, "5!=5 == 0");
    arithmetic_result_free(&r2);

    exec_destroy(&exp);
    (void)ctest;
}

/* ============================================================================
 * Bitwise Operators
 * ============================================================================ */

CTEST(test_arithmetic_bitwise_and)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "12&10");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 8, "12&10 == 8");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_bitwise_or)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "12|10");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 14, "12|10 == 14");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_bitwise_xor)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "12^10");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 6, "12^10 == 6");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_left_shift)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "1<<4");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 16, "1<<4 == 16");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_right_shift)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "16>>2");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 4, "16>>2 == 4");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

/* ============================================================================
 * Logical Operators
 * ============================================================================ */

CTEST(test_arithmetic_logical_and)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r1 = eval_expr(exp, "1&&1");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 1, "1&&1 == 1");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, "1&&0");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 0, "1&&0 == 0");
    arithmetic_result_free(&r2);

    ArithmeticResult r3 = eval_expr(exp, "0&&1");
    CTEST_ASSERT_EQ(ctest, r3.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r3.value, 0, "0&&1 == 0");
    arithmetic_result_free(&r3);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_logical_or)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r1 = eval_expr(exp, "0||0");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 0, "0||0 == 0");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, "1||0");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 1, "1||0 == 1");
    arithmetic_result_free(&r2);

    ArithmeticResult r3 = eval_expr(exp, "0||1");
    CTEST_ASSERT_EQ(ctest, r3.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r3.value, 1, "0||1 == 1");
    arithmetic_result_free(&r3);

    exec_destroy(&exp);
    (void)ctest;
}

/* ============================================================================
 * Ternary Operator
 * ============================================================================ */

CTEST(test_arithmetic_ternary)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r1 = eval_expr(exp, "1?10:20");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 10, "1?10:20 == 10");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, "0?10:20");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 20, "0?10:20 == 20");
    arithmetic_result_free(&r2);

    exec_destroy(&exp);
    (void)ctest;
}

/* ============================================================================
 * Parentheses and Precedence
 * ============================================================================ */

CTEST(test_arithmetic_parentheses)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r1 = eval_expr(exp, "2+3*4");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 14, "2+3*4 == 14 (precedence)");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, "(2+3)*4");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 20, "(2+3)*4 == 20");
    arithmetic_result_free(&r2);

    exec_destroy(&exp);
    (void)ctest;
}

/* ============================================================================
 * Octal and Hexadecimal Constants (POSIX requirement)
 * ============================================================================ */

CTEST(test_arithmetic_octal)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r1 = eval_expr(exp, "010");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 8, "010 (octal) == 8");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, "0777");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 511, "0777 (octal) == 511");
    arithmetic_result_free(&r2);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_hexadecimal)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r1 = eval_expr(exp, "0x10");
    CTEST_ASSERT_EQ(ctest, r1.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r1.value, 16, "0x10 == 16");
    arithmetic_result_free(&r1);

    ArithmeticResult r2 = eval_expr(exp, "0xFF");
    CTEST_ASSERT_EQ(ctest, r2.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r2.value, 255, "0xFF == 255");
    arithmetic_result_free(&r2);

    ArithmeticResult r3 = eval_expr(exp, "0XAB");
    CTEST_ASSERT_EQ(ctest, r3.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r3.value, 171, "0XAB == 171");
    arithmetic_result_free(&r3);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_zero)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "0");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 0, "0 == 0");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

/* ============================================================================
 * Variables
 * ============================================================================ */

CTEST(test_arithmetic_variable)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    variable_store_add_cstr(exec_frame_get_variables(exp->current_frame), "x", "10", false, false);
    variable_store_add_cstr(exec_frame_get_variables(exp->current_frame), "y", "5", false, false);

    ArithmeticResult r = eval_expr(exp, "x+y");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 15, "x+y == 15");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_variable_with_digits)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    variable_store_add_cstr(exec_frame_get_variables(exp->current_frame), "var1", "100", false, false);
    variable_store_add_cstr(exec_frame_get_variables(exp->current_frame), "count2", "50", false, false);

    ArithmeticResult r = eval_expr(exp, "var1+count2");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 150, "var1+count2 == 150");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_unset_variable)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "unset_var+5");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 5, "unset_var+5 == 5 (unset treated as 0)");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

/* ============================================================================
 * Assignment Operators
 * ============================================================================ */

CTEST(test_arithmetic_assignment)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "x=42");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 42, "x=42 returns 42");
    arithmetic_result_free(&r);

    const char *val = variable_store_get_value_cstr(exec_frame_get_variables(exp->current_frame), "x");
    CTEST_ASSERT_NOT_NULL(ctest, val, "x is set");
    CTEST_ASSERT_STR_EQ(ctest, val, "42", "x == 42");

    exec_destroy(&exp);
    (void)ctest;
}

CTEST(test_arithmetic_plus_assign)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    variable_store_add_cstr(exec_frame_get_variables(exp->current_frame), "x", "10", false, false);

    ArithmeticResult r = eval_expr(exp, "x+=5");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    arithmetic_result_free(&r);

    const char *val = variable_store_get_value_cstr(exec_frame_get_variables(exp->current_frame), "x");
    CTEST_ASSERT_NOT_NULL(ctest, val, "x is set");
    CTEST_ASSERT_STR_EQ(ctest, val, "15", "x == 15 after x+=5");

    exec_destroy(&exp);
    (void)ctest;
}

/* ============================================================================
 * Comma Operator
 * ============================================================================ */

CTEST(test_arithmetic_comma)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    ArithmeticResult r = eval_expr(exp, "1,2,3");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 1, "1,2,3 returns first value");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
    (void)ctest;
}

/* ============================================================================
 * Complex Expressions
 * ============================================================================ */

CTEST(test_arithmetic_complex_expression)
{
    exec_cfg_t cfg = {0};
    exec_t *exp = exec_create(&cfg);

    variable_store_add_cstr(exec_frame_get_variables(exp->current_frame), "a", "2", false, false);
    variable_store_add_cstr(exec_frame_get_variables(exp->current_frame), "b", "3", false, false);

    ArithmeticResult r = eval_expr(exp, "(a+b)*4-2");
    CTEST_ASSERT_EQ(ctest, r.failed, 0, "no error");
    CTEST_ASSERT_EQ(ctest, r.value, 18, "(2+3)*4-2 == 18");
    arithmetic_result_free(&r);

    exec_destroy(&exp);
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

/*
 * test_token_array_ctest.c
 *
 * Unit tests for token_array.c - dynamic array of token pointers
 */

#include "ctest.h"
#include "token_array.h"
#include "token.h"
#include "logging.h"
#include "lib.h"
#include "xalloc.h"

// ============================================================================
// Token Array Lifecycle
// ============================================================================

CTEST(test_token_array_create_destroy)
{
    token_array_t *arr = token_array_create();
    CTEST_ASSERT_NOT_NULL(ctest, arr, "array created");
    CTEST_ASSERT_EQ(ctest, token_array_size(arr), 0, "size is 0");
    CTEST_ASSERT_GT(ctest, (int)token_array_capacity(arr), 0, "capacity allocated");

    token_array_destroy(&arr);
    CTEST_ASSERT_NULL(ctest, arr, "array destroyed");
}

CTEST(test_token_array_is_empty)
{
    token_array_t *arr = token_array_create();
    CTEST_ASSERT_TRUE(ctest, token_array_is_empty(arr), "empty initially");

    token_array_append(arr, token_create_word());
    CTEST_ASSERT_FALSE(ctest, token_array_is_empty(arr), "not empty after append");

    token_array_destroy(&arr);
}

// ============================================================================
// Element Access
// ============================================================================

CTEST(test_token_array_append_get)
{
    token_array_t *arr = token_array_create();

    token_t *tok1 = token_create(TOKEN_WORD);
    token_t *tok2 = token_create(TOKEN_IF);
    token_t *tok3 = token_create(TOKEN_THEN);

    token_array_append(arr, tok1);
    token_array_append(arr, tok2);
    token_array_append(arr, tok3);

    CTEST_ASSERT_EQ(ctest, token_array_size(arr), 3, "size is 3");

    token_t *retrieved0 = token_array_get(arr, 0);
    CTEST_ASSERT_EQ(ctest, (int)token_get_type(retrieved0), (int)TOKEN_WORD, "element 0 is WORD");

    token_t *retrieved1 = token_array_get(arr, 1);
    CTEST_ASSERT_EQ(ctest, (int)token_get_type(retrieved1), (int)TOKEN_IF, "element 1 is IF");

    token_array_destroy(&arr);
}

CTEST(test_token_array_get_out_of_bounds)
{
    token_array_t *arr = token_array_create();
    token_array_append(arr, token_create_word());

    jmp_buf env;
    int jumped = log_fatal_try_begin(&env);
    if (jumped == 0)
    {
        token_array_get(arr, 5);
        log_fatal_try_end();
        CTEST_ASSERT_TRUE(ctest, false, "expected fatal precondition for out-of-bounds index");
    }
    else
    {
        log_fatal_try_end();
        CTEST_ASSERT_TRUE(ctest, true, "fatal precondition triggered");
    }

    token_array_destroy(&arr);
}

CTEST(test_token_array_get_negative_index)
{
    token_array_t *arr = token_array_create();
    token_array_append(arr, token_create_word());

    jmp_buf env;
    int jumped = log_fatal_try_begin(&env);
    if (jumped == 0)
    {
        token_array_get(arr, -1);
        log_fatal_try_end();
        CTEST_ASSERT_TRUE(ctest, false, "expected fatal precondition for negative index");
    }
    else
    {
        log_fatal_try_end();
        CTEST_ASSERT_TRUE(ctest, true, "fatal precondition triggered");
    }

    token_array_destroy(&arr);
}

// ============================================================================
// Set and Remove
// ============================================================================

CTEST(test_token_array_set)
{
    token_array_t *arr = token_array_create();

    token_t *tok1 = token_create(TOKEN_WORD);
    token_t *tok2 = token_create(TOKEN_IF);
    token_array_append(arr, tok1);

    token_t *replacement = token_create(TOKEN_THEN);
    token_array_set(arr, 0, replacement);

    token_t *retrieved = token_array_get(arr, 0);
    CTEST_ASSERT_EQ(ctest, (int)token_get_type(retrieved), (int)TOKEN_THEN, "element replaced");

    token_array_destroy(&arr);
}

CTEST(test_token_array_remove)
{
    token_array_t *arr = token_array_create();

    token_array_append(arr, token_create(TOKEN_WORD));
    token_array_append(arr, token_create(TOKEN_IF));
    token_array_append(arr, token_create(TOKEN_THEN));

    CTEST_ASSERT_EQ(ctest, token_array_size(arr), 3, "size is 3");

    token_array_remove(arr, 1);
    CTEST_ASSERT_EQ(ctest, token_array_size(arr), 2, "size is 2 after remove");

    token_t *elem = token_array_get(arr, 1);
    CTEST_ASSERT_EQ(ctest, (int)token_get_type(elem), (int)TOKEN_THEN, "THEN shifted to position 1");

    token_array_destroy(&arr);
}

CTEST(test_token_array_remove_first)
{
    token_array_t *arr = token_array_create();

    token_array_append(arr, token_create(TOKEN_WORD));
    token_array_append(arr, token_create(TOKEN_IF));

    token_array_remove(arr, 0);
    CTEST_ASSERT_EQ(ctest, token_array_size(arr), 1, "size is 1");

    token_t *elem = token_array_get(arr, 0);
    CTEST_ASSERT_EQ(ctest, (int)token_get_type(elem), (int)TOKEN_IF, "IF now at position 0");

    token_array_destroy(&arr);
}

// ============================================================================
// Clear
// ============================================================================

CTEST(test_token_array_clear)
{
    token_array_t *arr = token_array_create();

    for (int i = 0; i < 5; i++)
    {
        token_array_append(arr, token_create(TOKEN_WORD));
    }

    CTEST_ASSERT_EQ(ctest, token_array_size(arr), 5, "size is 5");

    token_array_clear(arr);

    CTEST_ASSERT_EQ(ctest, token_array_size(arr), 0, "size is 0 after clear");
    CTEST_ASSERT_TRUE(ctest, token_array_is_empty(arr), "array is empty");

    token_array_destroy(&arr);
}

// ============================================================================
// Growth
// ============================================================================

CTEST(test_token_array_growth)
{
    token_array_t *arr = token_array_create();

    int initial_capacity = token_array_capacity(arr);

    // Add more than initial capacity
    for (int i = 0; i < 20; i++)
    {
        token_array_append(arr, token_create(TOKEN_WORD));
    }

    CTEST_ASSERT_EQ(ctest, token_array_size(arr), 20, "size is 20");
    CTEST_ASSERT_GT(ctest, (int)token_array_capacity(arr), initial_capacity, "capacity grew");

    token_array_destroy(&arr);
}

// ============================================================================
// Resize
// ============================================================================

CTEST(test_token_array_resize_larger)
{
    token_array_t *arr = token_array_create();

    token_array_append(arr, token_create(TOKEN_WORD));
    token_array_append(arr, token_create(TOKEN_IF));

    token_array_resize(arr, 100);
    CTEST_ASSERT_EQ(ctest, (int)token_array_capacity(arr), 100, "capacity is 100");
    CTEST_ASSERT_EQ(ctest, token_array_size(arr), 2, "size unchanged");

    token_array_destroy(&arr);
}

CTEST(test_token_array_resize_smaller)
{
    token_array_t *arr = token_array_create();

    for (int i = 0; i < 10; i++)
    {
        token_array_append(arr, token_create(TOKEN_WORD));
    }

    CTEST_ASSERT_EQ(ctest, token_array_size(arr), 10, "size is 10");

    token_array_resize(arr, 5);
    CTEST_ASSERT_EQ(ctest, token_array_size(arr), 5, "size reduced to 5");

    token_array_destroy(&arr);
}

// ============================================================================
// Find
// ============================================================================

CTEST(test_token_array_find)
{
    token_array_t *arr = token_array_create();

    token_t *tok1 = token_create(TOKEN_WORD);
    token_t *tok2 = token_create(TOKEN_IF);
    token_t *tok3 = token_create(TOKEN_THEN);

    token_array_append(arr, tok1);
    token_array_append(arr, tok2);
    token_array_append(arr, tok3);

    int32_t idx = -1;
    int result = token_array_find(arr, tok2, &idx);

    CTEST_ASSERT_EQ(ctest, result, 0, "find succeeds");
    CTEST_ASSERT_EQ(ctest, (int)idx, 1, "found at index 1");

    token_array_destroy(&arr);
}

CTEST(test_token_array_find_not_found)
{
    token_array_t *arr = token_array_create();

    token_array_append(arr, token_create(TOKEN_WORD));

    token_t *other = token_create(TOKEN_IF);
    int32_t idx = -1;
    int result = token_array_find(arr, other, &idx);

    CTEST_ASSERT_NE(ctest, result, 0, "find fails");

    token_destroy(&other);
    token_array_destroy(&arr);
}

// ============================================================================
// Find with Compare
// ============================================================================

static int compare_token_type(const token_t *tok, const void *user_data)
{
    token_type_t *target_type = (token_type_t *)user_data;
    return (token_get_type(tok) == *target_type) ? 0 : 1;
}

CTEST(test_token_array_find_with_compare)
{
    token_array_t *arr = token_array_create();

    token_array_append(arr, token_create(TOKEN_WORD));
    token_array_append(arr, token_create(TOKEN_IF));
    token_array_append(arr, token_create(TOKEN_WORD));

    token_type_t target = TOKEN_IF;
    int32_t idx = -1;
    int result = token_array_find_with_compare(arr, &target, compare_token_type, &idx);

    CTEST_ASSERT_EQ(ctest, result, 0, "find with compare succeeds");
    CTEST_ASSERT_EQ(ctest, (int)idx, 1, "found TOKEN_IF at index 1");

    token_array_destroy(&arr);
}

// ============================================================================
// Foreach
// ============================================================================

static int foreach_count = 0;
static void count_tokens(token_t *tok, void *user_data)
{
    (void)tok;
    int *count_ptr = (int *)user_data;
    (*count_ptr)++;
}

CTEST(test_token_array_foreach)
{
    token_array_t *arr = token_array_create();

    token_array_append(arr, token_create(TOKEN_WORD));
    token_array_append(arr, token_create(TOKEN_IF));
    token_array_append(arr, token_create(TOKEN_THEN));

    int count = 0;
    token_array_foreach(arr, count_tokens, &count);

    CTEST_ASSERT_EQ(ctest, count, 3, "foreach visited all 3 tokens");

    token_array_destroy(&arr);
}

// ============================================================================
// Create with Free Function
// ============================================================================

static int free_call_count = 0;

static void counting_free_func(token_t **tok)
{
    if (tok && *tok)
    {
        free_call_count++;
        token_destroy(tok);
    }
}

CTEST(test_token_array_create_with_free)
{
    free_call_count = 0;

    token_array_t *arr = token_array_create_with_free(counting_free_func);
    CTEST_ASSERT_NOT_NULL(ctest, arr, "array created");

    token_array_append(arr, token_create(TOKEN_WORD));
    token_array_append(arr, token_create(TOKEN_IF));

    token_array_destroy(&arr);

    CTEST_ASSERT_EQ(ctest, free_call_count, 2, "free function called twice");
}

CTEST(test_token_array_set_with_free)
{
    free_call_count = 0;

    token_array_t *arr = token_array_create_with_free(counting_free_func);
    token_array_append(arr, token_create(TOKEN_WORD));

    token_array_set(arr, 0, token_create(TOKEN_IF));

    CTEST_ASSERT_EQ(ctest, free_call_count, 1, "free function called for replaced element");

    token_array_destroy(&arr);
}

// ============================================================================
// Main test runner
// ============================================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    arena_start();

    CTestEntry *suite[] = {
        CTEST_ENTRY(test_token_array_create_destroy),
        CTEST_ENTRY(test_token_array_is_empty),

        CTEST_ENTRY(test_token_array_append_get),
        CTEST_ENTRY(test_token_array_get_out_of_bounds),
        CTEST_ENTRY(test_token_array_get_negative_index),

        CTEST_ENTRY(test_token_array_set),
        CTEST_ENTRY(test_token_array_remove),
        CTEST_ENTRY(test_token_array_remove_first),

        CTEST_ENTRY(test_token_array_clear),

        CTEST_ENTRY(test_token_array_growth),

        CTEST_ENTRY(test_token_array_resize_larger),
        CTEST_ENTRY(test_token_array_resize_smaller),

        CTEST_ENTRY(test_token_array_find),
        CTEST_ENTRY(test_token_array_find_not_found),

        CTEST_ENTRY(test_token_array_find_with_compare),

        CTEST_ENTRY(test_token_array_foreach),

        CTEST_ENTRY(test_token_array_create_with_free),
        CTEST_ENTRY(test_token_array_set_with_free),

        NULL
    };

    int result = ctest_run_suite(suite);

    arena_end();

    return result;
}

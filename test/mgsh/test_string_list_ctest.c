/*
 * test_string_list_ctest.c
 *
 * Unit tests for string_list.c - dynamic array of string_t pointers
 */

#include "ctest.h"
#include "string_list.h"
#include "string_t.h"
#include "lib.h"
#include "xalloc.h"

// ============================================================================
// Basic lifecycle
// ============================================================================

CTEST(test_string_list_create_destroy)
{
    string_list_t *list = string_list_create();
    CTEST_ASSERT_NOT_NULL(ctest, list, "list created");
    CTEST_ASSERT_EQ(ctest, 0, string_list_size(list), "size is 0");
    string_list_destroy(&list);
    CTEST_ASSERT_NULL(ctest, list, "list destroyed");
}

CTEST(test_string_list_create_from_cstr_array_fixed_len)
{
    const char *strs[] = {"hello", "world", "test"};
    string_list_t *list = string_list_create_from_cstr_array(strs, 3);
    CTEST_ASSERT_NOT_NULL(ctest, list, "list created");
    CTEST_ASSERT_EQ(ctest, 3, string_list_size(list), "size is 3");

    const string_t *s0 = string_list_at(list, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s0), "hello", "element 0 is hello");

    const string_t *s2 = string_list_at(list, 2);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s2), "test", "element 2 is test");

    string_list_destroy(&list);
}

CTEST(test_string_list_create_from_cstr_array_null_terminated)
{
    const char *strs[] = {"one", "two", "three", NULL};
    string_list_t *list = string_list_create_from_cstr_array(strs, -1);
    CTEST_ASSERT_NOT_NULL(ctest, list, "list created");
    CTEST_ASSERT_EQ(ctest, 3, string_list_size(list), "size is 3");

    const string_t *s1 = string_list_at(list, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s1), "two", "element 1 is two");

    string_list_destroy(&list);
}

CTEST(test_string_list_create_from_empty_array)
{
    const char *strs[] = {NULL};
    string_list_t *list = string_list_create_from_cstr_array(strs, 0);
    CTEST_ASSERT_NOT_NULL(ctest, list, "list created");
    CTEST_ASSERT_EQ(ctest, 0, string_list_size(list), "size is 0");
    string_list_destroy(&list);
}

// ============================================================================
// Element access
// ============================================================================

CTEST(test_string_list_at_valid_index)
{
    string_list_t *list = string_list_create();
    string_t *s1 = string_create_from_cstr("first");
    string_t *s2 = string_create_from_cstr("second");
    string_list_push_back(list, s1);
    string_list_push_back(list, s2);
    string_destroy(&s1);
    string_destroy(&s2);

    const string_t *elem = string_list_at(list, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem), "first", "at(0) is first");

    elem = string_list_at(list, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem), "second", "at(1) is second");

    string_list_destroy(&list);
}

CTEST(test_string_list_at_out_of_bounds)
{
    string_list_t *list = string_list_create();
    string_t *s = string_create_from_cstr("test");
    string_list_push_back(list, s);
    string_destroy(&s);

    const string_t *elem = string_list_at(list, 10);
    CTEST_ASSERT_NULL(ctest, elem, "at(10) returns NULL");

    elem = string_list_at(list, -1);
    CTEST_ASSERT_NULL(ctest, elem, "at(-1) returns NULL");

    string_list_destroy(&list);
}

// ============================================================================
// Push back operations
// ============================================================================

CTEST(test_string_list_push_back_copy)
{
    string_list_t *list = string_list_create();

    string_t *s1 = string_create_from_cstr("apple");
    string_list_push_back(list, s1);
    string_destroy(&s1);

    string_t *s2 = string_create_from_cstr("banana");
    string_list_push_back(list, s2);
    string_destroy(&s2);

    CTEST_ASSERT_EQ(ctest, 2, string_list_size(list), "size is 2");

    const string_t *elem0 = string_list_at(list, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem0), "apple", "elem 0 is apple");

    const string_t *elem1 = string_list_at(list, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem1), "banana", "elem 1 is banana");

    string_list_destroy(&list);
}

CTEST(test_string_list_move_push_back)
{
    string_list_t *list = string_list_create();

    string_t *s1 = string_create_from_cstr("moved");
    string_list_move_push_back(list, &s1);
    CTEST_ASSERT_NULL(ctest, s1, "source set to NULL after move");

    CTEST_ASSERT_EQ(ctest, 1, string_list_size(list), "size is 1");
    const string_t *elem = string_list_at(list, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem), "moved", "elem is moved");

    string_list_destroy(&list);
}

CTEST(test_string_list_growth)
{
    string_list_t *list = string_list_create();

    // Add more than initial capacity (4)
    for (int i = 0; i < 10; i++)
    {
        char buf[20];
        snprintf(buf, sizeof(buf), "item%d", i);
        string_t *s = string_create_from_cstr(buf);
        string_list_push_back(list, s);
        string_destroy(&s);
    }

    CTEST_ASSERT_EQ(ctest, 10, string_list_size(list), "size is 10");
    CTEST_ASSERT_GT(ctest, (int)list->capacity, 4, "capacity grew");

    const string_t *elem = string_list_at(list, 9);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem), "item9", "last elem correct");

    string_list_destroy(&list);
}

// ============================================================================
// Insert operations
// ============================================================================

CTEST(test_string_list_insert_at_beginning)
{
    string_list_t *list = string_list_create();

    string_t *s1 = string_create_from_cstr("second");
    string_list_push_back(list, s1);
    string_destroy(&s1);

    string_t *s2 = string_create_from_cstr("first");
    string_list_insert(list, 0, s2);
    string_destroy(&s2);

    CTEST_ASSERT_EQ(ctest, 2, string_list_size(list), "size is 2");

    const string_t *elem0 = string_list_at(list, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem0), "first", "elem 0 is first");

    const string_t *elem1 = string_list_at(list, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem1), "second", "elem 1 is second");

    string_list_destroy(&list);
}

CTEST(test_string_list_insert_at_end)
{
    string_list_t *list = string_list_create();

    string_t *s1 = string_create_from_cstr("first");
    string_list_push_back(list, s1);
    string_destroy(&s1);

    string_t *s2 = string_create_from_cstr("second");
    string_list_insert(list, 1, s2);
    string_destroy(&s2);

    CTEST_ASSERT_EQ(ctest, 2, string_list_size(list), "size is 2");

    const string_t *elem1 = string_list_at(list, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem1), "second", "elem 1 is second");

    string_list_destroy(&list);
}

CTEST(test_string_list_insert_clamps_negative_index)
{
    string_list_t *list = string_list_create();

    string_t *s1 = string_create_from_cstr("elem");
    string_list_insert(list, -5, s1);
    string_destroy(&s1);

    CTEST_ASSERT_EQ(ctest, 1, string_list_size(list), "size is 1");
    const string_t *elem = string_list_at(list, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem), "elem", "inserted at start");

    string_list_destroy(&list);
}

CTEST(test_string_list_insert_null_creates_empty_string)
{
    string_list_t *list = string_list_create();

    string_list_insert(list, 0, NULL);

    CTEST_ASSERT_EQ(ctest, 1, string_list_size(list), "size is 1");
    const string_t *elem = string_list_at(list, 0);
    CTEST_ASSERT_EQ(ctest, 0, (int)string_length(elem), "empty string");

    string_list_destroy(&list);
}

CTEST(test_string_list_move_insert)
{
    string_list_t *list = string_list_create();

    string_t *s1 = string_create_from_cstr("first");
    string_t *s2 = string_create_from_cstr("second");
    string_list_move_push_back(list, &s1);
    string_list_move_push_back(list, &s2);

    string_t *mid = string_create_from_cstr("middle");
    string_list_move_insert(list, 1, &mid);
    CTEST_ASSERT_NULL(ctest, mid, "source set to NULL");

    CTEST_ASSERT_EQ(ctest, 3, string_list_size(list), "size is 3");

    const string_t *elem1 = string_list_at(list, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem1), "middle", "elem 1 is middle");

    string_list_destroy(&list);
}

// ============================================================================
// Erase and clear
// ============================================================================

CTEST(test_string_list_erase_middle)
{
    string_list_t *list = string_list_create();

    const char *strs[] = {"a", "b", "c"};
    for (int i = 0; i < 3; i++)
    {
        string_t *s = string_create_from_cstr(strs[i]);
        string_list_push_back(list, s);
        string_destroy(&s);
    }

    string_list_erase(list, 1);

    CTEST_ASSERT_EQ(ctest, 2, string_list_size(list), "size is 2");

    const string_t *elem0 = string_list_at(list, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem0), "a", "elem 0 is a");

    const string_t *elem1 = string_list_at(list, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem1), "c", "elem 1 is c");

    string_list_destroy(&list);
}

CTEST(test_string_list_clear)
{
    string_list_t *list = string_list_create();

    for (int i = 0; i < 5; i++)
    {
        char buf[10];
        snprintf(buf, sizeof(buf), "item%d", i);
        string_t *s = string_create_from_cstr(buf);
        string_list_push_back(list, s);
        string_destroy(&s);
    }

    CTEST_ASSERT_EQ(ctest, 5, string_list_size(list), "size was 5");

    string_list_clear(list);

    CTEST_ASSERT_EQ(ctest, 0, string_list_size(list), "size is 0 after clear");

    string_list_destroy(&list);
}

// ============================================================================
// Conversion utilities
// ============================================================================

CTEST(test_string_list_release_cstr_array)
{
    string_list_t *list = string_list_create();

    string_t *s1 = string_create_from_cstr("foo");
    string_t *s2 = string_create_from_cstr("bar");
    string_list_push_back(list, s1);
    string_list_push_back(list, s2);
    string_destroy(&s1);
    string_destroy(&s2);

    int out_size = 0;
    char **arr = string_list_release_cstr_array(&list, &out_size);

    CTEST_ASSERT_NULL(ctest, list, "list destroyed");
    CTEST_ASSERT_EQ(ctest, 2, out_size, "out_size is 2");
    CTEST_ASSERT_STR_EQ(ctest, arr[0], "foo", "arr[0] is foo");
    CTEST_ASSERT_STR_EQ(ctest, arr[1], "bar", "arr[1] is bar");
    CTEST_ASSERT_NULL(ctest, arr[2], "arr[2] is NULL");

    xfree(arr[0]);
    xfree(arr[1]);
    xfree(arr);
}

CTEST(test_string_list_join_move_with_separator)
{
    string_list_t *list = string_list_create();

    const char *strs[] = {"one", "two", "three"};
    for (int i = 0; i < 3; i++)
    {
        string_t *s = string_create_from_cstr(strs[i]);
        string_list_push_back(list, s);
        string_destroy(&s);
    }

    string_t *result = string_list_join_move(&list, " ");

    CTEST_ASSERT_NULL(ctest, list, "list destroyed");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(result), "one two three", "joined string correct");

    string_destroy(&result);
}

CTEST(test_string_list_join_move_empty_list)
{
    string_list_t *list = string_list_create();

    string_t *result = string_list_join_move(&list, ",");

    CTEST_ASSERT_NULL(ctest, list, "list destroyed");
    CTEST_ASSERT_EQ(ctest, 0, (int)string_length(result), "empty result");

    string_destroy(&result);
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
        CTEST_ENTRY(test_string_list_create_destroy),
        CTEST_ENTRY(test_string_list_create_from_cstr_array_fixed_len),
        CTEST_ENTRY(test_string_list_create_from_cstr_array_null_terminated),
        CTEST_ENTRY(test_string_list_create_from_empty_array),

        CTEST_ENTRY(test_string_list_at_valid_index),
        CTEST_ENTRY(test_string_list_at_out_of_bounds),

        CTEST_ENTRY(test_string_list_push_back_copy),
        CTEST_ENTRY(test_string_list_move_push_back),
        CTEST_ENTRY(test_string_list_growth),

        CTEST_ENTRY(test_string_list_insert_at_beginning),
        CTEST_ENTRY(test_string_list_insert_at_end),
        CTEST_ENTRY(test_string_list_insert_clamps_negative_index),
        CTEST_ENTRY(test_string_list_insert_null_creates_empty_string),
        CTEST_ENTRY(test_string_list_move_insert),

        CTEST_ENTRY(test_string_list_erase_middle),
        CTEST_ENTRY(test_string_list_clear),

        CTEST_ENTRY(test_string_list_release_cstr_array),
        CTEST_ENTRY(test_string_list_join_move_with_separator),
        CTEST_ENTRY(test_string_list_join_move_empty_list),

        NULL
    };

    int result = ctest_run_suite(suite);

    arena_end();

    return result;
}

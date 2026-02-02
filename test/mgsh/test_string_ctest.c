#include <string.h>
#include "ctest.h"
#include "string_t.h"
#include "xalloc.h"

// ------------------------------------------------------------
// Creation
// ------------------------------------------------------------

CTEST(test_string_create_basic)
{
    string_t* s = string_create();
    CTEST_ASSERT_NOT_NULL(ctest, s, "string created");
    CTEST_ASSERT_EQ(ctest, string_length(s), 0, "initial length 0");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s), "", "initial cstr empty");
    string_destroy(&s);
}

CTEST(test_string_create_from_cstr)
{
    string_t* s = string_create_from_cstr("hello");
    CTEST_ASSERT_EQ(ctest, string_length(s), 5, "length 5");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s), "hello", "cstr matches");
    string_destroy(&s);
}

CTEST(test_string_create_from_n_chars)
{
    string_t* s = string_create_from_n_chars(4, 'x');
    CTEST_ASSERT_EQ(ctest, string_length(s), 4, "length 4");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s), "xxxx", "content matches");
    string_destroy(&s);
}

// ------------------------------------------------------------
// Setters
// ------------------------------------------------------------

CTEST(test_string_set_cstr)
{
    string_t* s = string_create();
    string_set_cstr(s, "abc");
    CTEST_ASSERT_EQ(ctest, string_length(s), 3, "length 3");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s), "abc", "content matches");
    string_destroy(&s);
}

CTEST(test_string_set_char)
{
    string_t* s = string_create();
    string_set_char(s, 'Z');
    CTEST_ASSERT_EQ(ctest, string_length(s), 1, "length 1");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s), "Z", "content matches");
    string_destroy(&s);
}

CTEST(test_string_set_data)
{
    string_t* s = string_create();
    string_set_data(s, "abcdef", 3);
    CTEST_ASSERT_EQ(ctest, string_length(s), 3, "length 3");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s), "abc", "content matches");
    string_destroy(&s);
}

// ------------------------------------------------------------
// Substring creation
// ------------------------------------------------------------

CTEST(test_string_substring_basic)
{
    string_t* s = string_create_from_cstr("abcdef");
    string_t* sub = string_substring(s, 1, 4);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(sub), "bcd", "substring matches");
    string_destroy(&s);
    string_destroy(&sub);
}

CTEST(test_string_substring_clamped)
{
    string_t* s = string_create_from_cstr("abcdef");
    string_t* sub = string_substring(s, -5, 100);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(sub), "abcdef", "clamped substring matches");
    string_destroy(&s);
    string_destroy(&sub);
}

CTEST(test_string_substring_empty)
{
    string_t* s = string_create_from_cstr("abcdef");
    string_t* sub = string_substring(s, 3, 3);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(sub), "", "empty substring");
    string_destroy(&s);
    string_destroy(&sub);
}

// ------------------------------------------------------------
// Append
// ------------------------------------------------------------

CTEST(test_string_append)
{
    string_t* a = string_create_from_cstr("hello");
    string_t* b = string_create_from_cstr(" world");
    string_append(a, b);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(a), "hello world", "append works");
    string_destroy(&a);
    string_destroy(&b);
}

CTEST(test_string_append_cstr)
{
    string_t* s = string_create_from_cstr("foo");
    string_append_cstr(s, "bar");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s), "foobar", "append cstr works");
    string_destroy(&s);
}

CTEST(test_string_append_substring)
{
    string_t* s = string_create_from_cstr("hello");
    string_t* src = string_create_from_cstr("ABCDE");
    string_append_substring(s, src, 1, 4);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s), "helloBCD", "append substring works");
    string_destroy(&s);
    string_destroy(&src);
}

// ------------------------------------------------------------
// Push/pop
// ------------------------------------------------------------

CTEST(test_string_push_pop)
{
    string_t* s = string_create_from_cstr("ab");
    string_push_back(s, 'c');
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s), "abc", "push works");

    string_pop_back(s);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s), "ab", "pop works");

    string_destroy(&s);
}

// ------------------------------------------------------------
// Resize
// ------------------------------------------------------------

CTEST(test_string_resize_grow)
{
    string_t* s = string_create_from_cstr("abc");
    string_resize_with_char(s, 6, 'x');
    CTEST_ASSERT_EQ(ctest, string_length(s), 6, "length 6");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s), "abcxxx", "resize grow fills with 'x'");
    string_destroy(&s);
}

CTEST(test_string_resize_shrink)
{
    string_t* s = string_create_from_cstr("abcdef");
    string_resize(s, 3);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s), "abc", "resize shrink works");
    string_destroy(&s);
}

// ------------------------------------------------------------
// Find
// ------------------------------------------------------------

CTEST(test_string_find_basic)
{
    string_t* s = string_create_from_cstr("hello world");
    string_t* sub = string_create_from_cstr("world");
    int idx = string_find(s, sub);
    CTEST_ASSERT_EQ(ctest, idx, 6, "find works");
    string_destroy(&s);
    string_destroy(&sub);
}

CTEST(test_string_find_cstr)
{
    string_t* s = string_create_from_cstr("banana");
    int idx = string_find_cstr(s, "ana");
    CTEST_ASSERT_EQ(ctest, idx, 1, "find cstr works");
    string_destroy(&s);
}

// ------------------------------------------------------------
// Compare
// ------------------------------------------------------------

CTEST(test_string_compare_equal)
{
    string_t* a = string_create_from_cstr("abc");
    string_t* b = string_create_from_cstr("abc");
    CTEST_ASSERT_EQ(ctest, string_compare(a, b), 0, "equal strings");
    string_destroy(&a);
    string_destroy(&b);
}

CTEST(test_string_compare_less)
{
    string_t* a = string_create_from_cstr("abc");
    string_t* b = string_create_from_cstr("abd");
    CTEST_ASSERT_TRUE(ctest, string_compare(a, b) < 0, "abc < abd");
    string_destroy(&a);
    string_destroy(&b);
}

CTEST(test_string_compare_cstr)
{
    string_t* a = string_create_from_cstr("hello");
    CTEST_ASSERT_EQ(ctest, string_compare_cstr(a, "hello"), 0, "compare cstr equal");
    CTEST_ASSERT_TRUE(ctest, string_compare_cstr(a, "hellp") < 0, "compare cstr less");
    string_destroy(&a);
}

// ------------------------------------------------------------
// Release / destroy
// ------------------------------------------------------------

CTEST(test_string_release)
{
    string_t* s = string_create_from_cstr("xyz");
    char* raw = string_release(&s);
    CTEST_ASSERT_NOT_NULL(ctest, raw, "release returned buffer");
    CTEST_ASSERT_NULL(ctest, s, "string pointer null after release");
    CTEST_ASSERT_STR_EQ(ctest, raw, "xyz", "raw buffer matches");
    xfree(raw);
}

CTEST(test_string_destroy)
{
    string_t* s = string_create_from_cstr("abc");
    string_destroy(&s);
    CTEST_ASSERT_NULL(ctest, s, "destroy nulls pointer");
}

// ------------------------------------------------------------
// Test suite entry
// ------------------------------------------------------------

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    arena_start();

    CTestEntry* suite[] = {
        CTEST_ENTRY(test_string_create_basic),
        CTEST_ENTRY(test_string_create_from_cstr),
        CTEST_ENTRY(test_string_create_from_n_chars),
        CTEST_ENTRY(test_string_set_cstr),
        CTEST_ENTRY(test_string_set_char),
        CTEST_ENTRY(test_string_set_data),
        CTEST_ENTRY(test_string_substring_basic),
        CTEST_ENTRY(test_string_substring_clamped),
        CTEST_ENTRY(test_string_substring_empty),
        CTEST_ENTRY(test_string_append),
        CTEST_ENTRY(test_string_append_cstr),
        CTEST_ENTRY(test_string_append_substring),
        CTEST_ENTRY(test_string_push_pop),
        CTEST_ENTRY(test_string_resize_grow),
        CTEST_ENTRY(test_string_resize_shrink),
        CTEST_ENTRY(test_string_find_basic),
        CTEST_ENTRY(test_string_find_cstr),
        CTEST_ENTRY(test_string_compare_equal),
        CTEST_ENTRY(test_string_compare_less),
        CTEST_ENTRY(test_string_compare_cstr),
        CTEST_ENTRY(test_string_release),
        CTEST_ENTRY(test_string_destroy),
        NULL
    };

    int result = ctest_run_suite(suite);

    arena_end();
    return result;
}

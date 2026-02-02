/**
 * @file test_pattern_removal_ctest.c
 * @brief Unit tests for pattern removal parameter expansion functions
 */

#include "ctest.h"
#include "pattern_removal.h"
#include "string_t.h"
#include "xalloc.h"

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Helper to test prefix removal with string inputs
 */
static void test_remove_prefix_smallest_helper(CTest *ctest,
                                                const char *value,
                                                const char *pattern,
                                                const char *expected)
{
    string_t *val = string_create_from_cstr(value);
    string_t *pat = string_create_from_cstr(pattern);
    string_t *result = remove_prefix_smallest(val, pat);

    CTEST_ASSERT_NOT_NULL(ctest, result, "result not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(result), expected, "result matches expected");

    string_destroy(&result);
    string_destroy(&pat);
    string_destroy(&val);
}

static void test_remove_prefix_largest_helper(CTest *ctest,
                                               const char *value,
                                               const char *pattern,
                                               const char *expected)
{
    string_t *val = string_create_from_cstr(value);
    string_t *pat = string_create_from_cstr(pattern);
    string_t *result = remove_prefix_largest(val, pat);

    CTEST_ASSERT_NOT_NULL(ctest, result, "result not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(result), expected, "result matches expected");

    string_destroy(&result);
    string_destroy(&pat);
    string_destroy(&val);
}

static void test_remove_suffix_smallest_helper(CTest *ctest,
                                                const char *value,
                                                const char *pattern,
                                                const char *expected)
{
    string_t *val = string_create_from_cstr(value);
    string_t *pat = string_create_from_cstr(pattern);
    string_t *result = remove_suffix_smallest(val, pat);

    CTEST_ASSERT_NOT_NULL(ctest, result, "result not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(result), expected, "result matches expected");

    string_destroy(&result);
    string_destroy(&pat);
    string_destroy(&val);
}

static void test_remove_suffix_largest_helper(CTest *ctest,
                                               const char *value,
                                               const char *pattern,
                                               const char *expected)
{
    string_t *val = string_create_from_cstr(value);
    string_t *pat = string_create_from_cstr(pattern);
    string_t *result = remove_suffix_largest(val, pat);

    CTEST_ASSERT_NOT_NULL(ctest, result, "result not NULL");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(result), expected, "result matches expected");

    string_destroy(&result);
    string_destroy(&pat);
    string_destroy(&val);
}

/* ============================================================================
 * remove_prefix_smallest (${var#pattern}) Tests
 * ============================================================================ */

CTEST(test_prefix_smallest_basic)
{
    // Basic path removal: "path/to/file" with pattern "*/" removes "path/"
    test_remove_prefix_smallest_helper(ctest, "path/to/file", "*/", "to/file");
    (void)ctest;
}

CTEST(test_prefix_smallest_no_match)
{
    // No match - return original
    test_remove_prefix_smallest_helper(ctest, "hello", "xyz*", "hello");
    (void)ctest;
}

CTEST(test_prefix_smallest_empty_value)
{
    // Empty value
    test_remove_prefix_smallest_helper(ctest, "", "*", "");
    (void)ctest;
}

CTEST(test_prefix_smallest_empty_pattern)
{
    // Empty pattern - no removal
    test_remove_prefix_smallest_helper(ctest, "hello", "", "hello");
    (void)ctest;
}

CTEST(test_prefix_smallest_match_all)
{
    // Pattern * matches empty string (shortest match) - no removal
    test_remove_prefix_smallest_helper(ctest, "hello", "*", "hello");
    (void)ctest;
}

CTEST(test_prefix_smallest_multiple_slashes)
{
    // Multiple slashes - should match shortest (first one)
    test_remove_prefix_smallest_helper(ctest, "a/b/c/d", "*/", "b/c/d");
    (void)ctest;
}

CTEST(test_prefix_smallest_question_mark)
{
    // Question mark matches single char
    test_remove_prefix_smallest_helper(ctest, "xhello", "?", "hello");
    (void)ctest;
}

CTEST(test_prefix_smallest_literal)
{
    // Literal prefix
    test_remove_prefix_smallest_helper(ctest, "prefixrest", "prefix", "rest");
    (void)ctest;
}

CTEST(test_prefix_smallest_bracket)
{
    // Bracket expression
    test_remove_prefix_smallest_helper(ctest, "abc", "[ab]", "bc");
    (void)ctest;
}

CTEST(test_prefix_smallest_null_inputs)
{
    string_t *result = remove_prefix_smallest(NULL, NULL);
    CTEST_ASSERT_NOT_NULL(ctest, result, "result not NULL for NULL inputs");
    CTEST_ASSERT_EQ(ctest, string_length(result), 0, "result is empty");
    string_destroy(&result);
    (void)ctest;
}

/* ============================================================================
 * remove_prefix_largest (${var##pattern}) Tests
 * ============================================================================ */

CTEST(test_prefix_largest_basic)
{
    // Basic path removal: "path/to/file" with pattern "*/" removes "path/to/"
    test_remove_prefix_largest_helper(ctest, "path/to/file", "*/", "file");
    (void)ctest;
}

CTEST(test_prefix_largest_no_match)
{
    // No match - return original
    test_remove_prefix_largest_helper(ctest, "hello", "xyz*", "hello");
    (void)ctest;
}

CTEST(test_prefix_largest_empty_value)
{
    // Empty value
    test_remove_prefix_largest_helper(ctest, "", "*", "");
    (void)ctest;
}

CTEST(test_prefix_largest_empty_pattern)
{
    // Empty pattern - no removal
    test_remove_prefix_largest_helper(ctest, "hello", "", "hello");
    (void)ctest;
}

CTEST(test_prefix_largest_match_all)
{
    // Pattern matches entire string - remove all
    test_remove_prefix_largest_helper(ctest, "hello", "*", "");
    (void)ctest;
}

CTEST(test_prefix_largest_multiple_slashes)
{
    // Multiple slashes - should match longest (last one)
    test_remove_prefix_largest_helper(ctest, "a/b/c/d", "*/", "d");
    (void)ctest;
}

CTEST(test_prefix_largest_vs_smallest)
{
    // Compare with smallest: "one-two-three" with pattern "*-"
    // Smallest removes "one-", largest removes "one-two-"
    test_remove_prefix_smallest_helper(ctest, "one-two-three", "*-", "two-three");
    test_remove_prefix_largest_helper(ctest, "one-two-three", "*-", "three");
    (void)ctest;
}

CTEST(test_prefix_largest_literal)
{
    // Literal prefix
    test_remove_prefix_largest_helper(ctest, "prefixrest", "prefix", "rest");
    (void)ctest;
}

CTEST(test_prefix_largest_null_inputs)
{
    string_t *result = remove_prefix_largest(NULL, NULL);
    CTEST_ASSERT_NOT_NULL(ctest, result, "result not NULL for NULL inputs");
    CTEST_ASSERT_EQ(ctest, string_length(result), 0, "result is empty");
    string_destroy(&result);
    (void)ctest;
}

/* ============================================================================
 * remove_suffix_smallest (${var%pattern}) Tests
 * ============================================================================ */

CTEST(test_suffix_smallest_basic)
{
    // Basic extension removal: "file.txt" with pattern ".*" removes ".txt"
    test_remove_suffix_smallest_helper(ctest, "file.txt", ".*", "file");
    (void)ctest;
}

CTEST(test_suffix_smallest_no_match)
{
    // No match - return original
    test_remove_suffix_smallest_helper(ctest, "hello", "*xyz", "hello");
    (void)ctest;
}

CTEST(test_suffix_smallest_empty_value)
{
    // Empty value
    test_remove_suffix_smallest_helper(ctest, "", "*", "");
    (void)ctest;
}

CTEST(test_suffix_smallest_empty_pattern)
{
    // Empty pattern - no removal
    test_remove_suffix_smallest_helper(ctest, "hello", "", "hello");
    (void)ctest;
}

CTEST(test_suffix_smallest_match_all)
{
    // Pattern * matches empty string (shortest match) - no removal
    test_remove_suffix_smallest_helper(ctest, "hello", "*", "hello");
    (void)ctest;
}

CTEST(test_suffix_smallest_path)
{
    // Path removal: "/usr/local/bin" with pattern "/*" removes "/bin"
    test_remove_suffix_smallest_helper(ctest, "/usr/local/bin", "/*", "/usr/local");
    (void)ctest;
}

CTEST(test_suffix_smallest_multiple_dots)
{
    // Multiple dots - should match shortest (last one)
    test_remove_suffix_smallest_helper(ctest, "archive.tar.gz", ".*", "archive.tar");
    (void)ctest;
}

CTEST(test_suffix_smallest_question_mark)
{
    // Question mark matches single char
    test_remove_suffix_smallest_helper(ctest, "hellox", "?", "hello");
    (void)ctest;
}

CTEST(test_suffix_smallest_literal)
{
    // Literal suffix
    test_remove_suffix_smallest_helper(ctest, "textsuffix", "suffix", "text");
    (void)ctest;
}

CTEST(test_suffix_smallest_bracket)
{
    // Bracket expression
    test_remove_suffix_smallest_helper(ctest, "abc", "[bc]", "ab");
    (void)ctest;
}

CTEST(test_suffix_smallest_null_inputs)
{
    string_t *result = remove_suffix_smallest(NULL, NULL);
    CTEST_ASSERT_NOT_NULL(ctest, result, "result not NULL for NULL inputs");
    CTEST_ASSERT_EQ(ctest, string_length(result), 0, "result is empty");
    string_destroy(&result);
    (void)ctest;
}

/* ============================================================================
 * remove_suffix_largest (${var%%pattern}) Tests
 * ============================================================================ */

CTEST(test_suffix_largest_basic)
{
    // Basic extension removal: "archive.tar.gz" with pattern ".*" removes ".tar.gz"
    test_remove_suffix_largest_helper(ctest, "archive.tar.gz", ".*", "archive");
    (void)ctest;
}

CTEST(test_suffix_largest_no_match)
{
    // No match - return original
    test_remove_suffix_largest_helper(ctest, "hello", "*xyz", "hello");
    (void)ctest;
}

CTEST(test_suffix_largest_empty_value)
{
    // Empty value
    test_remove_suffix_largest_helper(ctest, "", "*", "");
    (void)ctest;
}

CTEST(test_suffix_largest_empty_pattern)
{
    // Empty pattern - no removal
    test_remove_suffix_largest_helper(ctest, "hello", "", "hello");
    (void)ctest;
}

CTEST(test_suffix_largest_match_all)
{
    // Pattern matches entire string - remove all
    test_remove_suffix_largest_helper(ctest, "hello", "*", "");
    (void)ctest;
}

CTEST(test_suffix_largest_path)
{
    // Path removal: "/usr/local/bin" with pattern "/*" removes "/usr/local/bin"
    test_remove_suffix_largest_helper(ctest, "/usr/local/bin", "/*", "");
    (void)ctest;
}

CTEST(test_suffix_largest_multiple_dots)
{
    // Multiple dots - should match longest (all extensions)
    test_remove_suffix_largest_helper(ctest, "archive.tar.gz", ".*", "archive");
    (void)ctest;
}

CTEST(test_suffix_largest_vs_smallest)
{
    // Compare with smallest: "one.two.three" with pattern ".*"
    // Smallest removes ".three", largest removes ".two.three"
    test_remove_suffix_smallest_helper(ctest, "one.two.three", ".*", "one.two");
    test_remove_suffix_largest_helper(ctest, "one.two.three", ".*", "one");
    (void)ctest;
}

CTEST(test_suffix_largest_literal)
{
    // Literal suffix
    test_remove_suffix_largest_helper(ctest, "textsuffix", "suffix", "text");
    (void)ctest;
}

CTEST(test_suffix_largest_null_inputs)
{
    string_t *result = remove_suffix_largest(NULL, NULL);
    CTEST_ASSERT_NOT_NULL(ctest, result, "result not NULL for NULL inputs");
    CTEST_ASSERT_EQ(ctest, string_length(result), 0, "result is empty");
    string_destroy(&result);
    (void)ctest;
}

/* ============================================================================
 * Real-world Examples from Documentation
 * ============================================================================ */

CTEST(test_doc_example_remove_prefix_smallest)
{
    // From docs: path="/usr/local/bin", ${path#/*/} -> "local/bin"
    test_remove_prefix_smallest_helper(ctest, "/usr/local/bin", "/*/", "local/bin");
    (void)ctest;
}

CTEST(test_doc_example_remove_prefix_largest)
{
    // From docs: path="/usr/local/bin", ${path##*/} -> "bin"
    test_remove_prefix_largest_helper(ctest, "/usr/local/bin", "*/", "bin");
    (void)ctest;
}

CTEST(test_doc_example_remove_suffix_smallest)
{
    // From docs: file="report.txt", ${file%.*} -> "report"
    test_remove_suffix_smallest_helper(ctest, "report.txt", ".*", "report");
    (void)ctest;
}

CTEST(test_doc_example_remove_suffix_largest)
{
    // From docs: file="archive.tar.gz", ${file%%.*} -> "archive"
    test_remove_suffix_largest_helper(ctest, "archive.tar.gz", ".*", "archive");
    (void)ctest;
}

CTEST(test_doc_example_path_manipulation)
{
    // From docs: path="/usr/local/bin", ${path%/*} -> "/usr/local"
    test_remove_suffix_smallest_helper(ctest, "/usr/local/bin", "/*", "/usr/local");
    (void)ctest;
}

CTEST(test_doc_example_filename_basename)
{
    // Common idiom: get basename
    test_remove_prefix_largest_helper(ctest, "/path/to/file.txt", "*/", "file.txt");
    (void)ctest;
}

CTEST(test_doc_example_filename_dirname)
{
    // Common idiom: get dirname
    test_remove_suffix_smallest_helper(ctest, "/path/to/file.txt", "/*", "/path/to");
    (void)ctest;
}

CTEST(test_doc_example_extension_removal)
{
    // Remove extension
    test_remove_suffix_smallest_helper(ctest, "document.pdf", ".*", "document");
    (void)ctest;
}

CTEST(test_doc_example_prefix_removal)
{
    // Remove prefix
    test_remove_prefix_smallest_helper(ctest, "prefix-name.txt", "*-", "name.txt");
    (void)ctest;
}

/* ============================================================================
 * Edge Cases
 * ============================================================================ */

CTEST(test_edge_case_single_char)
{
    // Single character value - * matches empty for smallest, entire string for largest
    test_remove_prefix_smallest_helper(ctest, "x", "*", "x");
    test_remove_prefix_largest_helper(ctest, "x", "*", "");
    test_remove_suffix_smallest_helper(ctest, "x", "*", "x");
    test_remove_suffix_largest_helper(ctest, "x", "*", "");
    (void)ctest;
}

CTEST(test_edge_case_pattern_longer_than_value)
{
    // Pattern longer than value - no match
    test_remove_prefix_smallest_helper(ctest, "hi", "hello", "hi");
    test_remove_suffix_smallest_helper(ctest, "hi", "hello", "hi");
    (void)ctest;
}

CTEST(test_edge_case_exact_match)
{
    // Pattern exactly matches value
    test_remove_prefix_smallest_helper(ctest, "hello", "hello", "");
    test_remove_suffix_smallest_helper(ctest, "hello", "hello", "");
    (void)ctest;
}

CTEST(test_edge_case_star_at_start)
{
    // Pattern *-suffix: smallest matches "-suffix", largest matches "prefix-suffix"
    test_remove_suffix_smallest_helper(ctest, "prefix-suffix", "*-suffix", "prefix");
    test_remove_suffix_largest_helper(ctest, "prefix-suffix", "*-suffix", "");
    (void)ctest;
}

CTEST(test_edge_case_star_at_end)
{
    // Pattern prefix-*: smallest matches "prefix-", largest matches "prefix-suffix"
    test_remove_prefix_smallest_helper(ctest, "prefix-suffix", "prefix-*", "suffix");
    test_remove_prefix_largest_helper(ctest, "prefix-suffix", "prefix-*", "");
    (void)ctest;
}

CTEST(test_edge_case_no_separator)
{
    // No separator in value but pattern expects one
    test_remove_prefix_smallest_helper(ctest, "noseparator", "*/", "noseparator");
    test_remove_suffix_smallest_helper(ctest, "noseparator", "/*", "noseparator");
    (void)ctest;
}

CTEST(test_edge_case_multiple_stars)
{
    // Multiple wildcards in pattern
    test_remove_prefix_smallest_helper(ctest, "abc-def-ghi", "*-*-", "ghi");
    (void)ctest;
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    arena_start();

    CTestEntry *suite[] = {
        /* Prefix smallest tests */
        CTEST_ENTRY(test_prefix_smallest_basic),
        CTEST_ENTRY(test_prefix_smallest_no_match),
        CTEST_ENTRY(test_prefix_smallest_empty_value),
        CTEST_ENTRY(test_prefix_smallest_empty_pattern),
        CTEST_ENTRY(test_prefix_smallest_match_all),
        CTEST_ENTRY(test_prefix_smallest_multiple_slashes),
        CTEST_ENTRY(test_prefix_smallest_question_mark),
        CTEST_ENTRY(test_prefix_smallest_literal),
        CTEST_ENTRY(test_prefix_smallest_bracket),
        CTEST_ENTRY(test_prefix_smallest_null_inputs),

        /* Prefix largest tests */
        CTEST_ENTRY(test_prefix_largest_basic),
        CTEST_ENTRY(test_prefix_largest_no_match),
        CTEST_ENTRY(test_prefix_largest_empty_value),
        CTEST_ENTRY(test_prefix_largest_empty_pattern),
        CTEST_ENTRY(test_prefix_largest_match_all),
        CTEST_ENTRY(test_prefix_largest_multiple_slashes),
        CTEST_ENTRY(test_prefix_largest_vs_smallest),
        CTEST_ENTRY(test_prefix_largest_literal),
        CTEST_ENTRY(test_prefix_largest_null_inputs),

        /* Suffix smallest tests */
        CTEST_ENTRY(test_suffix_smallest_basic),
        CTEST_ENTRY(test_suffix_smallest_no_match),
        CTEST_ENTRY(test_suffix_smallest_empty_value),
        CTEST_ENTRY(test_suffix_smallest_empty_pattern),
        CTEST_ENTRY(test_suffix_smallest_match_all),
        CTEST_ENTRY(test_suffix_smallest_path),
        CTEST_ENTRY(test_suffix_smallest_multiple_dots),
        CTEST_ENTRY(test_suffix_smallest_question_mark),
        CTEST_ENTRY(test_suffix_smallest_literal),
        CTEST_ENTRY(test_suffix_smallest_bracket),
        CTEST_ENTRY(test_suffix_smallest_null_inputs),

        /* Suffix largest tests */
        CTEST_ENTRY(test_suffix_largest_basic),
        CTEST_ENTRY(test_suffix_largest_no_match),
        CTEST_ENTRY(test_suffix_largest_empty_value),
        CTEST_ENTRY(test_suffix_largest_empty_pattern),
        CTEST_ENTRY(test_suffix_largest_match_all),
        CTEST_ENTRY(test_suffix_largest_path),
        CTEST_ENTRY(test_suffix_largest_multiple_dots),
        CTEST_ENTRY(test_suffix_largest_vs_smallest),
        CTEST_ENTRY(test_suffix_largest_literal),
        CTEST_ENTRY(test_suffix_largest_null_inputs),

        /* Documentation examples */
        CTEST_ENTRY(test_doc_example_remove_prefix_smallest),
        CTEST_ENTRY(test_doc_example_remove_prefix_largest),
        CTEST_ENTRY(test_doc_example_remove_suffix_smallest),
        CTEST_ENTRY(test_doc_example_remove_suffix_largest),
        CTEST_ENTRY(test_doc_example_path_manipulation),
        CTEST_ENTRY(test_doc_example_filename_basename),
        CTEST_ENTRY(test_doc_example_filename_dirname),
        CTEST_ENTRY(test_doc_example_extension_removal),
        CTEST_ENTRY(test_doc_example_prefix_removal),

        /* Edge cases */
        CTEST_ENTRY(test_edge_case_single_char),
        CTEST_ENTRY(test_edge_case_pattern_longer_than_value),
        CTEST_ENTRY(test_edge_case_exact_match),
        CTEST_ENTRY(test_edge_case_star_at_start),
        CTEST_ENTRY(test_edge_case_star_at_end),
        CTEST_ENTRY(test_edge_case_no_separator),
        CTEST_ENTRY(test_edge_case_multiple_stars),

        NULL
    };

    int result = ctest_run_suite(suite);

    arena_end();

    return result;
}

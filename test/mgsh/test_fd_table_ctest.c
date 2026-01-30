/**
 * @file test_fd_table_ctest.c
 * @brief Unit tests for file descriptor table (fd_table.c)
 */

#include <string.h>
#include "ctest.h"
#include "fd_table.h"
#include "string_t.h"
#include "xalloc.h"

// ------------------------------------------------------------
// Creation and Destruction Tests
// ------------------------------------------------------------

CTEST(test_fd_table_create)
{
    fd_table_t* table = fd_table_create();
    CTEST_ASSERT_NOT_NULL(ctest, table, "table created");
    CTEST_ASSERT_EQ(ctest, fd_table_count(table), 0, "initial count is 0");
    CTEST_ASSERT_EQ(ctest, fd_table_get_highest_fd(table), -1, "highest_fd is -1 when empty");
    fd_table_destroy(&table);
    CTEST_ASSERT_NULL(ctest, table, "table pointer null after destroy");
}

CTEST(test_fd_table_clone)
{
    fd_table_t* table = fd_table_create();

    // Add some entries
    string_t* path1 = string_create_from_cstr("/tmp/file1.txt");
    string_t* path2 = string_create_from_cstr("/dev/null");
    fd_table_add(table, 3, FD_REDIRECTED, path1);
    fd_table_add(table, 5, FD_CLOEXEC | FD_REDIRECTED, path2);
    fd_table_mark_saved(table, 10, 3);

    // Clone the table
    fd_table_t* clone = fd_table_clone(table);
    CTEST_ASSERT_NOT_NULL(ctest, clone, "clone created");
    CTEST_ASSERT_EQ(ctest, fd_table_count(clone), fd_table_count(table), "clone has same count");
    CTEST_ASSERT_EQ(ctest, fd_table_get_highest_fd(clone), fd_table_get_highest_fd(table), "clone has same highest_fd");

    // Verify entries match
    CTEST_ASSERT_TRUE(ctest, fd_table_is_open(clone, 3), "fd 3 is open in clone");
    CTEST_ASSERT_TRUE(ctest, fd_table_has_flag(clone, 3, FD_REDIRECTED), "fd 3 has FD_REDIRECTED in clone");
    CTEST_ASSERT_TRUE(ctest, fd_table_is_open(clone, 5), "fd 5 is open in clone");
    CTEST_ASSERT_TRUE(ctest, fd_table_has_flag(clone, 5, FD_CLOEXEC), "fd 5 has FD_CLOEXEC in clone");
    CTEST_ASSERT_TRUE(ctest, fd_table_is_open(clone, 10), "fd 10 is open in clone");
    CTEST_ASSERT_EQ(ctest, fd_table_get_original(clone, 10), 3, "fd 10 original is 3 in clone");

    // Verify paths are copied
    const string_t* path_clone = fd_table_get_path(clone, 3);
    CTEST_ASSERT_NOT_NULL(ctest, path_clone, "path exists in clone");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(path_clone), "/tmp/file1.txt", "path matches in clone");

    fd_table_destroy(&table);
    fd_table_destroy(&clone);
}

// ------------------------------------------------------------
// Entry Management Tests
// ------------------------------------------------------------

CTEST(test_fd_table_add_basic)
{
    fd_table_t* table = fd_table_create();

    string_t* path = string_create_from_cstr("/tmp/test.txt");
    bool result = fd_table_add(table, 3, FD_REDIRECTED, path);

    CTEST_ASSERT_TRUE(ctest, result, "add succeeded");
    CTEST_ASSERT_EQ(ctest, fd_table_count(table), 1, "count is 1");
    CTEST_ASSERT_EQ(ctest, fd_table_get_highest_fd(table), 3, "highest_fd is 3");
    CTEST_ASSERT_TRUE(ctest, fd_table_is_open(table, 3), "fd 3 is open");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_add_multiple)
{
    fd_table_t* table = fd_table_create();

    string_t* path1 = string_create_from_cstr("/tmp/file1.txt");
    string_t* path2 = string_create_from_cstr("/tmp/file2.txt");
    string_t* path3 = string_create_from_cstr("/tmp/file3.txt");

    fd_table_add(table, 3, FD_REDIRECTED, path1);
    fd_table_add(table, 7, FD_CLOEXEC, path2);
    fd_table_add(table, 5, FD_NONE, path3);

    CTEST_ASSERT_EQ(ctest, fd_table_count(table), 3, "count is 3");
    CTEST_ASSERT_EQ(ctest, fd_table_get_highest_fd(table), 7, "highest_fd is 7");
    CTEST_ASSERT_TRUE(ctest, fd_table_is_open(table, 3), "fd 3 is open");
    CTEST_ASSERT_TRUE(ctest, fd_table_is_open(table, 5), "fd 5 is open");
    CTEST_ASSERT_TRUE(ctest, fd_table_is_open(table, 7), "fd 7 is open");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_add_update_existing)
{
    fd_table_t* table = fd_table_create();

    // Add initial entry
    string_t* path1 = string_create_from_cstr("/tmp/old.txt");
    fd_table_add(table, 3, FD_REDIRECTED, path1);

    // Update with new path and flags
    string_t* path2 = string_create_from_cstr("/tmp/new.txt");
    fd_table_add(table, 3, FD_CLOEXEC, path2);

    CTEST_ASSERT_EQ(ctest, fd_table_count(table), 1, "count still 1");
    CTEST_ASSERT_TRUE(ctest, fd_table_has_flag(table, 3, FD_CLOEXEC), "fd 3 has new flag");
    CTEST_ASSERT_FALSE(ctest, fd_table_has_flag(table, 3, FD_REDIRECTED), "fd 3 lost old flag");

    const string_t* path = fd_table_get_path(table, 3);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(path), "/tmp/new.txt", "path updated");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_add_null_path)
{
    fd_table_t* table = fd_table_create();

    bool result = fd_table_add(table, 5, FD_NONE, NULL);
    CTEST_ASSERT_TRUE(ctest, result, "add with NULL path succeeded");
    CTEST_ASSERT_NULL(ctest, fd_table_get_path(table, 5), "path is NULL");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_mark_saved)
{
    fd_table_t* table = fd_table_create();

    // Add original FD
    string_t* path = string_create_from_cstr("/tmp/test.txt");
    fd_table_add(table, 3, FD_REDIRECTED, path);

    // Mark FD 10 as saved copy of FD 3
    bool result = fd_table_mark_saved(table, 10, 3);
    CTEST_ASSERT_TRUE(ctest, result, "mark_saved succeeded");
    CTEST_ASSERT_EQ(ctest, fd_table_count(table), 2, "count is 2");
    CTEST_ASSERT_TRUE(ctest, fd_table_has_flag(table, 10, FD_SAVED), "fd 10 has FD_SAVED");
    CTEST_ASSERT_EQ(ctest, fd_table_get_original(table, 10), 3, "fd 10 original is 3");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_mark_saved_new_entry)
{
    fd_table_t* table = fd_table_create();

    // Mark saved without original existing
    bool result = fd_table_mark_saved(table, 10, 3);
    CTEST_ASSERT_TRUE(ctest, result, "mark_saved succeeded for new entry");
    CTEST_ASSERT_EQ(ctest, fd_table_count(table), 1, "count is 1");
    CTEST_ASSERT_EQ(ctest, fd_table_get_original(table, 10), 3, "original is 3");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_mark_closed)
{
    fd_table_t* table = fd_table_create();

    string_t* path = string_create_from_cstr("/tmp/test.txt");
    fd_table_add(table, 3, FD_REDIRECTED, path);
    CTEST_ASSERT_TRUE(ctest, fd_table_is_open(table, 3), "fd 3 initially open");

    bool result = fd_table_mark_closed(table, 3);
    CTEST_ASSERT_TRUE(ctest, result, "mark_closed succeeded");
    CTEST_ASSERT_FALSE(ctest, fd_table_is_open(table, 3), "fd 3 is closed");
    CTEST_ASSERT_EQ(ctest, fd_table_count(table), 1, "entry still in table");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_mark_closed_nonexistent)
{
    fd_table_t* table = fd_table_create();

    bool result = fd_table_mark_closed(table, 99);
    CTEST_ASSERT_FALSE(ctest, result, "mark_closed returns false for nonexistent fd");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_remove)
{
    fd_table_t* table = fd_table_create();

    string_t* path = string_create_from_cstr("/tmp/test.txt");
    fd_table_add(table, 3, FD_REDIRECTED, path);
    CTEST_ASSERT_EQ(ctest, fd_table_count(table), 1, "count is 1");

    bool result = fd_table_remove(table, 3);
    CTEST_ASSERT_TRUE(ctest, result, "remove succeeded");
    CTEST_ASSERT_EQ(ctest, fd_table_count(table), 0, "count is 0");
    CTEST_ASSERT_NULL(ctest, fd_table_find(table, 3), "fd 3 not found");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_remove_highest_fd)
{
    fd_table_t* table = fd_table_create();

    fd_table_add(table, 3, FD_NONE, NULL);
    fd_table_add(table, 5, FD_NONE, NULL);
    fd_table_add(table, 7, FD_NONE, NULL);

    CTEST_ASSERT_EQ(ctest, fd_table_get_highest_fd(table), 7, "highest_fd is 7");

    fd_table_remove(table, 7);
    CTEST_ASSERT_EQ(ctest, fd_table_get_highest_fd(table), 5, "highest_fd recalculated to 5");

    fd_table_remove(table, 5);
    CTEST_ASSERT_EQ(ctest, fd_table_get_highest_fd(table), 3, "highest_fd recalculated to 3");

    fd_table_remove(table, 3);
    CTEST_ASSERT_EQ(ctest, fd_table_get_highest_fd(table), -1, "highest_fd is -1 when empty");

    fd_table_destroy(&table);
}

// ------------------------------------------------------------
// Query Operation Tests
// ------------------------------------------------------------

CTEST(test_fd_table_find)
{
    fd_table_t* table = fd_table_create();

    string_t* path = string_create_from_cstr("/tmp/test.txt");
    fd_table_add(table, 3, FD_REDIRECTED, path);

    fd_entry_t* entry = fd_table_find(table, 3);
    CTEST_ASSERT_NOT_NULL(ctest, entry, "entry found");
    CTEST_ASSERT_EQ(ctest, entry->fd, 3, "fd matches");
    CTEST_ASSERT_TRUE(ctest, entry->is_open, "is_open is true");
    CTEST_ASSERT_EQ(ctest, entry->flags, FD_REDIRECTED, "flags match");

    fd_entry_t* not_found = fd_table_find(table, 99);
    CTEST_ASSERT_NULL(ctest, not_found, "nonexistent entry not found");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_is_open)
{
    fd_table_t* table = fd_table_create();

    fd_table_add(table, 3, FD_NONE, NULL);
    CTEST_ASSERT_TRUE(ctest, fd_table_is_open(table, 3), "fd 3 is open");

    fd_table_mark_closed(table, 3);
    CTEST_ASSERT_FALSE(ctest, fd_table_is_open(table, 3), "fd 3 is closed");

    CTEST_ASSERT_FALSE(ctest, fd_table_is_open(table, 99), "nonexistent fd is not open");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_get_flags)
{
    fd_table_t* table = fd_table_create();

    fd_table_add(table, 3, FD_REDIRECTED | FD_CLOEXEC, NULL);

    fd_flags_t flags = fd_table_get_flags(table, 3);
    CTEST_ASSERT_EQ(ctest, flags, FD_REDIRECTED | FD_CLOEXEC, "flags match");

    fd_flags_t no_flags = fd_table_get_flags(table, 99);
    CTEST_ASSERT_EQ(ctest, no_flags, FD_NONE, "nonexistent fd returns FD_NONE");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_has_flag)
{
    fd_table_t* table = fd_table_create();

    fd_table_add(table, 3, FD_REDIRECTED | FD_CLOEXEC, NULL);

    CTEST_ASSERT_TRUE(ctest, fd_table_has_flag(table, 3, FD_REDIRECTED), "has FD_REDIRECTED");
    CTEST_ASSERT_TRUE(ctest, fd_table_has_flag(table, 3, FD_CLOEXEC), "has FD_CLOEXEC");
    CTEST_ASSERT_FALSE(ctest, fd_table_has_flag(table, 3, FD_SAVED), "does not have FD_SAVED");
    CTEST_ASSERT_FALSE(ctest, fd_table_has_flag(table, 99, FD_REDIRECTED), "nonexistent fd has no flags");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_get_original)
{
    fd_table_t* table = fd_table_create();

    fd_table_mark_saved(table, 10, 3);
    CTEST_ASSERT_EQ(ctest, fd_table_get_original(table, 10), 3, "original is 3");

    fd_table_add(table, 5, FD_NONE, NULL);
    CTEST_ASSERT_EQ(ctest, fd_table_get_original(table, 5), -1, "non-saved fd returns -1");

    CTEST_ASSERT_EQ(ctest, fd_table_get_original(table, 99), -1, "nonexistent fd returns -1");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_get_path)
{
    fd_table_t* table = fd_table_create();

    string_t* path = string_create_from_cstr("/tmp/test.txt");
    fd_table_add(table, 3, FD_REDIRECTED, path);

    const string_t* retrieved = fd_table_get_path(table, 3);
    CTEST_ASSERT_NOT_NULL(ctest, retrieved, "path retrieved");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(retrieved), "/tmp/test.txt", "path matches");

    // FD with no path
    fd_table_add(table, 5, FD_NONE, NULL);
    CTEST_ASSERT_NULL(ctest, fd_table_get_path(table, 5), "null path returns NULL");

    // Nonexistent FD
    CTEST_ASSERT_NULL(ctest, fd_table_get_path(table, 99), "nonexistent fd returns NULL");

    fd_table_destroy(&table);
}

// ------------------------------------------------------------
// Flag Manipulation Tests
// ------------------------------------------------------------

CTEST(test_fd_table_set_flag)
{
    fd_table_t* table = fd_table_create();

    fd_table_add(table, 3, FD_REDIRECTED, NULL);
    CTEST_ASSERT_FALSE(ctest, fd_table_has_flag(table, 3, FD_CLOEXEC), "initially no FD_CLOEXEC");

    bool result = fd_table_set_flag(table, 3, FD_CLOEXEC);
    CTEST_ASSERT_TRUE(ctest, result, "set_flag succeeded");
    CTEST_ASSERT_TRUE(ctest, fd_table_has_flag(table, 3, FD_CLOEXEC), "now has FD_CLOEXEC");
    CTEST_ASSERT_TRUE(ctest, fd_table_has_flag(table, 3, FD_REDIRECTED), "still has FD_REDIRECTED");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_set_flag_nonexistent)
{
    fd_table_t* table = fd_table_create();

    bool result = fd_table_set_flag(table, 99, FD_CLOEXEC);
    CTEST_ASSERT_FALSE(ctest, result, "set_flag returns false for nonexistent fd");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_clear_flag)
{
    fd_table_t* table = fd_table_create();

    fd_table_add(table, 3, FD_REDIRECTED | FD_CLOEXEC, NULL);
    CTEST_ASSERT_TRUE(ctest, fd_table_has_flag(table, 3, FD_CLOEXEC), "initially has FD_CLOEXEC");

    bool result = fd_table_clear_flag(table, 3, FD_CLOEXEC);
    CTEST_ASSERT_TRUE(ctest, result, "clear_flag succeeded");
    CTEST_ASSERT_FALSE(ctest, fd_table_has_flag(table, 3, FD_CLOEXEC), "no longer has FD_CLOEXEC");
    CTEST_ASSERT_TRUE(ctest, fd_table_has_flag(table, 3, FD_REDIRECTED), "still has FD_REDIRECTED");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_clear_flag_nonexistent)
{
    fd_table_t* table = fd_table_create();

    bool result = fd_table_clear_flag(table, 99, FD_CLOEXEC);
    CTEST_ASSERT_FALSE(ctest, result, "clear_flag returns false for nonexistent fd");

    fd_table_destroy(&table);
}

// ------------------------------------------------------------
// Utility Operation Tests
// ------------------------------------------------------------

CTEST(test_fd_table_get_fds_with_flag)
{
    fd_table_t* table = fd_table_create();

    fd_table_add(table, 3, FD_REDIRECTED, NULL);
    fd_table_add(table, 5, FD_CLOEXEC, NULL);
    fd_table_add(table, 7, FD_REDIRECTED | FD_CLOEXEC, NULL);
    fd_table_add(table, 9, FD_NONE, NULL);

    size_t count = 0;
    int* redirected = fd_table_get_fds_with_flag(table, FD_REDIRECTED, &count);

    CTEST_ASSERT_NOT_NULL(ctest, redirected, "array returned");
    CTEST_ASSERT_EQ(ctest, count, 2, "2 FDs with FD_REDIRECTED");

    // Check that FDs 3 and 7 are in the array (order not guaranteed)
    bool found_3 = false;
    bool found_7 = false;
    for (size_t i = 0; i < count; i++) {
        if (redirected[i] == 3) found_3 = true;
        if (redirected[i] == 7) found_7 = true;
    }
    CTEST_ASSERT_TRUE(ctest, found_3, "fd 3 found");
    CTEST_ASSERT_TRUE(ctest, found_7, "fd 7 found");

    xfree(redirected);
    fd_table_destroy(&table);
}

CTEST(test_fd_table_get_fds_with_flag_none_found)
{
    fd_table_t* table = fd_table_create();

    fd_table_add(table, 3, FD_REDIRECTED, NULL);
    fd_table_add(table, 5, FD_REDIRECTED, NULL);

    size_t count = 0;
    int* saved = fd_table_get_fds_with_flag(table, FD_SAVED, &count);

    CTEST_ASSERT_NULL(ctest, saved, "NULL returned when no matches");
    CTEST_ASSERT_EQ(ctest, count, 0, "count is 0");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_get_fds_with_flag_empty_table)
{
    fd_table_t* table = fd_table_create();

    size_t count = 0;
    int* fds = fd_table_get_fds_with_flag(table, FD_CLOEXEC, &count);

    CTEST_ASSERT_NULL(ctest, fds, "NULL returned for empty table");
    CTEST_ASSERT_EQ(ctest, count, 0, "count is 0");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_count)
{
    fd_table_t* table = fd_table_create();

    CTEST_ASSERT_EQ(ctest, fd_table_count(table), 0, "count is 0 initially");

    fd_table_add(table, 3, FD_NONE, NULL);
    CTEST_ASSERT_EQ(ctest, fd_table_count(table), 1, "count is 1");

    fd_table_add(table, 5, FD_NONE, NULL);
    fd_table_add(table, 7, FD_NONE, NULL);
    CTEST_ASSERT_EQ(ctest, fd_table_count(table), 3, "count is 3");

    fd_table_remove(table, 5);
    CTEST_ASSERT_EQ(ctest, fd_table_count(table), 2, "count is 2 after removal");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_get_highest_fd)
{
    fd_table_t* table = fd_table_create();

    CTEST_ASSERT_EQ(ctest, fd_table_get_highest_fd(table), -1, "highest_fd is -1 initially");

    fd_table_add(table, 3, FD_NONE, NULL);
    CTEST_ASSERT_EQ(ctest, fd_table_get_highest_fd(table), 3, "highest_fd is 3");

    fd_table_add(table, 5, FD_NONE, NULL);
    CTEST_ASSERT_EQ(ctest, fd_table_get_highest_fd(table), 5, "highest_fd is 5");

    fd_table_add(table, 10, FD_NONE, NULL);
    CTEST_ASSERT_EQ(ctest, fd_table_get_highest_fd(table), 10, "highest_fd is 10");

    fd_table_destroy(&table);
}

// ------------------------------------------------------------
// Edge Cases and Stress Tests
// ------------------------------------------------------------

CTEST(test_fd_table_null_handling)
{
    // Test that operations handle NULL table gracefully
    CTEST_ASSERT_FALSE(ctest, fd_table_add(NULL, 3, FD_NONE, NULL), "add with NULL table");
    CTEST_ASSERT_FALSE(ctest, fd_table_mark_saved(NULL, 10, 3), "mark_saved with NULL table");
    CTEST_ASSERT_FALSE(ctest, fd_table_mark_closed(NULL, 3), "mark_closed with NULL table");
    CTEST_ASSERT_FALSE(ctest, fd_table_remove(NULL, 3), "remove with NULL table");
    CTEST_ASSERT_NULL(ctest, fd_table_find(NULL, 3), "find with NULL table");
    CTEST_ASSERT_FALSE(ctest, fd_table_is_open(NULL, 3), "is_open with NULL table");
    CTEST_ASSERT_EQ(ctest, fd_table_get_flags(NULL, 3), FD_NONE, "get_flags with NULL table");
    CTEST_ASSERT_FALSE(ctest, fd_table_has_flag(NULL, 3, FD_NONE), "has_flag with NULL table");
    CTEST_ASSERT_EQ(ctest, fd_table_get_original(NULL, 3), -1, "get_original with NULL table");
    CTEST_ASSERT_NULL(ctest, fd_table_get_path(NULL, 3), "get_path with NULL table");
    CTEST_ASSERT_FALSE(ctest, fd_table_set_flag(NULL, 3, FD_NONE), "set_flag with NULL table");
    CTEST_ASSERT_FALSE(ctest, fd_table_clear_flag(NULL, 3, FD_NONE), "clear_flag with NULL table");
    CTEST_ASSERT_EQ(ctest, fd_table_count(NULL), 0, "count with NULL table");
    CTEST_ASSERT_EQ(ctest, fd_table_get_highest_fd(NULL), -1, "get_highest_fd with NULL table");

    size_t count = 0;
    CTEST_ASSERT_NULL(ctest, fd_table_get_fds_with_flag(NULL, FD_NONE, &count), "get_fds_with_flag with NULL table");
    CTEST_ASSERT_EQ(ctest, count, 0, "count is 0 for NULL table");
}

CTEST(test_fd_table_large_fd_numbers)
{
    fd_table_t* table = fd_table_create();

    // Add FDs with large numbers
    fd_table_add(table, 1000, FD_NONE, NULL);
    fd_table_add(table, 5000, FD_CLOEXEC, NULL);
    fd_table_add(table, 100, FD_REDIRECTED, NULL);

    CTEST_ASSERT_EQ(ctest, fd_table_get_highest_fd(table), 5000, "highest_fd is 5000");
    CTEST_ASSERT_TRUE(ctest, fd_table_is_open(table, 1000), "fd 1000 is open");
    CTEST_ASSERT_TRUE(ctest, fd_table_is_open(table, 5000), "fd 5000 is open");
    CTEST_ASSERT_TRUE(ctest, fd_table_is_open(table, 100), "fd 100 is open");

    fd_table_destroy(&table);
}

CTEST(test_fd_table_capacity_growth)
{
    fd_table_t* table = fd_table_create();

    // Add more entries than initial capacity to trigger growth
    for (int i = 0; i < 20; i++) {
        bool result = fd_table_add(table, i, FD_NONE, NULL);
        CTEST_ASSERT_TRUE(ctest, result, "add succeeded during growth");
    }

    CTEST_ASSERT_EQ(ctest, fd_table_count(table), 20, "count is 20");
    CTEST_ASSERT_EQ(ctest, fd_table_get_highest_fd(table), 19, "highest_fd is 19");

    // Verify all entries are accessible
    for (int i = 0; i < 20; i++) {
        CTEST_ASSERT_TRUE(ctest, fd_table_is_open(table, i), "fd is open");
    }

    fd_table_destroy(&table);
}

CTEST(test_fd_table_multiple_flags)
{
    fd_table_t* table = fd_table_create();

    // Add with no flags, then set multiple flags
    fd_table_add(table, 3, FD_NONE, NULL);
    fd_table_set_flag(table, 3, FD_REDIRECTED);
    fd_table_set_flag(table, 3, FD_CLOEXEC);
    fd_table_set_flag(table, 3, FD_SAVED);

    CTEST_ASSERT_TRUE(ctest, fd_table_has_flag(table, 3, FD_REDIRECTED), "has FD_REDIRECTED");
    CTEST_ASSERT_TRUE(ctest, fd_table_has_flag(table, 3, FD_CLOEXEC), "has FD_CLOEXEC");
    CTEST_ASSERT_TRUE(ctest, fd_table_has_flag(table, 3, FD_SAVED), "has FD_SAVED");

    // Clear one flag at a time
    fd_table_clear_flag(table, 3, FD_CLOEXEC);
    CTEST_ASSERT_FALSE(ctest, fd_table_has_flag(table, 3, FD_CLOEXEC), "FD_CLOEXEC cleared");
    CTEST_ASSERT_TRUE(ctest, fd_table_has_flag(table, 3, FD_REDIRECTED), "still has FD_REDIRECTED");
    CTEST_ASSERT_TRUE(ctest, fd_table_has_flag(table, 3, FD_SAVED), "still has FD_SAVED");

    fd_table_destroy(&table);
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
        // Creation and destruction
        CTEST_ENTRY(test_fd_table_create),
        CTEST_ENTRY(test_fd_table_clone),

        // Entry management
        CTEST_ENTRY(test_fd_table_add_basic),
        CTEST_ENTRY(test_fd_table_add_multiple),
        CTEST_ENTRY(test_fd_table_add_update_existing),
        CTEST_ENTRY(test_fd_table_add_null_path),
        CTEST_ENTRY(test_fd_table_mark_saved),
        CTEST_ENTRY(test_fd_table_mark_saved_new_entry),
        CTEST_ENTRY(test_fd_table_mark_closed),
        CTEST_ENTRY(test_fd_table_mark_closed_nonexistent),
        CTEST_ENTRY(test_fd_table_remove),
        CTEST_ENTRY(test_fd_table_remove_highest_fd),

        // Query operations
        CTEST_ENTRY(test_fd_table_find),
        CTEST_ENTRY(test_fd_table_is_open),
        CTEST_ENTRY(test_fd_table_get_flags),
        CTEST_ENTRY(test_fd_table_has_flag),
        CTEST_ENTRY(test_fd_table_get_original),
        CTEST_ENTRY(test_fd_table_get_path),

        // Flag manipulation
        CTEST_ENTRY(test_fd_table_set_flag),
        CTEST_ENTRY(test_fd_table_set_flag_nonexistent),
        CTEST_ENTRY(test_fd_table_clear_flag),
        CTEST_ENTRY(test_fd_table_clear_flag_nonexistent),

        // Utility operations
        CTEST_ENTRY(test_fd_table_get_fds_with_flag),
        CTEST_ENTRY(test_fd_table_get_fds_with_flag_none_found),
        CTEST_ENTRY(test_fd_table_get_fds_with_flag_empty_table),
        CTEST_ENTRY(test_fd_table_count),
        CTEST_ENTRY(test_fd_table_get_highest_fd),

        // Edge cases and stress tests
        CTEST_ENTRY(test_fd_table_null_handling),
        CTEST_ENTRY(test_fd_table_large_fd_numbers),
        CTEST_ENTRY(test_fd_table_capacity_growth),
        CTEST_ENTRY(test_fd_table_multiple_flags),

        NULL
    };

    int result = ctest_run_suite(suite);

    arena_end();
    return result;
}

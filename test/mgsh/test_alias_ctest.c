#include <string.h>
#include "ctest.h"
#include "alias_store.h"
#include "string_t.h"
#include "xalloc.h"

// ------------------------------------------------------------
// Creation and destruction
// ------------------------------------------------------------

CTEST(test_alias_store_create)
{
    alias_store_t *store = alias_store_create();
    CTEST_ASSERT_NOT_NULL(ctest, store, "store created");
    CTEST_ASSERT_EQ(ctest, alias_store_size(store), 0, "initial size is 0");
    alias_store_destroy(&store);
    CTEST_ASSERT_NULL(ctest, store, "store is null after destroy");
}

// ------------------------------------------------------------
// Name validation
// ------------------------------------------------------------

CTEST(test_alias_name_is_valid_accepts_valid)
{
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("ls"), "simple lowercase");
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("LS"), "simple uppercase");
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("ls1"), "with digit");
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("123"), "digits only");
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("my_alias"), "with underscore");
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("my-alias"), "with hyphen");
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("my@alias"), "with at sign");
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("my!alias"), "with exclamation");
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("my%alias"), "with percent");
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("my,alias"), "with comma");
}

CTEST(test_alias_name_is_valid_rejects_invalid)
{
    CTEST_ASSERT_FALSE(ctest, alias_name_is_valid(""), "empty string");
    CTEST_ASSERT_FALSE(ctest, alias_name_is_valid("my alias"), "with space");
    CTEST_ASSERT_FALSE(ctest, alias_name_is_valid("my\talias"), "with tab");
    CTEST_ASSERT_FALSE(ctest, alias_name_is_valid("my$alias"), "with dollar");
    CTEST_ASSERT_FALSE(ctest, alias_name_is_valid("my=alias"), "with equals");
    CTEST_ASSERT_FALSE(ctest, alias_name_is_valid("my#alias"), "with hash");
    CTEST_ASSERT_FALSE(ctest, alias_name_is_valid("my&alias"), "with ampersand");
    CTEST_ASSERT_FALSE(ctest, alias_name_is_valid("my*alias"), "with asterisk");
    CTEST_ASSERT_FALSE(ctest, alias_name_is_valid("my(alias"), "with open paren");
    CTEST_ASSERT_FALSE(ctest, alias_name_is_valid("my)alias"), "with close paren");
}

// ------------------------------------------------------------
// Add and get (string_t variants)
// ------------------------------------------------------------

CTEST(test_alias_store_add_and_get)
{
    alias_store_t *store = alias_store_create();

    string_t *name = string_create_from_cstr("ls");
    string_t *value = string_create_from_cstr("ls -la");
    alias_store_add(store, name, value);

    CTEST_ASSERT_EQ(ctest, alias_store_size(store), 1, "size is 1 after add");
    CTEST_ASSERT_TRUE(ctest, alias_store_has_name(store, name), "has name");

    const string_t *retrieved = alias_store_get_value(store, name);
    CTEST_ASSERT_NOT_NULL(ctest, retrieved, "value not null");
    CTEST_ASSERT_EQ(ctest, string_compare(retrieved, value), 0, "value matches");

    string_destroy(&name);
    string_destroy(&value);
    alias_store_destroy(&store);
}

// ------------------------------------------------------------
// Add and get (cstr variants)
// ------------------------------------------------------------

CTEST(test_alias_store_add_cstr_and_get_cstr)
{
    alias_store_t *store = alias_store_create();

    alias_store_add_cstr(store, "ll", "ls -l");

    CTEST_ASSERT_EQ(ctest, alias_store_size(store), 1, "size is 1 after add");
    CTEST_ASSERT_TRUE(ctest, alias_store_has_name_cstr(store, "ll"), "has name 'll'");

    const char *value = alias_store_get_value_cstr(store, "ll");
    CTEST_ASSERT_NOT_NULL(ctest, value, "value not null");
    CTEST_ASSERT_STR_EQ(ctest, value, "ls -l", "value matches");

    alias_store_destroy(&store);
}

// ------------------------------------------------------------
// Overwrite existing alias
// ------------------------------------------------------------

CTEST(test_alias_store_overwrite)
{
    alias_store_t *store = alias_store_create();

    alias_store_add_cstr(store, "ls", "ls -la");
    alias_store_add_cstr(store, "ls", "ls -lah --color");

    CTEST_ASSERT_EQ(ctest, alias_store_size(store), 1, "size still 1 after overwrite");

    const char *value = alias_store_get_value_cstr(store, "ls");
    CTEST_ASSERT_STR_EQ(ctest, value, "ls -lah --color", "value was overwritten");

    alias_store_destroy(&store);
}

CTEST(test_alias_store_overwrite_string_t)
{
    alias_store_t *store = alias_store_create();

    string_t *name = string_create_from_cstr("grep");
    string_t *value1 = string_create_from_cstr("grep --color");
    string_t *value2 = string_create_from_cstr("grep --color=auto");

    alias_store_add(store, name, value1);
    alias_store_add(store, name, value2);

    CTEST_ASSERT_EQ(ctest, alias_store_size(store), 1, "size still 1 after overwrite");

    const string_t *retrieved = alias_store_get_value(store, name);
    CTEST_ASSERT_EQ(ctest, string_compare(retrieved, value2), 0, "value was overwritten");

    string_destroy(&name);
    string_destroy(&value1);
    string_destroy(&value2);
    alias_store_destroy(&store);
}

// ------------------------------------------------------------
// Remove
// ------------------------------------------------------------

CTEST(test_alias_store_remove_cstr)
{
    alias_store_t *store = alias_store_create();

    alias_store_add_cstr(store, "ls", "ls -la");
    alias_store_add_cstr(store, "ll", "ls -l");
    alias_store_add_cstr(store, "la", "ls -a");

    CTEST_ASSERT_EQ(ctest, alias_store_size(store), 3, "size is 3");

    bool removed = alias_store_remove_cstr(store, "ll");
    CTEST_ASSERT_TRUE(ctest, removed, "remove returned true");
    CTEST_ASSERT_EQ(ctest, alias_store_size(store), 2, "size is 2 after remove");
    CTEST_ASSERT_FALSE(ctest, alias_store_has_name_cstr(store, "ll"), "'ll' no longer exists");
    CTEST_ASSERT_TRUE(ctest, alias_store_has_name_cstr(store, "ls"), "'ls' still exists");
    CTEST_ASSERT_TRUE(ctest, alias_store_has_name_cstr(store, "la"), "'la' still exists");

    alias_store_destroy(&store);
}

CTEST(test_alias_store_remove_string_t)
{
    alias_store_t *store = alias_store_create();

    string_t *name1 = string_create_from_cstr("foo");
    string_t *name2 = string_create_from_cstr("bar");
    string_t *value = string_create_from_cstr("value");

    alias_store_add(store, name1, value);
    alias_store_add(store, name2, value);

    bool removed = alias_store_remove(store, name1);
    CTEST_ASSERT_TRUE(ctest, removed, "remove returned true");
    CTEST_ASSERT_FALSE(ctest, alias_store_has_name(store, name1), "'foo' no longer exists");
    CTEST_ASSERT_TRUE(ctest, alias_store_has_name(store, name2), "'bar' still exists");

    string_destroy(&name1);
    string_destroy(&name2);
    string_destroy(&value);
    alias_store_destroy(&store);
}

CTEST(test_alias_store_remove_nonexistent)
{
    alias_store_t *store = alias_store_create();

    alias_store_add_cstr(store, "ls", "ls -la");

    bool removed = alias_store_remove_cstr(store, "nonexistent");
    CTEST_ASSERT_FALSE(ctest, removed, "remove nonexistent returns false");
    CTEST_ASSERT_EQ(ctest, alias_store_size(store), 1, "size unchanged");

    alias_store_destroy(&store);
}

// ------------------------------------------------------------
// Clear
// ------------------------------------------------------------

CTEST(test_alias_store_clear)
{
    alias_store_t *store = alias_store_create();

    alias_store_add_cstr(store, "ls", "ls -la");
    alias_store_add_cstr(store, "ll", "ls -l");
    alias_store_add_cstr(store, "la", "ls -a");

    CTEST_ASSERT_EQ(ctest, alias_store_size(store), 3, "size is 3 before clear");

    alias_store_clear(store);

    CTEST_ASSERT_EQ(ctest, alias_store_size(store), 0, "size is 0 after clear");
    CTEST_ASSERT_FALSE(ctest, alias_store_has_name_cstr(store, "ls"), "'ls' no longer exists");

    alias_store_destroy(&store);
}

// ------------------------------------------------------------
// Clone
// ------------------------------------------------------------

CTEST(test_alias_store_clone_empty)
{
    alias_store_t *store = alias_store_create();
    alias_store_t *clone = alias_store_clone(store);

    CTEST_ASSERT_NOT_NULL(ctest, clone, "clone created");
    CTEST_ASSERT_EQ(ctest, alias_store_size(clone), 0, "clone size is 0");

    alias_store_destroy(&store);
    alias_store_destroy(&clone);
}

CTEST(test_alias_store_clone_with_entries)
{
    alias_store_t *store = alias_store_create();

    alias_store_add_cstr(store, "ls", "ls -la");
    alias_store_add_cstr(store, "ll", "ls -l");
    alias_store_add_cstr(store, "grep", "grep --color");

    alias_store_t *clone = alias_store_clone(store);

    CTEST_ASSERT_NOT_NULL(ctest, clone, "clone created");
    CTEST_ASSERT_EQ(ctest, alias_store_size(clone), 3, "clone has same size");

    CTEST_ASSERT_TRUE(ctest, alias_store_has_name_cstr(clone, "ls"), "clone has 'ls'");
    CTEST_ASSERT_TRUE(ctest, alias_store_has_name_cstr(clone, "ll"), "clone has 'll'");
    CTEST_ASSERT_TRUE(ctest, alias_store_has_name_cstr(clone, "grep"), "clone has 'grep'");

    CTEST_ASSERT_STR_EQ(ctest, alias_store_get_value_cstr(clone, "ls"), "ls -la", "'ls' value matches");
    CTEST_ASSERT_STR_EQ(ctest, alias_store_get_value_cstr(clone, "ll"), "ls -l", "'ll' value matches");
    CTEST_ASSERT_STR_EQ(ctest, alias_store_get_value_cstr(clone, "grep"), "grep --color", "'grep' value matches");

    alias_store_destroy(&store);
    alias_store_destroy(&clone);
}

CTEST(test_alias_store_clone_is_independent)
{
    alias_store_t *store = alias_store_create();
    alias_store_add_cstr(store, "ls", "ls -la");

    alias_store_t *clone = alias_store_clone(store);

    // Modify original
    alias_store_add_cstr(store, "new", "new value");
    alias_store_remove_cstr(store, "ls");

    // Clone should be unaffected
    CTEST_ASSERT_EQ(ctest, alias_store_size(clone), 1, "clone size unchanged");
    CTEST_ASSERT_TRUE(ctest, alias_store_has_name_cstr(clone, "ls"), "clone still has 'ls'");
    CTEST_ASSERT_FALSE(ctest, alias_store_has_name_cstr(clone, "new"), "clone does not have 'new'");

    // Modify clone
    alias_store_add_cstr(clone, "clone_only", "clone value");

    // Original should be unaffected
    CTEST_ASSERT_FALSE(ctest, alias_store_has_name_cstr(store, "clone_only"), "original does not have 'clone_only'");

    alias_store_destroy(&store);
    alias_store_destroy(&clone);
}

// ------------------------------------------------------------
// Get nonexistent
// ------------------------------------------------------------

CTEST(test_alias_store_get_nonexistent)
{
    alias_store_t *store = alias_store_create();

    alias_store_add_cstr(store, "ls", "ls -la");

    const char *value = alias_store_get_value_cstr(store, "nonexistent");
    CTEST_ASSERT_NULL(ctest, value, "get nonexistent returns NULL");

    string_t *name = string_create_from_cstr("also_nonexistent");
    const string_t *value2 = alias_store_get_value(store, name);
    CTEST_ASSERT_NULL(ctest, value2, "get nonexistent string_t returns NULL");

    string_destroy(&name);
    alias_store_destroy(&store);
}

// ------------------------------------------------------------
// Multiple entries
// ------------------------------------------------------------

CTEST(test_alias_store_multiple_entries)
{
    alias_store_t *store = alias_store_create();

    alias_store_add_cstr(store, "a", "value_a");
    alias_store_add_cstr(store, "b", "value_b");
    alias_store_add_cstr(store, "c", "value_c");
    alias_store_add_cstr(store, "d", "value_d");
    alias_store_add_cstr(store, "e", "value_e");

    CTEST_ASSERT_EQ(ctest, alias_store_size(store), 5, "size is 5");

    CTEST_ASSERT_STR_EQ(ctest, alias_store_get_value_cstr(store, "a"), "value_a", "'a' correct");
    CTEST_ASSERT_STR_EQ(ctest, alias_store_get_value_cstr(store, "b"), "value_b", "'b' correct");
    CTEST_ASSERT_STR_EQ(ctest, alias_store_get_value_cstr(store, "c"), "value_c", "'c' correct");
    CTEST_ASSERT_STR_EQ(ctest, alias_store_get_value_cstr(store, "d"), "value_d", "'d' correct");
    CTEST_ASSERT_STR_EQ(ctest, alias_store_get_value_cstr(store, "e"), "value_e", "'e' correct");

    alias_store_destroy(&store);
}

// ------------------------------------------------------------
// Test suite entry
// ------------------------------------------------------------

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    arena_start();

    CTestEntry *suite[] = {
        CTEST_ENTRY(test_alias_store_create),
        CTEST_ENTRY(test_alias_name_is_valid_accepts_valid),
        CTEST_ENTRY(test_alias_name_is_valid_rejects_invalid),
        CTEST_ENTRY(test_alias_store_add_and_get),
        CTEST_ENTRY(test_alias_store_add_cstr_and_get_cstr),
        CTEST_ENTRY(test_alias_store_overwrite),
        CTEST_ENTRY(test_alias_store_overwrite_string_t),
        CTEST_ENTRY(test_alias_store_remove_cstr),
        CTEST_ENTRY(test_alias_store_remove_string_t),
        CTEST_ENTRY(test_alias_store_remove_nonexistent),
        CTEST_ENTRY(test_alias_store_clear),
        CTEST_ENTRY(test_alias_store_clone_empty),
        CTEST_ENTRY(test_alias_store_clone_with_entries),
        CTEST_ENTRY(test_alias_store_clone_is_independent),
        CTEST_ENTRY(test_alias_store_get_nonexistent),
        CTEST_ENTRY(test_alias_store_multiple_entries),
        NULL
    };

    int result = ctest_run_suite(suite);

    arena_end();
    return result;
}

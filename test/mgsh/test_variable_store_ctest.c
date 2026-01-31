/**
 * @file test_variable_store_ctest.c
 * @brief Unit tests for variable store (variable_store.c)
 */

#include <string.h>
#include "ctest.h"
#include "variable_store.h"
#include "string_t.h"
#include "xalloc.h"

// ------------------------------------------------------------
// Creation and Destruction Tests
// ------------------------------------------------------------

CTEST(test_variable_store_create)
{
    variable_store_t *store = variable_store_create();
    CTEST_ASSERT_NOT_NULL(ctest, store, "store created");
    CTEST_ASSERT_NOT_NULL(ctest, store->map, "store map created");
    CTEST_ASSERT_EQ(ctest, store->generation, 0, "initial generation is 0");
    variable_store_destroy(&store);
    CTEST_ASSERT_NULL(ctest, store, "store is null after destroy");
}

CTEST(test_variable_store_destroy_null)
{
    variable_store_t *store = NULL;
    variable_store_destroy(&store);
    CTEST_ASSERT_NULL(ctest, store, "null pointer handled");
}

CTEST(test_variable_store_clear)
{
    variable_store_t *store = variable_store_create();

    variable_store_add_cstr(store, "VAR1", "value1", false, false);
    variable_store_add_cstr(store, "VAR2", "value2", false, false);

    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(store, "VAR1"), "VAR1 exists");
    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(store, "VAR2"), "VAR2 exists");

    variable_store_clear(store);

    CTEST_ASSERT_FALSE(ctest, variable_store_has_name_cstr(store, "VAR1"), "VAR1 removed");
    CTEST_ASSERT_FALSE(ctest, variable_store_has_name_cstr(store, "VAR2"), "VAR2 removed");

    variable_store_destroy(&store);
}

// ------------------------------------------------------------
// Add Variable Tests
// ------------------------------------------------------------

CTEST(test_variable_store_add_cstr)
{
    variable_store_t *store = variable_store_create();

    var_store_error_t err = variable_store_add_cstr(store, "MY_VAR", "my_value", false, false);

    CTEST_ASSERT_EQ(ctest, err, VAR_STORE_ERROR_NONE, "add succeeded");
    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(store, "MY_VAR"), "variable exists");

    const char *value = variable_store_get_value_cstr(store, "MY_VAR");
    CTEST_ASSERT_STR_EQ(ctest, value, "my_value", "value matches");

    variable_store_destroy(&store);
}

CTEST(test_variable_store_add_string_t)
{
    variable_store_t *store = variable_store_create();

    string_t *name = string_create_from_cstr("TEST_VAR");
    string_t *value = string_create_from_cstr("test_value");

    var_store_error_t err = variable_store_add(store, name, value, false, false);

    CTEST_ASSERT_EQ(ctest, err, VAR_STORE_ERROR_NONE, "add succeeded");
    CTEST_ASSERT_TRUE(ctest, variable_store_has_name(store, name), "variable exists");

    string_destroy(&name);
    string_destroy(&value);
    variable_store_destroy(&store);
}

CTEST(test_variable_store_add_multiple)
{
    variable_store_t *store = variable_store_create();

    variable_store_add_cstr(store, "VAR1", "value1", false, false);
    variable_store_add_cstr(store, "VAR2", "value2", false, false);
    variable_store_add_cstr(store, "VAR3", "value3", false, false);

    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(store, "VAR1"), "VAR1 exists");
    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(store, "VAR2"), "VAR2 exists");
    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(store, "VAR3"), "VAR3 exists");

    variable_store_destroy(&store);
}

CTEST(test_variable_store_add_with_empty_name)
{
    variable_store_t *store = variable_store_create();

    var_store_error_t err = variable_store_add_cstr(store, "", "value", false, false);

    CTEST_ASSERT_EQ(ctest, err, VAR_STORE_ERROR_EMPTY_NAME, "empty name error");

    variable_store_destroy(&store);
}

CTEST(test_variable_store_add_with_digit_start)
{
    variable_store_t *store = variable_store_create();

    var_store_error_t err = variable_store_add_cstr(store, "1VAR", "value", false, false);

    CTEST_ASSERT_EQ(ctest, err, VAR_STORE_ERROR_NAME_STARTS_WITH_DIGIT, "digit start error");

    variable_store_destroy(&store);
}

CTEST(test_variable_store_add_overwrites_existing)
{
    variable_store_t *store = variable_store_create();

    variable_store_add_cstr(store, "VAR", "value1", false, false);
    const char *v1 = variable_store_get_value_cstr(store, "VAR");
    CTEST_ASSERT_STR_EQ(ctest, v1, "value1", "first value set");

    variable_store_add_cstr(store, "VAR", "value2", false, false);
    const char *v2 = variable_store_get_value_cstr(store, "VAR");
    CTEST_ASSERT_STR_EQ(ctest, v2, "value2", "value overwritten");

    variable_store_destroy(&store);
}

CTEST(test_variable_store_add_env_string)
{
    variable_store_t *store = variable_store_create();

    variable_store_add_env(store, "MY_VAR=my_value");

    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(store, "MY_VAR"), "variable exists");
    const char *value = variable_store_get_value_cstr(store, "MY_VAR");
    CTEST_ASSERT_STR_EQ(ctest, value, "my_value", "value extracted from env string");

    variable_store_destroy(&store);
}

// ------------------------------------------------------------
// Remove Variable Tests
// ------------------------------------------------------------

CTEST(test_variable_store_remove_cstr)
{
    variable_store_t *store = variable_store_create();

    variable_store_add_cstr(store, "VAR", "value", false, false);
    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(store, "VAR"), "variable exists");

    variable_store_remove_cstr(store, "VAR");
    CTEST_ASSERT_FALSE(ctest, variable_store_has_name_cstr(store, "VAR"), "variable removed");

    variable_store_destroy(&store);
}

CTEST(test_variable_store_remove_string_t)
{
    variable_store_t *store = variable_store_create();

    string_t *name = string_create_from_cstr("VAR");
    variable_store_add_cstr(store, "VAR", "value", false, false);

    variable_store_remove(store, name);
    CTEST_ASSERT_FALSE(ctest, variable_store_has_name(store, name), "variable removed");

    string_destroy(&name);
    variable_store_destroy(&store);
}

CTEST(test_variable_store_remove_nonexistent)
{
    variable_store_t *store = variable_store_create();

    variable_store_remove_cstr(store, "NONEXISTENT");

    CTEST_ASSERT_FALSE(ctest, variable_store_has_name_cstr(store, "NONEXISTENT"), "still not found");

    variable_store_destroy(&store);
}

// ------------------------------------------------------------
// Query Tests (has_name, get_value, etc.)
// ------------------------------------------------------------

CTEST(test_variable_store_has_name_cstr)
{
    variable_store_t *store = variable_store_create();

    variable_store_add_cstr(store, "PRESENT", "value", false, false);

    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(store, "PRESENT"), "found present");
    CTEST_ASSERT_FALSE(ctest, variable_store_has_name_cstr(store, "MISSING"), "not found missing");

    variable_store_destroy(&store);
}

CTEST(test_variable_store_get_value_cstr)
{
    variable_store_t *store = variable_store_create();

    variable_store_add_cstr(store, "VAR", "the_value", false, false);

    const char *value = variable_store_get_value_cstr(store, "VAR");
    CTEST_ASSERT_STR_EQ(ctest, value, "the_value", "value retrieved");

    const char *missing = variable_store_get_value_cstr(store, "MISSING");
    CTEST_ASSERT_NULL(ctest, missing, "missing returns null");

    variable_store_destroy(&store);
}

CTEST(test_variable_store_get_variable_entry)
{
    variable_store_t *store = variable_store_create();

    variable_store_add_cstr(store, "VAR", "value", true, false);

    const variable_map_entry_t *entry = variable_store_get_variable_cstr(store, "VAR");
    CTEST_ASSERT_NOT_NULL(ctest, entry, "entry found");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(entry->mapped.value), "value", "value in entry");
    CTEST_ASSERT_TRUE(ctest, entry->mapped.exported, "exported flag in entry");
    CTEST_ASSERT_FALSE(ctest, entry->mapped.read_only, "read_only flag in entry");

    variable_store_destroy(&store);
}

CTEST(test_variable_store_get_value_length)
{
    variable_store_t *store = variable_store_create();

    variable_store_add_cstr(store, "SHORT", "hi", false, false);
    variable_store_add_cstr(store, "LONG", "this is a longer value", false, false);

    int32_t len1 = variable_store_get_value_length(store, string_create_from_cstr("SHORT"));
    int32_t len2 = variable_store_get_value_length(store, string_create_from_cstr("LONG"));
    int32_t len3 = variable_store_get_value_length(store, string_create_from_cstr("MISSING"));

    CTEST_ASSERT_EQ(ctest, len1, 2, "SHORT length is 2");
    CTEST_ASSERT_EQ(ctest, len2, 22, "LONG length is 22");
    CTEST_ASSERT_EQ(ctest, len3, -1, "MISSING returns -1");

    variable_store_destroy(&store);
}

// ------------------------------------------------------------
// Export Flag Tests
// ------------------------------------------------------------

CTEST(test_variable_store_exported_flag)
{
    variable_store_t *store = variable_store_create();

    variable_store_add_cstr(store, "EXPORTED", "value", true, false);
    variable_store_add_cstr(store, "NOT_EXPORTED", "value", false, false);

    CTEST_ASSERT_TRUE(ctest, variable_store_is_exported_cstr(store, "EXPORTED"), "exported flag true");
    CTEST_ASSERT_FALSE(ctest, variable_store_is_exported_cstr(store, "NOT_EXPORTED"), "exported flag false");

    variable_store_destroy(&store);
}

CTEST(test_variable_store_set_exported)
{
    variable_store_t *store = variable_store_create();

    variable_store_add_cstr(store, "VAR", "value", false, false);
    CTEST_ASSERT_FALSE(ctest, variable_store_is_exported_cstr(store, "VAR"), "initially not exported");

    var_store_error_t err = variable_store_set_exported_cstr(store, "VAR", true);
    CTEST_ASSERT_EQ(ctest, err, VAR_STORE_ERROR_NONE, "set_exported succeeded");
    CTEST_ASSERT_TRUE(ctest, variable_store_is_exported_cstr(store, "VAR"), "now exported");

    variable_store_destroy(&store);
}

// ------------------------------------------------------------
// Read-Only Flag Tests
// ------------------------------------------------------------

CTEST(test_variable_store_read_only_flag)
{
    variable_store_t *store = variable_store_create();

    variable_store_add_cstr(store, "READONLY", "value", false, true);
    variable_store_add_cstr(store, "WRITABLE", "value", false, false);

    CTEST_ASSERT_TRUE(ctest, variable_store_is_read_only_cstr(store, "READONLY"), "read_only flag true");
    CTEST_ASSERT_FALSE(ctest, variable_store_is_read_only_cstr(store, "WRITABLE"), "read_only flag false");

    variable_store_destroy(&store);
}

CTEST(test_variable_store_set_read_only)
{
    variable_store_t *store = variable_store_create();

    variable_store_add_cstr(store, "VAR", "value", false, false);
    CTEST_ASSERT_FALSE(ctest, variable_store_is_read_only_cstr(store, "VAR"), "initially writable");

    var_store_error_t err = variable_store_set_read_only_cstr(store, "VAR", true);
    CTEST_ASSERT_EQ(ctest, err, VAR_STORE_ERROR_NONE, "set_read_only succeeded");
    CTEST_ASSERT_TRUE(ctest, variable_store_is_read_only_cstr(store, "VAR"), "now read_only");

    variable_store_destroy(&store);
}

// ------------------------------------------------------------
// Clone Tests
// ------------------------------------------------------------

CTEST(test_variable_store_clone)
{
    variable_store_t *orig = variable_store_create();
    variable_store_add_cstr(orig, "VAR1", "value1", true, false);
    variable_store_add_cstr(orig, "VAR2", "value2", false, true);

    variable_store_t *cloned = variable_store_clone(orig);

    CTEST_ASSERT_NOT_NULL(ctest, cloned, "clone created");
    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(cloned, "VAR1"), "VAR1 in clone");
    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(cloned, "VAR2"), "VAR2 in clone");
    CTEST_ASSERT_STR_EQ(ctest, variable_store_get_value_cstr(cloned, "VAR1"), "value1", "VAR1 value correct");
    CTEST_ASSERT_TRUE(ctest, variable_store_is_exported_cstr(cloned, "VAR1"), "VAR1 exported flag preserved");
    CTEST_ASSERT_TRUE(ctest, variable_store_is_read_only_cstr(cloned, "VAR2"), "VAR2 read_only flag preserved");

    variable_store_destroy(&orig);
    variable_store_destroy(&cloned);
}

CTEST(test_variable_store_clone_exported)
{
    variable_store_t *orig = variable_store_create();
    variable_store_add_cstr(orig, "EXPORTED1", "value1", true, false);
    variable_store_add_cstr(orig, "EXPORTED2", "value2", true, false);
    variable_store_add_cstr(orig, "NOT_EXPORTED", "value3", false, false);

    variable_store_t *cloned = variable_store_clone_exported(orig);

    CTEST_ASSERT_NOT_NULL(ctest, cloned, "clone created");
    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(cloned, "EXPORTED1"), "EXPORTED1 in clone");
    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(cloned, "EXPORTED2"), "EXPORTED2 in clone");
    CTEST_ASSERT_FALSE(ctest, variable_store_has_name_cstr(cloned, "NOT_EXPORTED"), "NOT_EXPORTED not in clone");

    variable_store_destroy(&orig);
    variable_store_destroy(&cloned);
}

CTEST(test_variable_store_clone_independence)
{
    variable_store_t *orig = variable_store_create();
    variable_store_add_cstr(orig, "VAR", "original", false, false);

    variable_store_t *cloned = variable_store_clone(orig);

    variable_store_add_cstr(cloned, "VAR", "modified", false, false);

    const char *orig_value = variable_store_get_value_cstr(orig, "VAR");
    const char *clone_value = variable_store_get_value_cstr(cloned, "VAR");

    CTEST_ASSERT_STR_EQ(ctest, orig_value, "original", "original unchanged");
    CTEST_ASSERT_STR_EQ(ctest, clone_value, "modified", "clone modified");

    variable_store_destroy(&orig);
    variable_store_destroy(&cloned);
}

// ------------------------------------------------------------
// Copy Tests
// ------------------------------------------------------------

CTEST(test_variable_store_copy_all)
{
    variable_store_t *src = variable_store_create();
    variable_store_add_cstr(src, "SRC1", "value1", true, false);
    variable_store_add_cstr(src, "SRC2", "value2", false, true);

    variable_store_t *dst = variable_store_create();
    variable_store_copy_all(dst, src);

    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(dst, "SRC1"), "SRC1 copied");
    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(dst, "SRC2"), "SRC2 copied");
    CTEST_ASSERT_STR_EQ(ctest, variable_store_get_value_cstr(dst, "SRC1"), "value1", "SRC1 value correct");
    CTEST_ASSERT_TRUE(ctest, variable_store_is_exported_cstr(dst, "SRC1"), "SRC1 exported flag preserved");
    CTEST_ASSERT_TRUE(ctest, variable_store_is_read_only_cstr(dst, "SRC2"), "SRC2 read_only flag preserved");

    variable_store_destroy(&src);
    variable_store_destroy(&dst);
}

// Counter for iterator tests
static int iter_count = 0;
static void count_iterator(const string_t *name, const string_t *value, bool exported,
                          bool read_only, void *user_data)
{
    (void)name;
    (void)value;
    (void)exported;
    (void)read_only;
    (void)user_data;
    iter_count++;
}

// Flag iterator for checking presence of variables
static bool found_var = false;
static void find_var_iterator(const string_t *name, const string_t *value, bool exported,
                             bool read_only, void *user_data)
{
    (void)value;
    (void)exported;
    (void)read_only;
    const char *search_name = (const char *)user_data;
    if (strcmp(string_cstr(name), search_name) == 0)
    {
        found_var = true;
    }
}

// ------------------------------------------------------------
// Iterator Tests
// ------------------------------------------------------------

CTEST(test_variable_store_for_each)
{
    variable_store_t *store = variable_store_create();
    variable_store_add_cstr(store, "VAR1", "value1", false, false);
    variable_store_add_cstr(store, "VAR2", "value2", false, false);
    variable_store_add_cstr(store, "VAR3", "value3", false, false);

    iter_count = 0;
    variable_store_for_each(store, count_iterator, NULL);

    CTEST_ASSERT_EQ(ctest, iter_count, 3, "iterator called 3 times");

    variable_store_destroy(&store);
}

CTEST(test_variable_store_for_each_finds_variable)
{
    variable_store_t *store = variable_store_create();
    variable_store_add_cstr(store, "FIND_ME", "value", false, false);
    variable_store_add_cstr(store, "OTHER", "value", false, false);

    found_var = false;
    variable_store_for_each(store, find_var_iterator, (void *)"FIND_ME");

    CTEST_ASSERT_TRUE(ctest, found_var, "variable found in iteration");

    variable_store_destroy(&store);
}

CTEST(test_variable_store_for_each_empty)
{
    variable_store_t *store = variable_store_create();

    iter_count = 0;
    variable_store_for_each(store, count_iterator, NULL);

    CTEST_ASSERT_EQ(ctest, iter_count, 0, "iterator called 0 times for empty store");

    variable_store_destroy(&store);
}

// ------------------------------------------------------------
// Environment Array Tests
// ------------------------------------------------------------

CTEST(test_variable_store_get_envp)
{
    variable_store_t *store = variable_store_create();
    variable_store_add_cstr(store, "EXPORTED1", "value1", true, false);
    variable_store_add_cstr(store, "NOT_EXPORTED", "value2", false, false);
    variable_store_add_cstr(store, "EXPORTED2", "value3", true, false);

    char *const *envp = variable_store_get_envp(store);

    CTEST_ASSERT_NOT_NULL(ctest, envp, "envp returned");

    // Count entries and verify they're formatted as NAME=VALUE
    int count = 0;
    for (char *const *p = envp; *p; p++)
    {
        count++;
        // Check for = sign (basic validation)
        CTEST_ASSERT_NOT_NULL(ctest, strchr(*p, '='), "entry has = sign");
    }

    // Should have 2 exported variables only
    CTEST_ASSERT_EQ(ctest, count, 2, "envp has 2 exported variables");

    variable_store_destroy(&store);
}

CTEST(test_variable_store_get_envp_empty)
{
    variable_store_t *store = variable_store_create();

    char *const *envp = variable_store_get_envp(store);

    CTEST_ASSERT_NOT_NULL(ctest, envp, "envp returned for empty store");
    CTEST_ASSERT_NULL(ctest, envp[0], "envp is null-terminated");

    variable_store_destroy(&store);
}

CTEST(test_variable_store_create_from_envp)
{
    char *test_envp[] = {
        "VAR1=value1",
        "VAR2=value2",
        "VAR3=value3",
        NULL
    };

    variable_store_t *store = variable_store_create_from_envp(test_envp);

    CTEST_ASSERT_NOT_NULL(ctest, store, "store created from envp");
    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(store, "VAR1"), "VAR1 loaded");
    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(store, "VAR2"), "VAR2 loaded");
    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(store, "VAR3"), "VAR3 loaded");
    CTEST_ASSERT_STR_EQ(ctest, variable_store_get_value_cstr(store, "VAR1"), "value1", "VAR1 value correct");

    variable_store_destroy(&store);
}

CTEST(test_variable_store_create_from_envp_with_equals_in_value)
{
    char *test_envp[] = {
        "PATH=/usr/bin:/opt/bin",
        "EQUATION=x=y+z",
        NULL
    };

    variable_store_t *store = variable_store_create_from_envp(test_envp);

    CTEST_ASSERT_STR_EQ(ctest, variable_store_get_value_cstr(store, "PATH"), "/usr/bin:/opt/bin", "PATH value correct");
    CTEST_ASSERT_STR_EQ(ctest, variable_store_get_value_cstr(store, "EQUATION"), "x=y+z", "EQUATION with = sign");

    variable_store_destroy(&store);
}

// ------------------------------------------------------------
// Integration Tests
// ------------------------------------------------------------

CTEST(test_variable_store_complex_scenario)
{
    variable_store_t *store = variable_store_create();

    // Add various variables with different flags
    variable_store_add_cstr(store, "HOME", "/home/user", true, true);
    variable_store_add_cstr(store, "USER", "testuser", true, false);
    variable_store_add_cstr(store, "TEMP_VAR", "temporary", false, false);
    variable_store_add_cstr(store, "PATH", "/usr/bin:/bin", true, false);

    // Verify they all exist and have correct flags
    CTEST_ASSERT_TRUE(ctest, variable_store_has_name_cstr(store, "HOME"), "HOME exists");
    CTEST_ASSERT_TRUE(ctest, variable_store_is_read_only_cstr(store, "HOME"), "HOME is read-only");
    CTEST_ASSERT_TRUE(ctest, variable_store_is_exported_cstr(store, "HOME"), "HOME is exported");

    CTEST_ASSERT_TRUE(ctest, variable_store_is_exported_cstr(store, "USER"), "USER is exported");
    CTEST_ASSERT_FALSE(ctest, variable_store_is_read_only_cstr(store, "USER"), "USER is not read-only");

    CTEST_ASSERT_FALSE(ctest, variable_store_is_exported_cstr(store, "TEMP_VAR"), "TEMP_VAR not exported");

    // Modify USER and try to modify HOME (read-only)
    variable_store_add_cstr(store, "USER", "newuser", true, false);
    const char *new_user = variable_store_get_value_cstr(store, "USER");
    CTEST_ASSERT_STR_EQ(ctest, new_user, "newuser", "USER modified");

    // Remove a variable
    variable_store_remove_cstr(store, "TEMP_VAR");
    CTEST_ASSERT_FALSE(ctest, variable_store_has_name_cstr(store, "TEMP_VAR"), "TEMP_VAR removed");

    // Get environment with only exported variables
    char *const *envp = variable_store_get_envp(store);
    int count = 0;
    for (char *const *p = envp; *p; p++)
    {
        count++;
    }
    CTEST_ASSERT_EQ(ctest, count, 3, "envp has 3 exported variables");

    variable_store_destroy(&store);
}

CTEST(test_variable_store_generation_tracking)
{
    variable_store_t *store = variable_store_create();
    uint32_t gen_before = store->generation;

    variable_store_add_cstr(store, "VAR", "value", false, false);
    uint32_t gen_after = store->generation;

    CTEST_ASSERT_GT(ctest, gen_after, gen_before, "generation incremented on add");

    uint32_t gen_before_remove = store->generation;
    variable_store_remove_cstr(store, "VAR");
    uint32_t gen_after_remove = store->generation;

    CTEST_ASSERT_GT(ctest, gen_after_remove, gen_before_remove, "generation incremented on remove");

    variable_store_destroy(&store);
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
            // Creation and destruction
            CTEST_ENTRY(test_variable_store_create),
            CTEST_ENTRY(test_variable_store_destroy_null),
            CTEST_ENTRY(test_variable_store_clear),

            // Add variable tests
            CTEST_ENTRY(test_variable_store_add_cstr),
            CTEST_ENTRY(test_variable_store_add_string_t),
            CTEST_ENTRY(test_variable_store_add_multiple),
            CTEST_ENTRY(test_variable_store_add_with_empty_name),
            CTEST_ENTRY(test_variable_store_add_with_digit_start),
            CTEST_ENTRY(test_variable_store_add_overwrites_existing),
            CTEST_ENTRY(test_variable_store_add_env_string),

            // Remove variable tests
            CTEST_ENTRY(test_variable_store_remove_cstr),
            CTEST_ENTRY(test_variable_store_remove_string_t),
            CTEST_ENTRY(test_variable_store_remove_nonexistent),

            // Query tests
            CTEST_ENTRY(test_variable_store_has_name_cstr),
            CTEST_ENTRY(test_variable_store_get_value_cstr),
            CTEST_ENTRY(test_variable_store_get_variable_entry),
            CTEST_ENTRY(test_variable_store_get_value_length),

            // Export flag tests
            CTEST_ENTRY(test_variable_store_exported_flag),
            CTEST_ENTRY(test_variable_store_set_exported),

            // Read-only flag tests
            CTEST_ENTRY(test_variable_store_read_only_flag),
            CTEST_ENTRY(test_variable_store_set_read_only),

            // Clone tests
            CTEST_ENTRY(test_variable_store_clone),
            CTEST_ENTRY(test_variable_store_clone_exported),
            CTEST_ENTRY(test_variable_store_clone_independence),

            // Copy tests
            CTEST_ENTRY(test_variable_store_copy_all),

            // Iterator tests
            CTEST_ENTRY(test_variable_store_for_each),
            CTEST_ENTRY(test_variable_store_for_each_finds_variable),
            CTEST_ENTRY(test_variable_store_for_each_empty),

            // Environment array tests
            CTEST_ENTRY(test_variable_store_get_envp),
            CTEST_ENTRY(test_variable_store_get_envp_empty),
            CTEST_ENTRY(test_variable_store_create_from_envp),
            CTEST_ENTRY(test_variable_store_create_from_envp_with_equals_in_value),

            // Integration tests
            CTEST_ENTRY(test_variable_store_complex_scenario),
            CTEST_ENTRY(test_variable_store_generation_tracking),

            NULL
        };

        int result = ctest_run_suite(suite);

        arena_end();
        return result;
    }

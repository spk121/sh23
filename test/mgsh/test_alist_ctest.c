#include <string.h>
#include "ctest.h"
#include "alias.h"
#include "alias_store.h"
#include "alias_array.h"
#include "xalloc.h"

// Test alias_name_is_valid with valid names
CTEST(test_alias_name_valid_simple)
{
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("ls"), "simple lowercase");
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("LS"), "simple uppercase");
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("ls1"), "with digit");
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("l_s"), "with underscore");
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("my-alias"), "with hyphen");
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("my@alias"), "with at sign");
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("my!alias"), "with exclamation");
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("my%alias"), "with percent");
    CTEST_ASSERT_TRUE(ctest, alias_name_is_valid("my,alias"), "with comma");
    (void)ctest;
}

// Test alias_name_is_valid with invalid names
CTEST(test_alias_name_invalid)
{
    CTEST_ASSERT_FALSE(ctest, alias_name_is_valid(""), "empty string");
    CTEST_ASSERT_FALSE(ctest, alias_name_is_valid("my alias"), "with space");
    CTEST_ASSERT_FALSE(ctest, alias_name_is_valid("my\talias"), "with tab");
    CTEST_ASSERT_FALSE(ctest, alias_name_is_valid("my$alias"), "with dollar");
    CTEST_ASSERT_FALSE(ctest, alias_name_is_valid("my=alias"), "with equals");
    CTEST_ASSERT_FALSE(ctest, alias_name_is_valid("my#alias"), "with hash");
    CTEST_ASSERT_FALSE(ctest, alias_name_is_valid("my&alias"), "with ampersand");
    CTEST_ASSERT_FALSE(ctest, alias_name_is_valid("my*alias"), "with asterisk");
    (void)ctest;
}

// Test alias_store_t basic operations
CTEST(test_alias_store_create_destroy)
{
    alias_store_t *store = alias_store_create();
    CTEST_ASSERT_NOT_NULL(ctest, store, "store created");
    CTEST_ASSERT_EQ(ctest, alias_store_size(store), 0, "initial size is 0");
    alias_store_destroy(&store);
    (void)ctest;
}

// Test alias_store_t add and get
CTEST(test_alias_store_add_get)
{
    alias_store_t *store = alias_store_create();
    alias_store_add_cstr(store, "ls", "ls -la");
    
    CTEST_ASSERT_EQ(ctest, alias_store_size(store), 1, "size is 1 after add");
    CTEST_ASSERT_TRUE(ctest, alias_store_has_name_cstr(store, "ls"), "has name 'ls'");
    
    const char *value = alias_store_get_value_cstr(store, "ls");
    CTEST_ASSERT_NOT_NULL(ctest, value, "value not null");
    CTEST_ASSERT_STR_EQ(ctest, value, "ls -la", "value matches");
    
    alias_store_destroy(&store);
    (void)ctest;
}

// Test alias_store_t overwrite existing
CTEST(test_alias_store_overwrite)
{
    alias_store_t *store = alias_store_create();
    alias_store_add_cstr(store, "ls", "ls -la");
    alias_store_add_cstr(store, "ls", "ls -lah");
    
    CTEST_ASSERT_EQ(ctest, alias_store_size(store), 1, "size is still 1 after overwrite");
    
    const char *value = alias_store_get_value_cstr(store, "ls");
    CTEST_ASSERT_STR_EQ(ctest, value, "ls -lah", "value was overwritten");
    
    alias_store_destroy(&store);
    (void)ctest;
}

// Test alias_store_t remove
CTEST(test_alias_store_remove)
{
    alias_store_t *store = alias_store_create();
    alias_store_add_cstr(store, "ls", "ls -la");
    alias_store_add_cstr(store, "ll", "ls -l");
    
    CTEST_ASSERT_EQ(ctest, alias_store_size(store), 2, "size is 2");
    
    bool removed = alias_store_remove_cstr(store, "ls");
    CTEST_ASSERT_TRUE(ctest, removed, "remove returned true");
    CTEST_ASSERT_EQ(ctest, alias_store_size(store), 1, "size is 1 after remove");
    CTEST_ASSERT_FALSE(ctest, alias_store_has_name_cstr(store, "ls"), "ls no longer exists");
    CTEST_ASSERT_TRUE(ctest, alias_store_has_name_cstr(store, "ll"), "ll still exists");
    
    alias_store_destroy(&store);
    (void)ctest;
}

// Test alias_store_t clear
CTEST(test_alias_store_clear)
{
    alias_store_t *store = alias_store_create();
    alias_store_add_cstr(store, "ls", "ls -la");
    alias_store_add_cstr(store, "ll", "ls -l");
    
    alias_store_clear(store);
    
    CTEST_ASSERT_EQ(ctest, alias_store_size(store), 0, "size is 0 after clear");
    
    alias_store_destroy(&store);
    (void)ctest;
}

// Test alias_t creation
CTEST(test_alias_create_destroy)
{
    alias_t *alias = alias_create_from_cstr("myalias", "echo hello");
    CTEST_ASSERT_NOT_NULL(ctest, alias, "alias created");
    
    CTEST_ASSERT_STR_EQ(ctest, alias_get_name_cstr(alias), "myalias", "name matches");
    CTEST_ASSERT_STR_EQ(ctest, alias_get_value_cstr(alias), "echo hello", "value matches");
    
    alias_destroy(&alias);
    (void)ctest;
}

// Test alias_array_t operations
CTEST(test_alias_array_operations)
{
    alias_array_t *array = alias_array_create_with_free((alias_array_free_func_t)alias_destroy);
    
    CTEST_ASSERT_TRUE(ctest, alias_array_is_empty(array), "initially empty");
    
    alias_t *a1 = alias_create_from_cstr("a1", "value1");
    alias_array_append(array, a1);
    
    CTEST_ASSERT_FALSE(ctest, alias_array_is_empty(array), "not empty after append");
    CTEST_ASSERT_EQ(ctest, alias_array_size(array), 1, "size is 1");
    
    alias_t *retrieved = alias_array_get(array, 0);
    CTEST_ASSERT_EQ(ctest, retrieved, a1, "retrieved same alias");
    
    alias_array_destroy(&array);
    (void)ctest;
}

int main()
{
    arena_start();
    
    CTestEntry *suite[] = {
        CTEST_ENTRY(test_alias_name_valid_simple),
        CTEST_ENTRY(test_alias_name_invalid),
        CTEST_ENTRY(test_alias_store_create_destroy),
        CTEST_ENTRY(test_alias_store_add_get),
        CTEST_ENTRY(test_alias_store_overwrite),
        CTEST_ENTRY(test_alias_store_remove),
        CTEST_ENTRY(test_alias_store_clear),
        CTEST_ENTRY(test_alias_create_destroy),
        CTEST_ENTRY(test_alias_array_operations),
        NULL
    };
    
    int result = ctest_run_suite(suite);
    
    arena_end();
    
    return result;
}

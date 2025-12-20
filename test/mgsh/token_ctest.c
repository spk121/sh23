#include "ctest.h"
#include "token.h"

// === Test: token_refcount_basic ===
CTEST(token_refcount_basic) {
    token_t *tok = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, tok, "token_create_word should succeed");
    CTEST_ASSERT_EQ(ctest, tok->refcount, 0, "Initial refcount should be 0");

    // Increment refcount
    token_t *tok_ref = token_ref(tok);
    CTEST_ASSERT_EQ(ctest, tok_ref, tok, "token_ref should return the same token");
    CTEST_ASSERT_EQ(ctest, tok->refcount, 1, "Refcount should be 1 after token_ref");

    // Increment again
    token_ref(tok);
    CTEST_ASSERT_EQ(ctest, tok->refcount, 2, "Refcount should be 2 after second token_ref");

    // Decrement refcount - should not destroy yet
    token_unref(&tok_ref);
    CTEST_ASSERT_NULL(ctest, tok_ref, "token_unref should set pointer to NULL");
    CTEST_ASSERT_EQ(ctest, tok->refcount, 1, "Refcount should be 1 after first token_unref");

    // Final decrement - should destroy
    token_unref(&tok);
    CTEST_ASSERT_NULL(ctest, tok, "token should be NULL after final unref");
}

// === Test: token_refcount_with_null ===
CTEST(token_refcount_with_null) {
    token_t *tok = NULL;
    
    // token_ref with NULL should return NULL
    token_t *tok_ref = token_ref(tok);
    CTEST_ASSERT_NULL(ctest, tok_ref, "token_ref with NULL should return NULL");
    
    // token_unref with NULL should be safe
    token_unref(&tok);
    CTEST_ASSERT_NULL(ctest, tok, "token_unref with NULL should be safe");
}

// === Test: token_list_refcount_basic ===
CTEST(token_list_refcount_basic) {
    token_list_t *list = token_list_create();
    CTEST_ASSERT_NOT_NULL(ctest, list, "token_list_create should succeed");
    CTEST_ASSERT_EQ(ctest, list->refcount, 0, "Initial refcount should be 0");

    // Add some tokens
    token_t *t1 = token_create_word();
    token_t *t2 = token_create_word();
    token_list_append(list, t1);
    token_list_append(list, t2);
    CTEST_ASSERT_EQ(ctest, token_list_size(list), 2, "List should have 2 tokens");

    // Increment refcount
    token_list_t *list_ref = token_list_ref(list);
    CTEST_ASSERT_EQ(ctest, list_ref, list, "token_list_ref should return the same list");
    CTEST_ASSERT_EQ(ctest, list->refcount, 1, "Refcount should be 1 after token_list_ref");

    // Increment again
    token_list_ref(list);
    CTEST_ASSERT_EQ(ctest, list->refcount, 2, "Refcount should be 2 after second token_list_ref");

    // Decrement refcount - should not destroy yet
    token_list_unref(&list_ref);
    CTEST_ASSERT_NULL(ctest, list_ref, "token_list_unref should set pointer to NULL");
    CTEST_ASSERT_EQ(ctest, list->refcount, 1, "Refcount should be 1 after first token_list_unref");

    // Final decrement - should destroy
    token_list_unref(&list);
    CTEST_ASSERT_NULL(ctest, list, "list should be NULL after final unref");
}

// === Test: token_list_refcount_with_null ===
CTEST(token_list_refcount_with_null) {
    token_list_t *list = NULL;
    
    // token_list_ref with NULL should return NULL
    token_list_t *list_ref = token_list_ref(list);
    CTEST_ASSERT_NULL(ctest, list_ref, "token_list_ref with NULL should return NULL");
    
    // token_list_unref with NULL should be safe
    token_list_unref(&list);
    CTEST_ASSERT_NULL(ctest, list, "token_list_unref with NULL should be safe");
}

// === Test: token_recompute_flags ===
CTEST(token_recompute_flags_basic) {
    token_t *tok = token_create_word();
    CTEST_ASSERT_NOT_NULL(ctest, tok, "token_create_word should succeed");

    // Append a literal (no expansion)
    string_t *lit = string_create_from_cstr("hello");
    token_add_literal_part(tok, lit);
    string_destroy(&lit);

    token_recompute_expansion_flags(tok);
    CTEST_ASSERT_FALSE(ctest, tok->needs_expansion, "Literal should not need expansion");
    CTEST_ASSERT_FALSE(ctest, tok->needs_field_splitting, "Literal should not need field splitting");
    CTEST_ASSERT_FALSE(ctest, tok->needs_pathname_expansion, "Literal should not need pathname expansion");

    // Append a parameter expansion
    string_t *param = string_create_from_cstr("USER");
    token_append_parameter(tok, param);
    string_destroy(&param);

    token_recompute_expansion_flags(tok);
    CTEST_ASSERT_TRUE(ctest, tok->needs_expansion, "Parameter should trigger expansion");
    CTEST_ASSERT_TRUE(ctest, tok->needs_field_splitting, "Unquoted parameter should trigger field splitting");

    token_destroy(&tok);
}

// === Test: part_list_remove ===
CTEST(part_list_remove_basic) {
    part_list_t *plist = part_list_create();
    CTEST_ASSERT_NOT_NULL(ctest, plist, "part_list_create should succeed");

    string_t *lit1 = string_create_from_cstr("foo");
    string_t *lit2 = string_create_from_cstr("bar");
    part_list_append(plist, part_create_literal(lit1));
    part_list_append(plist, part_create_literal(lit2));
    string_destroy(&lit1);
    string_destroy(&lit2);

    CTEST_ASSERT_EQ(ctest, part_list_size(plist), 2, "List should have 2 parts");

    // Remove first part
    int rc = part_list_remove(plist, 0);
    CTEST_ASSERT_EQ(ctest, rc, 0, "Remove should succeed");
    CTEST_ASSERT_EQ(ctest, part_list_size(plist), 1, "List should now have 1 part");

    part_list_destroy(&plist);
}

// === Test: token_list_remove ===
CTEST(token_list_remove_basic) {
    token_list_t *tlist = token_list_create();
    CTEST_ASSERT_NOT_NULL(ctest, tlist, "token_list_create should succeed");

    token_t *t1 = token_create_word();
    token_t *t2 = token_create_word();
    token_list_append(tlist, t1);
    token_list_append(tlist, t2);

    CTEST_ASSERT_EQ(ctest, token_list_size(tlist), 2, "List should have 2 tokens");

    // Remove second token
    int rc = token_list_remove(tlist, 1);
    CTEST_ASSERT_EQ(ctest, rc, 0, "Remove should succeed");
    CTEST_ASSERT_EQ(ctest, token_list_size(tlist), 1, "List should now have 1 token");

    token_list_destroy(&tlist);
}

// === Suite definition ===
CTestEntry* token_suite[] = {
    CTEST_ENTRY(token_refcount_basic),
    CTEST_ENTRY(token_refcount_with_null),
    CTEST_ENTRY(token_list_refcount_basic),
    CTEST_ENTRY(token_list_refcount_with_null),
    CTEST_ENTRY(token_recompute_flags_basic),
    CTEST_ENTRY(part_list_remove_basic),
    CTEST_ENTRY(token_list_remove_basic),
    NULL
};

// === Runner main ===
int main(void) {
    return ctest_run_suite(token_suite);
}

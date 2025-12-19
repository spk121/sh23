#include <string.h>
#include <setjmp.h>
#include "ctest.h"
#include "xalloc.h"

// Test arena_xmalloc basic allocation
CTEST(test_arena_xmalloc_basic)
{
    arena_t arena = {0};
    arena_init_ex(&arena);
    
    // Set up jump point for error handling
    if (setjmp(arena.rollback_point) == 0)
    {
        void *ptr = arena_xmalloc(&arena, 100);
        CTEST_ASSERT_NOT_NULL(ctest, ptr, "allocated pointer should not be NULL");
        
        arena_xfree(&arena, ptr);
    }
    
    arena_end_ex(&arena);
    (void)ctest;
}

// Test arena_xcalloc zero initialization
CTEST(test_arena_xcalloc_zero_init)
{
    arena_t arena = {0};
    arena_init_ex(&arena);
    
    if (setjmp(arena.rollback_point) == 0)
    {
        int *arr = arena_xcalloc(&arena, 10, sizeof(int));
        CTEST_ASSERT_NOT_NULL(ctest, arr, "allocated array should not be NULL");
        
        // Verify zero initialization
        for (int i = 0; i < 10; i++)
        {
            CTEST_ASSERT_EQ(ctest, arr[i], 0, "array element should be zero");
        }
        
        arena_xfree(&arena, arr);
    }
    
    arena_end_ex(&arena);
    (void)ctest;
}

// Test arena_xrealloc resizing
CTEST(test_arena_xrealloc_resize)
{
    arena_t arena = {0};
    arena_init_ex(&arena);
    
    if (setjmp(arena.rollback_point) == 0)
    {
        int *ptr = arena_xmalloc(&arena, sizeof(int) * 5);
        CTEST_ASSERT_NOT_NULL(ctest, ptr, "initial allocation should not be NULL");
        
        // Set some values
        for (int i = 0; i < 5; i++)
        {
            ptr[i] = i * 10;
        }
        
        // Reallocate to larger size
        int *new_ptr = arena_xrealloc(&arena, ptr, sizeof(int) * 10);
        CTEST_ASSERT_NOT_NULL(ctest, new_ptr, "reallocated pointer should not be NULL");
        
        // Verify original values are preserved
        for (int i = 0; i < 5; i++)
        {
            CTEST_ASSERT_EQ(ctest, new_ptr[i], i * 10, "original values should be preserved");
        }
        
        arena_xfree(&arena, new_ptr);
    }
    
    arena_end_ex(&arena);
    (void)ctest;
}

// Test arena_xstrdup string duplication
CTEST(test_arena_xstrdup_duplicate)
{
    arena_t arena = {0};
    arena_init_ex(&arena);
    
    if (setjmp(arena.rollback_point) == 0)
    {
        const char *original = "Hello, World!";
        char *dup = arena_xstrdup(&arena, original);
        
        CTEST_ASSERT_NOT_NULL(ctest, dup, "duplicated string should not be NULL");
        CTEST_ASSERT_STR_EQ(ctest, dup, original, "duplicated string should match original");
        CTEST_ASSERT_TRUE(ctest, dup != original, "duplicated string should be different pointer");
        
        arena_xfree(&arena, dup);
    }
    
    arena_end_ex(&arena);
    (void)ctest;
}

// Test arena_xfree and basic tracking
CTEST(test_arena_xfree_tracking)
{
    arena_t arena = {0};
    arena_init_ex(&arena);
    
    if (setjmp(arena.rollback_point) == 0)
    {
        void *ptr1 = arena_xmalloc(&arena, 50);
        void *ptr2 = arena_xmalloc(&arena, 100);
        void *ptr3 = arena_xmalloc(&arena, 150);
        
        CTEST_ASSERT_NOT_NULL(ctest, ptr1, "ptr1 should be allocated");
        CTEST_ASSERT_NOT_NULL(ctest, ptr2, "ptr2 should be allocated");
        CTEST_ASSERT_NOT_NULL(ctest, ptr3, "ptr3 should be allocated");
        
        // Free in different order
        arena_xfree(&arena, ptr2);
        arena_xfree(&arena, ptr1);
        arena_xfree(&arena, ptr3);
    }
    
    arena_end_ex(&arena);
    (void)ctest;
}

// Test arena lifecycle (init/reset/end)
CTEST(test_arena_lifecycle)
{
    arena_t arena = {0};
    arena_init_ex(&arena);
    
    if (setjmp(arena.rollback_point) == 0)
    {
        // Allocate some memory
        void *ptr1 = arena_xmalloc(&arena, 100);
        void *ptr2 = arena_xmalloc(&arena, 200);
        
        CTEST_ASSERT_NOT_NULL(ctest, ptr1, "ptr1 should be allocated");
        CTEST_ASSERT_NOT_NULL(ctest, ptr2, "ptr2 should be allocated");
        
        // Reset should free all allocations
        arena_reset_ex(&arena, false);
        
        // After reset, we should be able to allocate again
        arena_init_ex(&arena);
        if (setjmp(arena.rollback_point) == 0)
        {
            void *ptr3 = arena_xmalloc(&arena, 50);
            CTEST_ASSERT_NOT_NULL(ctest, ptr3, "ptr3 should be allocated after reset");
            arena_xfree(&arena, ptr3);
        }
    }
    
    arena_end_ex(&arena);
    (void)ctest;
}

// Test multiple allocations and frees
CTEST(test_arena_multiple_allocs)
{
    arena_t arena = {0};
    arena_init_ex(&arena);
    
    if (setjmp(arena.rollback_point) == 0)
    {
#if __STDC_VERSION__ >= 202311L && !defined(_MSC_VER)
        constexpr int COUNT = 20;
#else
        #define COUNT 20
#endif
        void *ptrs[COUNT];
        
        // Allocate multiple blocks
        for (int i = 0; i < COUNT; i++)
        {
            ptrs[i] = arena_xmalloc(&arena, (i + 1) * 10);
            CTEST_ASSERT_NOT_NULL(ctest, ptrs[i], "allocation should succeed");
        }
        
        // Free every other one
        for (int i = 0; i < COUNT; i += 2)
        {
            arena_xfree(&arena, ptrs[i]);
        }
        
        // Free the rest
        for (int i = 1; i < COUNT; i += 2)
        {
            arena_xfree(&arena, ptrs[i]);
        }
    }
    
    arena_end_ex(&arena);
    (void)ctest;
}

// Test that NULL can be safely passed to arena_xfree
CTEST(test_arena_xfree_null)
{
    arena_t arena = {0};
    arena_init_ex(&arena);
    
    if (setjmp(arena.rollback_point) == 0)
    {
        // This should not crash
        arena_xfree(&arena, NULL);
        CTEST_ASSERT_TRUE(ctest, true, "freeing NULL should be safe");
    }
    
    arena_end_ex(&arena);
    (void)ctest;
}

int main()
{
    CTestEntry *suite[] = {
        CTEST_ENTRY(test_arena_xmalloc_basic),
        CTEST_ENTRY(test_arena_xcalloc_zero_init),
        CTEST_ENTRY(test_arena_xrealloc_resize),
        CTEST_ENTRY(test_arena_xstrdup_duplicate),
        CTEST_ENTRY(test_arena_xfree_tracking),
        CTEST_ENTRY(test_arena_lifecycle),
        CTEST_ENTRY(test_arena_multiple_allocs),
        CTEST_ENTRY(test_arena_xfree_null),
        NULL
    };
    
    int result = ctest_run_suite(suite);
    
    return result;
}

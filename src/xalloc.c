#include "xalloc.h"
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Default arena configuration constants
#if __STDC_VERSION__ >= 202311L && !defined(_MSC_VER)
constexpr long ARENA_INITIAL_CAP = 64;
constexpr long ARENA_MAX_ALLOCATIONS = 1000000;
#else
#define ARENA_INITIAL_CAP 64
#define ARENA_MAX_ALLOCATIONS 1000000
#endif

// Global singleton arena instance
static arena_t global_arena = {
    .rollback_in_progress = false,
    .allocated_ptrs = NULL,
    .allocated_count = 0,
    .allocated_cap = 0,
    .initial_cap = ARENA_INITIAL_CAP,
    .max_allocations = ARENA_MAX_ALLOCATIONS
};

// Provide access to global arena for arena_start() macro
arena_t *arena_get_global(void)
{
    return &global_arena;
}

// -------------------------------------------------------------
// Internal helpers for maintaining the sorted pointer list
// -------------------------------------------------------------
static int ptr_compare(const void *a, const void *b)
{
    const void *pa = *(const void **)a;
    const void *pb = *(const void **)b;
    return (pa > pb) - (pa < pb);
}

// Find index of pointer p (returns -1 if not found)
static long find_ptr(arena_t *arena, const void *p)
{
    if (!p)
    {
        fprintf(stderr, "find_ptr: NULL pointer passed\n");
        abort();
    }
    // binary search because list is kept sorted
    void **res = bsearch(&p, arena->allocated_ptrs, arena->allocated_count, sizeof(void *), ptr_compare);
    if (!res)
        return -1;
    return res - arena->allocated_ptrs;
}

static long find_insertion_index(arena_t *arena, void *new_ptr)
{
    if (arena->allocated_count == 0)
        return 0;

    long low = 0;
    long high = arena->allocated_count;

    while (low < high)
    {
        long mid = (low + high) / 2;
        if (new_ptr < arena->allocated_ptrs[mid])
        {
            high = mid;
        }
        else
        {
            low = mid + 1;
        }
    }
    return low;
}

// Insert pointer in sorted order (keeps the array sorted)
static void insert_ptr(arena_t *arena, void *p)
{
    if (!p)
    {
        fprintf(stderr, "insert_ptr: NULL pointer passed\n");
        abort();
    }

    if (arena->allocated_count >= arena->max_allocations)
    {
        free(p);
        fprintf(stderr, "exceeded maximum allocation limit (%ld)\n", arena->max_allocations);
        if (!arena->rollback_in_progress)
            longjmp(arena->rollback_point, 1);
        return;
    }

    // Note that we can't use xrealloc here because it would call insert_ptr again.
    if (arena->allocated_count == arena->allocated_cap)
    {
        long new_cap = (arena->allocated_cap > 0) ? arena->allocated_cap * 2 : arena->initial_cap;
        if (new_cap > arena->max_allocations)
            new_cap = arena->max_allocations;
        void **new_ptrs = calloc(new_cap, sizeof(void *));
        if (!new_ptrs)
        {
            free(p);
            if (!arena->rollback_in_progress)
                longjmp(arena->rollback_point, 1);
            return;
        }
        memmove(new_ptrs, arena->allocated_ptrs, arena->allocated_count * sizeof(void *));
        free(arena->allocated_ptrs);
        arena->allocated_ptrs = new_ptrs;
        arena->allocated_cap = new_cap;
    }

    // Find insertion position with binary search
    long idx = find_insertion_index(arena, p);

    // Shift everything right one position
    if (idx < arena->allocated_count)
    {
        memmove(arena->allocated_ptrs + idx + 1, arena->allocated_ptrs + idx, (arena->allocated_count - idx) * sizeof(void *));
    }
    arena->allocated_ptrs[idx] = p;
    arena->allocated_count++;
}

// Remove pointer at index idx (fast removal, preserves order)
static void remove_ptr_at(arena_t *arena, long idx)
{
    if (idx < 0 || idx >= arena->allocated_count)
    {
        fprintf(stderr, "remove_ptr_at: invalid index %ld\n", idx);
        abort();
    }
    memmove(arena->allocated_ptrs + idx, arena->allocated_ptrs + idx + 1, (arena->allocated_count - idx - 1) * sizeof(void *));
    arena->allocated_count--;
}

// -------------------------------------------------------------
// Arena-based public API
// -------------------------------------------------------------
void *arena_xmalloc(arena_t *arena, size_t size)
{
    if (size == 0)
    {
        fprintf(stderr, "arena_xmalloc: invalid argument (size=0)\n");
        abort();
    }

    void *p = malloc(size);
    if (!p)
    {
        if (!arena->rollback_in_progress)
            longjmp(arena->rollback_point, 1); // triggers full cleanup
        return NULL;                          // during cleanup we must not jump again
    }

    insert_ptr(arena, p);
    return p;
}

void *arena_xcalloc(arena_t *arena, size_t n, size_t size)
{
    if (n == 0 || size == 0)
    {
        fprintf(stderr, "arena_xcalloc: invalid arguments (n=%zu, size=%zu)\n", n, size);
        abort();
    }

    void *p = calloc(n, size);
    if (!p)
    {
        if (!arena->rollback_in_progress)
            longjmp(arena->rollback_point, 1);
        return NULL;
    }

    insert_ptr(arena, p);
    return p;
}

void *arena_xrealloc(arena_t *arena, void *old_ptr, size_t new_size)
{
    if (new_size == 0 || old_ptr == NULL)
    {
        fprintf(stderr, "arena_xrealloc: invalid arguments (old_ptr=%p, new_size=%zu)\n", old_ptr, new_size);
        abort();
    }

    long idx = find_ptr(arena, old_ptr);
    void *p = realloc(old_ptr, new_size);
    if (!p)
    {
        if (!arena->rollback_in_progress)
            longjmp(arena->rollback_point, 1);
        return NULL;
    }
    if (idx >= 0)
        remove_ptr_at(arena, idx);
    insert_ptr(arena, p);

    return p;
}

char *arena_xstrdup(arena_t *arena, const char *s)
{
    if (s == NULL)
    {
        fprintf(stderr, "arena_xstrdup: NULL pointer passed\n");
        abort();
    }

    char *p = strdup(s);
    if (!p)
    {
        if (!arena->rollback_in_progress)
            longjmp(arena->rollback_point, 1);
        return NULL;
    }
    insert_ptr(arena, p);
    return p;
}

void arena_xfree(arena_t *arena, void *p)
{
    if (!p)
        return;

    long idx = find_ptr(arena, p);
    if (idx < 0)
    {
        fprintf(stderr, "arena_xfree: double free or corruption detected (%p)\n", p);
        abort();
    }

    remove_ptr_at(arena, idx);
    free(p);
}

void arena_init_ex(arena_t *arena)
{
    // Free existing allocations if arena was previously initialized and still has allocations
    // We check both allocated_ptrs and allocated_count to avoid false positives from uninitialized memory
    if (arena->allocated_ptrs != NULL && arena->allocated_count > 0)
    {
        arena_reset_ex(arena, false);
    }
    
    arena->rollback_in_progress = false;
    arena->initial_cap = ARENA_INITIAL_CAP;
    arena->max_allocations = ARENA_MAX_ALLOCATIONS;
    arena->allocated_ptrs = calloc(arena->initial_cap, sizeof(void *));
    if (!arena->allocated_ptrs)
    {
        fprintf(stderr, "arena_init_ex: failed to allocate initial pointer array\n");
        abort();
    }
    arena->allocated_cap = arena->initial_cap;
    arena->allocated_count = 0;
}

void arena_reset_ex(arena_t *arena, bool verbose)
{
    arena->rollback_in_progress = true;

    // Free from the end to avoid expensive memmove on every removal
    long count = arena->allocated_count;
    while (arena->allocated_count > 0)
    {
        void *p = arena->allocated_ptrs[--arena->allocated_count];
        free(p);
    }

    free(arena->allocated_ptrs);
    arena->allocated_ptrs = NULL;
    if (verbose)
    {
        fprintf(stderr, "Arena reset: freeing %ld allocated blocks\n", count);
    }
    arena->allocated_cap = arena->allocated_count = 0;

    arena->rollback_in_progress = false;
}

void arena_end_ex(arena_t *arena)
{
#ifdef DEBUG
    arena_reset_ex(arena, true);
#else
    arena_reset_ex(arena, false);
#endif
}

// -------------------------------------------------------------
// Public API
// -------------------------------------------------------------
void *xmalloc(size_t size)
{
    return arena_xmalloc(&global_arena, size);
}

void *xcalloc(size_t n, size_t size)
{
    return arena_xcalloc(&global_arena, n, size);
}

void *xrealloc(void *old_ptr, size_t new_size)
{
    return arena_xrealloc(&global_arena, old_ptr, new_size);
}

char *xstrdup(const char *s)
{
    return arena_xstrdup(&global_arena, s);
}

void xfree(void *p)
{
    arena_xfree(&global_arena, p);
}

void arena_init(void)
{
    // Free existing allocations if arena was previously used
    if (global_arena.allocated_ptrs != NULL)
    {
        arena_reset(false);
    }
    
    global_arena.rollback_in_progress = false;
    global_arena.initial_cap = ARENA_INITIAL_CAP;
    global_arena.max_allocations = ARENA_MAX_ALLOCATIONS;
    global_arena.allocated_ptrs = calloc(global_arena.initial_cap, sizeof(void *));
    if (!global_arena.allocated_ptrs)
    {
        fprintf(stderr, "arena_init: failed to allocate initial pointer array\n");
        abort();
    }
    global_arena.allocated_cap = global_arena.initial_cap;
    global_arena.allocated_count = 0;
}

// -------------------------------------------------------------
// Full rollback — called automatically on allocation failure
// -------------------------------------------------------------
void arena_reset(bool verbose)
{
    global_arena.rollback_in_progress = true;

    // Free from the end to avoid expensive memmove on every removal
    long count = global_arena.allocated_count;
    while (global_arena.allocated_count > 0)
    {
        void *p = global_arena.allocated_ptrs[--global_arena.allocated_count];
        free(p);
    }

    free(global_arena.allocated_ptrs);
    global_arena.allocated_ptrs = NULL;
    if (verbose)
    {
        fprintf(stderr, "Arena reset: freeing %ld allocated blocks\n", count);
    }
    global_arena.allocated_cap = global_arena.allocated_count = 0;

    global_arena.rollback_in_progress = false;
}

void arena_end(void)
{
#ifdef DEBUG
    arena_reset(true);
#else
    arena_reset(false);
#endif
}

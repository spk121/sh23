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
#ifdef DEBUG
static int ptr_compare(const void *a, const void *b)
{
    const arena_alloc_t *pa = (const arena_alloc_t *)a;
    const arena_alloc_t *pb = (const arena_alloc_t *)b;
    return (pa->ptr > pb->ptr) - (pa->ptr < pb->ptr);
}
#else
static int ptr_compare(const void *a, const void *b)
{
    const void *pa = *(const void **)a;
    const void *pb = *(const void **)b;
    return (pa > pb) - (pa < pb);
}
#endif

// Find index of pointer p (returns -1 if not found)
static long find_ptr(arena_t *arena, const void *p)
{
    if (!p)
    {
        fprintf(stderr, "find_ptr: NULL pointer passed\n");
        abort();
    }
    // binary search because list is kept sorted
#ifdef DEBUG
    arena_alloc_t key = {.ptr = (void *)p};
    arena_alloc_t *res = bsearch(&key, arena->allocated_ptrs, arena->allocated_count, sizeof(arena_alloc_t), ptr_compare);
#else
    void **res = bsearch(&p, arena->allocated_ptrs, arena->allocated_count, sizeof(void *), ptr_compare);
#endif
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
#ifdef DEBUG
        if (new_ptr < arena->allocated_ptrs[mid].ptr)
#else
        if (new_ptr < arena->allocated_ptrs[mid])
#endif
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
#ifdef DEBUG
static void insert_ptr(arena_t *arena, void *p, const char *file, int line, size_t size)
#else
static void insert_ptr(arena_t *arena, void *p)
#endif
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
#ifdef DEBUG
        arena_alloc_t *new_ptrs = calloc(new_cap, sizeof(arena_alloc_t));
        if (!new_ptrs)
        {
            free(p);
            if (!arena->rollback_in_progress)
                longjmp(arena->rollback_point, 1);
            return;
        }
        memmove(new_ptrs, arena->allocated_ptrs, arena->allocated_count * sizeof(arena_alloc_t));
#else
        void **new_ptrs = calloc(new_cap, sizeof(void *));
        if (!new_ptrs)
        {
            free(p);
            if (!arena->rollback_in_progress)
                longjmp(arena->rollback_point, 1);
            return;
        }
        memmove(new_ptrs, arena->allocated_ptrs, arena->allocated_count * sizeof(void *));
#endif
        free(arena->allocated_ptrs);
        arena->allocated_ptrs = new_ptrs;
        arena->allocated_cap = new_cap;
    }

    // Find insertion position with binary search
    long idx = find_insertion_index(arena, p);

    // Shift everything right one position
    if (idx < arena->allocated_count)
    {
#ifdef DEBUG
        memmove(arena->allocated_ptrs + idx + 1, arena->allocated_ptrs + idx, (arena->allocated_count - idx) * sizeof(arena_alloc_t));
#else
        memmove(arena->allocated_ptrs + idx + 1, arena->allocated_ptrs + idx, (arena->allocated_count - idx) * sizeof(void *));
#endif
    }
#ifdef DEBUG
    arena->allocated_ptrs[idx].ptr = p;
    snprintf(arena->allocated_ptrs[idx].file, sizeof(arena->allocated_ptrs[idx].file), "%s", file);
    arena->allocated_ptrs[idx].line = line;
    arena->allocated_ptrs[idx].size = (int)size;
    fprintf(stderr, "ALLOC: %p %s:%d %d\n", p, file, line, (int)size);
#else
    arena->allocated_ptrs[idx] = p;
#endif
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
#ifdef DEBUG
    memmove(arena->allocated_ptrs + idx, arena->allocated_ptrs + idx + 1, (arena->allocated_count - idx - 1) * sizeof(arena_alloc_t));
#else
    memmove(arena->allocated_ptrs + idx, arena->allocated_ptrs + idx + 1, (arena->allocated_count - idx - 1) * sizeof(void *));
#endif
    arena->allocated_count--;
}

// -------------------------------------------------------------
// Arena-based public API
// -------------------------------------------------------------
#ifdef DEBUG
void *arena_xmalloc(arena_t *arena, size_t size, const char *file, int line)
#else
void *arena_xmalloc(arena_t *arena, size_t size)
#endif
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

#ifdef DEBUG
    insert_ptr(arena, p, file, line, size);
#else
    insert_ptr(arena, p);
#endif
    return p;
}

#ifdef DEBUG
void *arena_xcalloc(arena_t *arena, size_t n, size_t size, const char *file, int line)
#else
void *arena_xcalloc(arena_t *arena, size_t n, size_t size)
#endif
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

#ifdef DEBUG
    insert_ptr(arena, p, file, line, n * size);
#else
    insert_ptr(arena, p);
#endif
    return p;
}

#ifdef DEBUG
void *arena_xrealloc(arena_t *arena, void *old_ptr, size_t new_size, const char *file, int line)
#else
void *arena_xrealloc(arena_t *arena, void *old_ptr, size_t new_size)
#endif
{
    if (new_size == 0 || old_ptr == NULL)
    {
        fprintf(stderr, "arena_xrealloc: invalid arguments (old_ptr=%p, new_size=%zu)\n", old_ptr, new_size);
        abort();
    }

    long idx = find_ptr(arena, old_ptr);
#ifdef DEBUG
    if (idx >= 0)
    {
        // Print old allocation info
        fprintf(stderr, "REALLOC: %p %s:%d %d -> ", 
                arena->allocated_ptrs[idx].ptr,
                arena->allocated_ptrs[idx].file,
                arena->allocated_ptrs[idx].line,
                arena->allocated_ptrs[idx].size);
    }
#endif
    void *p = realloc(old_ptr, new_size);
    if (!p)
    {
        if (!arena->rollback_in_progress)
            longjmp(arena->rollback_point, 1);
        return NULL;
    }
    if (idx >= 0)
        remove_ptr_at(arena, idx);
#ifdef DEBUG
    // Print new allocation info
    fprintf(stderr, "%p %s:%d %d\n", p, file, line, (int)new_size);
    insert_ptr(arena, p, file, line, new_size);
#else
    insert_ptr(arena, p);
#endif

    return p;
}

#ifdef DEBUG
char *arena_xstrdup(arena_t *arena, const char *s, const char *file, int line)
#else
char *arena_xstrdup(arena_t *arena, const char *s)
#endif
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
#ifdef DEBUG
    insert_ptr(arena, p, file, line, strlen(s) + 1);
#else
    insert_ptr(arena, p);
#endif
    return p;
}

void arena_xfree(arena_t *arena, void *p)
{
    if (!p)
        return;

    long idx = find_ptr(arena, p);
    if (idx < 0)
    {
#ifdef DEBUG
        fprintf(stderr, "FREE: %p (unknown):0 0 (double free or corruption detected)\n", p);
#endif
        fprintf(stderr, "arena_xfree: double free or corruption detected (%p)\n", p);
        abort();
    }

#ifdef DEBUG
    fprintf(stderr, "FREE: %p %s:%d %d -> %p (freed):0 0\n",
            arena->allocated_ptrs[idx].ptr,
            arena->allocated_ptrs[idx].file,
            arena->allocated_ptrs[idx].line,
            arena->allocated_ptrs[idx].size,
            p);
#endif
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
#ifdef DEBUG
    arena->allocated_ptrs = calloc(arena->initial_cap, sizeof(arena_alloc_t));
#else
    arena->allocated_ptrs = calloc(arena->initial_cap, sizeof(void *));
#endif
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
#ifdef DEBUG
    if (verbose)
    {
        for (long i = 0; i < arena->allocated_count; i++)
        {
            fprintf(stderr, "LEAK: %p %s:%d %d\n",
                    arena->allocated_ptrs[i].ptr,
                    arena->allocated_ptrs[i].file,
                    arena->allocated_ptrs[i].line,
                    arena->allocated_ptrs[i].size);
        }
    }
#endif
    while (arena->allocated_count > 0)
    {
#ifdef DEBUG
        void *p = arena->allocated_ptrs[--arena->allocated_count].ptr;
#else
        void *p = arena->allocated_ptrs[--arena->allocated_count];
#endif
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
#ifndef DEBUG
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
#endif

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
#ifdef DEBUG
    global_arena.allocated_ptrs = calloc(global_arena.initial_cap, sizeof(arena_alloc_t));
#else
    global_arena.allocated_ptrs = calloc(global_arena.initial_cap, sizeof(void *));
#endif
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
#ifdef DEBUG
    if (verbose)
    {
        for (long i = 0; i < global_arena.allocated_count; i++)
        {
            fprintf(stderr, "LEAK: %p %s:%d %d\n",
                    global_arena.allocated_ptrs[i].ptr,
                    global_arena.allocated_ptrs[i].file,
                    global_arena.allocated_ptrs[i].line,
                    global_arena.allocated_ptrs[i].size);
        }
    }
#endif
    while (global_arena.allocated_count > 0)
    {
#ifdef DEBUG
        void *p = global_arena.allocated_ptrs[--global_arena.allocated_count].ptr;
#else
        void *p = global_arena.allocated_ptrs[--global_arena.allocated_count];
#endif
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

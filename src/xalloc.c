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
static arena_t global_arena = {.rollback_in_progress = false,
                               .allocated_ptrs = NULL,
                               .allocated_count = 0,
                               .allocated_cap = 0,
                               .initial_cap = ARENA_INITIAL_CAP,
                               .max_allocations = ARENA_MAX_ALLOCATIONS};

// Provide access to global arena for arena_start() macro
arena_t *arena_get_global(void)
{
    return &global_arena;
}

// -------------------------------------------------------------
// Internal helpers for maintaining the sorted pointer list
// -------------------------------------------------------------
#ifdef ARENA_DEBUG
// Helper function to extract basename from file path (returns pointer to basename within the
// original string)
static const char *get_basename(const char *file)
{
    const char *basename = file;
    const char *p = file;
    while (*p)
    {
#ifdef POSIX_API
        if (*p == '/')
#elifdef UCRT_API
        if (*p == '/' || *p == '\\')
#else
        if (*p == '/' || *p == '\\')
#endif
        {
            basename = p + 1;
        }
        p++;
    }
    return basename;
}

static void strncpy_t(char *s1, const char *s2, int n)
{
    if (!s1 || !s2 || n <= 0)
        return;
    if (n == 1)
    {
        *s1 = '\0';
        return;
    }
    if (s1 == s2)
    {
        s1[n - 1] = '\0';
        return;
    }
    strncpy(s1, s2, n - 1);
    s1[n - 1] = '\0';
}

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
#ifdef ARENA_DEBUG
    arena_alloc_t key = {.ptr = (void *)p};
    arena_alloc_t *res = bsearch(&key, arena->allocated_ptrs, arena->allocated_count,
                                 sizeof(arena_alloc_t), ptr_compare);
#else
    void **res =
        bsearch(&p, arena->allocated_ptrs, arena->allocated_count, sizeof(void *), ptr_compare);
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
#ifdef ARENA_DEBUG
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

// Insert pointer in sorted order (keeps the array sorted).
// On allocation failure, frees p and does longjmp to rollback point.
// Returns false if the pointer wasn't inserted.
#ifdef ARENA_DEBUG
static bool insert_ptr(arena_t *arena, void *p, const char *file, int line, size_t size)
#else
static bool insert_ptr(arena_t *arena, void *p)
#endif
{
    if (!p)
    {
        fprintf(stderr, "insert_ptr: NULL pointer passed\n");
        abort();
    }

#ifdef ARENA_DEBUG
    // Check for ANY overlap with existing allocations.
    // Yes, I know this is O(n^2) in the worst case, but this is only debug mode.
    for (long i = 0; i < arena->allocated_count; i++)
    {
        void *old_begin = arena->allocated_ptrs[i].ptr;
        void *old_end = (void *)((char *)old_begin + arena->allocated_ptrs[i].size);
        void *new_begin = p;
        void *new_end = (void *)((char *)p + size);

        if (!(old_end <= new_begin || new_end <= old_begin))
        {
            fprintf(stderr,
                    "SHADOW: existing allocation [%p-%p] %s:%d %zu overlaps new allocation [%p-%p] "
                    "%s:%d %zu\n",
                    old_begin, old_end, arena->allocated_ptrs[i].file,
                    arena->allocated_ptrs[i].line, arena->allocated_ptrs[i].size, new_begin,
                    new_end, get_basename(file), line, size);
            fprintf(stderr,
                    "ERROR: overlapping memory allocations detected - possible heap corruption\n");
            abort();
        }
    }
#endif

    if (arena->allocated_count >= arena->max_allocations)
    {
        fprintf(stderr, "exceeded maximum allocation limit (%ld)\n", arena->max_allocations);
        if (!arena->rollback_in_progress)
            longjmp(arena->rollback_point, 1);
        return false;
    }

    // Note that we can't use xrealloc here because it would call insert_ptr again.
    if (arena->allocated_count == arena->allocated_cap)
    {
        long new_cap = (arena->allocated_cap > 0) ? arena->allocated_cap * 2 : arena->initial_cap;
        if (new_cap > arena->max_allocations)
            new_cap = arena->max_allocations;
#ifdef ARENA_DEBUG
        arena_alloc_t *new_ptrs = calloc(new_cap, sizeof(arena_alloc_t));
        if (!new_ptrs)
        {
            if (!arena->rollback_in_progress)
                longjmp(arena->rollback_point, 1);
            return false;
        }
        memmove(new_ptrs, arena->allocated_ptrs, arena->allocated_count * sizeof(arena_alloc_t));
#else
        void **new_ptrs = calloc(new_cap, sizeof(void *));
        if (!new_ptrs)
        {
            if (!arena->rollback_in_progress)
                longjmp(arena->rollback_point, 1);
            return false;
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
#ifdef ARENA_DEBUG
        memmove(arena->allocated_ptrs + idx + 1, arena->allocated_ptrs + idx,
                (arena->allocated_count - idx) * sizeof(arena_alloc_t));
#else
        memmove(arena->allocated_ptrs + idx + 1, arena->allocated_ptrs + idx,
                (arena->allocated_count - idx) * sizeof(void *));
#endif
    }
#ifdef ARENA_DEBUG
    arena->allocated_ptrs[idx].ptr = p;
    strncpy_t(arena->allocated_ptrs[idx].file, get_basename(file), ARENA_DEBUG_FILENAME_MAX_LEN);
    arena->allocated_ptrs[idx].line = line;
    arena->allocated_ptrs[idx].size = size;
    fprintf(stderr, "ALLOC: %p %s:%d %zu\n", p, arena->allocated_ptrs[idx].file, line, size);
#else
    arena->allocated_ptrs[idx] = p;
#endif
    arena->allocated_count++;
    return true;
}

// Remove pointer at index idx (fast removal, preserves order)
static void remove_ptr_at(arena_t *arena, long idx)
{
    if (idx < 0 || idx >= arena->allocated_count)
    {
        fprintf(stderr, "remove_ptr_at: invalid index %ld\n", idx);
        abort();
    }
#ifdef ARENA_DEBUG
    memmove(arena->allocated_ptrs + idx, arena->allocated_ptrs + idx + 1,
            (arena->allocated_count - idx - 1) * sizeof(arena_alloc_t));
#else
    memmove(arena->allocated_ptrs + idx, arena->allocated_ptrs + idx + 1,
            (arena->allocated_count - idx - 1) * sizeof(void *));
#endif
    arena->allocated_count--;
}

// -------------------------------------------------------------
// Arena-based public API
// -------------------------------------------------------------
void *arena_xmalloc(arena_t *arena, size_t size ARENA_DEBUG_PARAMS)
{
    if (!arena)
    {
        fprintf(stderr, "arena_xmalloc: NULL pointer passed\n");
        abort();
    }
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
        return NULL;                           // during cleanup we must not jump again
    }

    bool inserted;
#ifdef ARENA_DEBUG
    inserted = insert_ptr(arena, p, file, line, size);
#else
    inserted = insert_ptr(arena, p);
#endif
    if (!inserted)
    {
        fprintf(stderr, "%s: %p failed to track allocation\n", __func__, p);
    }
    return p;
}

void *arena_xcalloc(arena_t *arena, size_t n, size_t size ARENA_DEBUG_PARAMS)
{
    if (!arena)
    {
        fprintf(stderr, "arena_xcalloc: NULL pointer passed\n");
        abort();
    }
    if (n == 0 || size == 0)
    {
        fprintf(stderr, "arena_xcalloc: invalid arguments (n=%zu, size=%zu)\n", n, size);
        abort();
    }

    void *p = calloc(n, size);
    if (!p)
    {
        if (!arena->rollback_in_progress)
        {
            longjmp(arena->rollback_point, 1);
        }
        return NULL;
    }

    bool inserted;
#ifdef ARENA_DEBUG
    inserted = insert_ptr(arena, p, file, line, n * size);
#else
    inserted = insert_ptr(arena, p);
#endif
    if (!inserted)
    {
        fprintf(stderr, "%s: %p failed to track allocation\n", __func__, p);
    }
    return p;
}

void *arena_xrealloc(arena_t *arena, void *old_ptr, size_t new_size ARENA_DEBUG_PARAMS)
{
    if (arena == NULL)
    {
        fprintf(stderr, "arena_xrealloc: NULL pointer passed\n");
        abort();
    }
    if (old_ptr == NULL)
#ifdef ARENA_DEBUG
        return arena_xmalloc(arena, new_size, file, line);
#else
        return arena_xmalloc(arena, new_size);
#endif

    if (new_size == 0)
    {
#ifdef ARENA_DEBUG
        arena_xfree(arena, old_ptr, file, line);
#else
        arena_xfree(arena, old_ptr);
#endif
        return NULL;
    }

    long idx = find_ptr(arena, old_ptr);
#ifdef ARENA_DEBUG
    if (idx >= 0)
    {
        // Print old allocation info for tracked pointer
        fprintf(stderr, "REALLOC: %p %s:%d %zu -> ", arena->allocated_ptrs[idx].ptr,
                arena->allocated_ptrs[idx].file, arena->allocated_ptrs[idx].line,
                arena->allocated_ptrs[idx].size);
    }
    else
    {
        // Pointer was not tracked; still log the reallocation for consistency
        fprintf(stderr, "REALLOC: %p (untracked) -> ", old_ptr);
    }
#endif
    void *p = realloc(old_ptr, new_size);
    if (!p)
    {
        if (!arena->rollback_in_progress)
        {
            longjmp(arena->rollback_point, 1);
        }
        return NULL;
    }
    if (idx >= 0)
        remove_ptr_at(arena, idx);
    bool inserted;
#ifdef ARENA_DEBUG
    // Print new allocation info
    fprintf(stderr, "%p %s:%d %zu\n", p, get_basename(file), line, new_size);
    inserted = insert_ptr(arena, p, file, line, new_size);
#else
    inserted = insert_ptr(arena, p);
#endif
    if (!inserted)
    {
        fprintf(stderr, "%s: %p failed to track allocation\n", __func__, p);
    }
    return p;
}

char *arena_xstrdup(arena_t *arena, const char *s ARENA_DEBUG_PARAMS)
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
    bool inserted;
#ifdef ARENA_DEBUG
    inserted = insert_ptr(arena, p, file, line, strlen(s) + 1);
#else
    inserted = insert_ptr(arena, p);
#endif
    if (!inserted)
    {
        fprintf(stderr, "%s: %p failed to track allocation\n", __func__, p);
    }
    return p;
}

void arena_xfree(arena_t *arena, void *p ARENA_DEBUG_PARAMS)
{
    if (!arena)
    {
        fprintf(stderr, "arena_xfree: NULL arena pointer\n");
        abort();
    }
    if (!p)
        return;

#ifdef ARENA_DEBUG
    fprintf(stderr, "FREE: %p %s:%d 0 \n", p, get_basename(file), line);
#endif
    long idx = find_ptr(arena, p);
    if (idx < 0)
    {
#ifdef ARENA_DEBUG
        fprintf(stderr, "DEALLOC: %p (unknown):0 0 (double free or corruption detected)\n", p);
#endif
        fprintf(stderr, "arena_xfree: double free or corruption detected (%p)\n", p);
        abort();
    }

#ifdef ARENA_DEBUG
    fprintf(stderr, "DEALLOC: %p %s:%d %zu -> %p (freed):0 0\n", arena->allocated_ptrs[idx].ptr,
            arena->allocated_ptrs[idx].file, arena->allocated_ptrs[idx].line,
            arena->allocated_ptrs[idx].size, p);
#endif
    remove_ptr_at(arena, idx);
    free(p);
}

void arena_init_ex(arena_t *arena)
{
    if (!arena)
    {
        fprintf(stderr, "arena_init_ex: NULL arena pointer\n");
        abort();
    }

    // Free existing allocations if arena was previously initialized and still has allocations
    // We check both allocated_ptrs and allocated_count to avoid false positives from uninitialized
    // memory
    if (arena->allocated_ptrs != NULL && arena->allocated_count > 0)
    {
        arena_reset_ex(arena);
    }
    arena->rollback_in_progress = false;
    arena->initial_cap = ARENA_INITIAL_CAP;
    arena->max_allocations = ARENA_MAX_ALLOCATIONS;
    arena->resource_cleanup = NULL;
    arena->resource_cleanup_user_data = NULL;
#ifdef ARENA_DEBUG
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

void arena_set_cleanup_ex(arena_t *arena, arena_resource_cleanup_fn fn, void *user_data)
{
    if (!arena)
    {
        fprintf(stderr, "arena_set_cleanup_ex: NULL arena pointer\n");
        abort();
    }
    arena->resource_cleanup = fn;
    arena->resource_cleanup_user_data = user_data;
}

void arena_reset_ex(arena_t *arena)
{
    if (!arena)
    {
        fprintf(stderr, "arena_reset_ex: NULL arena pointer\n");
        abort();
    }
#ifdef ARENA_DEBUG
    long count = arena->allocated_count;
#endif
    arena->rollback_in_progress = true;

    // Call cleanup callback BEFORE freeing memory so it can access the structures
    if (arena->resource_cleanup)
    {
        arena->resource_cleanup(arena->resource_cleanup_user_data);
    }

    // Free from the end to avoid expensive memmove on every removal
#ifdef ARENA_DEBUG
    for (long i = 0; i < arena->allocated_count; i++)
    {
        fprintf(stderr, "LEAK: %p %s:%d %zu\n", arena->allocated_ptrs[i].ptr,
                arena->allocated_ptrs[i].file, arena->allocated_ptrs[i].line,
                arena->allocated_ptrs[i].size);
    }
#endif
    while (arena->allocated_count > 0)
    {
#ifdef ARENA_DEBUG
        void *p = arena->allocated_ptrs[--arena->allocated_count].ptr;
#else
        void *p = arena->allocated_ptrs[--arena->allocated_count];
#endif
        free(p);
    }

    free(arena->allocated_ptrs);
    arena->allocated_ptrs = NULL;
#ifdef ARENA_DEBUG
    if (count > 0)
    {
        fprintf(stderr, "Arena reset: freeing %ld allocated blocks\n", count);
    }
#endif
    arena->allocated_cap = arena->allocated_count = 0;

    arena->rollback_in_progress = false;
}

void arena_end_ex(arena_t *arena)
{
    arena_reset_ex(arena);
}

// -------------------------------------------------------------
// Public API
// -------------------------------------------------------------
#ifndef ARENA_DEBUG
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
#endif

void arena_init(void)
{
    arena_init_ex(&global_arena);
}

void arena_set_cleanup(arena_resource_cleanup_fn fn, void *user_data)
{
    arena_set_cleanup_ex(&global_arena, fn, user_data);
}

// -------------------------------------------------------------
// Full rollback — called automatically on allocation failure
// -------------------------------------------------------------
void arena_reset(void)
{
    arena_reset_ex(&global_arena);
}

void arena_end(void)
{
    arena_reset_ex(&global_arena);
}

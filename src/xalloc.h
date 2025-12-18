#ifndef ARENA_ALLOC_H
#define ARENA_ALLOC_H

#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>

extern jmp_buf arena_rollback_point;
extern bool arena_rollback_in_progress;

/**
 * Arena allocator state structure.
 * Encapsulates all the state needed for memory tracking.
 */
typedef struct arena_t {
    jmp_buf rollback_point;
    bool rollback_in_progress;
    void **allocated_ptrs;  // dynamically resized sorted array
    long allocated_count;
    long allocated_cap;
    long initial_cap;       // initial capacity for allocated_ptrs array
    long max_allocations;   // maximum number of allocations allowed
} arena_t;

/**
 * Place this code in main, just after initializing the program but before any allocations:
 */
#define arena_start()                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        arena_init();                                                                                                  \
        if (setjmp(arena_rollback_point) != 0)                                                                         \
        {                                                                                                              \
            arena_reset(false);                                                                                        \
            fprintf(stderr, "Out of memory — all allocated memory has been freed, restarting logic...\n");             \
        }                                                                                                              \
    } while (0)

/**
 * Allocate memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 */
void *xmalloc(size_t size);

/**
 * Allocate zero-initialized memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 */
void *xcalloc(size_t n, size_t size);

/**
 * Reallocate memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 * If old_ptr is NULL, behaves like xmalloc.
 * If new_size is 0, behaves like xfree.
 */
void *xrealloc(void *old_ptr, size_t new_size);

/**
 * Duplicate a string, with memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 */
char *xstrdup(const char *s);

/**
 * Free memory allocated by the arena.
 * Safe to call with NULL.
 */
void xfree(void *ptr);

void arena_init(void);

/**
 * Free all allocated memory tracked by the arena.
 * This is called automatically on out-of-memory conditions.
 */
void arena_reset(bool verbose);

/**
 * Call this at the end of main to free all allocated memory.
 */
void arena_end(void);

/**
 * Arena-based allocation functions that operate on an explicit arena_t.
 * 
 * These functions provide the same functionality as the global arena functions
 * (xmalloc, xcalloc, etc.) but operate on a specific arena instance, allowing
 * multiple independent arenas and better testability.
 * 
 * Note: These are primarily provided for unit testing. General code should use
 * the shorter API like xmalloc and xfree which operate on the global arena.
 */
void *arena_xmalloc(arena_t *arena, size_t size);
void *arena_xcalloc(arena_t *arena, size_t n, size_t size);
void *arena_xrealloc(arena_t *arena, void *old_ptr, size_t new_size);
char *arena_xstrdup(arena_t *arena, const char *s);
void arena_xfree(arena_t *arena, void *ptr);
void arena_init_ex(arena_t *arena);
void arena_reset_ex(arena_t *arena, bool verbose);
void arena_end_ex(arena_t *arena);

#endif // ARENA_ALLOC_H
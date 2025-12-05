#ifndef ARENA_ALLOC_H
#define ARENA_ALLOC_H

#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>

extern jmp_buf arena_rollback_point;
extern bool arena_rollback_in_progress;

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

#endif // ARENA_ALLOC_H
#ifndef ARENA_ALLOC_H
#define ARENA_ALLOC_H

#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#if defined(ARENA_THREAD_SAFE) && !defined(__STDC_NO_THREADS__)
#include <threads.h>
#endif

#ifdef DEBUG
/**
 * Structure to track allocation information in DEBUG mode.
 */
typedef struct {
    void *ptr;
    char file[256];
    int line;
    size_t size;
} arena_alloc_t;
#endif

/**
 * Arena allocator state structure.
 * Encapsulates all the state needed for memory tracking.
 */
typedef struct arena_t {
    jmp_buf rollback_point;
#if defined(ARENA_THREAD_SAFE) && !defined(__STDC_NO_THREADS__)
    mtx_t mutex;
#endif    
    bool rollback_in_progress;
#ifdef DEBUG
    arena_alloc_t *allocated_ptrs;  // dynamically resized sorted array
#else
    void **allocated_ptrs;  // dynamically resized sorted array
#endif
    long allocated_count;
    long allocated_cap;
    long initial_cap;       // initial capacity for allocated_ptrs array
    long max_allocations;   // maximum number of allocations allowed
} arena_t;

// Access to global singleton arena for use in arena_start() macro
extern arena_t *arena_get_global(void);

/**
 * Place this code in main, just after initializing the program but before any allocations:
 */
#define arena_start()                                                                                        \
    do                                                                                                       \
    {                                                                                                        \
        arena_init();                                                                                        \
        if (setjmp(arena_get_global()->rollback_point) != 0)                                                 \
        {                                                                                                    \
            arena_reset();                                                                                   \
            fprintf(stderr, "Out of memory — all allocated memory has been freed, restarting logic...\n");   \
        }                                                                                                    \
    } while (0)

/**
 * Allocate memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 */
#ifdef DEBUG
#define xmalloc(size) arena_xmalloc(arena_get_global(), (size), __FILE__, __LINE__)
#else
void *xmalloc(size_t size);
#endif

/**
 * Allocate zero-initialized memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 */
#ifdef DEBUG
#define xcalloc(n, size) arena_xcalloc(arena_get_global(), (n), (size), __FILE__, __LINE__)
#else
void *xcalloc(size_t n, size_t size);
#endif

/**
 * Reallocate memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 * If old_ptr is NULL, behaves like xmalloc.
 * If new_size is 0, behaves like xfree.
 */
#ifdef DEBUG
#define xrealloc(old_ptr, new_size) arena_xrealloc(arena_get_global(), (old_ptr), (new_size), __FILE__, __LINE__)
#else
void *xrealloc(void *old_ptr, size_t new_size);
#endif

/**
 * Duplicate a string, with memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 */
#ifdef DEBUG
#define xstrdup(s) arena_xstrdup(arena_get_global(), (s), __FILE__, __LINE__)
#else
char *xstrdup(const char *s);
#endif

/**
 * Free memory allocated by the arena.
 * Safe to call with NULL.
 */
void xfree(void *ptr);

#if __STDC_VERSION__ >= 202311L
void arena_init();
#else
void arena_init(void);
#endif

/**
 * Free all allocated memory tracked by the arena.
 * This is called automatically on out-of-memory conditions.
 */
#if __STDC_VERSION__ >= 202311L
void arena_reset();
#else
void arena_reset(void);
#endif

/**
 * Call this at the end of main to free all allocated memory.
 */
#if __STDC_VERSION__ >= 202311L
void arena_end();
#else
void arena_end(void);
#endif

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
#ifdef DEBUG
void *arena_xmalloc(arena_t *arena, size_t size, const char *file, int line);
void *arena_xcalloc(arena_t *arena, size_t n, size_t size, const char *file, int line);
void *arena_xrealloc(arena_t *arena, void *old_ptr, size_t new_size, const char *file, int line);
char *arena_xstrdup(arena_t *arena, const char *s, const char *file, int line);
void arena_xfree(arena_t *arena, void *ptr, const char *file, int line);
#else
void *arena_xmalloc(arena_t *arena, size_t size);
void *arena_xcalloc(arena_t *arena, size_t n, size_t size);
void *arena_xrealloc(arena_t *arena, void *old_ptr, size_t new_size);
char *arena_xstrdup(arena_t *arena, const char *s);
void arena_xfree(arena_t *arena, void *ptr);
#endif
void arena_init_ex(arena_t *arena);
void arena_reset_ex(arena_t *arena);
void arena_end_ex(arena_t *arena);

#endif // ARENA_ALLOC_H
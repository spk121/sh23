#ifndef ARENA_ALLOC_H
#define ARENA_ALLOC_H

#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>

// #define ARENA_DEBUG
#ifdef ARENA_DEBUG
/**
 * Structure to track allocation information in ARENA_DEBUG mode.
 */
#define ARENA_DEBUG_FILENAME_MAX_LEN 256
typedef struct
{
    void *ptr;
    char file[ARENA_DEBUG_FILENAME_MAX_LEN];
    int line;
    size_t size;
} arena_alloc_t;
#endif

typedef void (*arena_resource_cleanup_fn)(void *user_data);

/**
 * Arena allocator state structure.
 * Encapsulates all the state needed for memory tracking.
 */
typedef struct arena_t
{
    jmp_buf rollback_point;
    bool rollback_in_progress;
#ifdef ARENA_DEBUG
    arena_alloc_t *allocated_ptrs; // dynamically resized sorted array
#else
    void **allocated_ptrs; // dynamically resized sorted array
#endif
    long allocated_count;
    long allocated_cap;
    long initial_cap;     // initial capacity for allocated_ptrs array
    long max_allocations; // maximum number of allocations allowed
    arena_resource_cleanup_fn resource_cleanup;
    void *resource_cleanup_user_data;
} arena_t;

// Access to global singleton arena for use in arena_start() macro
extern arena_t *arena_get_global(void);

/**
 * Place this code in main, just after initializing the program but before any allocations:
 */
#define arena_start()                                                                              \
    do                                                                                             \
    {                                                                                              \
        arena_init();                                                                              \
        if (setjmp(arena_get_global()->rollback_point) != 0)                                       \
        {                                                                                          \
            arena_reset();                                                                         \
            fprintf(stderr,                                                                        \
                    "Out of memory — all allocated memory has been freed, restarting logic...\n"); \
        }                                                                                          \
    } while (0)

/**
 * Allocate memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 */
#ifdef ARENA_DEBUG
#define xmalloc(size) arena_xmalloc(arena_get_global(), (size), __FILE__, __LINE__)
#else
void *xmalloc(size_t size);
#endif

/**
 * Allocate zero-initialized memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 */
#ifdef ARENA_DEBUG
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
#ifdef ARENA_DEBUG
#define xrealloc(old_ptr, new_size)                                                                \
    arena_xrealloc(arena_get_global(), (old_ptr), (new_size), __FILE__, __LINE__)
#else
void *xrealloc(void *old_ptr, size_t new_size);
#endif

/**
 * Duplicate a string, with memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 */
#ifdef ARENA_DEBUG
#define xstrdup(s) arena_xstrdup(arena_get_global(), (s), __FILE__, __LINE__)
#else
char *xstrdup(const char *s);
#endif

/**
 * Free memory allocated by the arena.
 * Safe to call with NULL.
 */
#ifdef ARENA_DEBUG
#define xfree(ptr) arena_xfree(arena_get_global(), (ptr), __FILE__, __LINE__)
#else
void xfree(void *ptr);
#endif

/**
 * Initialize the global arena allocator.
 * Must be called before any allocations are made.
 */
void arena_init(void);

void arena_set_cleanup(arena_resource_cleanup_fn fn, void *user_data);

/**
 * Free all allocated memory tracked by the arena.
 * This is called automatically on out-of-memory conditions.
 */
void arena_reset(void);

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
#ifdef ARENA_DEBUG
#define ARENA_DEBUG_PARAMS , const char *file, int line
#else
#define ARENA_DEBUG_PARAMS
#endif

void *arena_xmalloc(arena_t *arena, size_t size ARENA_DEBUG_PARAMS);
void *arena_xcalloc(arena_t *arena, size_t n, size_t size ARENA_DEBUG_PARAMS);
void *arena_xrealloc(arena_t *arena, void *old_ptr, size_t new_size ARENA_DEBUG_PARAMS);
char *arena_xstrdup(arena_t *arena, const char *s ARENA_DEBUG_PARAMS);
void arena_xfree(arena_t *arena, void *p ARENA_DEBUG_PARAMS);

void arena_init_ex(arena_t *arena);
void arena_set_cleanup_ex(arena_t *arena, arena_resource_cleanup_fn fn, void *user_data);
void arena_reset_ex(arena_t *arena);
void arena_end_ex(arena_t *arena);

#endif // ARENA_ALLOC_H

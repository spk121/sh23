#ifndef MIGA_XALLOC_H
#define MIGA_XALLOC_H

#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>

#include "miga/api.h"
#include "miga/migaconf.h"
#include "miga/mutex.h"

MIGA_EXTERN_C_START

// #define MIGA_ARENA_DEBUG
#ifdef MIGA_ARENA_DEBUG
/**
 * Structure to track allocation information in MIGA_ARENA_DEBUG mode.
 */
#define MIGA_ARENA_DEBUG_FILENAME_MAX_LEN 256
typedef struct
{
    void *ptr;
    char file[MIGA_ARENA_DEBUG_FILENAME_MAX_LEN];
    int line;
    size_t size;
} miga_arena_alloc_t;
#else
typedef struct
{
    void *ptr;
} miga_arena_alloc_t;
#endif

typedef void (*miga_arena_resource_cleanup_fn)(void *user_data);

/**
 * Arena allocator state structure.
 * Encapsulates all the state needed for memory tracking.
 */
typedef struct miga_arena_t
{
    jmp_buf rollback_point;
    bool rollback_in_progress;
    bool initialized;       // true after arena_init_ex has been called
    bool setjmp_set;        // true after miga_setjmp() has been called
    bool mutex_initialized; // true after miga_mutex_init has been called on mtx
    char reserved[4];
    miga_mutex_t mtx;
    miga_arena_alloc_t *allocated_ptrs; // dynamically resized sorted array
    long allocated_count;
    long allocated_cap;
    long initial_cap;     // initial capacity for allocated_ptrs array
    long max_allocations; // maximum number of allocations allowed
    miga_arena_resource_cleanup_fn resource_cleanup;
    void *resource_cleanup_user_data;
} miga_arena_t;

// Access to global singleton arena for use in miga_setjmp() macro
MIGA_API extern miga_arena_t *miga_arena_get_global(void);

/**
 * Place this macro early in startup. Memory failures will cause the code to
 * jump to this location. Calls miga_arena_init() automatically if needed.
 */
#define miga_setjmp()                                                                              \
    do                                                                                             \
    {                                                                                              \
        if (!miga_arena_get_global()->initialized)                                                 \
            miga_arena_init();                                                                     \
        miga_arena_get_global()->setjmp_set = true;                                                \
        if (setjmp(miga_arena_get_global()->rollback_point) != 0)                                  \
        {                                                                                          \
            miga_arena_reset();                                                                    \
            fprintf(stderr,                                                                        \
                    "Out of memory — all allocated memory has been freed, restarting logic...\n"); \
        }                                                                                          \
    } while (0)


/**
 * Initialize the global arena allocator.
 * Optional — the arena will lazy-initialize on first use if this is not called.
 * Calling this explicitly allows a clean reset of all state.
 */
MIGA_API void miga_arena_init(void);

/**
 * This sets a function that will be called upon an out-of-memory
 * condition. It is called just before the memory allocator
 * frees all the memory it is tracking and then either jumps to
 * the location of the miga_setjmp call or exits.
 */
MIGA_API void miga_set_out_of_memory_cleanup_cb(miga_arena_resource_cleanup_fn fn, void *user_data);

/**
 * Free all allocated memory tracked by the arena and invoke the cleanup
 * callback.  The setjmp rollback point and cleanup callback are preserved,
 * so the arena can be reused immediately (it will lazy-reinitialize on the
 * next allocation).  This is called automatically on out-of-memory conditions.
 */
MIGA_API void miga_arena_reset(void);

/**
 * Full teardown.  Like miga_arena_reset(), but also clears the setjmp
 * rollback point and cleanup callback, returning the arena to its completely
 * dormant initial state.  Call this at program exit or when the library is
 * being unloaded.
 */
MIGA_API void miga_arena_end(void);

#ifndef MIGA_HIDE_LOCALS    
/**
 * Allocate memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 */
#ifdef MIGA_ARENA_DEBUG
#define xmalloc(size) arena_xmalloc(miga_arena_get_global(), (size), __FILE__, __LINE__)
#else
MIGA_LOCAL void *xmalloc(size_t size);
#endif

/**
 * Allocate zero-initialized memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 */
#ifdef MIGA_ARENA_DEBUG
#define xcalloc(n, size) arena_xcalloc(miga_arena_get_global(), (n), (size), __FILE__, __LINE__)
#else
MIGA_LOCAL void *xcalloc(size_t n, size_t size);
#endif

/**
 * Reallocate memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 * If old_ptr is NULL, behaves like xmalloc.
 * If new_size is 0, behaves like xfree.
 */
#ifdef MIGA_ARENA_DEBUG
#define xrealloc(old_ptr, new_size)                                                                \
    arena_xrealloc(miga_arena_get_global(), (old_ptr), (new_size), __FILE__, __LINE__)
#else
MIGA_LOCAL void *xrealloc(void *old_ptr, size_t new_size);
#endif

/**
 * Duplicate a string, with memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 */
#ifdef MIGA_ARENA_DEBUG
#define xstrdup(s) arena_xstrdup(miga_arena_get_global(), (s), __FILE__, __LINE__)
#else
MIGA_LOCAL char *xstrdup(const char *s);
#endif

/**
 * Free memory allocated by the arena.
 * Safe to call with NULL.
 */
#ifdef MIGA_ARENA_DEBUG
#define xfree(ptr) arena_xfree(miga_arena_get_global(), (ptr), __FILE__, __LINE__)
#else
MIGA_LOCAL void xfree(void *ptr);
#endif

/**
 * Arena-based allocation functions that operate on an explicit miga_arena_t.
 *
 * These functions provide the same functionality as the global arena functions
 * (xmalloc, xcalloc, etc.) but operate on a specific arena instance, allowing
 * multiple independent arenas and better testability.
 *
 * Note: These are primarily provided for unit testing. General code should use
 * the shorter API like xmalloc and xfree which operate on the global arena.
 */
#ifdef MIGA_ARENA_DEBUG
#define MIGA_ARENA_DEBUG_PARAMS , const char *file, int line
#else
#define MIGA_ARENA_DEBUG_PARAMS
#endif

MIGA_LOCAL void *arena_xmalloc(miga_arena_t *arena, size_t size MIGA_ARENA_DEBUG_PARAMS);
MIGA_LOCAL void *arena_xcalloc(miga_arena_t *arena, size_t n, size_t size MIGA_ARENA_DEBUG_PARAMS);
MIGA_LOCAL void *arena_xrealloc(miga_arena_t *arena, void *old_ptr, size_t new_size MIGA_ARENA_DEBUG_PARAMS);
MIGA_LOCAL char *arena_xstrdup(miga_arena_t *arena, const char *s MIGA_ARENA_DEBUG_PARAMS);
MIGA_LOCAL void arena_xfree(miga_arena_t *arena, void *p MIGA_ARENA_DEBUG_PARAMS);

MIGA_LOCAL void arena_init_ex(miga_arena_t *arena);
MIGA_LOCAL void arena_set_cleanup_ex(miga_arena_t *arena, miga_arena_resource_cleanup_fn fn, void *user_data);
MIGA_LOCAL void arena_reset_ex(miga_arena_t *arena);
MIGA_LOCAL void arena_end_ex(miga_arena_t *arena);
#endif /* MIGA_HIDE_LOCALS */

MIGA_EXTERN_C_END
#endif // MIGA_XALLOC_H

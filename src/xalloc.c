#include "xalloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <stdint.h>

jmp_buf arena_rollback_point;
bool arena_rollback_in_progress = false;
static void   **allocated_ptrs = NULL;   // dynamically resized sorted array
static long   allocated_count = 0;
static long   allocated_cap   = 0;
static const long MAX_ALLOCATIONS = 1000000;

// -------------------------------------------------------------
// Internal helpers for maintaining the sorted pointer list
// -------------------------------------------------------------
static int ptr_compare(const void *a, const void *b) {
    const void *pa = *(const void **)a;
    const void *pb = *(const void **)b;
    return (pa > pb) - (pa < pb);
}

// Find index of pointer p (returns -1 if not found)
static long find_ptr(const void *p) {
    if(!p) {
        fprintf(stderr, "find_ptr: NULL pointer passed\n");
        abort();
    }
    // binary search because list is kept sorted
    void **res = bsearch(&p, allocated_ptrs, allocated_count,
                         sizeof(void*), ptr_compare);
    if (!res) return -1;
    return res - allocated_ptrs;
}

static long find_insertion_index(void *allocated_ptrs[], long allocated_count, void *new_ptr) {
    if (allocated_count == 0)
        return 0;

    long low = 0;
    long high = allocated_count;

    while (low < high) {
        long mid = (low + high) / 2;
        if (new_ptr < allocated_ptrs[mid]) {
            high = mid;
        } else {
            low = mid + 1;
        }
    }
    return low;
}
// Insert pointer in sorted order (keeps the array sorted)
static void insert_ptr(void *p) {
    if(!p) {
        fprintf(stderr, "insert_ptr: NULL pointer passed\n");
        abort();
    }

    if (allocated_count >= MAX_ALLOCATIONS) {
        free(p);
        fprintf(stderr, "exceeded maximum allocation limit (%ld)\n", MAX_ALLOCATIONS);
        if (!arena_rollback_in_progress)
            longjmp(arena_rollback_point, 1);
        return;
    }

    // Note that we can't use xrealloc here because it would call insert_ptr again.
    if (allocated_count == allocated_cap) {
        long new_cap = allocated_cap * 2;
        void **new_ptrs = calloc(new_cap, sizeof(void*));
        if (!new_ptrs) {
            free(p);
            if (!arena_rollback_in_progress)
                longjmp(arena_rollback_point, 1);
            return;
        }
        memmove(new_ptrs, allocated_ptrs, allocated_count * sizeof(void*));
        free(allocated_ptrs);
        allocated_ptrs = new_ptrs;
        allocated_cap = new_cap;
    }

    // Find insertion position with binary search
    long idx = find_insertion_index(allocated_ptrs, allocated_count, p);

    // Shift everything right one position
    if (idx < allocated_count) {
        memmove(allocated_ptrs + idx + 1,
                allocated_ptrs + idx,
                (allocated_count - idx) * sizeof(void*));
    }
    allocated_ptrs[idx] = p;
    allocated_count++;
}

// Remove pointer at index idx (fast removal, preserves order)
static void remove_ptr_at(long idx) {
    if(idx < 0 || idx >= allocated_count) {
        fprintf(stderr, "remove_ptr_at: invalid index %ld\n", idx);
        abort();
    }
    memmove(allocated_ptrs + idx,
            allocated_ptrs + idx + 1,
            (allocated_count - idx - 1) * sizeof(void*));
    allocated_count--;
}

// -------------------------------------------------------------
// Public API
// -------------------------------------------------------------
void *xmalloc(size_t size) {
    if (size == 0) {
        fprintf(stderr, "xmalloc: invalid argument (size=0)\n");
        abort();
    }

    void *p = malloc(size);
    if (!p) {
        if (!arena_rollback_in_progress)
            longjmp(arena_rollback_point, 1);   // triggers full cleanup
        return NULL;                      // during cleanup we must not jump again
    }

    insert_ptr(p);
    return p;
}

void *xcalloc(size_t n, size_t size) {
    if (n == 0 || size == 0) {
        fprintf(stderr, "xcalloc: invalid arguments (n=%zu, size=%zu)\n", n, size);
        abort();
    }

    void *p = calloc(n, size);
    if (!p) {
        if (!arena_rollback_in_progress)
            longjmp(arena_rollback_point, 1);
        return NULL;
    }

    insert_ptr(p);
    return p;
}

void *xrealloc(void *old_ptr, size_t new_size) {
    if (new_size == 0 || old_ptr == NULL) {
        fprintf(stderr, "xrealloc: invalid arguments (old_ptr=%p, new_size=%zu)\n", old_ptr, new_size);
        abort();
    }

    long idx = find_ptr(old_ptr);
    void *p = realloc(old_ptr, new_size);
    if (!p) {
        if (!arena_rollback_in_progress)
            longjmp(arena_rollback_point, 1);
        return NULL;
    }
    if (idx >= 0)
        remove_ptr_at(idx);
    insert_ptr(p);

    return p;
}

char *xstrdup(const char *s) {
    if(s == NULL) {
        fprintf(stderr, "xstrdup: NULL pointer passed\n");
        abort();
    }

    char *p = strdup(s);
    if (!p) {
        if (!arena_rollback_in_progress)
            longjmp(arena_rollback_point, 1);
        return NULL;
    }
    insert_ptr(p);
    return p;
}

void xfree(void *p) {
    if (!p)
        return;

    long idx = find_ptr(p);
    if (idx < 0) {
        fprintf(stderr, "xfree: double free or corruption detected (%p)\n", p);
        abort();
    }

    remove_ptr_at(idx);
    free(p);
}

void arena_init(void) {
    arena_rollback_in_progress = false;
    allocated_ptrs = calloc(64, sizeof(void*));
    if (!allocated_ptrs) {
        fprintf(stderr, "arena_init: failed to allocate initial pointer array\n");
        abort();
    }
    allocated_cap = 64;
    allocated_count = 0;
}

// -------------------------------------------------------------
// Full rollback — called automatically on allocation failure
// -------------------------------------------------------------
void arena_reset(bool verbose) {
    arena_rollback_in_progress = true;

    // Free from the end to avoid expensive memmove on every removal
    long count = allocated_count;
    while (allocated_count > 0) {
        void *p = allocated_ptrs[--allocated_count];
        free(p);
    }

    free(allocated_ptrs);
    allocated_ptrs = NULL;
    if (verbose) {
        fprintf(stderr, "Arena reset: freeing %ld allocated blocks\n", count);
    }
    allocated_cap = allocated_count = 0;

    arena_rollback_in_progress = false;
}

void arena_end(void) {
#ifdef DEBUG
    arena_reset(true);
#else
    arena_reset(false);
#endif
}

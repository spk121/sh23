#ifndef MIGA_MUTEX_H
#define MIGA_MUTEX_H

/**
 * Cross-platform recursive mutex abstraction.
 *
 * Selection order:
 *   1. MIGA_POSIX_API defined          → pthread_mutex_t (recursive)
 *   2. MIGA_UCRT_API defined           → Win32 CRITICAL_SECTION (recursive by default)
 *   3. __STDC_NO_THREADS__ defined→ no-op stubs (single-threaded fallback)
 *   4. otherwise                  → C11 mtx_t (mtx_recursive)
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "miga/api.h"
#include "miga/migaconf.h"

MIGA_EXTERN_C_START

/* ------------------------------------------------------------------ */
/* 1. POSIX                                                           */
/* ------------------------------------------------------------------ */
#if defined(MIGA_POSIX_API)

#include <pthread.h>

typedef pthread_mutex_t miga_mutex_t;

static inline void miga_mutex_init(miga_mutex_t *mtx)
{
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0)
    {
        fprintf(stderr, "miga_mutex_init: pthread_mutexattr_init failed\n");
        abort();
    }
    if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0)
    {
        fprintf(stderr, "miga_mutex_init: pthread_mutexattr_settype failed\n");
        pthread_mutexattr_destroy(&attr);
        abort();
    }
    if (pthread_mutex_init(mtx, &attr) != 0)
    {
        fprintf(stderr, "miga_mutex_init: pthread_mutex_init failed\n");
        pthread_mutexattr_destroy(&attr);
        abort();
    }
    pthread_mutexattr_destroy(&attr);
}

static inline void miga_mutex_lock(miga_mutex_t *mtx)
{
    if (pthread_mutex_lock(mtx) != 0)
    {
        fprintf(stderr, "miga_mutex_lock: pthread_mutex_lock failed\n");
        abort();
    }
}

static inline void miga_mutex_unlock(miga_mutex_t *mtx)
{
    if (pthread_mutex_unlock(mtx) != 0)
    {
        fprintf(stderr, "miga_mutex_unlock: pthread_mutex_unlock failed\n");
        abort();
    }
}

static inline void miga_mutex_destroy(miga_mutex_t *mtx)
{
    pthread_mutex_destroy(mtx);
}

/* ------------------------------------------------------------------ */
/* 2. Windows (UCRT)                                                  */
/* ------------------------------------------------------------------ */
#elif defined(MIGA_UCRT_API)

/*
 * CRITICAL_SECTION is recursive by default on Windows — a thread can
 * enter it multiple times without deadlocking, as long as it leaves
 * the same number of times.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

typedef CRITICAL_SECTION miga_mutex_t;

static inline void miga_mutex_init(miga_mutex_t *mtx)
{
    InitializeCriticalSection(mtx);
}

static inline void miga_mutex_lock(miga_mutex_t *mtx)
{
    EnterCriticalSection(mtx);
}

static inline void miga_mutex_unlock(miga_mutex_t *mtx)
{
    LeaveCriticalSection(mtx);
}

static inline void miga_mutex_destroy(miga_mutex_t *mtx)
{
    DeleteCriticalSection(mtx);
}

/* ------------------------------------------------------------------ */
/* 3. No threads available — single-threaded stubs                    */
/* ------------------------------------------------------------------ */
#elif defined(__STDC_NO_THREADS__)

typedef int miga_mutex_t; /* placeholder, never used */

static inline void miga_mutex_init(miga_mutex_t *mtx)    { (void)mtx; }
static inline void miga_mutex_lock(miga_mutex_t *mtx)    { (void)mtx; }
static inline void miga_mutex_unlock(miga_mutex_t *mtx)  { (void)mtx; }
static inline void miga_mutex_destroy(miga_mutex_t *mtx) { (void)mtx; }

/* ------------------------------------------------------------------ */
/* 4. C11 threads                                                     */
/* ------------------------------------------------------------------ */
#else

#include <threads.h>

typedef mtx_t miga_mutex_t;

static inline void miga_mutex_init(miga_mutex_t *mtx)
{
    if (mtx_init(mtx, mtx_recursive) != thrd_success)
    {
        fprintf(stderr, "miga_mutex_init: mtx_init failed\n");
        abort();
    }
}

static inline void miga_mutex_lock(miga_mutex_t *mtx)
{
    if (mtx_lock(mtx) != thrd_success)
    {
        fprintf(stderr, "miga_mutex_lock: mtx_lock failed\n");
        abort();
    }
}

static inline void miga_mutex_unlock(miga_mutex_t *mtx)
{
    if (mtx_unlock(mtx) != thrd_success)
    {
        fprintf(stderr, "miga_mutex_unlock: mtx_unlock failed\n");
        abort();
    }
}

static inline void miga_mutex_destroy(miga_mutex_t *mtx)
{
    mtx_destroy(mtx);
}

#endif /* platform selection */

MIGA_EXTERN_C_END

#endif /* MIGA_MUTEX_H */
// ============================================================================
// sig_act.c
// ============================================================================

#include "sig_act.h"
#include "xmalloc.h"
#include <string.h>

// Platform-specific signal count based on explicitly supported signals
#ifdef POSIX_API
// POSIX signals: ABRT, ALRM, BUS, CHLD, CONT, FPE, HUP, ILL, INT, PIPE,
//                QUIT, SEGV, TERM, TSTP, TTIN, TTOU, USR1, USR2, WINCH
#define SIG_ACT_SIGNAL_COUNT 19

// Map to get the highest signal number we support
static inline int get_max_signal_number(void)
{
    int max = SIGABRT;
    if (SIGALRM > max)
        max = SIGALRM;
    if (SIGBUS > max)
        max = SIGBUS;
    if (SIGCHLD > max)
        max = SIGCHLD;
    if (SIGCONT > max)
        max = SIGCONT;
    if (SIGFPE > max)
        max = SIGFPE;
    if (SIGHUP > max)
        max = SIGHUP;
    if (SIGILL > max)
        max = SIGILL;
    if (SIGINT > max)
        max = SIGINT;
    if (SIGPIPE > max)
        max = SIGPIPE;
    if (SIGQUIT > max)
        max = SIGQUIT;
    if (SIGSEGV > max)
        max = SIGSEGV;
    if (SIGTERM > max)
        max = SIGTERM;
    if (SIGTSTP > max)
        max = SIGTSTP;
    if (SIGTTIN > max)
        max = SIGTTIN;
    if (SIGTTOU > max)
        max = SIGTTOU;
    if (SIGUSR1 > max)
        max = SIGUSR1;
    if (SIGUSR2 > max)
        max = SIGUSR2;
#ifdef SIGWINCH
    if (SIGWINCH > max)
        max = SIGWINCH;
#endif
    return max;
}

// Array of signal numbers we track
static const int tracked_signals[] = {SIGABRT, SIGALRM, SIGBUS,  SIGCHLD, SIGCONT, SIGFPE,
                                      SIGHUP,  SIGILL,  SIGINT,  SIGPIPE, SIGQUIT, SIGSEGV,
                                      SIGTERM, SIGTSTP, SIGTTIN, SIGTTOU, SIGUSR1, SIGUSR2,
#ifdef SIGWINCH
                                      SIGWINCH
#endif
};

#elifdef UCRT_API
// UCRT signals: ABRT, FPE, ILL, INT, SEGV, TERM
#define SIG_ACT_SIGNAL_COUNT 6

static inline int get_max_signal_number(void)
{
    int max = SIGABRT;
    if (SIGFPE > max)
        max = SIGFPE;
    if (SIGILL > max)
        max = SIGILL;
    if (SIGINT > max)
        max = SIGINT;
    if (SIGSEGV > max)
        max = SIGSEGV;
    if (SIGTERM > max)
        max = SIGTERM;
    return max;
}

static const int tracked_signals[] = {SIGABRT, SIGFPE, SIGILL, SIGINT, SIGSEGV, SIGTERM};

#else
// ISO C signals: ABRT, FPE, ILL, INT, SEGV, TERM
#define SIG_ACT_SIGNAL_COUNT 6

static inline int get_max_signal_number(void)
{
    int max = SIGABRT;
    if (SIGFPE > max)
        max = SIGFPE;
    if (SIGILL > max)
        max = SIGILL;
    if (SIGINT > max)
        max = SIGINT;
    if (SIGSEGV > max)
        max = SIGSEGV;
    if (SIGTERM > max)
        max = SIGTERM;
    return max;
}

static const int tracked_signals[] = {SIGABRT, SIGFPE, SIGILL, SIGINT, SIGSEGV, SIGTERM};
#endif

// ============================================================================
// Create/Destroy Functions
// ============================================================================

sig_act_store_t *sig_act_store_create(void)
{
    sig_act_store_t *store = xcalloc(1, sizeof(sig_act_store_t));

    // Allocate array large enough to index by signal number
    int max_signo = get_max_signal_number();
    store->capacity = (size_t)(max_signo + 1);
    store->actions = xcalloc(store->capacity, sizeof(sig_act_t));

    // Initialize all entries
    for (size_t i = 0; i < store->capacity; i++)
    {
        store->actions[i].signal_number = (int)i;
        store->actions[i].was_ignored = false;
#ifdef POSIX_API
        memset(&store->actions[i].original_action, 0, sizeof(struct sigaction));
#else
        store->actions[i].original_handler = SIG_DFL;
#endif
    }

    return store;
}

void sig_act_store_destroy(sig_act_store_t *store)
{
    if (!store)
        return;

    xfree(store->actions);
    xfree(store);
}

sig_act_store_t *sig_act_store_copy(const sig_act_store_t *store)
{
    if (!store)
        return NULL;

    sig_act_store_t *copy = sig_act_store_create();

    // Copy all signal dispositions
    for (size_t i = 0; i < store->capacity && i < copy->capacity; i++)
    {
        copy->actions[i].signal_number = store->actions[i].signal_number;
        copy->actions[i].was_ignored = store->actions[i].was_ignored;
#ifdef POSIX_API
        copy->actions[i].original_action = store->actions[i].original_action;
#else
        copy->actions[i].original_handler = store->actions[i].original_handler;
#endif
    }

    return copy;
}

// ============================================================================
// Capture/Restore Functions
// ============================================================================

sig_act_store_t *sig_act_store_capture(void)
{
    sig_act_store_t *store = sig_act_store_create();

    // Capture current signal dispositions for all tracked signals
    for (size_t i = 0; i < SIG_ACT_SIGNAL_COUNT; i++)
    {
        int signo = tracked_signals[i];

        if (signo < 0 || (size_t)signo >= store->capacity)
            continue;

        sig_act_t *action = &store->actions[signo];
        action->signal_number = signo;

#ifdef POSIX_API
        // Use sigaction to get current disposition
        if (sigaction(signo, NULL, &action->original_action) == 0)
        {
            // Check if signal was ignored
            action->was_ignored = (action->original_action.sa_handler == SIG_IGN);
        }
        else
        {
            // If sigaction fails, assume default
            memset(&action->original_action, 0, sizeof(struct sigaction));
            action->original_action.sa_handler = SIG_DFL;
            action->was_ignored = false;
        }
#else
        // Use signal() to get current handler (this has side effects on some systems)
        // On most systems, signal(sig, SIG_DFL) followed by signal(sig, handler)
        // can be used to query, but this is not ideal
        // For simplicity, we'll just store SIG_DFL as we can't reliably query
        // without potentially changing the handler.

        action->original_handler = SIG_DFL;
        action->was_ignored = false;

        // Alternative: some platforms support getting handler without changing it
        // but this is not portable
#endif
    }

    return store;
}

// Acts like signal(), setting the handler. But also, if the executor
void sig_act_set_handler(sig_act_store_t *store, int sig, void (*func)(int))
{
    void (*old_handler)(int);

    errno = 0;
    old_handler = signal(sig, func);
    if (old_handler == SIG_ERR)
    {
        errno = 0;
        return;
    }

    // Capture current signal dispositions for all tracked signals
    for (size_t i = 0; i < SIG_ACT_SIGNAL_COUNT; i++)
    {
        int signo = tracked_signals[i];

        if (signo < 0 || (size_t)signo >= store->capacity)
            continue;

        sig_act_t *action = &store->actions[signo];
        action->original_handler = old_handler;
        action->was_ignored = (old_handler == SIG_IGN);
    }
}

void sig_act_store_restore(const sig_act_store_t *store)
{
    if (!store)
        return;

    // Restore signal dispositions for all tracked signals
    for (size_t i = 0; i < SIG_ACT_SIGNAL_COUNT; i++)
    {
        int signo = tracked_signals[i];

        if (signo < 0 || (size_t)signo >= store->capacity)
            continue;

        // Skip SIGKILL and SIGSTOP as they cannot be caught or ignored
#ifdef POSIX_API
        if (signo == SIGKILL || signo == SIGSTOP)
            continue;
#endif

        const sig_act_t *action = &store->actions[signo];

#ifdef POSIX_API
        // Restore using sigaction
        sigaction(signo, &action->original_action, NULL);
#else
        // Restore using signal()
        signal(signo, action->original_handler);
#endif
    }
}

// ============================================================================
// Query Functions
// ============================================================================

const sig_act_t *sig_act_store_get(const sig_act_store_t *store, int signal_number)
{
    if (!store || signal_number < 0 || (size_t)signal_number >= store->capacity)
        return NULL;

    return &store->actions[signal_number];
}

bool sig_act_store_was_ignored(const sig_act_store_t *store, int signal_number)
{
    if (!store || signal_number < 0 || (size_t)signal_number >= store->capacity)
        return false;

    return store->actions[signal_number].was_ignored;
}
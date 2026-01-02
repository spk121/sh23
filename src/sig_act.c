// ============================================================================
// sig_act.c
// Signal handler archiving implementation
// ============================================================================

#include "sig_act.h"
#include "xalloc.h"
#include <errno.h>
#include <string.h>

// Platform-specific signal count and definitions
// We track signals that shells commonly need to handle

#ifdef POSIX_API
// POSIX signals that shells typically manage
static const int shell_signals[] = {
    SIGINT,  SIGQUIT, SIGTERM, SIGHUP,  SIGALRM, SIGCHLD,
    SIGCONT, SIGTSTP, SIGTTIN, SIGTTOU, SIGUSR1, SIGUSR2,
    SIGPIPE,
#ifdef SIGWINCH
    SIGWINCH,
#endif
};
#define NUM_SHELL_SIGNALS (sizeof(shell_signals) / sizeof(shell_signals[0]))

static inline int get_max_signal_number(void)
{
    int max = SIGTERM;
    for (size_t i = 0; i < NUM_SHELL_SIGNALS; i++)
    {
        if (shell_signals[i] > max)
            max = shell_signals[i];
    }
    return max;
}

#elifdef UCRT_API
// UCRT signals: ABRT, FPE, ILL, INT, SEGV, TERM
static const int shell_signals[] = {SIGINT, SIGTERM, SIGABRT};
#define NUM_SHELL_SIGNALS (sizeof(shell_signals) / sizeof(shell_signals[0]))

static inline int get_max_signal_number(void)
{
    return SIGTERM > SIGABRT ? SIGTERM : SIGABRT;
}

#else
// ISO C signals: ABRT, FPE, ILL, INT, SEGV, TERM
static const int shell_signals[] = {SIGINT, SIGTERM, SIGABRT};
#define NUM_SHELL_SIGNALS (sizeof(shell_signals) / sizeof(shell_signals[0]))

static inline int get_max_signal_number(void)
{
    return SIGTERM > SIGABRT ? SIGTERM : SIGABRT;
}
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

    // Initialize all entries as not-saved
    for (size_t i = 0; i < store->capacity; i++)
    {
        store->actions[i].signal_number = (int)i;
        store->actions[i].is_saved = false;
        store->actions[i].was_ignored = false;
#ifdef POSIX_API
        memset(&store->actions[i].original_action, 0, sizeof(struct sigaction));
#else
        store->actions[i].original_handler = SIG_DFL;
#endif
    }

    return store;
}

void sig_act_store_destroy(sig_act_store_t **store_ptr)
{
    if (!store_ptr || !*store_ptr)
        return;

    sig_act_store_t *store = *store_ptr;
    xfree(store->actions);
    xfree(store);
    *store_ptr = NULL;
}

// ============================================================================
// Set and Save Function
// ============================================================================

#ifdef POSIX_API
int sig_act_store_set_and_save(sig_act_store_t *store, int signo,
                                 const struct sigaction *new_action)
{
    if (!store || !new_action)
        return -1;

    // Validate signal number
    if (signo < 0 || (size_t)signo >= store->capacity)
        return -1;

    // SIGKILL and SIGSTOP cannot be caught
    if (signo == SIGKILL || signo == SIGSTOP)
        return -1;

    sig_act_t *entry = &store->actions[signo];

    // If not already saved, save the current handler before setting new one
    if (!entry->is_saved)
    {
        struct sigaction old_action;
        if (sigaction(signo, new_action, &old_action) == -1)
            return -1;

        // Save the old handler
        entry->original_action = old_action;
        entry->was_ignored = (old_action.sa_handler == SIG_IGN);
        entry->is_saved = true;
    }
    else
    {
        // Already saved, just set the new handler
        if (sigaction(signo, new_action, NULL) == -1)
            return -1;
    }

    return 0;
}

#else  // UCRT_API or ISO_C

void (*sig_act_store_set_and_save(sig_act_store_t *store, int signo,
                                    void (*new_handler)(int)))(int)
{
    if (!store)
        return SIG_ERR;

    // Validate signal number
    if (signo < 0 || (size_t)signo >= store->capacity)
        return SIG_ERR;

    sig_act_t *entry = &store->actions[signo];

    // Set the new handler and get the old one
    void (*old_handler)(int) = signal(signo, new_handler);
    if (old_handler == SIG_ERR)
        return SIG_ERR;

    // If not already saved, save the old handler we just got
    if (!entry->is_saved)
    {
        entry->original_handler = old_handler;
        entry->was_ignored = (old_handler == SIG_IGN);
        entry->is_saved = true;
    }

    return old_handler;
}
#endif

// ============================================================================
// Restore Functions
// ============================================================================

void sig_act_store_restore(const sig_act_store_t *store)
{
    if (!store)
        return;

    // Restore all saved signal dispositions
    for (size_t i = 0; i < store->capacity; i++)
    {
        if (store->actions[i].is_saved)
        {
            sig_act_store_restore_one(store, (int)i);
        }
    }
}

bool sig_act_store_restore_one(const sig_act_store_t *store, int signo)
{
    if (!store || signo < 0 || (size_t)signo >= store->capacity)
        return false;

    const sig_act_t *entry = &store->actions[signo];

    // Only restore if we have a saved handler
    if (!entry->is_saved)
        return false;

#ifdef POSIX_API
    // Skip SIGKILL and SIGSTOP as they cannot be caught or ignored
    if (signo == SIGKILL || signo == SIGSTOP)
        return false;

    // Restore using sigaction
    return (sigaction(signo, &entry->original_action, NULL) == 0);
#else
    // Restore using signal()
    return (signal(signo, entry->original_handler) != SIG_ERR);
#endif
}

// ============================================================================
// Query Functions
// ============================================================================

bool sig_act_store_is_saved(const sig_act_store_t *store, int signo)
{
    if (!store || signo < 0 || (size_t)signo >= store->capacity)
        return false;

    return store->actions[signo].is_saved;
}

bool sig_act_store_was_ignored(const sig_act_store_t *store, int signo)
{
    if (!store || signo < 0 || (size_t)signo >= store->capacity)
        return false;

    const sig_act_t *entry = &store->actions[signo];
    return entry->is_saved && entry->was_ignored;
}

const sig_act_t *sig_act_store_get(const sig_act_store_t *store, int signo)
{
    if (!store || signo < 0 || (size_t)signo >= store->capacity)
        return NULL;

    const sig_act_t *entry = &store->actions[signo];
    return entry->is_saved ? entry : NULL;
}

// ============================================================================
// trap_store.c
// ============================================================================

#include "trap_store.h"
#include "xalloc.h"
#include <string.h>

// Platform-specific signal support gates
#ifdef TRAP_USE_POSIX_SIGNALS
// POSIX signals: ABRT, ALRM, BUS, CHLD, CONT, FPE, HUP, ILL, INT, PIPE,
//                QUIT, SEGV, TERM, TSTP, TTIN, TTOU, USR1, USR2, WINCH
#define TRAP_SIGNAL_COUNT 19

static inline int get_max_signal_number(void)
{
    int max = SIGABRT;
#ifdef SIGALRM
    if (SIGALRM > max)
        max = SIGALRM;
#endif
#ifdef SIGBUS
    if (SIGBUS > max)
        max = SIGBUS;
#endif
#ifdef SIGCHLD
    if (SIGCHLD > max)
        max = SIGCHLD;
#endif
#ifdef SIGCONT
    if (SIGCONT > max)
        max = SIGCONT;
#endif
    if (SIGFPE > max)
        max = SIGFPE;
#ifdef SIGHUP
    if (SIGHUP > max)
        max = SIGHUP;
#endif
    if (SIGILL > max)
        max = SIGILL;
    if (SIGINT > max)
        max = SIGINT;
#ifdef SIGPIPE
    if (SIGPIPE > max)
        max = SIGPIPE;
#endif
#ifdef SIGQUIT
    if (SIGQUIT > max)
        max = SIGQUIT;
#endif
    if (SIGSEGV > max)
        max = SIGSEGV;
    if (SIGTERM > max)
        max = SIGTERM;
#ifdef SIGTSTP
    if (SIGTSTP > max)
        max = SIGTSTP;
#endif
#ifdef SIGTTIN
    if (SIGTTIN > max)
        max = SIGTTIN;
#endif
#ifdef SIGTTOU
    if (SIGTTOU > max)
        max = SIGTTOU;
#endif
#ifdef SIGUSR1
    if (SIGUSR1 > max)
        max = SIGUSR1;
#endif
#ifdef SIGUSR2
    if (SIGUSR2 > max)
        max = SIGUSR2;
#endif
#ifdef SIGWINCH
    if (SIGWINCH > max)
        max = SIGWINCH;
#endif
    return max;
}

#elif defined(UCRT_API)
// UCRT signals: ABRT, FPE, ILL, INT, SEGV, TERM
#define TRAP_SIGNAL_COUNT 6

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

#else
// ISO C signals: ABRT, FPE, ILL, INT, SEGV, TERM
#define TRAP_SIGNAL_COUNT 6

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
#endif

// ============================================================================
// Create/Destroy Functions
// ============================================================================

trap_store_t *trap_store_create(void)
{
    trap_store_t *store = xcalloc(1, sizeof(trap_store_t));

    // Allocate array large enough to index by signal number
    // (signal numbers are not necessarily contiguous, so we need space
    // up to the maximum signal number)
    int max_signo = get_max_signal_number();
    store->capacity = (size_t)(max_signo + 1);
    store->traps = xcalloc(store->capacity, sizeof(trap_action_t));

    // Initialize all traps as default (not set)
    for (size_t i = 0; i < store->capacity; i++)
    {
        store->traps[i].signal_number = (int)i;
        store->traps[i].action = NULL;
        store->traps[i].is_ignored = false;
        store->traps[i].is_default = true;
    }

    store->exit_trap_set = false;
    store->exit_action = NULL;

    return store;
}

void trap_store_destroy(trap_store_t **store_ptr)
{
    if (!store_ptr || !*store_ptr)
        return;

    trap_store_t *store = *store_ptr;

    // Free all trap action strings
    for (size_t i = 0; i < store->capacity; i++)
    {
        if (store->traps[i].action)
            string_destroy(&store->traps[i].action);
    }

    xfree(store->traps);

    if (store->exit_action)
        string_destroy(&store->exit_action);

    xfree(store);
    *store_ptr = NULL;
}

trap_store_t *trap_store_copy(const trap_store_t *store)
{
    if (!store)
        return NULL;

    trap_store_t *copy = trap_store_create();

    // Copy all trap actions
    for (size_t i = 0; i < store->capacity && i < copy->capacity; i++)
    {
        copy->traps[i].signal_number = store->traps[i].signal_number;
        copy->traps[i].action = store->traps[i].action ? string_create_from(store->traps[i].action) : NULL;
        copy->traps[i].is_ignored = store->traps[i].is_ignored;
        copy->traps[i].is_default = store->traps[i].is_default;
    }

    // Copy EXIT trap
    copy->exit_trap_set = store->exit_trap_set;
    copy->exit_action = store->exit_action ? string_create_from(store->exit_action) : NULL;

    return copy;
}

// ============================================================================
// Set/Get Functions
// ============================================================================

void trap_store_set(trap_store_t *store, int signal_number, string_t *action, bool is_ignored,
                    bool is_default)
{
    if (!store || signal_number < 0 || (size_t)signal_number >= store->capacity)
        return;

    trap_action_t *trap = &store->traps[signal_number];

    // Free old action if it exists
    if (trap->action)
        string_destroy(&trap->action);

    trap->signal_number = signal_number;
    trap->action = action ? string_create_from(action) : NULL;
    trap->is_ignored = is_ignored;
    trap->is_default = is_default;
}

void trap_store_set_exit(trap_store_t *store, string_t *action, bool is_ignored, bool is_default)
{
    if (!store)
        return;

    if (store->exit_action)
        string_destroy(&store->exit_action);

    store->exit_trap_set = true;
    store->exit_action = action ? string_create_from(action) : NULL;

    // Note: EXIT trap doesn't use is_ignored/is_default the same way
    // as signal traps, but we could extend the struct to track this if needed
    (void)is_ignored;
    (void)is_default;
}

const trap_action_t *trap_store_get(const trap_store_t *store, int signal_number)
{
    if (!store || signal_number < 0 || (size_t)signal_number >= store->capacity)
        return NULL;

    const trap_action_t *trap = &store->traps[signal_number];

    // Only return if trap is actually set (not default)
    if (trap->is_default && !trap->is_ignored && !trap->action)
        return NULL;

    return trap;
}

const string_t *trap_store_get_exit(const trap_store_t *store)
{
    if (!store || !store->exit_trap_set)
        return NULL;

    return store->exit_action;
}

bool trap_store_is_set(const trap_store_t *store, int signal_number)
{
    if (!store || signal_number < 0 || (size_t)signal_number >= store->capacity)
        return false;

    const trap_action_t *trap = &store->traps[signal_number];
    return !trap->is_default || trap->is_ignored || trap->action != NULL;
}

bool trap_store_is_exit_set(const trap_store_t *store)
{
    return store && store->exit_trap_set;
}

void trap_store_clear(trap_store_t *store, int signal_number)
{
    if (!store || signal_number < 0 || (size_t)signal_number >= store->capacity)
        return;

    trap_action_t *trap = &store->traps[signal_number];

    if (trap->action)
        string_destroy(&trap->action);

    trap->action = NULL;
    trap->is_ignored = false;
    trap->is_default = true;
}

void trap_store_clear_exit(trap_store_t *store)
{
    if (!store)
        return;

    if (store->exit_action)
        string_destroy(&store->exit_action);

    store->exit_trap_set = false;
    store->exit_action = NULL;
}

// ============================================================================
// Signal Name Conversion
// ============================================================================

int trap_signal_name_to_number(const char *name)
{
    if (!name)
        return -1;

    // Handle special case: EXIT (signal 0)
    if (strcmp(name, "EXIT") == 0)
        return 0;

    // ISO C signals (available on all platforms)
    if (strcmp(name, "ABRT") == 0)
        return SIGABRT;
    if (strcmp(name, "FPE") == 0)
        return SIGFPE;
    if (strcmp(name, "ILL") == 0)
        return SIGILL;
    if (strcmp(name, "INT") == 0)
        return SIGINT;
    if (strcmp(name, "SEGV") == 0)
        return SIGSEGV;
    if (strcmp(name, "TERM") == 0)
        return SIGTERM;

#ifdef TRAP_USE_POSIX_SIGNALS
    // POSIX-specific signals
    if (strcmp(name, "ALRM") == 0)
#ifdef SIGALRM
        return SIGALRM;
#else
        return -1;
#endif
    if (strcmp(name, "BUS") == 0)
#ifdef SIGBUS
        return SIGBUS;
#else
        return -1;
#endif
    if (strcmp(name, "CHLD") == 0)
#ifdef SIGCHLD
        return SIGCHLD;
#else
        return -1;
#endif
    if (strcmp(name, "CONT") == 0)
#ifdef SIGCONT
        return SIGCONT;
#else
        return -1;
#endif
    if (strcmp(name, "HUP") == 0)
#ifdef SIGHUP
        return SIGHUP;
#else
        return -1;
#endif
    if (strcmp(name, "KILL") == 0)
#ifdef SIGKILL
        return SIGKILL; // Not catchable, but recognized
#else
        return -1;
#endif
    if (strcmp(name, "PIPE") == 0)
#ifdef SIGPIPE
        return SIGPIPE;
#else
        return -1;
#endif
    if (strcmp(name, "QUIT") == 0)
#ifdef SIGQUIT
        return SIGQUIT;
#else
        return -1;
#endif
    if (strcmp(name, "STOP") == 0)
#ifdef SIGSTOP
        return SIGSTOP; // Not catchable, but recognized
#else
        return -1;
#endif
    if (strcmp(name, "TSTP") == 0)
#ifdef SIGTSTP
        return SIGTSTP;
#else
        return -1;
#endif
    if (strcmp(name, "TTIN") == 0)
#ifdef SIGTTIN
        return SIGTTIN;
#else
        return -1;
#endif
    if (strcmp(name, "TTOU") == 0)
#ifdef SIGTTOU
        return SIGTTOU;
#else
        return -1;
#endif
    if (strcmp(name, "USR1") == 0)
#ifdef SIGUSR1
        return SIGUSR1;
#else
        return -1;
#endif
    if (strcmp(name, "USR2") == 0)
#ifdef SIGUSR2
        return SIGUSR2;
#else
        return -1;
#endif

#ifdef SIGWINCH
    if (strcmp(name, "WINCH") == 0)
        return SIGWINCH;
#endif
#endif

    // Signal name not recognized
    return -1;
}

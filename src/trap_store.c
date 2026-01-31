// ============================================================================
// trap_store.c
// ============================================================================

#include "trap_store.h"
#include "sig_act.h"
#include "xalloc.h"
#include <string.h>
#include "logging.h"
#include <errno.h>

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

#elifdef TRAP_USE_UCRT_SIGNALS
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
    Expects_not_null(store_ptr);
    Expects_not_null(*store_ptr);

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

trap_store_t *trap_store_clone(const trap_store_t *store)
{
    Expects_not_null(store);

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

static trap_store_t *signal_handler_trap_store = NULL;
static sig_act_store_t *signal_handler_sig_act_store = NULL;

void trap_store_set_current(trap_store_t* store)
{
    Expects_not_null(store);
    signal_handler_trap_store = store;
}

trap_store_t *trap_store_get_current(void)
{
    return signal_handler_trap_store;
}

void trap_store_set_sig_act_store(sig_act_store_t *store)
{
    signal_handler_sig_act_store = store;
}

sig_act_store_t *trap_store_get_sig_act_store(void)
{
    return signal_handler_sig_act_store;
}

// ============================================================================
// Trap Execution Functions
// ============================================================================

static int execute_trap_action(const trap_action_t *trap)
{
    if (!trap || !trap->action)
        return -1;
    // Here we would normally execute the command string in the shell context.
    // For this example, we'll just print it to indicate execution.
    printf("Executing trap action for signal %d: %s\n",
           trap->signal_number, string_cstr(trap->action));
    // In a real shell, we would execute the command and return its exit status.
    return 0; // Indicate success
}

#ifdef TRAP_USE_POSIX_SIGNALS
    // POSIX uses a sigaction handler
void trap_handler(int signal, siginfo_t *info, void *context)
{
    /* This handler initializes the execution of a shell command string
     * based on the received signal and context information.*/
    trap_store_t *trap_store = trap_store_get_current();
    if (!trap_store)
        return;

    // Get the trap action for the received signal
    const trap_action_t *trap = trap_store_get(trap_store, signal);
    if (!trap)
        return;

    // Execute the trap action
    execute_trap_action(trap);
}
#elifdef TRAP_USE_UCRT_SIGNALS
int trap_handler(int signal, int fpe_code)
{
    /* This handler initializes the execution of a shell command string
     * based on the received signal. The fpe_code parameter distinguishes
     * different FPE exception types (FPE_INTDIV, FPE_FLTDIV, etc.) on UCRT,
     * but since shell arithmetic is performed on 'long' integers (not
     * floating-point), FPE exceptions should be rare or impossible.
     * We treat all FPE exceptions the same: lookup and execute the single
     * trap action set for SIGFPE. */
    (void)fpe_code; // Suppress unused parameter warning

    trap_store_t *trap_store = trap_store_get_current();
    if (!trap_store)
        return -1;

    // Get the trap action for the received signal (handles all FPE types)
    const trap_action_t *trap = trap_store_get(trap_store, signal);
    if (!trap)
        return -1;

    // Execute the trap action
    return execute_trap_action(trap);
}
#else
    // ISO C signal support
void trap_handler(int signal)
{
    /* This handler initializes the execution of a shell command string
     * based on the received signal.*/
    trap_store_t *trap_store = trap_store_get_current();
    if (!trap_store)
        return;

    // Get the trap action for the received signal
    const trap_action_t *trap = trap_store_get(trap_store, signal);
    if (!trap)
        return;
    // Execute the trap action
    execute_trap_action(trap);
}
#endif

// ============================================================================
// Set/Get Functions
// ============================================================================

static bool is_non_catchable_signal(int signal_number)
{
    // SIGKILL and SIGSTOP cannot be caught or ignored
#ifdef SIGKILL
    if (signal_number == SIGKILL)
        return true;
#endif
#ifdef SIGSTOP
    if (signal_number == SIGSTOP)
        return true;
#endif
    return false;
}

bool trap_store_set(trap_store_t *store, int signal_number, string_t *action, bool is_ignored,
                    bool is_default)
{
    Expects_not_null(store);
    Expects_ge(signal_number, 0);
    Expects_lt(signal_number, (int)store->capacity);
    // SIGKILL and SIGSTOP cannot be caught
#ifdef SIGKILL
    if (signal_number == SIGKILL)
        return false;
#endif
#ifdef SIGSTOP
    if (signal_number == SIGSTOP)
        return false;
#endif

    // EXIT trap (signal 0) is not a real signal, doesn't need handler installation
    if (signal_number == 0)
    {
        return trap_store_set_exit(store, action, is_ignored, is_default);
    }


    trap_action_t *trap = &store->traps[signal_number];

    // Free old action if it exists
    if (trap->action)
        string_destroy(&trap->action);

    trap->signal_number = signal_number;
    trap->action = action ? string_create_from(action) : NULL;
    trap->is_ignored = is_ignored;
    trap->is_default = is_default;

    // Install the signal handler or restore default/ignore disposition
#ifdef TRAP_USE_POSIX_SIGNALS
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));

        if (is_ignored)
        {
            // Ignore this signal
            sa.sa_handler = SIG_IGN;
            if (sigaction(signal_number, &sa, NULL) == -1)
                return false; // sigaction failed
        }
        else if (is_default)
        {
            // Restore default handler
            sa.sa_handler = SIG_DFL;
            if (sigaction(signal_number, &sa, NULL) == -1)
                return false; // sigaction failed
        }
        else if (trap->action)
        {
            // Install trap handler
            sig_act_store_t *sig_act_store = trap_store_get_sig_act_store();
            if (sig_act_store)
            {
                sa.sa_sigaction = trap_handler;
                sa.sa_flags = SA_SIGINFO;
                sigemptyset(&sa.sa_mask);
                if (sig_act_store_set_and_save(sig_act_store, signal_number, &sa) == -1)
                    return false; // sig_act_store_set_and_save failed
            }
            else
            {
                // Fallback: just use sigaction directly
                sa.sa_sigaction = trap_handler;
                sa.sa_flags = SA_SIGINFO;
                sigemptyset(&sa.sa_mask);
                if (sigaction(signal_number, &sa, NULL) == -1)
                    return false; // sigaction failed
            }
        }
    }
#else
    // UCRT and ISO C use signal()
    if (is_ignored)
    {
        if (signal(signal_number, SIG_IGN) == SIG_ERR)
            return false; // signal() failed
    }
    else if (is_default)
    {
        if (signal(signal_number, SIG_DFL) == SIG_ERR)
            return false; // signal() failed
    }
    else if (trap->action)
    {
        // Install trap handler
        sig_act_store_t *sig_act_store = trap_store_get_sig_act_store();
        if (sig_act_store)
        {
            void (*result)(int) = sig_act_store_set_and_save(sig_act_store, signal_number, trap_handler);
            if (result == SIG_ERR)
                return false; // sig_act_store_set_and_save failed
        }
        else
        {
            // Fallback: just use signal() directly
            if (signal(signal_number, trap_handler) == SIG_ERR)
                return false; // signal() failed
        }
    }
#endif

    return true; // Success
}

bool trap_store_set_exit(trap_store_t *store, string_t *action, bool is_ignored, bool is_default)
{
    Expects_not_null(store);

    if (store->exit_action)
        string_destroy(&store->exit_action);

    store->exit_trap_set = true;
    store->exit_action = action ? string_create_from(action) : NULL;

    // Note: EXIT trap doesn't use is_ignored/is_default the same way
    // as signal traps, but we could extend the struct to track this if needed.
    // EXIT traps don't install OS signal handlers, so this always succeeds.
    (void)is_ignored;
    (void)is_default;

    return true; // EXIT trap setup always succeeds (no OS calls needed)
}

const trap_action_t *trap_store_get(const trap_store_t *store, int signal_number)
{
    Expects_not_null(store);
    Expects_ge(signal_number, 0);
    Expects_lt(signal_number, (int)store->capacity);

    const trap_action_t *trap = &store->traps[signal_number];

    // Only return if trap is actually set (not default)
    if (trap->is_default && !trap->is_ignored && !trap->action)
        return NULL;

    return trap;
}

const string_t *trap_store_get_exit(const trap_store_t *store)
{
    Expects_not_null(store);
    if (!store->exit_trap_set)
        return string_create_from_cstr("");

    return store->exit_action;
}

#ifdef TRAP_USE_UCRT_SIGNALS
const trap_action_t *trap_store_get_fpe(const trap_store_t *store, int fpe_code)
{
    Expects_not_null(store);

    /* FPE trap simplification: UCRT SIGFPE handler can receive different
     * exception type codes (FPE_INTDIV, FPE_INTOVF, FPE_FLTDIV, FPE_FLTOVF,
     * FPE_FLTUND, FPE_FLTRES, FPE_FLTINV). However, since shell arithmetic
     * is performed on 'long' integers and not floating-point, FPE exceptions
     * should be rare or impossible. We store and return a single FPE trap
     * action for all exception types.
     *
     * The fpe_code parameter is accepted but currently ignored, allowing for
     * future per-exception-type handling if needed.
     * (fpe_code examples: FPE_INTDIV, FPE_INTOVF, FPE_FLTDIV, FPE_FLTOVF, etc.)
     */
    (void)fpe_code; // Suppress unused parameter warning

    trap_action_t *trap = &store->fpe_trap;

    // Only return if FPE trap is actually set (not default)
    if (trap->is_default && !trap->is_ignored && !trap->action)
        return NULL;

    return trap;
}
#endif

bool trap_store_is_set(const trap_store_t *store, int signal_number)
{
    Expects_not_null(store);
    Expects_ge(signal_number, 0);
    Expects_lt(signal_number, (int)store->capacity);

    const trap_action_t *trap = &store->traps[signal_number];
    return !trap->is_default || trap->is_ignored || trap->action != NULL;
}

bool trap_store_is_exit_set(const trap_store_t *store)
{
    Expects_not_null(store);
    return store->exit_trap_set;
}

void trap_store_clear(trap_store_t *store, int signal_number)
{
    Expects_not_null(store);
    Expects_ge(signal_number, 0);
    Expects_lt(signal_number, (int)store->capacity);

    // EXIT trap (signal 0) is not a real signal
    if (signal_number == 0)
    {
        trap_store_clear_exit(store);
        return;
    }

    trap_action_t *trap = &store->traps[signal_number];

    if (trap->action)
        string_destroy(&trap->action);

    trap->action = NULL;
    trap->is_ignored = false;
    trap->is_default = true;

    // Restore the original signal disposition via sig_act_store
    sig_act_store_t *sig_act_store = trap_store_get_sig_act_store();
    if (sig_act_store)
    {
        sig_act_store_restore_one(sig_act_store, signal_number);
    }
    else
    {
        // Fallback: restore to default
        signal(signal_number, SIG_DFL);
    }
}

void trap_store_clear_exit(trap_store_t *store)
{
    Expects_not_null(store);

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
    Expects_not_null(name);

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

// Helper to check if a signal name is a known signal (regardless of platform support)
// Returns true if the name is recognized as a valid signal name
static bool is_known_signal_name(const char *name)
{
    Expects_not_null(name);

    // Handle special case: EXIT (signal 0)
    if (strcmp(name, "EXIT") == 0)
        return true;

    // ISO C signals (always known)
    if (strcmp(name, "ABRT") == 0 || strcmp(name, "FPE") == 0 ||
        strcmp(name, "ILL") == 0 || strcmp(name, "INT") == 0 ||
        strcmp(name, "SEGV") == 0 || strcmp(name, "TERM") == 0)
        return true;

#ifdef TRAP_USE_POSIX_SIGNALS
    // POSIX signals (known on POSIX platforms)
    if (strcmp(name, "ALRM") == 0 || strcmp(name, "BUS") == 0 ||
        strcmp(name, "CHLD") == 0 || strcmp(name, "CONT") == 0 ||
        strcmp(name, "HUP") == 0 || strcmp(name, "KILL") == 0 ||
        strcmp(name, "PIPE") == 0 || strcmp(name, "QUIT") == 0 ||
        strcmp(name, "STOP") == 0 || strcmp(name, "TSTP") == 0 ||
        strcmp(name, "TTIN") == 0 || strcmp(name, "TTOU") == 0 ||
        strcmp(name, "USR1") == 0 || strcmp(name, "USR2") == 0 ||
        strcmp(name, "WINCH") == 0)
        return true;
#endif

    return false;
}

bool trap_signal_name_is_invalid(const char *name)
{
    Expects_not_null(name);
    return !is_known_signal_name(name);
}

bool trap_signal_name_is_unsupported(const char *name)
{
    Expects_not_null(name);

    // If it's invalid, it's not "unsupported" - it's invalid
    if (!is_known_signal_name(name))
        return false;

    // If it's known, check if it's actually available on this platform
    // If trap_signal_name_to_number returns -1, it's unsupported
    // But we need to exclude EXIT (signal 0) which always succeeds
    if (strcmp(name, "EXIT") == 0)
        return false; // EXIT is always supported

    // For all other known signals, check if they're available on this platform
    return trap_signal_name_to_number(name) == -1;
}

const char *trap_signal_number_to_name(int signo)
{
    Expects_ge(signo, 0);  // Signal numbers must be non-negative (0 for EXIT, >0 for signals)

    // Handle special case: EXIT (signal 0)
    if (signo == 0)
        return "EXIT";

    // ISO C signals (available on all platforms)
    if (signo == SIGABRT)
        return "ABRT";
    if (signo == SIGFPE)
        return "FPE";
    if (signo == SIGILL)
        return "ILL";
    if (signo == SIGINT)
        return "INT";
    if (signo == SIGSEGV)
        return "SEGV";
    if (signo == SIGTERM)
        return "TERM";

#ifdef TRAP_USE_POSIX_SIGNALS
    // POSIX-specific signals
#ifdef SIGALRM
    if (signo == SIGALRM)
        return "ALRM";
#endif
#ifdef SIGBUS
    if (signo == SIGBUS)
        return "BUS";
#endif
#ifdef SIGCHLD
    if (signo == SIGCHLD)
        return "CHLD";
#endif
#ifdef SIGCONT
    if (signo == SIGCONT)
        return "CONT";
#endif
#ifdef SIGHUP
    if (signo == SIGHUP)
        return "HUP";
#endif
#ifdef SIGKILL
    if (signo == SIGKILL)
        return "KILL";
#endif
#ifdef SIGPIPE
    if (signo == SIGPIPE)
        return "PIPE";
#endif
#ifdef SIGQUIT
    if (signo == SIGQUIT)
        return "QUIT";
#endif
#ifdef SIGSTOP
    if (signo == SIGSTOP)
        return "STOP";
#endif
#ifdef SIGTSTP
    if (signo == SIGTSTP)
        return "TSTP";
#endif
#ifdef SIGTTIN
    if (signo == SIGTTIN)
        return "TTIN";
#endif
#ifdef SIGTTOU
    if (signo == SIGTTOU)
        return "TTOU";
#endif
#ifdef SIGUSR1
    if (signo == SIGUSR1)
        return "USR1";
#endif
#ifdef SIGUSR2
    if (signo == SIGUSR2)
        return "USR2";
#endif
#ifdef SIGWINCH
    if (signo == SIGWINCH)
        return "WINCH";
#endif
#endif

    // Signal number is not recognized in the available signal set
    // Check if it's a known signal on any platform but unsupported here
    // We need to check if it's a known ISO C signal that we couldn't match above
    // (This shouldn't happen as we check all ISO C signals)

    // Check for known POSIX signals that might not be available on this platform
#ifdef TRAP_USE_POSIX_SIGNALS
    // If we're on POSIX, all POSIX signals should have been checked above
    return "INVALID";
#else
    // On non-POSIX platforms, check if this is a known POSIX signal
    // If so, it's unsupported; otherwise it's invalid

    // Create a temporary name variable for the check
    // We'll use a heuristic: known POSIX signals have specific numbers
    // But without platform definitions, we can't reliably detect them
    // So we return "UNSUPPORTED" for any unmatched signal to be conservative
    // This handles the case where a POSIX system compiled on UCRT might have
    // signal numbers that don't map to our static list

    // Actually, a safer approach: if the signal number falls in a plausible range,
    // it might be unsupported; otherwise it's invalid
    if (signo > 0 && signo < 64)
        return "UNSUPPORTED"; // Plausible signal number, but not recognized
    else
        return "INVALID"; // Out of typical signal range
#endif
}

void trap_store_reset_non_ignored(trap_store_t* store)
{
    Expects_not_null(store);
    for (size_t i = 0; i < store->capacity; i++)
    {
        trap_action_t* trap = &store->traps[i];
        if (!trap->is_ignored)
        {
            // Reset to default
            if (trap->action)
                string_destroy(&trap->action);
            trap->action = NULL;
            trap->is_default = true;

            // TODO: where to reset the actual signal handler to SIG_DFL?
#ifdef TRAP_USE_POSIX_SIGNALS
            sigaction(trap->signal_number, NULL, NULL);
#elifdef TRAP_USE_UCRT_SIGNALS
            if (trap->signal_number == SIGABRT ||
                trap->signal_number == SIGFPE ||
                trap->signal_number == SIGILL ||
                trap->signal_number == SIGINT ||
                trap->signal_number == SIGSEGV || trap->signal_number == SIGTERM)
            {
                signal(trap->signal_number, SIG_DFL);
            }
#else
            // ISO C signal handling
            if (trap->signal_number == SIGABRT ||
                trap->signal_number == SIGFPE ||
                trap->signal_number == SIGILL ||
                trap->signal_number == SIGINT ||
                trap->signal_number == SIGSEGV || trap->signal_number == SIGTERM)
            {
                signal(trap->signal_number, SIG_DFL);
            }
#endif
        }
    }
}

void trap_store_for_each_set_trap(const trap_store_t *store,
                                   void (*callback)(int signal_number,
                                                    const trap_action_t *trap,
                                                    void *context),
                                   void *context)
{
    Expects_not_null(store);
    Expects_not_null(callback);

    // Iterate through all signal traps in the store
    for (size_t i = 0; i < store->capacity; i++)
    {
        const trap_action_t *trap = &store->traps[i];

        // Only report traps that are actually set (not in default state)
        if (trap->is_default && !trap->is_ignored && !trap->action)
            continue; // This trap is not set, skip it

        // Call the callback for this trap
        callback((int)i, trap, context);
    }

#ifdef TRAP_USE_UCRT_SIGNALS
    // Also report FPE trap if it's set
    const trap_action_t *fpe_trap = &store->fpe_trap;
    if (!fpe_trap->is_default || fpe_trap->is_ignored || fpe_trap->action)
    {
        // FPE is a special case with its own trap, but we need to report it
        // Use SIGFPE as the signal number for the callback
        callback(SIGFPE, fpe_trap, context);
    }
#endif

    // Report EXIT trap if it's set (signal 0 represents EXIT)
    if (store->exit_trap_set && store->exit_action)
    {
        // Create a temporary trap_action_t for the EXIT trap
        trap_action_t exit_trap;
        exit_trap.signal_number = 0;
        exit_trap.action = store->exit_action;
        exit_trap.is_ignored = false;
        exit_trap.is_default = false;

        callback(0, &exit_trap, context);
    }
}

void trap_store_run_exit_trap(const trap_store_t* store, exec_frame_t* frame)
{
    Expects_not_null(store);
    Expects_not_null(frame);
    Expects_eq(store->exit_trap_set, true);
    Expects_not_null(store->exit_action);
    // Execute the EXIT trap action
    printf("Executing EXIT trap action: %s\n", string_cstr(store->exit_action));
    // In a real shell, we would execute the command and handle its exit status.
}

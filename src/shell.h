#ifndef SHELL_T_H
#define SHELL_T_H

//#include "executor.h"
#include "string_t.h"

// Status/result conventions applied everywhere
typedef enum {
    SH_OK,
    SH_INCOMPLETE,    // more input required (multiline)
    SH_SYNTAX_ERROR,
    SH_RUNTIME_ERROR,
    SH_INTERNAL_ERROR,
    SH_FATAL
} sh_status_t;

/* ============================================================================
 * Shell
 * ============================================================================ */

/**
 * The shell_t structure maintains all the state of an instance of the shell.
 * It holds the root node of a tree of executors. Each executor holds all the state
 * required for a shell session. The root executor is spawned at initialization,
 * and child executors are spawned for each fork.
 */

typedef struct shell_t shell_t;

/* ============================================================================
 * Shell Configuration
 * ============================================================================ */

/**
 * The shell_cfg_t structure contains all the startup initialization information
 * required to launch a shell instances. argv[0], option flags, command-line
 * args, envp.
 */

typedef struct
{
    bool allexport; // -a
    bool errexit;   // -e
    bool ignoreeof; // no flag
    //   interactive -i is determined from mode
    bool monitor;   // -m (optional)
    bool noclobber; // -C
    bool noglob;    // -f
    bool noexec;    // -n
    bool nounset;   // -u
    bool pipefail;  // no flag
    bool verbose;   // -v
    bool vi;
    bool xtrace; // -x
} shell_flags_t;

typedef enum
{
    SHELL_MODE_UNKNOWN = 0,
    SHELL_MODE_INTERACTIVE,
    SHELL_MODE_COMMAND_STRING,
    SHELL_MODE_STDIN,
    SHELL_MODE_SCRIPT_FILE,
    SHELL_MODE_INVALID_UID_GID
} shell_mode_t;

typedef struct
{
    // Start-up environment
    shell_mode_t mode;
    char *command_name; // argv[0]
    char *command_string; // -c command string (if any)
    char *command_file;   // command file (if any)
    char **arguments;     // argv for the shell

    // Flags
    shell_flags_t flags;
} shell_cfg_t;


shell_t *shell_create(const shell_cfg_t *opts);
void     shell_destroy(shell_t **sh);

// Cleanup function for arena allocator.
// This gets registered with arena_set_cleanup when a shell is created.
// It is called when a catastrophic OOM occurs.
void shell_cleanup(void *sh_ptr);

// Feeds a single line and processes if complete.
// Returns SH_INCOMPLETE if the shell expects more input.
// If line_num is non-zero, it's used for error reporting.
sh_status_t shell_feed_line(shell_t *sh, const char *line, int line_num);

// Runs the shell. This handles prompts, input reading, etc.
// It chooses between interactive and non-interactive modes
// based on sh->cfg.mode.
sh_status_t shell_execute(shell_t *sh);

// If shell_feed_line returns SH_OK and a command is complete,
// the shell has already executed it. Errors are reported via sh->error.
const char *shell_last_error(shell_t *sh);

// Clear the last error message.
void shell_reset_error(shell_t *sh);

// Process a full script buffer (no prompt logic).
sh_status_t shell_run_script(shell_t *sh, const char *script);

// Various getters/setters and helpers
const char *shell_get_ps1(const shell_t *sh);
const char *shell_get_ps2(const shell_t *sh);

#ifdef FUTURE
/*
// POSIX signals: ABRT, ALRM, BUS, CHLD, CONT, FPE, HUP, ILL, INT, PIPE,
//                QUIT, SEGV, TERM, TSTP, TTIN, TTOU, USR1, USR2, WINCH

// UCRT signals: ABRT, FPE, ILL, INT, SEGV, TERM
*/

/*
The sh utility shall take the standard action for all signals (see 1.4 Utility Description Defaults)
with the following exceptions.

If the shell is interactive, SIGINT signals received during command line editing shall be handled as
described in the EXTENDED DESCRIPTION, and SIGINT signals received at other times shall be caught
but no action performed.

If the shell is interactive:

SIGQUIT and SIGTERM signals shall be ignored.

If the -m option is in effect, SIGTTIN, SIGTTOU, and SIGTSTP signals shall be ignored.

If the -m option is not in effect, it is unspecified whether SIGTTIN, SIGTTOU, and SIGTSTP signals
are ignored, set to the default action, or caught. If they are caught, the shell shall, in the
signal-catching function, set the signal to the default action and raise the signal (after taking
any appropriate steps, such as restoring terminal settings).
*/

#endif

#endif
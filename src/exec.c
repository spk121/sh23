/**
 * exec.c - Shell executor implementation
 *
 * This file implements the public executor API and high-level execution.
 * Frame management and policy-driven execution is in exec_frame.c.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef MIGA_POSIX_API
#define _POSIX_C_SOURCE 202405L
#endif
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "miga/exec.h"

#include "miga/strlist.h"
#include "miga/string_t.h"
#include "miga/frame.h"

#include "alias_store.h"
#include "ast.h"
#include "builtin_store.h"
#include "exec_frame.h"
#include "exec_frame_policy.h"
#include "parse_session.h"
#include "exec_types_internal.h"
#include "miga/type_pub.h"
#include "fd_table.h"
#include "miga/frame.h"
#include "func_store.h"
#include "gnode.h"
#include "job_store.h"
#include "lexer.h"
#include "lexer_t.h"
#include "logging.h"
#include "lower.h"
#include "parser.h"
#include "positional_params.h"
#include "sig_act.h"
#include "token.h"
#include "tokenizer.h"
#include "trap_store.h"
#include "variable_store.h"
#include "miga/xalloc.h"

#ifdef MIGA_POSIX_API
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef MIGA_UCRT_API
#if defined(_WIN64)
#define _AMD64_
#elif defined(_WIN32)
#define _X86_
#endif
#include <handleapi.h>
#include <io.h>
#include <process.h>
#include <processthreadsapi.h> // GetProcessId, OpenProcess, GetExitCodeProcess, etc.
#include <synchapi.h>          // WaitForSingleObject
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define EXECUTOR_ERROR_BUFFER_SIZE 512

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */


/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static miga_exec_status_t source_rc_files(struct miga_exec_t *e)
{
    // FIXME
    (void)e;
    return MIGA_EXEC_STATUS_OK;
#if 0
#ifdef EXEC_SYSTEM_RC_PATH
    exec_load_config_file(e, EXEC_SYSTEM_RC_PATH, func_store, alias_store);
#endif
#ifdef EXEC_USER_RC_PATH
    if (e->is_interactive)
    {
        /* Check $ENV for a filename. It is subject to pathname expansion.*/
        const char *env_raw_rc_path = variable_store_get_value_cstr(e->variables, "ENV");
        if (env_raw_rc_path && *env_raw_rc_path)
        {
            char *env_rc_path = exec_expand_pathname(e, env_raw_rc_path);
#ifdef MIGA_POSIX_API
            if (getuid() == geteuid() && getgid() == getegid() && getuid())
            {
                exec_load_config_file(e, env_rc_path, func_store, alias_store);
            }
#endif
            exec_load_config_file(e, env_rc_path, func_store, alias_store);
        }
        else
        {
            exec_load_config_file_from_config_directory(e, EXEC_USER_RC_NAME, func_store,
                                                        alias_store);
        }
    }
#endif

#endif
}

/* ============================================================================
 * Executor Lifecycle
 * ============================================================================ */

struct miga_exec_t *exec_create(void)
{
    return xcalloc(1, sizeof(struct miga_exec_t));
}

void exec_destroy(miga_exec_t **executor_ptr)
{
    if (!executor_ptr || !*executor_ptr)
        return;

    miga_exec_t *e = *executor_ptr;

    /* Pop all frames */
    while (e->current_frame)
    {
        exec_frame_pop(&e->current_frame);
    }

    /* Clean up executor-owned resources */
    if (e->working_directory)
        string_destroy(&e->working_directory);
    string_destroy(&e->shell_name);
    string_destroy(&e->error_msg);

    if (e->last_argument)
        string_destroy(&e->last_argument);

    /* If no frames were created, these may still be owned by the executor. */
    if (e->variables)
        variable_store_destroy(&e->variables);
    if (e->local_variables)
        variable_store_destroy(&e->local_variables);
    if (e->positional_params)
        positional_params_destroy(&e->positional_params);
    if (e->functions)
        func_store_destroy(&e->functions);
    if (e->aliases)
        alias_store_destroy(&e->aliases);
    if (e->session)
        parse_session_destroy(&e->session);
    if (e->traps)
        trap_store_destroy(&e->traps);
    if (e->original_signals)
        sig_act_store_destroy(&e->original_signals);

    job_store_destroy(&e->jobs);

#if defined(MIGA_POSIX_API) || defined(MIGA_UCRT_API)
    if (e->open_fds)
        fd_table_destroy(&e->open_fds);
#endif

    if (e->pipe_statuses)
        xfree(e->pipe_statuses);

    xfree(e);
    *executor_ptr = NULL;
}

/* ============================================================================
 * Pre-Execution Configuration
 * ============================================================================
 *
 * These setters configure the executor before execution begins.  They may only
 * be called before the top frame is initialised.  After that point the setters
 * return false to indicate that the value was not applied.
 *
 * When RC parsing is enabled, values set here may be overridden by the RC
 * files.
 */

/* ── Startup environment ─────────────────────────────────────────────────── */

bool exec_is_args_set(const miga_exec_t *executor)
{
    return executor->argc > 0 && executor->argv != NULL;
}

char *const *exec_get_args_cstr(const miga_exec_t *executor, int *argc_out)
{
    Expects_not_null(executor);

    if (!exec_is_args_set(executor))
        return NULL;
    if (argc_out)
        *argc_out = executor->argc;
    return executor->argv;
}

bool exec_set_args(miga_exec_t *executor, const strlist_t *args)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    if (args && strlist_size(args) > 0)
    {
        executor->argv = strlist_to_cstr_array(args, &executor->argc);
    }
    else
    {
        executor->argc = 0;
        executor->argv = NULL;
    }
    return true;
}

bool exec_set_args_cstr(miga_exec_t *executor, int argc, char *const *argv)
{
    Expects_not_null(executor);

    if (exec_is_top_frame_initialized(executor))
        return false;
    if (argc < 0 || (argc > 0 && argv == NULL))
        return false;

    executor->argc = argc;
    executor->argv = argv;
    return true;
}

bool exec_is_envp_set(const miga_exec_t *executor)
{
    Expects_not_null(executor);

    return executor->envp != NULL;
}

char *const *exec_get_envp_cstr(const miga_exec_t *executor)
{
    Expects_not_null(executor);

    return executor->envp;
}

bool exec_set_envp_cstr(miga_exec_t *executor, char *const *envp)
{
    Expects_not_null(executor);

    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->envp = envp;
    return true;
}

/* ── Shell identity ──────────────────────────────────────────────────────── */

bool exec_is_shell_name_set(const miga_exec_t *executor)
{
    Expects_not_null(executor);

    return executor->shell_name != NULL;
}

const char *exec_get_shell_name_cstr(const miga_exec_t *executor)
{
    Expects_not_null(executor);

    if (!executor->shell_name)
        return NULL;
    return string_cstr(executor->shell_name);
}

bool exec_set_shell_name_cstr(miga_exec_t *executor, const char *shell_name)
{
    Expects_not_null(executor);

    if (exec_is_top_frame_initialized(executor))
        return false;

    if (!shell_name && executor->shell_name)
        string_destroy(&executor->shell_name);
    else if (shell_name && !executor->shell_name)
        executor->shell_name = string_create_from_cstr(shell_name);
    else if (shell_name && executor->shell_name)
        string_set_cstr(executor->shell_name, shell_name);

    return true;
}

/* ── Shell option flags ──────────────────────────────────────────────────── */

bool exec_get_flag_allexport(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->opt.allexport;
}

bool exec_set_flag_allexport(miga_exec_t *executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.allexport = value;
    return true;
}

bool exec_get_flag_errexit(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->opt.errexit;
}

bool exec_set_flag_errexit(miga_exec_t *executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.errexit = value;
    return true;
}

bool exec_get_flag_ignoreeof(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->opt.ignoreeof;
}

bool exec_set_flag_ignoreeof(miga_exec_t *executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.ignoreeof = value;
    return true;
}

bool exec_get_flag_noclobber(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->opt.noclobber;
}

bool exec_set_flag_noclobber(miga_exec_t *executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.noclobber = value;
    return true;
}

bool exec_get_flag_noglob(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->opt.noglob;
}

bool exec_set_flag_noglob(miga_exec_t *executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.noglob = value;
    return true;
}

bool exec_get_flag_noexec(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->opt.noexec;
}

bool exec_set_flag_noexec(miga_exec_t *executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.noexec = value;
    return true;
}

bool exec_get_flag_nounset(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->opt.nounset;
}

bool exec_set_flag_nounset(miga_exec_t *executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.nounset = value;
    return true;
}

bool exec_get_flag_pipefail(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->opt.pipefail;
}

bool exec_set_flag_pipefail(miga_exec_t *executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.pipefail = value;
    return true;
}

bool exec_get_flag_verbose(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->opt.verbose;
}

bool exec_set_flag_verbose(miga_exec_t *executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.verbose = value;
    return true;
}

bool exec_get_flag_vi(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->opt.vi;
}

bool exec_set_flag_vi(miga_exec_t *executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.vi = value;
    return true;
}

bool exec_get_flag_xtrace(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->opt.xtrace;
}

bool exec_set_flag_xtrace(miga_exec_t *executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.xtrace = value;
    return true;
}

/* ── Interactive / login mode ────────────────────────────────────────────── */

bool exec_get_is_interactive(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->is_interactive;
}

bool exec_set_is_interactive(miga_exec_t *executor, bool is_interactive)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->is_interactive = is_interactive;
    return true;
}

bool exec_get_is_login_shell(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->is_login_shell;
}

bool exec_set_is_login_shell(miga_exec_t *executor, bool is_login_shell)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->is_login_shell = is_login_shell;
    return true;
}

/* ── Job control ─────────────────────────────────────────────────────────── */

bool exec_get_job_control_disabled(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->job_control_disabled;
}

bool exec_set_job_control_disabled(miga_exec_t *executor, bool disabled)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->job_control_disabled = disabled;
    return true;
}

/* ── Working directory ───────────────────────────────────────────────────── */

bool exec_is_working_directory_set(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->working_directory != NULL;
}

const char *exec_get_working_directory_cstr(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    if (!executor->working_directory)
        return NULL;
    return string_cstr(executor->working_directory);
}

bool exec_set_working_directory_cstr(miga_exec_t *executor, const char *path)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    if (!path && executor->working_directory)
        string_destroy(&executor->working_directory);
    else if (path && !executor->working_directory)
        executor->working_directory = string_create_from_cstr(path);
    else if (path && executor->working_directory)
        string_set_cstr(executor->working_directory, path);
    return true;
}

/* ── File permissions ────────────────────────────────────────────────────── */

bool exec_is_umask_set(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->umask != 0;
}

int exec_get_umask(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->umask;
}

bool exec_set_umask(miga_exec_t *executor, int mask)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->umask = mask;
    return true;
}

#ifdef MIGA_POSIX_API
mode_t exec_get_umask_posix(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->umask;
}

bool exec_set_umask_posix(miga_exec_t *executor, mode_t mask)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->umask = mask;
    return true;
}

bool exec_is_file_size_limit_set(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->file_size_limit != 0;
}

rlim_t exec_get_file_size_limit(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->file_size_limit;
}

bool exec_set_file_size_limit(miga_exec_t *executor, rlim_t limit)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->file_size_limit = limit;
    return true;
}
#endif

/* ── Process identity ────────────────────────────────────────────────────── */

bool exec_is_process_group_set(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->pgid_valid;
}

int exec_get_process_group(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    if (!executor->pgid_valid)
        return -1;
    return executor->pgid;
}

bool exec_set_process_group(miga_exec_t *executor, int pgid)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->pgid = pgid;
    executor->pgid_valid = true;
    return true;
}

bool exec_is_shell_pid_set(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->shell_pid_valid;
}

int exec_get_shell_pid(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    if (!executor->shell_pid_valid)
        return -1;
    return executor->shell_pid;
}

bool exec_set_shell_pid(miga_exec_t *executor, int pid)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->shell_pid = pid;
    executor->shell_pid_valid = true;
    return true;
}

bool exec_is_shell_ppid_set(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->shell_ppid_valid;
}

int exec_get_shell_ppid(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    if (!executor->shell_ppid_valid)
        return -1;
    return executor->shell_ppid;
}

bool exec_set_shell_ppid(miga_exec_t *executor, int ppid)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->shell_ppid = ppid;
    executor->shell_ppid_valid = true;
    return true;
}

/* ── RC file control ─────────────────────────────────────────────────────── */

bool exec_get_inhibit_rc_files(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->inhibit_rc_files;
}

bool exec_set_inhibit_rc_files(miga_exec_t *executor, bool inhibit)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->inhibit_rc_files = inhibit;
    return true;
}

bool exec_get_flag_nobuiltins(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->nobuiltins;
}

bool exec_set_flag_nobuiltins(miga_exec_t *executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->nobuiltins = value;
    return true;
}

bool exec_is_system_rc_filename_set(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->system_rc_filename != NULL;
}

const char *exec_get_system_rc_filename_cstr(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    if (!executor->system_rc_filename)
        return NULL;
    return string_cstr(executor->system_rc_filename);
}

bool exec_set_system_rc_filename_cstr(miga_exec_t *executor, const char *filename)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    if (!filename && executor->system_rc_filename)
        string_destroy(&executor->system_rc_filename);
    else if (filename && !executor->system_rc_filename)
        executor->system_rc_filename = string_create_from_cstr(filename);
    else if (filename && executor->system_rc_filename)
        string_set_cstr(executor->system_rc_filename, filename);
    return true;
}

bool exec_is_user_rc_filename_set(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->user_rc_filename != NULL;
}

const char *exec_get_user_rc_filename_cstr(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    if (!executor->user_rc_filename)
        return NULL;
    return string_cstr(executor->user_rc_filename);
}

bool exec_set_user_rc_filename_cstr(miga_exec_t *executor, const char *filename)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    if (!filename && executor->user_rc_filename)
        string_destroy(&executor->user_rc_filename);
    else if (filename && !executor->user_rc_filename)
        executor->user_rc_filename = string_create_from_cstr(filename);
    else if (filename && executor->user_rc_filename)
        string_set_cstr(executor->user_rc_filename, filename);
    return true;
}

/* ── Special parameters ──────────────────────────────────────────────────── */

int exec_get_last_exit_status(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    if (!executor->last_exit_status_set)
        return -1;
    return executor->last_exit_status;
}

bool exec_set_last_exit_status(miga_exec_t *executor, int status)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->last_exit_status = status;
    executor->last_exit_status_set = true;
    return true;
}

int exec_get_last_background_pid(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    if (!executor->last_background_pid_set)
        return -1;
    return executor->last_background_pid;
}

bool exec_set_last_background_pid(miga_exec_t *executor, int pid)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->last_background_pid = pid;
    executor->last_background_pid_set = true;
    return true;
}

const char *exec_get_last_argument_cstr(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    if (!executor->last_argument_set)
        return NULL;
    return string_cstr(executor->last_argument);
}

bool exec_set_last_argument_cstr(miga_exec_t *executor, const char *arg)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    if (!arg && executor->last_argument)
        string_destroy(&executor->last_argument);
    else if (arg && !executor->last_argument)
        executor->last_argument = string_create_from_cstr(arg);
    else if (arg && executor->last_argument)
        string_set_cstr(executor->last_argument, arg);
    executor->last_argument_set = true;
    return true;
}

/* ============================================================================
 * Frame Access
 * ============================================================================ */

bool exec_is_top_frame_initialized(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->top_frame_initialized;
}

miga_frame_t *exec_get_current_frame(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->current_frame;
}

/* ============================================================================
 * Builtin Registration (delegates to builtin_store)
 * ============================================================================ */

bool exec_register_builtin_cstr(miga_exec_t *executor, const char *name, miga_builtin_fn_t fn,
                                miga_builtin_category_t category)
{
    Expects_not_null(executor);

    if (!name || !fn)
        return false;

    /* The builtin store can be created at any time — it is not tied
       to the top-frame lifecycle like other pre-execution state. */
    if (!executor->builtins)
    {
        executor->builtins = builtin_store_create();
        if (!executor->builtins)
            return false;
    }

    /* Map the public enum to the internal enum.  The values are
       deliberately kept in sync (both start at 0 for SPECIAL), but
       we do an explicit conversion for type safety. */
    miga_builtin_category_t internal_cat =
        (category == MIGA_BUILTIN_CATEGORY_SPECIAL) ? MIGA_BUILTIN_CATEGORY_SPECIAL : MIGA_BUILTIN_CATEGORY_REGULAR;

    return builtin_store_set(executor->builtins, name, (miga_builtin_fn_t)fn, internal_cat);
}

bool exec_unregister_builtin_cstr(miga_exec_t *executor, const char *name)
{
    Expects_not_null(executor);

    if (!name || !executor->builtins)
        return false;

    return builtin_store_remove(executor->builtins, name);
}

bool exec_has_builtin_cstr(const miga_exec_t *executor, const char *name)
{
    Expects_not_null(executor);

    if (!name || !executor->builtins)
        return false;

    return builtin_store_has(executor->builtins, name);
}

miga_builtin_fn_t exec_get_builtin_cstr(const miga_exec_t *executor, const char *name)
{
    Expects_not_null(executor);

    if (!name || !executor->builtins)
        return NULL;

    return (miga_builtin_fn_t)builtin_store_get(executor->builtins, name);
}

bool exec_get_builtin_category_cstr(const miga_exec_t *executor, const char *name,
                                    miga_builtin_category_t *category_out)
{
    Expects_not_null(executor);

    if (!name || !executor->builtins)
        return false;

    miga_builtin_category_t internal_cat;
    bool found = builtin_store_lookup(executor->builtins, name, NULL, &internal_cat);
    if (found && category_out)
    {
        *category_out = (internal_cat == MIGA_BUILTIN_CATEGORY_SPECIAL) ? MIGA_BUILTIN_CATEGORY_SPECIAL : MIGA_BUILTIN_CATEGORY_REGULAR;
    }

    return found;
}

/* ============================================================================
 * Execution Setup
 * ============================================================================ */

static miga_exec_status_t exec_setup_core(miga_exec_t *e, bool interactive)
{
    Expects_not_null(e);

    if (e->top_frame_initialized)
        return MIGA_EXEC_STATUS_ERROR;

    // Finish initializing the top-level parameters that weren't explicitly set by the caller.
    // Override all the frame parameters with the executor's pre-configured values.
#ifdef MIGA_POSIX_API
    pid_t default_pid = getpid();
    pid_t default_ppid = getppid();
    bool default_pid_valid = true;
    bool default_ppid_valid = true;
#elifdef MIGA_UCRT_API
    int default_pid = _getpid();
    int default_ppid = 0; /* No getppid in UCRT */
    bool default_pid_valid = true;
    bool default_ppid_valid = false;
#else
    int default_pid = 0;
    int default_ppid = 0;
    bool default_pid_valid = false;
    bool default_ppid_valid = false;
#endif

    if (!e->shell_pid_valid && default_pid_valid)
    {
        e->shell_pid = default_pid;
        e->shell_pid_valid = true;
    }
    if (!e->shell_ppid_valid && default_ppid_valid)
    {
        e->shell_ppid = default_ppid;
        e->shell_ppid_valid = true;
    }

    e->is_interactive = interactive;

    // If the caller didn't explicitly set login shell status, guess based on if
    // argv[0] starts with a '-', which is a Unix convention.
    if (!e->is_login_shell)
    {
        bool default_is_login_shell =
            (e->argc > 0 && e->argv && e->argv[0] && e->argv[0][0] == '-');
        e->is_login_shell = default_is_login_shell;
    }

    /* Install default signal handlers for interactive mode */
    if (!e->signals_installed)
    {
        sig_act_store_t *original_signals = sig_act_store_create();
        // FIXME: what should be the behavior when a POSIX shell executor is created
        // with default signal handling? What does it mean to have "default signal handling"?
        // sig_act_store_install_default_signal_handlers(original_signals);
        e->original_signals = original_signals;
        e->signals_installed = true;
    }

    e->sigint_received = false;
    e->sigchld_received = false;
    memset((void *)e->trap_pending, 0, sizeof(e->trap_pending));

    if (!e->job_control_disabled)
    {
        if (e->is_interactive)
            e->jobs = job_store_create();
        else
            e->job_control_disabled = true;
    }

#ifdef MIGA_POSIX_API
    if (!e->pgid_valid)
    {
        pid_t pgid = getpgrp();
        if (pgid != -1)
        {
            e->pgid = pgid;
            e->pgid_valid = true;
        }
    }
#elifdef MIGA_UCRT_API
    e->pgid_valid = false; /* No getpgrp in UCRT, and we don't want to assume pgid == pid */
    e->pgid = -1;
#else
    e->pgid_valid = false;
    e->pgid = -1;
#endif

    /* Pipeline status (pipefail / PIPESTATUS) */
    e->pipe_statuses = NULL;
    e->pipe_status_count = 0;
    e->pipe_status_capacity = 0;

    /* Error state */
    if (e->error_msg)
        string_destroy(&e->error_msg);

    /* Exit request (set by exec_request_exit, checked by the main loop) */
    e->exit_requested = false;

    /* Builtin registry. Builtins are singleton, not frame-specific, and they can be registered
     * at any time: before or after when the top frame is initialized.
     */
    if (!e->builtins)
    {
        e->builtins = builtin_store_create();
    }

    /* Register the default set of builtin commands unless suppressed.
     * builtins_init_default uses builtin_store_set which replaces duplicates,
     * so this is safe even if the caller pre-registered some builtins. */
    if (!e->nobuiltins)
    {
        builtins_init_default(e->builtins);
    }

    // e->env_vars is only used for debugging and for keeping a permanent record of the
    // initial environment variables.
    // e->envp is the source of the initial environment variables for the top frame when the
    // top frame is initialized.  It is not used after the top frame is initialized.
    //
    // N.B. If e->variables is set, e->variables, rather than e->envp, becomes the source of the
    // initial environment variables for the top frame when the top frame is initialized.
    if (e->envp)
        e->env_vars = strlist_create_from_cstr_array((const char **)e->envp, -1);
    else
        // In MIGA_POSIX_API and MIGA_UCRT_API, this gets the env from the `environ` global.
        // In ISO C, there is no `environ`, so this will be initialized as an empty list.
        e->env_vars = strlist_create_from_system_env();

    if (!e->top_frame)
    {
        // This initialization is very involved. Lots of things happen in
        // exec_frame_create_top_level()
        //
        // STEP 1: Populates the frame with default stores and parameters
        // - e->envp is used to populate the variable store
        // - e->shell_name, e->argc, and e->argv are used to populate the positional parameters
        // - an empty file descriptor table is created for the frame
        // - an empty trap store is created for the frame
        // - an empty options set is created for the frame: err_exit is enabled, MUST BE POPULATED
        // - an empty CWD is created for the frame. Initialized to getcwd() if possible.
        // - an empty umask is created for the frame. Initialized to the system umask if possible.
        // - an empty function store is created for the frame
        // - an empty alias store is created for the frame
        // - the frame is set with RETURN and LOOP disallowed, since it's the top-level frame.
        // - the frame is set where exiting terminates process
        // - the frame is set as not a subshell and not a background job
        // - the frame's last exit status and last BG PID are zero
        //
        // STEP 2: Overrides
        // - if e->variables is already set, the variable store created in step 1 is discarded and
        //   the frame points to e->variables instead. This allows the caller to pre-populate the
        //   variable store with custom values before the top frame is initialized.
        // - if e->positional_params is already set, the positional parameters created in step 1 are
        //   discarded and the frame points to e->positional_params instead. This allows the caller
        //   to pre-populate the positional parameters with custom values before the top frame is
        //   initialized.
        // - similarly functions
        // - similarly aliases
        // - similarly traps
        // - similarly open fds
        // - similarly working directory
        // - similarly umask
        // - the options are *always* overridden with the executor's opt, since the executor's opt
        // is
        //   meant to be the pre-configured default options for the top frame.
        // - last_exit_status, last background_pid, and last_argument are always overridden with the
        //   executor's values, since they are meant to be the pre-configured default values for the
        //   top frame.
        // - the source line is set to 0
        // - the frame is marked as the top-level frame, the current frame pointer is set to the new
        //   frame, and the executor is marked as having an initialized top frame.

        exec_frame_create_top_level(e);
        log_debug("Top-level frame created and initialized.");
    }

    /* Source RC files */
    if (!e->inhibit_rc_files && (interactive || e->is_login_shell))
    {
        miga_exec_status_t ret = source_rc_files(e);
        e->rc_loaded = ret == MIGA_EXEC_STATUS_OK;
        if (e->rc_loaded)
            log_debug("RC files loaded successfully.");
        else
            log_debug("RC file loading failed.");
        return ret;
    }
    else
        log_debug("Skipped loading RC files");

    return MIGA_EXEC_STATUS_OK;
}

miga_exec_status_t exec_setup_interactive(miga_exec_t *executor)
{
    return exec_setup_core(executor, true);
}

miga_exec_status_t exec_setup_noninteractive(miga_exec_t *executor)
{
    return exec_setup_core(executor, false);
}

/**
 * A fallback in case an integrator failed to call either
 * exec_setup_interactive() or exec_setup_noninteractive().
 * This function attempts to guess the mode based on the provided file pointer.
 * @param executor The executor instance to set up.
 * @param fp The file pointer to use for guessing interactivity.
 * @return MIGA_EXEC_STATUS_OK if setup was successful, otherwise MIGA_EXEC_STATUS_ERROR.
 */
static miga_exec_status_t exec_setup_lazy(miga_exec_t *executor, FILE *fp)
{
    Expects_not_null(executor);
    if (executor->top_frame_initialized)
        return MIGA_EXEC_STATUS_ERROR;
    // Don't know if this is interactive or not.
    // Let's try to guess.

    log_warn("Executor setup was not explicitly called. Attempting to guess interactive mode based "
             "on file pointer.");
    bool is_interactive;
#ifdef MIGA_POSIX_API
    is_interactive = isatty(fileno(fp));
#elifdef MIGA_UCRT_API
    is_interactive = _isatty(_fileno(fp));
#else
    is_interactive = false;
#endif
    if (is_interactive)
    {
        log_debug("Guessed interactive mode based on file pointer.");
        return exec_setup_core(executor, true);
    }
    else
    {
        log_debug("Guessed non-interactive mode based on file pointer.");
        return exec_setup_core(executor, false);
    }
    // Unreachable
    return MIGA_EXEC_STATUS_ERROR;
}

#if 0
struct miga_exec_t *exec_create(const struct exec_cfg_t *cfg)
{
    struct miga_exec_t *e = xcalloc(1, sizeof(struct miga_exec_t));

    /* The singleton exec stores
     * 1. Singleton values
     * 2. Info required to initialize the top-level frame. The top-level
     *    frame is lazily initialized on the first call to the execution.
     */

    /* -------------------------------------------------------------------------
     * Shell Identity — PID / PPID
     * -------------------------------------------------------------------------
     */
#ifdef MIGA_POSIX_API
    pid_t default_pid = getpid();
    pid_t default_ppid = getppid();
    bool default_pid_valid = true;
    bool default_ppid_valid = true;
#elifdef MIGA_UCRT_API
    int default_pid = _getpid();
    int default_ppid = 0; /* No getppid in UCRT */
    bool default_pid_valid = true;
    bool default_ppid_valid = false;
#else
    int default_pid = 0;
    int default_ppid = 0;
    bool default_pid_valid = false;
    bool default_ppid_valid = false;
#endif

    e->shell_pid = cfg->shell_pid_set ? cfg->shell_pid : default_pid;
    e->shell_ppid = cfg->shell_ppid_set ? cfg->shell_ppid : default_ppid;
    e->shell_pid_valid = cfg->shell_pid_valid_set ? cfg->shell_pid_valid
                                                  : (cfg->shell_pid_set ? true : default_pid_valid);
    e->shell_ppid_valid = cfg->shell_ppid_valid_set
                              ? cfg->shell_ppid_valid
                              : (cfg->shell_ppid_set ? true : default_ppid_valid);

    /* -------------------------------------------------------------------------
     * Command Line — argc / argv / envp
     * -------------------------------------------------------------------------
     */
    e->argc = cfg->argv_set ? cfg->argc : 0;
    e->argv = cfg->argv_set ? (char **)cfg->argv : NULL;
    e->envp = cfg->envp_set ? (char **)cfg->envp : NULL;

    /* -------------------------------------------------------------------------
     * Shell Name ($0) and Arguments ($@)
     * -------------------------------------------------------------------------
     */
    if (cfg->shell_name_set && cfg->shell_name)
    {
        e->shell_name = string_create_from_cstr(cfg->shell_name);
    }
    else if (e->argc > 0 && e->argv && e->argv[0])
    {
        e->shell_name = string_create_from_cstr(e->argv[0]);
    }
    else
    {
        e->shell_name = string_create_from_cstr(EXEC_CFG_FALLBACK_ARGV0);
    }

    if (cfg->shell_args_set && cfg->shell_args)
    {
        e->shell_args = cfg->shell_args;
    }
    else if (e->argc > 1 && e->argv)
    {
        e->shell_args = strlist_create_from_cstr_array((const char **)e->argv + 1, e->argc - 1);
    }
    else
    {
        e->shell_args = strlist_create();
    }

    /* -------------------------------------------------------------------------
     * Environment Variables
     * -------------------------------------------------------------------------
     */
    if (cfg->env_vars_set && cfg->env_vars)
    {
        e->env_vars = cfg->env_vars;
    }
    else if (cfg->envp_set && cfg->envp)
    {
        e->env_vars = strlist_create_from_cstr_array((const char **)cfg->envp, -1);
    }
    else
    {
        // In MIGA_POSIX_API and MIGA_UCRT_API, this gets the env from the `environ` global.
        // In ISO C, there is no `environ`, so this will be initialized as an empty list.
        e->env_vars = strlist_create_from_system_env();
    }

    /* -------------------------------------------------------------------------
     * Option Flags
     * -------------------------------------------------------------------------
     */
    if (cfg->opt_flags_set)
    {
        e->opt = cfg->opt;
    }
    else
    {
        exec_opt_flags_t opt_fallback = EXEC_CFG_FALLBACK_OPT_FLAGS_INIT;
        e->opt = opt_fallback;
    }

    /* -------------------------------------------------------------------------
     * Interactive / Login Shell Detection
     * -------------------------------------------------------------------------
     */
#ifdef MIGA_POSIX_API
    bool default_is_interactive = isatty(STDIN_FILENO);
#elifdef MIGA_UCRT_API
    bool default_is_interactive = _isatty(_fileno(stdin));
#else
    bool default_is_interactive = false;
#endif

    bool default_is_login_shell = (e->argc > 0 && e->argv && e->argv[0] && e->argv[0][0] == '-');

    e->is_interactive = cfg->is_interactive_set ? cfg->is_interactive : default_is_interactive;
    e->is_login_shell = cfg->is_login_shell_set ? cfg->is_login_shell : default_is_login_shell;

    /* -------------------------------------------------------------------------
     * Job Control and Process Groups
     * -------------------------------------------------------------------------
     */
    e->jobs = job_store_create();
    e->job_control_disabled =  !(
        cfg->job_control_enabled_set ? cfg->job_control_enabled : e->is_interactive);

#ifdef MIGA_POSIX_API
    if (cfg->pgid_set)
    {
        e->pgid = cfg->pgid;
        e->pgid_valid = cfg->pgid_valid_set ? cfg->pgid_valid : true;
    }
    else
    {
        e->pgid = getpgrp();

        if (e->is_interactive)
        {
            e->pgid = getpid();
            setpgid(0, e->pgid); /* Ignore errors */
            tcsetpgrp(STDIN_FILENO, e->pgid);
        }
        e->pgid_valid = cfg->pgid_valid_set ? cfg->pgid_valid : true;
    }
#else
    e->pgid = cfg->pgid_set ? cfg->pgid : 0;
    e->pgid_valid = cfg->pgid_valid_set ? cfg->pgid_valid : false;
#endif

    /* -------------------------------------------------------------------------
     * Signals and Traps
     * -------------------------------------------------------------------------
     */
    e->signals_installed = false;
    e->sigint_received = 0;
    e->sigchld_received = 0;
    for (int i = 0; i < NSIG; i++)
        e->trap_pending[i] = 0;

    e->original_signals = sig_act_store_create();

    /* -------------------------------------------------------------------------
     * Pipeline Status (pipefail / PIPESTATUS)
     * -------------------------------------------------------------------------
     */
    e->pipe_statuses = NULL;
    e->pipe_status_count = 0;
    e->pipe_status_capacity = 0;

    /* -------------------------------------------------------------------------
     * Top-Frame Initialization Data (lazy frame creation)
     * -------------------------------------------------------------------------
     * These stores are owned by miga_exec_t until the top frame is created,
     * at which point ownership transfers to the frame.
     */
    e->variables = NULL;
    e->local_variables = NULL;
    e->positional_params = NULL;
    e->functions = NULL;
    e->aliases = alias_store_create();
    e->traps = NULL;

#if defined(MIGA_POSIX_API) || defined(MIGA_UCRT_API)
    e->open_fds = NULL;
    e->next_fd = 0;
#endif

    if (cfg->working_directory_set && cfg->working_directory)
    {
        e->working_directory = string_create_from_cstr(cfg->working_directory);
    }
    else
    {
        e->working_directory = NULL;
    }

#ifdef MIGA_POSIX_API
    e->umask = cfg->umask_set ? cfg->umask : 0;
    e->file_size_limit = cfg->file_size_limit_set ? cfg->file_size_limit : 0;
#else
    e->umask = cfg->umask_set ? cfg->umask : 0;
#endif

    /* -------------------------------------------------------------------------
     * Special Parameters ($?, $!, $_)
     * -------------------------------------------------------------------------
     */
    if (cfg->last_exit_status_set)
    {
        e->last_exit_status = cfg->last_exit_status;
        e->last_exit_status_set = true;
    }
    else
    {
        e->last_exit_status = 0;
        e->last_exit_status_set = true;
    }

    if (cfg->last_background_pid_set)
    {
        e->last_background_pid = cfg->last_background_pid;
        e->last_background_pid_set = true;
    }
    else
    {
        e->last_background_pid = 0;
        e->last_background_pid_set = false;
    }

    if (cfg->last_argument_set && cfg->last_argument)
    {
        e->last_argument = string_create_from_cstr(cfg->last_argument);
        e->last_argument_set = true;
    }
    else
    {
        e->last_argument = NULL;
        e->last_argument_set = false;
    }

    /* -------------------------------------------------------------------------
     * RC File State
     * -------------------------------------------------------------------------
     */
#if defined(EXEC_SYSTEM_RC_PATH) || defined(EXEC_USER_RC_PATH)
    e->rc_loaded = cfg->rc_loaded_set ? cfg->rc_loaded : false;
    e->rc_files_sourced = cfg->rc_files_sourced_set ? cfg->rc_files_sourced : false;
#else
    e->rc_loaded = cfg->rc_loaded_set ? cfg->rc_loaded : true;
    e->rc_files_sourced = cfg->rc_files_sourced_set ? cfg->rc_files_sourced : true;
#endif

    /* -------------------------------------------------------------------------
     * Parse session (created lazily on first use)
     * -------------------------------------------------------------------------
     */
    e->session = NULL;

    /* -------------------------------------------------------------------------
     * Frame Stack (lazily initialized on first execution)
     * -------------------------------------------------------------------------
     */
    e->top_frame_initialized = false;
    e->top_frame = NULL;
    e->current_frame = NULL;

    /* -------------------------------------------------------------------------
     * Error State
     * -------------------------------------------------------------------------
     */
    e->error_msg = string_create_from_cstr("");

    return e;
}
#endif

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/* exec_get_exit_status, exec_set_exit_status, exec_get_error,
 * exec_set_error, and exec_clear_error are defined in the
 * "Global State Queries" section below. */

string_t *exec_get_ps1(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    /* We expect to use the current frame's variable store, but
     * if called too early, there may only be a variable store
     * at top-level, or perhaps not at all */
    if (executor->current_frame && executor->current_frame->variables)
    {
        const char *ps1 = variable_store_get_value_cstr(executor->current_frame->variables, "PS1");
        if (ps1 && *ps1)
            return string_create_from_cstr(ps1);
    }
    else if (executor->variables)
    {
        const char *ps1 = variable_store_get_value_cstr(executor->variables, "PS1");
        if (ps1 && *ps1)
            return string_create_from_cstr(ps1);
    }

    return string_create_from_cstr("$ ");
}

char *exec_get_ps1_cstr(const miga_exec_t *executor)
{
    string_t *s = exec_get_ps1(executor);
    return string_release(&s);
}

static bool is_valid_ps1_variable_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static bool is_xdigit(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

/**
 * Render the PS1 prompt string.
 *
 * POSIX (XBD 8.1, XCU 2.5.3) requires that PS1 undergo parameter expansion
 * before being displayed. This implementation performs $VAR / ${VAR}
 * substitution on the raw PS1 value. The special parameters $$ and $? are
 * also expanded, as they are ordinary special parameters defined by POSIX
 * and routinely used in prompts.
 *
 * As a non-POSIX extension, the following backslash escape sequences are
 * also recognised and expanded before parameter substitution:
 *
 *   \n          Newline
 *   \r          Carriage return
 *   \t          Horizontal tab
 *   \\          Backslash
 *   \xhh        Unicode code point U+00hh encoded as UTF-8 (exactly 2 hex digits)
 *   \uhhhh      Unicode code point U+hhhh encoded as UTF-8 (exactly 4 hex digits)
 *   \Uhhhhhhhh  Unicode code point encoded as UTF-8 (exactly 6 hex digits, full range)
 *
 * An unrecognised escape sequence is passed through literally (backslash
 * and following character both emitted) so that typos are visible.
 *
 * Returns a newly heap-allocated C string. The caller is responsible for
 * freeing it.
 */
static string_t *render_ps1(const miga_exec_t *exec)
{
    Expects_not_null(exec);

    if (!exec->top_frame_initialized)
    {
        /* No frames at all yet — return the default PS1 value. */
        return string_create_from_cstr("$ ");
    }

    const miga_frame_t *frame = exec->current_frame;
    Expects_not_null(frame);

    /* -------------------------------------------------------------------------
     * 1. Retrieve the raw PS1 value.
     *
     * If PS1 is unset or empty, use the POSIX
     * default of "$ ".
     * -------------------------------------------------------------------------
     */
    string_t *ps1_str = string_create_from_cstr("PS1");
    const string_t *ps1_raw = exec_frame_get_variable(frame, ps1_str);
    string_destroy(&ps1_str);

    if (!ps1_raw)
    {
        return string_create_from_cstr("$ ");
    }

    /* -------------------------------------------------------------------------
     * 2. Expand parameter references in the PS1 string.
     *
     * Walk the raw string character by character. On '$', attempt to parse a
     * parameter reference and substitute its value. Everything else is copied
     * verbatim. No quoting, no field splitting, no globbing — strict POSIX
     * prompt expansion only.
     * -------------------------------------------------------------------------
     */
    string_t *out = string_create();
    if (!out)
        return NULL;

    int i = 0;
    char c;
    while (i < string_length(ps1_raw))
    {
        /* ------------------------------------------------------------------
         * Backslash escape sequences (non-POSIX extension).
         * Processed before '$' so that e.g. "\\$HOME" emits a literal
         * backslash then expands HOME, not a literal backslash-dollar-HOME.
         * ------------------------------------------------------------------ */
        char c = string_at(ps1_raw, i);
        if (c == '\\')
        {
            i++; /* consume '\\' */
            if (i >= string_length(ps1_raw))
            {
                /* Trailing backslash at end of string — emit literally. */
                string_append_char(out, '\\');
                break;
            }

            char c2 = string_at(ps1_raw, i);
            switch (c2)
            {
            case 'n':
                string_append_char(out, '\n');
                i++;
                break;
            case 'r':
                string_append_char(out, '\r');
                i++;
                break;
            case 't':
                string_append_char(out, '\t');
                i++;
                break;
            case '\\':
                string_append_char(out, '\\');
                i++;
                break;

            case 'x': {
                /* \xhh — exactly 2 hex digits, emitted as a UTF-8 byte.
                 * We interpret the value as a Unicode code point in the
                 * range U+0000..U+00FF (Latin-1 supplement), which maps
                 * trivially to its UTF-8 representation.  Fewer than 2
                 * valid hex digits: copy the escape literally. */
                i++; /* consume 'x' */
                int end = string_find_first_not_of_predicate_at(ps1_raw, is_xdigit, i);
                if (end == -1 || end - i < 2)
                {
                    /* Not enough hex digits — emit literally. */
                    string_append_cstr(out, "\\x");
                    continue;
                }
                string_t *hex_str = string_substring(ps1_raw, i, i + 2);
                uint32_t cp = (uint32_t)strtoul(string_cstr(hex_str), NULL, 16);
                string_append_utf8(out, cp);
                string_destroy(&hex_str);
                i += 2;
                break;
            }

            case 'u': {
                /* \uhhhh — exactly 4 hex digits, BMP Unicode code point. */
                i++; /* consume 'u' */
                int end = string_find_first_not_of_predicate_at(ps1_raw, is_xdigit, i);
                if (end == -1 || end - i < 4)
                {
                    /* Not enough hex digits — emit literally. */
                    string_append_cstr(out, "\\u");
                    continue;
                }
                string_t *hex_str = string_substring(ps1_raw, i, i + 4);
                uint32_t cp = (uint32_t)strtoul(string_cstr(hex_str), NULL, 16);
                string_append_utf8(out, cp);
                string_destroy(&hex_str);
                i += 4;
                break;
            }

            case 'U': {
                /* \Uhhhhhh — exactly 6 hex digits, full Unicode range.
                 * (Unicode only needs 5.5 hex digits to cover U+10FFFF,
                 * but 6 gives a clean fixed-width field and matches common
                 * shell practice for emoji, e.g. \U01F600.) */
                i++; /* consume 'U' */
                int end = string_find_first_not_of_predicate_at(ps1_raw, is_xdigit, i);
                if (end == -1 || end - i < 6)
                {
                    /* Not enough hex digits — emit literally. */
                    string_append_cstr(out, "\\U");
                    continue;
                }
                string_t *hex_str = string_substring(ps1_raw, i, i + 6);
                uint32_t cp = (uint32_t)strtoul(string_cstr(hex_str), NULL, 16);
                string_append_utf8(out, cp);
                string_destroy(&hex_str);
                i += 6;
                break;
            }

            default:
                /* Unrecognised escape — emit literally so the user sees
                 * what they typed and can diagnose typos. */
                string_append_char(out, '\\');
                string_append_char(out, string_at(ps1_raw, i));
                i++;
                break;
            }
            continue;
        }

        if (string_at(ps1_raw, i) != '$')
        {
            /* Ordinary character — copy verbatim. */
            string_append_char(out, string_at(ps1_raw, i));
            i++;
            continue;
        }

        /* '$' found — identify the parameter reference. */
        i++; /* consume '$' */

        if (string_at(ps1_raw, i) == '{')
        {
            /* ${VAR} form */
            i++; /* consume '{' */
            int end = string_find_first_of_cstr_at(ps1_raw, "}", i);
            if (end == -1)
            {
                /* No closing '}' found — emit literally. */
                string_append_cstr(out, "${");
                continue;
            }
            string_t *name_str = string_substring(ps1_raw, i, end);
            i = end + 1; /* consume up to and including '}' */
            const string_t *val = exec_frame_get_variable(frame, name_str);
            if (val)
                string_append(out, val);
            else
            {
                /* Variable not found - emit literally. */
                string_append_cstr(out, "${");
                string_append(out, name_str);
                string_append_char(out, '}');
            }
            string_destroy(&name_str);
        }
        else if (string_at(ps1_raw, i) == '?')
        {
            /* $? — last exit status */
            i++;
            string_t *exit_status_str = string_from_int(exec->last_exit_status);
            string_append(out, exit_status_str);
            string_destroy(&exit_status_str);
        }
        else if (string_at(ps1_raw, i) == '$')
        {
            /* $$ — shell PID */
            i++;
            if (exec->shell_pid_valid)
            {
                string_t *pid_str = string_from_int(exec->shell_pid);
                string_append(out, pid_str);
                string_destroy(&pid_str);
            }
            else
            {
                /* PID not available — emit literally. */
                string_append_cstr(out, "$$");
            }
        }
        else if (string_at(ps1_raw, i) == '_' || isalpha((unsigned char)string_at(ps1_raw, i)))
        {
            /* $VAR — unbraced name */
            int end = string_find_first_not_of_predicate_at(ps1_raw, is_valid_ps1_variable_char, i);
            Expects_ne(end, -1);
            string_t *name_str = string_substring(ps1_raw, i, end);
            i = end; /* consume the variable name */

            const string_t *val = exec_frame_get_variable(frame, name_str);
            if (val)
                string_append(out, val);
            else
            {
                /* Variable not found - emit literally. */
                string_append_char(out, '$');
                string_append(out, name_str);
            }
            string_destroy(&name_str);
        }
        else
        {
            /* Bare '$' not followed by a recognised parameter — copy literally.
             * POSIX leaves the result unspecified; copying verbatim is the most
             * conservative and user-friendly choice. */
            string_append_char(out, '$');
            /* Do not advance p — the character after '$' will be handled by
             * the next iteration of the loop. */
        }
    }

    return out;
}

string_t *exec_get_rendered_ps1(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return render_ps1(executor);
}

char *exec_get_rendered_ps1_cstr(const miga_exec_t *executor)
{
    string_t *s = exec_get_rendered_ps1(executor);
    return string_release(&s);
}

string_t *exec_get_ps2(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    const char *ps2 = NULL;
    if (executor->current_frame && executor->current_frame->variables)
    {
        ps2 = variable_store_get_value_cstr(executor->current_frame->variables, "PS2");
        if (ps2 && *ps2)
            return string_create_from_cstr(ps2);
    }
    if (executor->variables)
    {
        ps2 = variable_store_get_value_cstr(executor->variables, "PS2");
    }
    return string_create_from_cstr((ps2 && *ps2) ? ps2 : "> ");
}

char *exec_get_ps2_cstr(const miga_exec_t *executor)
{
    string_t *s = exec_get_ps2(executor);
    return string_release(&s);
}

positional_params_t *exec_get_positional_params(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->positional_params;
}

variable_store_t *exec_get_variables(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->variables;
}

alias_store_t *exec_get_aliases(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->aliases;
}

bool exec_is_interactive(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->is_interactive;
}

bool exec_is_login_shell(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->is_login_shell;
}

/* ============================================================================
 * Execution Functions
 * ============================================================================ */

void exec_setup_interactive_execute(miga_exec_t *executor)
{
    Expects_not_null(executor);

    /* Ensure we have a top-level frame */
    if (!executor->top_frame_initialized)
    {
        executor->top_frame = exec_frame_create_top_level(executor);
        executor->top_frame_initialized = true;
    }
    if (!executor->current_frame)
    {
        executor->current_frame = executor->top_frame;
    }

    /* FIXME handle rcfile here */
}

/* ============================================================================
 * Stream Execution Core
 * ============================================================================ */

miga_exec_status_t exec_execute_stream_repl(miga_exec_t *executor, FILE *fp, bool interactive)
{
    Expects_not_null(executor);
    Expects_not_null(fp);

    /* ------------------------------------------------------------------
     * Ensure the frame stack is initialized
     * ------------------------------------------------------------------ */
    if (!executor->top_frame_initialized)
    {
        miga_exec_status_t status = exec_setup_lazy(executor, fp);
        if (status != MIGA_EXEC_STATUS_OK)
            log_warn("lazy setup failed with status %d", status);
        // Not a fatal error
    }
    /* ------------------------------------------------------------------
     * Create / reuse the persistent parse session
     * ------------------------------------------------------------------ */
    if (!executor->session)
    {
        executor->session = exec_create_parse_session(executor);
        if (!executor->session)
        {
            exec_set_error_cstr(executor, "Failed to create parse session");
            return MIGA_EXEC_STATUS_ERROR;
        }
    }
    parse_session_t *session = executor->session;

    /* ------------------------------------------------------------------
     * REPL state
     * ------------------------------------------------------------------ */
#define IGNOREEOF_MAX 10
    int consecutive_eof = 0;
    bool need_continuation = false;
    miga_exec_status_t final_result = MIGA_EXEC_STATUS_OK;

    /* ------------------------------------------------------------------
     * Main loop — one physical line per iteration
     * ------------------------------------------------------------------ */
    for (;;)
    {
        /* ---- 1. Prompt ---- */
        if (interactive)
        {
            if (need_continuation)
            {
                const char *ps2 = exec_get_ps2_cstr(executor);
                fprintf(stderr, "%s", ps2);
            }
            else
            {
                string_t *ps1 = render_ps1(executor);
                fprintf(stderr, "%s", ps1 ? string_cstr(ps1) : "$ ");
                string_destroy(&ps1);
            }
            fflush(stderr);
        }

        /* ---- 2. Read & execute one line ---- */
        miga_exec_status_t line_status = exec_frame_stream_core(executor->current_frame, fp, session);

        /* ---- 3. EOF handling ---- */
        if (feof(fp))
        {
            if (need_continuation)
            {
                /*
                 * EOF inside a multi-line construct.  POSIX XCU 2.3:
                 * "If the shell is not interactive, the shell need not
                 *  perform this check." Either way, an unterminated
                 *  construct is a syntax error.
                 */
                if (interactive)
                    fprintf(stderr, "\n%s: syntax error: unexpected end of file\n",
                            string_cstr(executor->shell_name));
                final_result = MIGA_EXEC_STATUS_ERROR;
                break;
            }

            if (interactive && executor->opt.ignoreeof)
            {
                consecutive_eof++;
                if (consecutive_eof < IGNOREEOF_MAX)
                {
                    int remaining = IGNOREEOF_MAX - consecutive_eof;
                    fprintf(stderr,
                            "Use \"exit\" to leave the shell "
                            "(or press Ctrl-D %d more time%s).\n",
                            remaining, remaining == 1 ? "" : "s");
                    clearerr(fp);
                    continue;
                }
                /* Too many consecutive EOFs — fall through to exit */
            }

            /* Clean EOF */
            final_result = MIGA_EXEC_STATUS_OK;
            break;
        }

        /* Got input — reset consecutive-EOF counter */
        consecutive_eof = 0;

        /* ---- 4. Dispatch on line status ---- */
        if (line_status == MIGA_EXEC_STATUS_INCOMPLETE)
        {
            /*
             * The lexer or parser needs more input.  The session holds the
             * incomplete lexer state and/or accumulated tokens; the
             * tokenizer may also be buffering compound-command tokens.
             * Loop back to read the next line (prompting with PS2).
             */
            need_continuation = true;
            continue;
        }

        /* Command complete (OK, EMPTY, or ERROR) — reset continuation */
        need_continuation = false;

        /*
         * The session was reset internally by exec_frame_string_core on success
         * (parse_session_reset clears the lexer and accumulated
         * tokens).  On error the session may have stale state, so reset
         * it explicitly to be safe.
         */
        if (line_status == MIGA_EXEC_STATUS_ERROR)
        {
            parse_session_reset(session);

            if (interactive)
            {
                const char *err = exec_get_error_cstr(executor);
                if (err)
                {
                    fprintf(stderr, "%s: %s\n", string_cstr(executor->shell_name), err);
                    exec_clear_error(executor);
                }
                /* Interactive shells keep going after errors */
            }
            else
            {
                /* Non-interactive + errexit: abort on non-zero status */
                if (executor->opt.errexit && executor->last_exit_status != 0)
                {
                    final_result = MIGA_EXEC_STATUS_ERROR;
                    break;
                }
            }
        }

        /* ---- 5. Check for exit / top-level return ---- */
        if (executor->current_frame &&
            (executor->current_frame->pending_control_flow == MIGA_FRAME_FLOW_RETURN ||
             executor->current_frame->pending_control_flow == MIGA_FRAME_FLOW_TOP) &&
            executor->current_frame == executor->top_frame)
        {
            /* A top-level 'return' or 'exit' unwind */
            final_result = MIGA_EXEC_STATUS_EXIT;
            break;
        }

        /* ---- 6. Reap background jobs ---- */
        exec_reap_background_jobs(executor, interactive);

        /* ---- 7. Process pending traps ---- */
        for (int signo = 1; signo < NSIG; signo++)
        {
            if (!executor->trap_pending[signo])
                continue;

            executor->trap_pending[signo] = 0;

            const trap_action_t *trap_action = trap_store_get(executor->traps, signo);
            if (!trap_action || !trap_action->action)
                continue;

            /* POSIX: $? is preserved across trap execution */
            int saved_exit_status = executor->last_exit_status;

            miga_frame_t *trap_frame =
                exec_frame_push(executor->current_frame, EXEC_FRAME_TRAP, executor, NULL);

            miga_exec_status_t trap_result =
                frame_execute_string(trap_frame, trap_action->action);

            exec_frame_pop(&executor->current_frame);

            executor->last_exit_status = saved_exit_status;

            if (trap_result == MIGA_EXEC_STATUS_EXIT)
            {
                final_result = MIGA_EXEC_STATUS_EXIT;
                goto done;
            }

            if (trap_result == MIGA_EXEC_STATUS_ERROR && interactive)
            {
                const char *err = exec_get_error_cstr(executor);
                if (err)
                {
                    fprintf(stderr, "%s: trap handler: %s\n", string_cstr(executor->shell_name),
                            err);
                    exec_clear_error(executor);
                }
            }
        }

        /* ---- 8. SIGINT handling ---- */
        if (executor->sigint_received)
        {
            executor->sigint_received = 0;

            if (interactive)
            {
                fprintf(stderr, "\n");
                need_continuation = false;

                /*
                 * Discard all partial state: hard-reset the session so
                 * the lexer, accumulated tokens, and any buffered
                 * compound-command tokens in the tokenizer are flushed.
                 */
                parse_session_hard_reset(session, executor->aliases);
            }
            else
            {
                executor->last_exit_status = 128 + SIGINT;
                final_result = MIGA_EXEC_STATUS_ERROR;
                break;
            }
        }
    } /* for (;;) */

done:
    /* session is owned by executor->session, not destroyed here */
    return final_result;
}

miga_exec_status_t exec_execute_stream(miga_exec_t *executor, FILE *fp)
{
    return exec_execute_stream_repl(executor, fp, executor->is_interactive);
}

/* For non-interactive execution of a named script */
miga_exec_status_t exec_execute_stream_named(miga_exec_t *executor, FILE *fp, const char *filename)
{
    Expects_not_null(executor);
    Expects_not_null(fp);
    Expects_not_null(filename);
    /* Need to make sure a frame exists so set can set the filename */
    if (!executor->top_frame_initialized)
    {
        miga_exec_status_t status = exec_setup_lazy(executor, fp);
        if (status != MIGA_EXEC_STATUS_OK)
            log_warn("lazy setup failed with status %d", status);
        // Not a fatal error
    }
    Expects_not_null(executor->current_frame);
    /* Set the source name for error reporting */
    if (executor->current_frame->source_name)
        string_set_cstr(executor->current_frame->source_name, filename);
    else
        executor->current_frame->source_name = string_create_from_cstr(filename);
    executor->current_frame->source_line = 0;
    miga_exec_status_t status = exec_execute_stream_repl(executor, fp, false);
    return status;
}

miga_exec_status_t exec_execute_stream_once(miga_exec_t *executor, FILE *fp)
{
    Expects_not_null(executor);
    Expects_not_null(fp);

    /* Ensure we have a top-level frame */
    if (!executor->top_frame_initialized)
    {
        executor->top_frame = exec_frame_create_top_level(executor);
        executor->top_frame_initialized = true;
    }
    if (!executor->current_frame)
    {
        executor->current_frame = executor->top_frame;
    }

    /* Create a transient parse session for this stream.
     * Unlike the REPL, this session does not persist across calls. */
    parse_session_t *session = exec_create_parse_session(executor);
    if (!session)
    {
        exec_set_error_cstr(executor, "Failed to create parse session");
        return MIGA_EXEC_STATUS_ERROR;
    }

    miga_exec_status_t raw_status = exec_frame_stream_core(executor->current_frame, fp, session);

    /* Tear down the transient session. */
    parse_session_destroy(&session);

    /* Map INCOMPLETE to OK — a non-interactive stream that ends mid-construct
     * is not an error for existing callers (exec_execute_stream,
     * frame_execute_stream). */
    miga_exec_status_t status;
    switch (raw_status)
    {
    case MIGA_EXEC_STATUS_OK:
    case MIGA_EXEC_STATUS_INCOMPLETE:
        status = MIGA_EXEC_STATUS_OK;
        break;
    case MIGA_EXEC_STATUS_ERROR:
        status = MIGA_EXEC_STATUS_ERROR;
        break;
    default:
        status = raw_status;
        break;
    }

    if (status == MIGA_EXEC_STATUS_OK)
    {
        /* Process any pending trap handlers */
        for (int signo = 1; signo < NSIG; signo++)
        {
            if (executor->trap_pending[signo])
            {
                executor->trap_pending[signo] = 0;

                /* Get trap action from current trap store */
                const trap_action_t *trap_action = trap_store_get(executor->traps, signo);
                if (!trap_action || !trap_action->action)
                    continue;

                /* Save exit status - POSIX says $? should be preserved across trap execution */
                int saved_exit_status = executor->last_exit_status;

                /* Push EXEC_FRAME_TRAP and execute the trap action string */
                miga_frame_t *trap_frame =
                    exec_frame_push(executor->current_frame, EXEC_FRAME_TRAP, executor, NULL);

                miga_exec_status_t trap_result =
                    frame_execute_string(trap_frame, trap_action->action);

                exec_frame_pop(&executor->current_frame);

                /* Restore exit status */
                executor->last_exit_status = saved_exit_status;

                if (trap_result == MIGA_EXEC_STATUS_ERROR)
                {
                    status = MIGA_EXEC_STATUS_ERROR;
                    break;
                }
            }
        }
    }

    return status;
}

/* this version is for executing -c complete strings. Incomplete inputs are errors */
miga_exec_result_t exec_execute_command_string(miga_exec_t *executor, const char *command)
{
    Expects_not_null(executor);
    Expects_not_null(command);

    miga_exec_result_t result = {.status = MIGA_EXEC_STATUS_OK, .exit_code = 0};

    /* ------------------------------------------------------------------
     * Ensure the frame stack is initialized.
     * For -c invocations the caller should have called
     * exec_setup_noninteractive() beforehand, but handle the lazy case.
     * ------------------------------------------------------------------ */
    if (!executor->top_frame_initialized)
    {
        miga_exec_status_t setup = exec_setup_core(executor, false);
        if (setup != MIGA_EXEC_STATUS_OK)
            log_warn("lazy setup for command string failed with status %d", setup);
        /* Not fatal — proceed with whatever state we have. */
    }

    miga_frame_t *frame = executor->current_frame;
    if (!frame)
    {
        exec_set_error_cstr(executor, "No execution frame available");
        result.status = MIGA_EXEC_STATUS_ERROR;
        result.exit_code = EXEC_EXIT_FAILURE;
        return result;
    }

    /* ------------------------------------------------------------------
     * Create a temporary parse session.
     * Unlike the REPL, we feed all input at once, so the session is
     * local to this call rather than persistent across lines.
     * ------------------------------------------------------------------ */
    parse_session_t *session = exec_create_parse_session(executor);
    if (!session)
    {
        exec_set_error_cstr(executor, "Failed to create parse session");
        result.status = MIGA_EXEC_STATUS_ERROR;
        result.exit_code = EXEC_EXIT_FAILURE;
        return result;
    }

    /* ------------------------------------------------------------------
     * Feed the entire command string to the core execution engine.
     * ------------------------------------------------------------------ */
    miga_exec_status_t str_status = exec_frame_string_core(frame, command, session);

    switch (str_status)
    {
    case MIGA_EXEC_STATUS_OK:
        result.status = MIGA_EXEC_STATUS_OK;
        result.exit_code = executor->last_exit_status;
        break;

    case MIGA_EXEC_STATUS_EMPTY:
        /* Empty command string (e.g. whitespace only, comments only).
         * POSIX: exit status is zero for an empty command. */
        result.status = MIGA_EXEC_STATUS_OK;
        result.exit_code = 0;
        break;

    case MIGA_EXEC_STATUS_INCOMPLETE:
        /* For -c style execution, incomplete input is an error — the
         * caller promised a complete command string. */
        if (!exec_get_error_cstr(executor))
        {
            exec_set_error_cstr(executor, "Unexpected end of input (unclosed quote, "
                                          "here-document, or compound command)");
        }
        result.status = MIGA_EXEC_STATUS_INCOMPLETE;
        result.exit_code = EXEC_EXIT_MISUSE;
        break;

    case MIGA_EXEC_STATUS_ERROR:
        result.status = MIGA_EXEC_STATUS_ERROR;
        result.exit_code =
            executor->last_exit_status ? executor->last_exit_status : EXEC_EXIT_FAILURE;
        break;
    }

    /* ------------------------------------------------------------------
     * Translate control-flow signals that may have been set during
     * execution into the appropriate top-level result status.
     * ------------------------------------------------------------------ */
    if (frame->pending_control_flow == MIGA_FRAME_FLOW_TOP || executor->exit_requested)
    {
        result.status = MIGA_EXEC_STATUS_EXIT;
        result.exit_code = executor->last_exit_status;
    }
    else if (frame == executor->top_frame && frame->pending_control_flow == MIGA_FRAME_FLOW_RETURN)
    {
        /* A top-level 'return' is equivalent to 'exit'. */
        result.status = MIGA_EXEC_STATUS_EXIT;
        result.exit_code = executor->last_exit_status;
    }

    /* ------------------------------------------------------------------
     * Clean up.
     * ------------------------------------------------------------------ */
    parse_session_destroy(&session);

    return result;
}

/* ============================================================================
 * Partial State Lifecycle
 * ============================================================================ */

parse_session_t *exec_create_parse_session(miga_exec_t *executor)
{
    Expects_not_null(executor);
    return parse_session_create(executor->aliases);
}

size_t exec_get_parse_session_size(void)
{
    return sizeof(parse_session_t);
}

void exec_reset_parse_session(parse_session_t *session)
{
    Expects_not_null(session);
    parse_session_reset(session);
}

void exec_hard_reset_parse_session(parse_session_t *session, miga_exec_t *executor)
{
    Expects_not_null(session);
    if (executor)
        parse_session_hard_reset(session, executor->aliases);
    else
        parse_session_hard_reset(session, NULL);
}

const char *exec_get_parse_session_filename_cstr(const parse_session_t *session)
{
    Expects_not_null(session);
    return session->filename ? string_cstr(session->filename) : NULL;
}

size_t exec_get_parse_session_line_number(const parse_session_t *session)
{
    Expects_not_null(session);
    return session->caller_line_number;
}

void exec_set_parse_session_filename_cstr(parse_session_t *session, const char *filename)
{
    Expects_not_null(session);
    if (filename)
    {
        if (!session->filename)
            session->filename = string_create_from_cstr(filename);
        else
            string_set_cstr(session->filename, filename);
    }
    else
    {
        if (session->filename)
        {
            string_destroy(&session->filename);
            session->filename = NULL;
        }
    }
}

void exec_set_parse_session_line_number(parse_session_t *session, size_t line_number)
{
    Expects_not_null(session);
    session->caller_line_number = line_number;
}

void exec_destroy_parse_session(parse_session_t **session)
{
    Expects_not_null(session);
    parse_session_destroy(session);
}

/* ============================================================================
 * Incremental Command String Execution
 * ============================================================================ */

miga_exec_status_t exec_execute_command_string_partial_cstr(miga_exec_t *executor, const char *command,
                                                       const char *filename, size_t line_number,
                                                       parse_session_t *session)
{
    Expects_not_null(executor);
    Expects_not_null(command);
    Expects_not_null(session);

    /* ------------------------------------------------------------------
     * Ensure the frame stack is initialized.
     * ------------------------------------------------------------------ */
    if (!executor->top_frame_initialized)
    {
        miga_exec_status_t setup = exec_setup_core(executor, false);
        if (setup != MIGA_EXEC_STATUS_OK)
            log_warn("lazy setup for partial execution failed with status %d", setup);
    }

    miga_frame_t *frame = executor->current_frame;
    if (!frame)
    {
        exec_set_error_cstr(executor, "No execution frame available");
        return MIGA_EXEC_STATUS_ERROR;
    }

    /* ------------------------------------------------------------------
     * If an exit has been requested (e.g. by a previous invocation's
     * exit builtin), honour it immediately without consuming input.
     * ------------------------------------------------------------------ */
    if (executor->exit_requested)
        return MIGA_EXEC_STATUS_EXIT;

    /* ------------------------------------------------------------------
     * Source location tracking.
     *
     * If the caller supplies a filename, store it in the session
     * so it persists across calls.  The filename is also pushed into the
     * frame for error messages.
     *
     * line_number is updated every call when >= 1.  When the caller
     * does not supply a line number (0), we auto-increment from the
     * value stored in the session on previous calls.
     * ------------------------------------------------------------------ */
    if (filename)
    {
        if (!session->filename)
            session->filename = string_create_from_cstr(filename);
        else
            string_set_cstr(session->filename, filename);

        /* Push into the frame for error reporting. */
        if (frame->source_name)
            string_set_cstr(frame->source_name, filename);
        else
            frame->source_name = string_create_from_cstr(filename);
    }
    else if (session->filename)
    {
        /* Caller previously supplied a filename — keep using it. */
        if (frame->source_name)
            string_set(frame->source_name, session->filename);
        else
            frame->source_name = string_create_from(session->filename);
    }

    if (line_number >= 1)
    {
        session->caller_line_number = line_number;
    }
    else if (session->caller_line_number > 0)
    {
        /* Auto-increment from last known line. */
        session->caller_line_number++;
    }

    if (session->caller_line_number >= 1)
        frame->source_line = (int)session->caller_line_number;

    /* ------------------------------------------------------------------
     * Lazily create the lexer and tokenizer inside the session.
     *
     * The session lives in the caller (not the executor) so that each
     * partial-execution sequence has its own state independent of the
     * executor's REPL session.
     * ------------------------------------------------------------------ */
    if (!session->tokenizer)
    {
        session->tokenizer = tokenizer_create(executor->aliases);
        if (!session->tokenizer)
        {
            exec_set_error_cstr(executor, "Failed to create tokenizer");
            return MIGA_EXEC_STATUS_ERROR;
        }
    }
    if (!session->lexer)
    {
        session->lexer = lexer_create();
        if (!session->lexer)
        {
            exec_set_error_cstr(executor, "Failed to create lexer");
            return MIGA_EXEC_STATUS_ERROR;
        }
    }

    /* ------------------------------------------------------------------
     * Synchronise the session's line number so that error messages from
     * exec_frame_string_core reference the correct line.
     * ------------------------------------------------------------------ */
    if (session->caller_line_number >= 1)
        session->line_num = (int)session->caller_line_number - 1;
    /* -1 because exec_frame_string_core increments before processing */

    /* ------------------------------------------------------------------
     * Feed this chunk to the core execution engine.
     * ------------------------------------------------------------------ */
    miga_exec_status_t str_status = exec_frame_string_core(frame, command, session);

    /* Update the caller line number from the session (exec_frame_string_core may
       have incremented it). */
    session->caller_line_number = (size_t)session->line_num;

    /* ------------------------------------------------------------------
     * Translate the internal status to the public miga_exec_status_t.
     * ------------------------------------------------------------------ */
    miga_exec_status_t result;

    switch (str_status)
    {
    case MIGA_EXEC_STATUS_OK:
        session->incomplete = false;
        result = MIGA_EXEC_STATUS_OK;
        break;

    case MIGA_EXEC_STATUS_EMPTY:
        /* An empty chunk (whitespace, comments) is OK.
         * If we were already mid-continuation, stay incomplete — the
         * empty line is just a blank line inside a multi-line construct.
         * Otherwise report OK. */
        if (session->incomplete)
            result = MIGA_EXEC_STATUS_INCOMPLETE;
        else
            result = MIGA_EXEC_STATUS_OK;
        break;

    case MIGA_EXEC_STATUS_INCOMPLETE:
        session->incomplete = true;
        result = MIGA_EXEC_STATUS_INCOMPLETE;
        break;

    case MIGA_EXEC_STATUS_ERROR:
        /* On error, clean up the session so the next call starts fresh.
         * The caller can inspect exec_get_error() for details. */
        parse_session_reset(session);
        session->incomplete = false;
        result = MIGA_EXEC_STATUS_ERROR;
        break;

    default:
        result = MIGA_EXEC_STATUS_ERROR;
        break;
    }

    /* ------------------------------------------------------------------
     * If the command completed (OK or ERROR), check for control flow
     * signals that should be propagated to the caller.
     * ------------------------------------------------------------------ */
    if (result == MIGA_EXEC_STATUS_OK || result == MIGA_EXEC_STATUS_ERROR)
    {
        if (frame->pending_control_flow == MIGA_FRAME_FLOW_TOP || executor->exit_requested)
        {
            result = MIGA_EXEC_STATUS_EXIT;
        }
        else if (frame == executor->top_frame && frame->pending_control_flow == MIGA_FRAME_FLOW_RETURN)
        {
            /* Top-level 'return' is equivalent to 'exit'. */
            result = MIGA_EXEC_STATUS_EXIT;
        }
    }

    /* ------------------------------------------------------------------
     * If the parse completed (not INCOMPLETE), release the internal
     * resources but keep the session struct valid for reuse.
     * The caller can start a new sequence without calling cleanup.
     * ------------------------------------------------------------------ */
    if (result != MIGA_EXEC_STATUS_INCOMPLETE)
    {
        parse_session_reset(session);
        session->incomplete = false;
        /* Keep session allocated for reuse on next call. */
    }

    return result;
}

/* ============================================================================
 * Line-Editor Integration
 * ============================================================================ */

miga_exec_status_t exec_execute_stream_with_line_editor(miga_exec_t *executor, FILE *fp,
                                                   line_editor_fn_t line_editor_fn,
                                                   void *line_editor_user_data)
{
    Expects_not_null(executor);
    Expects_not_null(fp);
    Expects_not_null(line_editor_fn);

    /* ------------------------------------------------------------------
     * This function is only valid for interactive mode.
     * ------------------------------------------------------------------ */
    if (!executor->is_interactive)
    {
        exec_set_error_cstr(executor, "exec_execute_stream_with_line_editor: "
                                      "only valid after exec_setup_interactive()");
        return MIGA_EXEC_STATUS_ERROR;
    }

    /* ------------------------------------------------------------------
     * Ensure the frame stack is initialized.
     * ------------------------------------------------------------------ */
    if (!executor->top_frame_initialized)
    {
        miga_exec_status_t status = exec_setup_core(executor, true);
        if (status != MIGA_EXEC_STATUS_OK)
            log_warn("lazy interactive setup failed with status %d", status);
    }

    /* ------------------------------------------------------------------
     * Create / reuse the persistent parse session.
     * ------------------------------------------------------------------ */
    if (!executor->session)
    {
        executor->session = exec_create_parse_session(executor);
        if (!executor->session)
        {
            exec_set_error_cstr(executor, "Failed to create parse session");
            return MIGA_EXEC_STATUS_ERROR;
        }
    }
    parse_session_t *session = executor->session;

    /* ------------------------------------------------------------------
     * REPL state
     * ------------------------------------------------------------------ */
#define IGNOREEOF_MAX 10
    int consecutive_eof = 0;
    bool need_continuation = false;
    miga_exec_status_t final_result = MIGA_EXEC_STATUS_OK;
    string_t *line = NULL;

    /* ------------------------------------------------------------------
     * Main loop — one logical line per iteration
     * ------------------------------------------------------------------ */
    for (;;)
    {
        /* ---- 0. Check for pending exit request ---- */
        if (executor->exit_requested)
        {
            final_result = MIGA_EXEC_STATUS_EXIT;
            break;
        }

        /* ---- 1. Build the prompt ---- */
        char *prompt = NULL;
        if (need_continuation)
        {
            prompt = exec_get_ps2_cstr(executor);
        }
        else
        {
            string_t *ps1 = render_ps1(executor);
            prompt = ps1 ? string_release(&ps1) : xstrdup("$ ");
        }

        /* ---- 2. Call the line editor ---- */
        line_edit_status_t le_status = line_editor_fn(prompt, &line, line_editor_user_data);

        xfree(prompt);
        prompt = NULL;

        /* ---- 3. Handle line-editor status ---- */

        if (le_status == LINE_EDIT_EOF)
        {
            /* ---- EOF ---- */
            if (need_continuation)
            {
                fprintf(stderr, "\n%s: syntax error: unexpected end of file\n",
                        string_cstr(executor->shell_name));
                final_result = MIGA_EXEC_STATUS_ERROR;
                break;
            }

            if (executor->opt.ignoreeof)
            {
                consecutive_eof++;
                if (consecutive_eof < IGNOREEOF_MAX)
                {
                    int remaining = IGNOREEOF_MAX - consecutive_eof;
                    fprintf(stderr,
                            "Use \"exit\" to leave the shell "
                            "(or press Ctrl-D %d more time%s).\n",
                            remaining, remaining == 1 ? "" : "s");
                    continue;
                }
                /* Too many consecutive EOFs — fall through to exit. */
            }

            /* Clean EOF. */
            final_result = MIGA_EXEC_STATUS_OK;
            break;
        }

        if (le_status == LINE_EDIT_ERROR)
        {
            exec_set_error_cstr(executor, "Line editor returned fatal error");
            final_result = MIGA_EXEC_STATUS_ERROR;
            break;
        }

        if (le_status == LINE_EDIT_INTERRUPT)
        {
            /* The line editor handled SIGINT (cleared the line, etc.).
             * Discard any partial parse state and loop back for a fresh
             * prompt, mirroring the SIGINT handling in the fgets REPL. */
            consecutive_eof = 0;
            need_continuation = false;

            parse_session_hard_reset(session, executor->aliases);

            /* POSIX: $? = 128 + SIGINT after an interrupted command. */
            executor->last_exit_status = 128 + SIGINT;
            continue;
        }

        /* LINE_EDIT_PREVIOUS, LINE_EDIT_NEXT, LINE_EDIT_CURRENT,
         * LINE_EDIT_HISTORY_IDX are informational — the line editor has
         * already filled *line_out with the selected history entry.
         * We treat them the same as LINE_EDIT_OK for execution purposes. */

        /* ---- 4. Validate the returned line ---- */
        if (!line)
        {
            /* Line editor returned OK but no string — treat as empty. */
            continue;
        }

        /* Got valid input — reset consecutive-EOF counter. */
        consecutive_eof = 0;

        /* ---- 5. Append a newline and feed to the execution core ----
         *
         * exec_frame_string_core expects input lines to end with '\n' (the
         * lexer uses newlines as token delimiters).  The line-editor
         * contract says the returned string has NO trailing newline,
         * so we append one.
         */
        string_t *input = string_create_from(line);
        string_append_char(input, '\n');

        miga_exec_status_t str_status =
            exec_frame_string_core(executor->current_frame, string_cstr(input), session);

        string_destroy(&input);

        if (session->line_num > 0)
            executor->current_frame->source_line = session->line_num;

        /* ---- 6. Dispatch on execution status ---- */

        if (str_status == MIGA_EXEC_STATUS_INCOMPLETE)
        {
            need_continuation = true;
            continue;
        }

        /* Command complete (OK, EMPTY, or ERROR) — reset continuation. */
        need_continuation = false;

        if (str_status == MIGA_EXEC_STATUS_ERROR)
        {
            parse_session_reset(session);

            const char *err = exec_get_error_cstr(executor);
            if (err)
            {
                fprintf(stderr, "%s: %s\n", string_cstr(executor->shell_name), err);
                exec_clear_error(executor);
            }
            /* Interactive shells keep going after errors. */
        }

        /* ---- 7. Check for exit / top-level return ---- */
        if (executor->exit_requested)
        {
            final_result = MIGA_EXEC_STATUS_EXIT;
            break;
        }

        if (executor->current_frame &&
            executor->current_frame->pending_control_flow == MIGA_FRAME_FLOW_TOP)
        {
            final_result = MIGA_EXEC_STATUS_EXIT;
            break;
        }

        if (executor->current_frame && executor->current_frame == executor->top_frame &&
            executor->current_frame->pending_control_flow == MIGA_FRAME_FLOW_RETURN)
        {
            /* Top-level 'return' is equivalent to 'exit'. */
            final_result = MIGA_EXEC_STATUS_EXIT;
            break;
        }

        /* ---- 8. Reap background jobs ---- */
        exec_reap_background_jobs(executor, true);

        /* ---- 9. Process pending traps ---- */
        for (int signo = 1; signo < NSIG; signo++)
        {
            if (!executor->trap_pending[signo])
                continue;

            executor->trap_pending[signo] = 0;

            const trap_action_t *trap_action = trap_store_get(executor->traps, signo);
            if (!trap_action || !trap_action->action)
                continue;

            /* POSIX: $? is preserved across trap execution. */
            int saved_exit_status = executor->last_exit_status;

            miga_frame_t *trap_frame =
                exec_frame_push(executor->current_frame, EXEC_FRAME_TRAP, executor, NULL);

            miga_exec_result_t trap_result =
                exec_execute_command_string(executor, string_cstr(trap_action->action));

            exec_frame_pop(&executor->current_frame);

            executor->last_exit_status = saved_exit_status;

            if (trap_result.status == MIGA_EXEC_STATUS_EXIT)
            {
                final_result = MIGA_EXEC_STATUS_EXIT;
                goto done;
            }

            if (trap_result.status == MIGA_EXEC_STATUS_ERROR)
            {
                const char *err = exec_get_error_cstr(executor);
                if (err)
                {
                    fprintf(stderr, "%s: trap handler: %s\n", string_cstr(executor->shell_name),
                            err);
                    exec_clear_error(executor);
                }
            }
        }

        /* ---- 10. SIGINT received outside the line editor ----
         *
         * The line editor normally catches SIGINT itself and returns
         * LINE_EDIT_INTERRUPT.  But a signal could arrive during
         * exec_frame_string_core or between steps.  Handle it here as a
         * fallback.
         */
        if (executor->sigint_received)
        {
            executor->sigint_received = 0;

            fprintf(stderr, "\n");
            need_continuation = false;

            parse_session_hard_reset(session, executor->aliases);
        }

    } /* for (;;) */

done:
    if (line)
        string_destroy(&line);
    /* session is owned by executor->session, not destroyed here */
    return final_result;
}

/* ============================================================================
 * Global State Queries
 * ============================================================================ */

/* ── Exit status ─────────────────────────────────────────────────────────── */

int exec_get_exit_status(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->last_exit_status;
}

void exec_set_exit_status(miga_exec_t *executor, int status)
{
    Expects_not_null(executor);
    executor->last_exit_status = status;
}

void exec_request_exit(miga_exec_t *executor, int status)
{
    Expects_not_null(executor);
    executor->exit_requested = true;
    executor->last_exit_status = status;
}

bool exec_is_exit_requested(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->exit_requested;
}

/* ── Error message ───────────────────────────────────────────────────────── */

const string_t *exec_get_error(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->error_msg;
}

const char *exec_get_error_cstr(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->error_msg ? string_cstr(executor->error_msg) : NULL;
}

void exec_set_error_cstr(miga_exec_t *executor, const char *error)
{
    Expects_not_null(executor);
    Expects_not_null(error);
    if (!executor->error_msg)
        executor->error_msg = string_create_from_cstr(error);
    else
    {
        string_clear(executor->error_msg);
        string_append_cstr(executor->error_msg, error);
    }
}

void exec_set_error_printf(miga_exec_t *executor, const char *format, ...)
{
    Expects_not_null(executor);
    Expects_not_null(format);

    va_list args;
    va_start(args, format);

    char buffer[EXECUTOR_ERROR_BUFFER_SIZE];
    vsnprintf(buffer, sizeof(buffer), format, args);

    va_end(args);

    if (!executor->error_msg)
        executor->error_msg = string_create_from_cstr(buffer);
    else
    {
        string_clear(executor->error_msg);
        string_append_cstr(executor->error_msg, buffer);
    }
}

void exec_clear_error(miga_exec_t *executor)
{
    Expects_not_null(executor);
    if (executor->error_msg)
        string_clear(executor->error_msg);
}

/* ── Pipe statuses (PIPESTATUS / pipefail) ───────────────────────────────── */

int exec_get_pipe_status_count(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return (int)(executor->pipe_status_count);
}

const int *exec_get_pipe_statuses(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return executor->pipe_statuses;
}

void exec_reset_pipe_statuses(miga_exec_t *executor)
{
    Expects_not_null(executor);
    executor->pipe_status_count = 0;
}

/* ============================================================================
 * Job Control
 * ============================================================================ */

/* exec_job_state_t is defined in exec.h.
 * We use EXEC_JOB_RUNNING as a fallback for unknown internal states.
 */

/* ── Enumeration ─────────────────────────────────────────────────────────── */

size_t exec_get_job_count(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return job_store_count(executor->jobs);
}

size_t exec_get_job_ids(const miga_exec_t *executor, int *job_ids, size_t max_jobs)
{
    Expects_not_null(executor);
    Expects_not_null(job_ids);
    return job_store_get_job_ids(executor->jobs, job_ids, max_jobs);
}

int exec_get_current_job_id(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    // job_t *job_store_get_current(const job_store_t *store);
    job_t *current_job = job_store_get_current(executor->jobs);
    return current_job ? current_job->job_id : -1;
}

int exec_get_previous_job_id(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    job_t *previous_job = job_store_get_previous(executor->jobs);
    return previous_job ? previous_job->job_id : -1;
}

/* ── Per-job queries ─────────────────────────────────────────────────────── */

exec_job_state_t exec_job_get_state(const miga_exec_t *executor, int job_id)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
        return EXEC_JOB_DONE; /* no such job */
    switch (job->state)
    {
    case JOB_RUNNING:
        return EXEC_JOB_RUNNING;
    case JOB_STOPPED:
        return EXEC_JOB_STOPPED;
    case JOB_DONE:
        return EXEC_JOB_DONE;
    case JOB_TERMINATED:
        return EXEC_JOB_TERMINATED;
    default:
        return EXEC_JOB_RUNNING; /* unrecognized internal state */
    }
}

const char *exec_job_get_command_cstr(const miga_exec_t *executor, int job_id)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    return job ? string_cstr(job->command_line) : NULL;
}

bool exec_job_is_background(const miga_exec_t *executor, int job_id)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    return job ? job->is_background : false;
}

#ifdef MIGA_POSIX_API
pid_t exec_job_get_pgid(const miga_exec_t *executor, int job_id)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    return job ? job->pgid : -1;
}
#else
int exec_job_get_pgid(const miga_exec_t *executor, int job_id)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    return job ? job->pgid : -1;
}
#endif

/* ── Per-process queries within a job ────────────────────────────────────── */

size_t exec_job_get_process_count(const miga_exec_t *executor, int job_id)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    if (job)
        return job_process_count(job);
    return 0;
}

exec_job_state_t exec_job_get_process_state(const miga_exec_t *executor, int job_id, size_t index)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
        return EXEC_JOB_DONE; /* no such job */
    if (index >= job_process_count(job))
        return EXEC_JOB_DONE; /* no such job */
    intptr_t pid = job_get_process_pid(job, index);
    job_state_t proc_state = job_get_process_state(job, index);

    switch (proc_state)
    {
    case JOB_RUNNING:
        return EXEC_JOB_RUNNING;
    case JOB_STOPPED:
        return EXEC_JOB_STOPPED;
    case JOB_DONE:
        return EXEC_JOB_DONE;
    case JOB_TERMINATED:
        return EXEC_JOB_TERMINATED;
    default:
        return EXEC_JOB_RUNNING; /* unrecognized internal state */
    }
}

int exec_job_get_process_exit_status(const miga_exec_t *executor, int job_id, size_t index)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
        return -1;
    if (index >= job_process_count(job))
        return -1;
    return job_get_process_exit_status(job, index);
}

#ifdef MIGA_POSIX_API
pid_t exec_job_get_process_pid(const miga_exec_t *executor, int job_id, size_t index)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
        return -1;
    if (index >= job_process_count(job))
        return -1;
    return job_get_process_pid(job, index);
}
#elif defined(MIGA_UCRT_API)
int exec_job_get_process_pid(const miga_exec_t *executor, int job_id, size_t index)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
        return -1;
    if (index >= job_process_count(job))
        return -1;
    return (int)job_get_process_pid(job, index);
}

uintptr_t exec_job_get_process_handle(const miga_exec_t *executor, int job_id, size_t index)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
        return (uintptr_t)-1;
    if (index >= job_process_count(job))
        return (uintptr_t)-1;
#ifdef MIGA_UCRT_API
    return job_get_process_handle(job, index);
#else
    return (intptr_t)-1;
#endif
}
#else
int exec_job_get_process_pid(const miga_exec_t *executor, int job_id, size_t index)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
        return -1;
    if (index >= job_process_count(job))
        return -1;
    return (int)job_get_process_pid(job, index);
}
#endif

/* ── Job actions ─────────────────────────────────────────────────────────── */

bool exec_job_foreground_cstr(miga_exec_t *executor, int job_id, char **out_cmd)
{
    Expects_not_null(executor);

    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
    {
        exec_set_error_cstr(executor, "fg: no such job");
        return false;
    }

    if (job->state == JOB_STOPPED)
    {
#ifdef MIGA_POSIX_API
        if (kill(-job->pgid, SIGCONT) < 0)
        {
            exec_set_error_printf(executor, "fg: cannot resume job: %s", strerror(errno));
            return false;
        }
        job_store_set_state(executor->jobs, job_id, JOB_RUNNING);
#else
        exec_set_error_cstr(executor, "fg: cannot resume suspended jobs on this platform");
        return false;
#endif
    }

#ifdef MIGA_POSIX_API
    if (tcsetpgrp(STDIN_FILENO, job->pgid) < 0)
    {
        exec_set_error_printf(executor, "fg: cannot set foreground process group: %s",
                              strerror(errno));
        return false;
    }

    job->is_background = false;
    if (out_cmd)
        *out_cmd = xstrdup(string_cstr(job->command_line));

    job_store_remove(executor->jobs, job_id);
    return true;
#else
    exec_set_error_cstr(executor, "fg: foreground job control not supported on this platform");
    return false;
#endif
}

bool exec_job_background(miga_exec_t *executor, int job_id)
{
    Expects_not_null(executor);

    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
    {
        exec_set_error_cstr(executor, "bg: no such job");
        return false;
    }

    if (job->state == JOB_STOPPED)
    {
#ifdef MIGA_POSIX_API
        if (kill(-job->pgid, SIGCONT) < 0)
        {
            exec_set_error_printf(executor, "bg: cannot resume job: %s", strerror(errno));
            return false;
        }
        job_store_set_state(executor->jobs, job_id, JOB_RUNNING);
#else
        exec_set_error_cstr(executor, "bg: cannot resume suspended jobs on this platform");
        return false;
#endif
    }

    job->is_background = true;
    return true;
}

bool exec_job_kill(miga_exec_t *executor, int job_id, int sig)
{
    Expects_not_null(executor);

    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
    {
        exec_set_error_cstr(executor, "kill: no such job");
        return false;
    }

    // Special case: sig == 0 → POSIX check existence/permissions, no signal sent
    if (sig == 0)
    {
        // On POSIX: kill(-pgid, 0) succeeds if we can signal the group
#ifdef MIGA_POSIX_API
        if (kill(-job->pgid, 0) < 0)
        {
            exec_set_error_printf(executor, "kill: job %d: no such process group: %s", job_id,
                                  strerror(errno));
            return false;
        }
        return true;
#else
        // On Windows: we can only check if handle is still valid/open
        // If you store HANDLE hProcess in job_t from _spawnvpe:
        // if (!job->hProcess || WaitForSingleObject(job->hProcess, 0) == WAIT_OBJECT_0) {
        //     exec_set_error(executor, "kill: job %d no longer exists", job_id);
        //     return false;
        // }
        // For simplicity: assume existence if still in table
        return true;
#endif
    }

    // Normal case: send real signal
#ifdef MIGA_POSIX_API
    // Send to the entire process group (negative pgid)
    if (kill(-job->pgid, sig) < 0)
    {
        // Common errors: ESRCH (no such process), EPERM (permission denied)
        exec_set_error_printf(executor, "kill: cannot send signal %d to job %d: %s", sig, job_id,
                              strerror(errno));
        return false;
    }

    return true;

#elif defined(MIGA_UCRT_API)
    // Very limited: only attempt forceful termination for common "kill" signals
    if (sig == SIGTERM
#ifdef SIGKILL
        || sig == SIGKILL
#endif
        || sig == 15 || sig == 9)
    {
        // Each job has a list of process_t, but, in UCRT, there will
        // only ever be one entry. We can get the handle from the process.
        if (job->processes)
        {
            unsigned long long handle;
            // Using the shortcut that we know there can only be one process
            // in a UCRT job.
            handle = job->processes->handle;
            if (handle && TerminateProcess((HANDLE)handle, 1))
            {
                CloseHandle((HANDLE)handle);
                job->processes->handle = 0;
            }
            else
            {
                exec_set_error_printf(executor, "kill: failed to terminate job %d on Windows",
                                      job_id);
                return false;
            }
        }
        // Have to update job store here, since we won't catch a signal or status change from the
        // process itself.
        job_store_set_state(executor->jobs, job_id, JOB_TERMINATED);
    }
    else
    {
        exec_set_error_cstr(
            executor, "kill: only SIGTERM/SIGKILL supported on Windows (job control limited)");
        return false;
    }

#else
    exec_set_error_cstr(executor, "kill: job control not supported on this platform");
#endif
    return false;
}

/* exec_jobs_format_t is defined in exec.h */

/**
 * Parse a job-ID specifier from a string.
 * Accepts: %n, %+, %%, %-, or a plain number.
 *
 * @return Job ID on success, -1 on error.
 */
int exec_parse_job_id_cstr(const miga_exec_t *executor, const char *spec)
{
    Expects_not_null(executor);
    Expects_not_null(spec);
    if (spec[0] == '%')
    {
        if (strcmp(spec, "%+") == 0 || strcmp(spec, "%%") == 0)
        {
            return exec_get_current_job_id(executor);
        }
        else if (strcmp(spec, "%-") == 0)
        {
            return exec_get_previous_job_id(executor);
        }
        else
        {
            // %n where n is a number
            char *endptr;
            long id = strtol(spec + 1, &endptr, 10);
            if (*endptr != '\0' || id <= 0)
            {
                exec_set_error_printf((miga_exec_t *)executor, "Invalid job specifier: %s", spec);
                return -1;
            }
            return (int)id;
        }
    }
    else
    {
        // Plain number
        char *endptr;
        long id = strtol(spec, &endptr, 10);
        if (*endptr != '\0' || id <= 0)
        {
            exec_set_error_printf((miga_exec_t *)executor, "Invalid job ID: %s", spec);
            return -1;
        }
        return (int)id;
    }
}

int exec_parse_job_id(const miga_exec_t* executor, const string_t* spec)
{
    Expects_not_null(executor);
    Expects_not_null(spec);
    return exec_parse_job_id_cstr(executor, string_cstr(spec));
}

/**
 * Print a single job.
 *
 * @return true if the job was found and printed.
 */
bool exec_print_job_by_id(const miga_exec_t *executor, int job_id, exec_jobs_format_t format,
                          FILE *output)
{
    Expects_not_null(executor);
    //         const char *state_str = job_state_to_string(job->state);
    // fprintf(output, "[%d] %s\t%s\n", job->job_id, state_str,
    //        job->command_line ? string_cstr(job->command_line) : "(no command)");
    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
    {
        exec_set_error_printf((miga_exec_t *)executor, "No such job: %d", job_id);
        return false;
    }
    fprintf(output, "[%d] ", job->job_id);
    switch (format)
    {
    case EXEC_JOBS_FORMAT_DEFAULT:
        fprintf(output, "%s\t%s\n", job_state_to_string(job->state),
                job->command_line ? string_cstr(job->command_line) : "(no command)");
        break;
    case EXEC_JOBS_FORMAT_LONG:
        fprintf(output, "%s\tPGID: %d\t%s\n", job_state_to_string(job->state), job->pgid,
                job->command_line ? string_cstr(job->command_line) : "(no command)");
        break;
    case EXEC_JOBS_FORMAT_PID_ONLY:
        fprintf(output, "PGID: %d\n", job->pgid);
        break;
    default:
        fprintf(output, "%s\t%s\n", job_state_to_string(job->state),
                job->command_line ? string_cstr(job->command_line) : "(no command)");
        break;
    }
    return true;
}

/**
 * Print all jobs.
 */
void exec_print_all_jobs(const miga_exec_t *executor, exec_jobs_format_t format, FILE *output)
{
    Expects_not_null(executor);
    Expects_not_null(output);

    job_store_t *store = executor->jobs;
    for (const job_t *job = store->jobs; job; job = job->next)
    {
        const char *state_str = job_state_to_string(job->state);
        switch (format)
        {
        case EXEC_JOBS_FORMAT_DEFAULT:
            fprintf(output, "[%d] %s\t%s\n", job->job_id, state_str,
                    job->command_line ? string_cstr(job->command_line) : "(no command)");
            break;
        case EXEC_JOBS_FORMAT_LONG:
            fprintf(output, "[%d] %s\tPGID: %d\t%s\n", job->job_id, state_str, job->pgid,
                    job->command_line ? string_cstr(job->command_line) : "(no command)");
            break;
        case EXEC_JOBS_FORMAT_PID_ONLY:
            fprintf(output, "[%d] PGID: %d\n", job->job_id, job->pgid);
            break;

        default:
            fprintf(output, "[%d] %s\t%s\n", job->job_id, state_str,
                    job->command_line ? string_cstr(job->command_line) : "(no command)");
        }
    }
}

/**
 * Prints all jobs and processes in the verbose debug format.
 */
void exec_print_jobs_verbose(const miga_exec_t *executor, FILE *output)
{
    Expects_not_null(executor);
    Expects_not_null(output);
    job_store_print_jobs(executor->jobs, output);
}

/**
 * Check whether any jobs exist.
 */
bool exec_has_jobs(const miga_exec_t *executor)
{
    Expects_not_null(executor);
    return job_store_count(executor->jobs) > 0;
}

/* ============================================================================
 * Job Control
 * ============================================================================ */

void exec_reap_background_jobs(miga_exec_t *executor, bool notify)
{
    Expects_not_null(executor);

#ifdef MIGA_POSIX_API
    int status;
    pid_t pid;
    bool any_reaped = false;

    /* -1 and WNOHANG mean we return the PID of any completed child process or -1
     * if no child process has recently completed */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        // Determine if the job is done, or if it was terminated by a signal
        bool terminated = WIFSIGNALED(status);
        int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        if (terminated)
            job_store_set_process_state(executor->jobs, pid, JOB_TERMINATED, exit_status);
        else
            job_store_set_process_state(executor->jobs, pid, JOB_DONE, exit_status);
        any_reaped = true;
    }
    if (any_reaped && notify)
    {
        job_store_print_completed_jobs(executor->jobs, stdout);
    }
    if (any_reaped)
        job_store_remove_completed(executor->jobs);
#elifdef MIGA_UCRT_API
    bool any_completed = false;
    job_store_t *store = executor->jobs;
    job_process_iterator_t iter = job_store_active_processes_begin(store);

    while (job_store_active_processes_next(&iter))
    {
        uintptr_t h = job_store_iter_get_handle(&iter);
        if (!h)
        {
            // Unreachable?
            job_store_iter_set_state(&iter, JOB_DONE, 0); // Return 0 as exit code?

            // Check if job is now complete
            job_state_t state = job_store_iter_get_job_state(&iter);
            if (state == JOB_DONE || state == JOB_TERMINATED)
                any_completed = true;
        }
        else if (!WaitForSingleObject((HANDLE)h, (DWORD)0))
        {
            DWORD exit_code;
            GetExitCodeProcess((HANDLE)h, &exit_code);
            job_store_iter_set_state(&iter, JOB_DONE, (int)exit_code);

            // Check if job is now complete
            job_state_t state = job_store_iter_get_job_state(&iter);
            if (state == JOB_DONE || state == JOB_TERMINATED)
                any_completed = true;
        }
    }
    if (any_completed && notify)
        job_store_print_completed_jobs(executor->jobs, stdout);
    if (any_completed)
        job_store_remove_completed(executor->jobs);
#endif
}

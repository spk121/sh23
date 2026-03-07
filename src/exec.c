/**
 * exec.c - Shell executor implementation
 *
 * This file implements the public executor API and high-level execution.
 * Frame management and policy-driven execution is in exec_frame.c.
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <ctype.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "exec.h"
#include "exec_internal.h"

#include "alias_store.h"
#include "ast.h"
#include "exec_expander.h"
#include "exec_frame.h"
#include "exec_redirect.h"
#include "fd_table.h"
#include "frame.h"
#include "func_store.h"
#include "glob_util.h"
#include "gnode.h"
#include "job_store.h"
#include "lexer.h"
#include "logging.h"
#include "lower.h"
#include "parser.h"
#include "positional_params.h"
#include "sig_act.h"
#include "string_list.h"
#include "token.h"
#include "tokenizer.h"
#include "trap_store.h"
#include "variable_store.h"
#include "xalloc.h"

#ifdef POSIX_API
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef UCRT_API
#if defined(_WIN64)
#define _AMD64_
#elif defined(_WIN32)
#define _X86_
#endif
#include <io.h>
#include <process.h>
#include <processthreadsapi.h>   // GetProcessId, OpenProcess, GetExitCodeProcess, etc.
#include <synchapi.h>            // WaitForSingleObject
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define EXECUTOR_ERROR_BUFFER_SIZE 512

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

exec_result_t exec_execute_dispatch(exec_frame_t *frame, const ast_node_t *node);
exec_result_t exec_while_clause(exec_frame_t *frame, ast_node_t *node);
exec_result_t exec_for_clause(exec_frame_t *frame, ast_node_t *node);
exec_result_t exec_function_def_clause(exec_frame_t *frame, ast_node_t *node);
exec_result_t exec_redirected_command(exec_frame_t *frame, ast_node_t *node);
exec_result_t exec_case_clause(exec_frame_t *frame, ast_node_t *node);

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static void source_rc_files(struct exec_t *e)
{
    // FIXME
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
#ifdef POSIX_API
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

struct exec_t *exec_create(void)
{
    return xcalloc(1, sizeof(struct exec_t));
}

void exec_destroy(exec_t **executor_ptr)
{
    if (!executor_ptr || !*executor_ptr)
        return;

    exec_t *e = *executor_ptr;

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
    if (e->tokenizer)
        tokenizer_destroy(&e->tokenizer);
    if (e->traps)
        trap_store_destroy(&e->traps);
    if (e->original_signals)
        sig_act_store_destroy(&e->original_signals);

    job_store_destroy(&e->jobs);

#if defined(POSIX_API) || defined(UCRT_API)
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

bool exec_is_args_set(const exec_t *executor)
{
    return executor->argc > 0 && executor->argv != NULL;
}

char* const* exec_get_args(const exec_t* executor, int* argc_out)
{
    Expects_not_null(executor);

    if (!exec_is_args_set(executor))
        return NULL;
    if (argc_out)
        *argc_out = executor->argc;
    return executor->argv;
}

bool exec_set_args(exec_t* executor, int argc, char* const* argv)
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

bool exec_is_envp_set(const exec_t* executor)
{
    Expects_not_null(executor);

    return executor->envp != NULL;
}

char* const* exec_get_envp(const exec_t* executor)
{
    Expects_not_null(executor);

    return executor->envp;
}

bool exec_set_envp(exec_t* executor, char* const* envp)
{
    Expects_not_null(executor);

    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->envp = envp;
    return true;
}

/* ── Shell identity ──────────────────────────────────────────────────────── */

bool exec_is_shell_name_set(const exec_t* executor)
{
    Expects_not_null(executor);

    return executor->shell_name != NULL;
}

const char *exec_get_shell_name(const exec_t *executor)
{
    Expects_not_null(executor);

    if (!executor->shell_name)
        return NULL;
    return string_cstr(executor->shell_name);
}

bool exec_set_shell_name(exec_t* executor, const char* shell_name)
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

bool exec_get_flag_allexport(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->opt.allexport;
}

bool exec_set_flag_allexport(exec_t* executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.allexport = value;
    return true;
}

bool exec_get_flag_errexit(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->opt.errexit;
}

bool exec_set_flag_errexit(exec_t* executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.errexit = value;
    return true;
}

bool exec_get_flag_ignoreeof(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->opt.ignoreeof;
}

bool exec_set_flag_ignoreeof(exec_t* executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.ignoreeof = value;
    return true;
}

bool exec_get_flag_noclobber(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->opt.noclobber;
}

bool exec_set_flag_noclobber(exec_t* executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.noclobber = value;
    return true;
}

bool exec_get_flag_noglob(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->opt.noglob;
}

bool exec_set_flag_noglob(exec_t* executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.noglob = value;
    return true;
}

bool exec_get_flag_noexec(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->opt.noexec;
}

bool exec_set_flag_noexec(exec_t* executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.noexec = value;
    return true;
}

bool exec_get_flag_nounset(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->opt.nounset;
}

bool exec_set_flag_nounset(exec_t* executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.nounset = value;
    return true;
}

bool exec_get_flag_pipefail(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->opt.pipefail;
}

bool exec_set_flag_pipefail(exec_t* executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.pipefail = value;
    return true;
}

bool exec_get_flag_verbose(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->opt.verbose;
}

bool exec_set_flag_verbose(exec_t* executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.verbose = value;
    return true;
}

bool exec_get_flag_vi(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->opt.vi;
}

bool exec_set_flag_vi(exec_t* executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.vi = value;
    return true;
}

bool exec_get_flag_xtrace(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->opt.xtrace;
}

bool exec_set_flag_xtrace(exec_t* executor, bool value)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->opt.xtrace = value;
    return true;
}

/* ── Interactive / login mode ────────────────────────────────────────────── */

bool exec_get_is_interactive(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->is_interactive;
}

bool exec_set_is_interactive(exec_t* executor, bool is_interactive)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->is_interactive = is_interactive;
    return true;
}

bool exec_get_is_login_shell(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->is_login_shell;
}

bool exec_set_is_login_shell(exec_t* executor, bool is_login_shell)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->is_login_shell = is_login_shell;
    return true;
}

/* ── Job control ─────────────────────────────────────────────────────────── */

bool exec_get_job_control_disabled(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->job_control_disabled;
}

bool exec_set_job_control_disabled(exec_t* executor, bool disabled)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->job_control_disabled = disabled;
    return true;
}

/* ── Working directory ───────────────────────────────────────────────────── */

bool exec_is_working_directory_set(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->working_directory != NULL;
}

const char* exec_get_working_directory(const exec_t* executor)
{
    Expects_not_null(executor);
    if (!executor->working_directory)
        return NULL;
    return string_cstr(executor->working_directory);
}

bool exec_set_working_directory(exec_t* executor, const char* path)
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

bool exec_is_umask_set(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->umask != 0;
}

int exec_get_umask(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->umask;
}

bool exec_set_umask(exec_t* executor, int mask)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->umask = mask;
    return true;
}

#ifdef POSIX_API
mode_t exec_get_umask_posix(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->umask;
}

bool exec_set_umask_posix(exec_t* executor, mode_t mask)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->umask = mask;
    return true;
}

bool exec_is_file_size_limit_set(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->file_size_limit != 0;
}

rlim_t exec_get_file_size_limit(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->file_size_limit;
}

bool exec_set_file_size_limit(exec_t* executor, rlim_t limit)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->file_size_limit = limit;
    return true;
}
#endif

/* ── Process identity ────────────────────────────────────────────────────── */

bool exec_is_process_group_set(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->pgid_valid;
}

int exec_get_process_group(const exec_t* executor)
{
    Expects_not_null(executor);
    if (!executor->pgid_valid)
        return -1;
    return executor->pgid;
}

bool exec_set_process_group(exec_t *executor, int pgid)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->pgid = pgid;
    executor->pgid_valid = true;
    return true;
}

bool exec_is_shell_pid_set(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->shell_pid_valid;
}

int exec_get_shell_pid(const exec_t* executor)
{
    Expects_not_null(executor);
    if (!executor->shell_pid_valid)
        return -1;
    return executor->shell_pid;
}

bool exec_set_shell_pid(exec_t* executor, int pid)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->shell_pid = pid;
    executor->shell_pid_valid = true;
    return true;
}

bool exec_is_shell_ppid_set(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->shell_ppid_valid;
}

int exec_get_shell_ppid(const exec_t* executor)
{
    Expects_not_null(executor);
    if (!executor->shell_ppid_valid)
        return -1;
    return executor->shell_ppid;
}

bool exec_set_shell_ppid(exec_t* executor, int ppid)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->shell_ppid = ppid;
    executor->shell_ppid_valid = true;
    return true;
}

/* ── RC file control ─────────────────────────────────────────────────────── */

bool exec_get_inhibit_rc_files(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->inhibit_rc_files;
}

bool exec_set_inhibit_rc_files(exec_t* executor, bool inhibit)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->inhibit_rc_files = inhibit;
    return true;
}

bool exec_is_system_rc_filename_set(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->system_rc_filename != NULL;
}

const char* exec_get_system_rc_filename(const exec_t* executor)
{
    Expects_not_null(executor);
    if (!executor->system_rc_filename)
        return NULL;
    return string_cstr(executor->system_rc_filename);
}

bool exec_set_system_rc_filename(exec_t* executor, const char* filename)
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

bool exec_is_user_rc_filename_set(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->user_rc_filename != NULL;
}

const char* exec_get_user_rc_filename(const exec_t* executor)
{
    Expects_not_null(executor);
    if (!executor->user_rc_filename)
        return NULL;
    return string_cstr(executor->user_rc_filename);
}

bool exec_set_user_rc_filename(exec_t* executor, const char* filename)
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

int exec_get_last_exit_status(const exec_t *executor)
{
    Expects_not_null(executor);
    if (!executor->last_exit_status_set)
        return -1;
    return executor->last_exit_status;
}

bool exec_set_last_exit_status(exec_t* executor, int status)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->last_exit_status = status;
    executor->last_exit_status_set = true;
    return true;
}

int exec_get_last_background_pid(const exec_t* executor)
{
    Expects_not_null(executor);
    if (!executor->last_background_pid_set)
        return -1;
    return executor->last_background_pid;
}

bool exec_set_last_background_pid(exec_t* executor, int pid)
{
    Expects_not_null(executor);
    if (exec_is_top_frame_initialized(executor))
        return false;
    executor->last_background_pid = pid;
    executor->last_background_pid_set = true;
    return true;
}

const char* exec_get_last_argument(const exec_t* executor)
{
    Expects_not_null(executor);
    if (!executor->last_argument_set)
        return NULL;
    return string_cstr(executor->last_argument);
}

bool exec_set_last_argument(exec_t* executor, const char* arg)
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

bool exec_is_top_frame_initialized(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->top_frame_initialized;
}

exec_frame_t* exec_get_current_frame(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->current_frame;
}

/* ============================================================================
 * Builtin Registration (delegates to builtin_store)
 * ============================================================================ */

bool exec_register_builtin(exec_t *executor, const char *name, exec_builtin_fn_t fn,
                           exec_builtin_category_t category)
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
    builtin_category_t internal_cat =
        (category == EXEC_BUILTIN_SPECIAL) ? BUILTIN_SPECIAL : BUILTIN_REGULAR;

    return builtin_store_set(executor->builtins, name, (builtin_fn_t)fn, internal_cat);
}

bool exec_unregister_builtin(exec_t *executor, const char *name)
{
    Expects_not_null(executor);

    if (!name || !executor->builtins)
        return false;

    return builtin_store_remove(executor->builtins, name);
}

bool exec_has_builtin(const exec_t *executor, const char *name)
{
    Expects_not_null(executor);

    if (!name || !executor->builtins)
        return false;

    return builtin_store_has(executor->builtins, name);
}

exec_builtin_fn_t exec_get_builtin(const exec_t *executor, const char *name)
{
    Expects_not_null(executor);

    if (!name || !executor->builtins)
        return NULL;

    return (exec_builtin_fn_t)builtin_store_get(executor->builtins, name);
}

bool exec_get_builtin_category(const exec_t *executor, const char *name,
                               exec_builtin_category_t *category_out)
{
    Expects_not_null(executor);

    if (!name || !executor->builtins)
        return false;

    builtin_category_t internal_cat;
    bool found = builtin_store_lookup(executor->builtins, name, NULL, &internal_cat);
    if (found && category_out)
    {
        *category_out =
            (internal_cat == BUILTIN_SPECIAL) ? EXEC_BUILTIN_SPECIAL : EXEC_BUILTIN_REGULAR;
    }

    return found;
}

/* ============================================================================
 * Execution Setup
 * ============================================================================ */

static exec_status_t exec_setup_core(exec_t* e, bool interactive)
{
    Expects_not_null(e);

    if (e->top_frame_initialized)
        return EXEC_ERROR;

    // Finish initializing the top-level parameters that weren't explicitly set by the caller.
    // Override all the frame parameters with the executor's pre-configured values.
#ifdef POSIX_API
    pid_t default_pid = getpid();
    pid_t default_ppid = getppid();
    bool default_pid_valid = true;
    bool default_ppid_valid = true;
#elifdef UCRT_API
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
        bool default_is_login_shell = (e->argc > 0 && e->argv && e->argv[0] && e->argv[0][0] == '-');
        e->is_login_shell = default_is_login_shell;
    }

    /* Install default signal handlers for interactive mode */
    if (!e->signals_installed)
    {
        sig_act_store_t *original_signals = sig_act_store_create();
        sig_act_store_install_default_signal_handlers(original_signals);
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

#ifdef POSIX_API
    if (!e->pgid_valid)
    {
        pid_t pgid = getpgrp();
        if (pgid != -1)
        {
            e->pgid = pgid;
            e->pgid_valid = true;
        }
    }
#elifdef UCRT_API
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

    // e->env_vars is only used for debugging and for keeping a permanent record of the
    // initial environment variables.
    // e->envp is the source of the initial environment variables for the top frame when the
    // top frame is initialized.  It is not used after the top frame is initialized.
    // 
    // N.B. If e->variables is set, e->variables, rather than e->envp, becomes the source of the initial
    // environment variables for the top frame when the top frame is initialized.
    if (e->envp)
        e->env_vars = string_list_create_from_cstr_array((const char **)e->envp, -1);
    else
        e->env_vars = string_list_create_from_system_env();

    if (!e->top_frame)
    {
        // This initialization is very involved. Lots of things happen in exec_frame_create_top_level()
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
        //   discarded and the frame points to e->positional_params instead. This allows the caller to
        //   pre-populate the positional parameters with custom values before the top frame is initialized.
        // - similarly functions
        // - similarly aliases
        // - similarly traps
        // - similarly open fds
        // - similarly working directory
        // - similarly umask
        // - the options are *always* overridden with the executor's opt, since the executor's opt is
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
        exec_status_t ret = source_rc_files(e, interactive);
        e->rc_loaded = ret == EXEC_OK;
        if (e->rc_loaded)
            log_debug("RC files loaded successfully.");
        else
            log_debug("RC file loading failed.");
        return ret;
    }
    else
        log_debug("Skipped loading RC files");

    return EXEC_OK
}

exec_status_t exec_setup_interactive(exec_t *executor)
{
    return exec_setup_core(executor, true);
}

exec_status_t exec_setup_noninteractive(exec_t *executor)
{
    return exec_setup_core(executor, false);
}

/**
 * A fallback in case an integrator failed to call either
 * exec_setup_interactive() or exec_setup_noninteractive().
 * This function attempts to guess the mode based on the provided file pointer.
 * @param executor The executor instance to set up.
 * @param fp The file pointer to use for guessing interactivity.
 * @return EXEC_OK if setup was successful, otherwise EXEC_ERROR.
 */
static exec_status_t exec_setup_lazy(exec_t* executor, FILE *fp)
{
    Expects_not_null(executor);
    if (executor->top_frame_initialized)
        return EXEC_ERROR;
    // Don't know if this is interactive or not.
    // Let's try to guess.

    log_warn("Executor setup was not explicitly called. Attempting to guess interactive mode based "
             "on file pointer.");
    bool is_interactive;
#ifdef POSIX_API
    is_interactive = isatty(fileno(fp));
#elifdef UCRT_API
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
    return EXEC_ERROR;
}

#if 0
struct exec_t *exec_create(const struct exec_cfg_t *cfg)
{
    struct exec_t *e = xcalloc(1, sizeof(struct exec_t));

    /* The singleton exec stores
     * 1. Singleton values
     * 2. Info required to initialize the top-level frame. The top-level
     *    frame is lazily initialized on the first call to the execution.
     */

    /* -------------------------------------------------------------------------
     * Shell Identity — PID / PPID
     * -------------------------------------------------------------------------
     */
#ifdef POSIX_API
    pid_t default_pid = getpid();
    pid_t default_ppid = getppid();
    bool default_pid_valid = true;
    bool default_ppid_valid = true;
#elifdef UCRT_API
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
        e->shell_args = string_list_create_from_cstr_array((const char **)e->argv + 1, e->argc - 1);
    }
    else
    {
        e->shell_args = string_list_create();
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
        e->env_vars = string_list_create_from_cstr_array((const char **)cfg->envp, -1);
    }
    else
    {
        e->env_vars = string_list_create_from_system_env();
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
#ifdef POSIX_API
    bool default_is_interactive = isatty(STDIN_FILENO);
#elifdef UCRT_API
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
    
#ifdef POSIX_API
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
     * These stores are owned by exec_t until the top frame is created,
     * at which point ownership transfers to the frame.
     */
    e->variables = NULL;
    e->local_variables = NULL;
    e->positional_params = NULL;
    e->functions = NULL;
    e->aliases = alias_store_create();
    e->traps = NULL;

#if defined(POSIX_API) || defined(UCRT_API)
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

#ifdef POSIX_API
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
     * Tokenizer (created lazily on first use)
     * -------------------------------------------------------------------------
     */
    e->tokenizer = NULL;

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

int exec_get_exit_status(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->last_exit_status;
}

void exec_set_exit_status(exec_t *executor, int status)
{
    Expects_not_null(executor);
    executor->last_exit_status = status;
    executor->last_exit_status_set = true;
}

const char *exec_get_error(const exec_t *executor)
{
    Expects_not_null(executor);
    if (string_length(executor->error_msg) == 0)
        return NULL;
    return string_data(executor->error_msg);
}

void exec_set_error(exec_t *executor, const char *format, ...)
{
    Expects_not_null(executor);
    Expects_not_null(format);

    va_list args;
    va_start(args, format);

    char buffer[EXECUTOR_ERROR_BUFFER_SIZE];
    vsnprintf(buffer, sizeof(buffer), format, args);

    va_end(args);

    string_clear(executor->error_msg);
    string_append_cstr(executor->error_msg, buffer);
}

void exec_clear_error(exec_t *executor)
{
    Expects_not_null(executor);
    string_clear(executor->error_msg);
}

const char *exec_get_ps1(const exec_t *executor)
{
    Expects_not_null(executor);
    /* We expect to use the current frame's variable store, but
     * if called too early, there may only be a variable store
     * at top-level, or perhaps not at all */
    if (executor->current_frame && executor->current_frame->variables)
        {
        const char *ps1 =
            variable_store_get_value_cstr(executor->current_frame->variables, "PS1");
        if (ps1 && *ps1)
            return ps1;
    }
    else if (executor->variables)
    {
        const char *ps1 =
            variable_store_get_value_cstr(executor->variables, "PS1");
        if (ps1 && *ps1)
            return ps1;
    }

    return "$ ";
}

/* Forward declarations — defined after frame_render_ps1 below. */
static const char *ps1_lookup_variable(const exec_frame_t *frame, const char *name);

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
char *frame_render_ps1(const exec_frame_t *frame)
{
    Expects_not_null(frame);

    /* -------------------------------------------------------------------------
     * 1. Retrieve the raw PS1 value.
     *
     * Check the frame's variable store first (covers local overrides), then
     * fall back to the executor-level store (covers the normal case where the
     * top frame owns all variables). If PS1 is unset or empty, use the POSIX
     * default of "$ ".
     * -------------------------------------------------------------------------
     */
    const char *ps1_raw = NULL;
    if (frame->variables)
    {
        const char *v = variable_store_get_value_cstr(frame->variables, "PS1");
        if (v && *v)
            ps1_raw = v;
    }
    if (!ps1_raw && frame->executor && frame->executor->variables)
    {
        const char *v = variable_store_get_value_cstr(frame->executor->variables, "PS1");
        if (v && *v)
            ps1_raw = v;
    }
    if (!ps1_raw)
        ps1_raw = "$ ";

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

    const char *p = ps1_raw;
    while (*p)
    {
        /* ------------------------------------------------------------------
         * Backslash escape sequences (non-POSIX extension).
         * Processed before '$' so that e.g. "\\$HOME" emits a literal
         * backslash then expands HOME, not a literal backslash-dollar-HOME.
         * ------------------------------------------------------------------ */
        if (*p == '\\')
        {
            p++; /* consume '\\' */
            switch (*p)
            {
            case 'n':
                string_append_char(out, '\n');
                p++;
                break;
            case 'r':
                string_append_char(out, '\r');
                p++;
                break;
            case 't':
                string_append_char(out, '\t');
                p++;
                break;
            case '\\':
                string_append_char(out, '\\');
                p++;
                break;

            case 'x': {
                /* \xhh — exactly 2 hex digits, emitted as a UTF-8 byte.
                 * We interpret the value as a Unicode code point in the
                 * range U+0000..U+00FF (Latin-1 supplement), which maps
                 * trivially to its UTF-8 representation.  Fewer than 2
                 * valid hex digits: copy the escape literally. */
                p++; /* consume 'x' */
                char hi = *p, lo = p[1];
                if (isxdigit((unsigned char)hi) && isxdigit((unsigned char)lo))
                {
                    char hex[3] = {hi, lo, '\0'};
                    uint32_t cp = (uint32_t)strtoul(hex, NULL, 16);
                    string_append_utf8(out, cp);
                    p += 2;
                }
                else
                {
                    /* Not a valid escape — emit literally. */
                    string_append_cstr(out, "\\x");
                }
                break;
            }

            case 'u': {
                /* \uhhhh — exactly 4 hex digits, BMP Unicode code point. */
                p++; /* consume 'u' */
                char hex[5];
                int n = 0;
                while (n < 4 && isxdigit((unsigned char)p[n]))
                    hex[n] = p[n], n++;
                if (n == 4)
                {
                    hex[4] = '\0';
                    uint32_t cp = (uint32_t)strtoul(hex, NULL, 16);
                    string_append_utf8(out, cp);
                    p += 4;
                }
                else
                {
                    string_append_cstr(out, "\\u");
                }
                break;
            }

            case 'U': {
                /* \Uhhhhhh — exactly 6 hex digits, full Unicode range.
                 * (Unicode only needs 5.5 hex digits to cover U+10FFFF,
                 * but 6 gives a clean fixed-width field and matches common
                 * shell practice for emoji, e.g. \U01F600.) */
                p++; /* consume 'U' */
                char hex[7];
                int n = 0;
                while (n < 6 && isxdigit((unsigned char)p[n]))
                    hex[n] = p[n], n++;
                if (n == 6)
                {
                    hex[6] = '\0';
                    uint32_t cp = (uint32_t)strtoul(hex, NULL, 16);
                    string_append_utf8(out, cp);
                    p += 6;
                }
                else
                {
                    string_append_cstr(out, "\\U");
                }
                break;
            }

            case '\0':
                /* Trailing backslash at end of string — emit literally. */
                string_append_char(out, '\\');
                break;

            default:
                /* Unrecognised escape — emit literally so the user sees
                 * what they typed and can diagnose typos. */
                string_append_char(out, '\\');
                string_append_char(out, *p);
                p++;
                break;
            }
            continue;
        }

        if (*p != '$')
        {
            /* Ordinary character — copy verbatim. */
            string_append_char(out, *p++);
            continue;
        }

        /* '$' found — identify the parameter reference. */
        p++; /* consume '$' */

        if (*p == '{')
        {
            /* ${VAR} form */
            p++; /* consume '{' */
            const char *name_start = p;
            while (*p && *p != '}')
                p++;
            int name_len = (int)(p - name_start);
            if (*p == '}')
                p++; /* consume '}' */

            if (name_len > 0)
            {
                char name[256];
                if (name_len < (int)sizeof(name))
                {
                    memcpy(name, name_start, name_len);
                    name[name_len] = '\0';
                    const char *val = ps1_lookup_variable(frame, name);
                    if (val)
                        string_append_cstr(out, val);
                }
                /* Names that are too long are silently dropped per POSIX
                 * (undefined behaviour for extremely long names). */
            }
        }
        else if (*p == '?')
        {
            /* $? — last exit status */
            p++;
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", frame->last_exit_status);
            string_append_cstr(out, buf);
        }
        else if (*p == '$')
        {
            /* $$ — shell PID */
            p++;
            if (frame->executor && frame->executor->shell_pid_valid)
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", (int)frame->executor->shell_pid);
                string_append_cstr(out, buf);
            }
        }
        else if (*p == '_' || isalpha((unsigned char)*p))
        {
            /* $VAR — unbraced name */
            const char *name_start = p;
            while (*p == '_' || isalnum((unsigned char)*p))
                p++;
            int name_len = (int)(p - name_start);

            char name[256];
            if (name_len < (int)sizeof(name))
            {
                memcpy(name, name_start, name_len);
                name[name_len] = '\0';
                const char *val = ps1_lookup_variable(frame, name);
                if (val)
                    string_append_cstr(out, val);
            }
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

    return string_release(&out);
}

/**
 * Look up a variable for PS1 expansion.
 * Checks the frame's variable store first, then the executor-level store.
 * Returns the C string value, or NULL if not set.
 */
static const char *ps1_lookup_variable(const exec_frame_t *frame, const char *name)
{
    if (frame->variables)
    {
        const char *v = variable_store_get_value_cstr(frame->variables, name);
        if (v)
            return v;
    }
    if (frame->executor && frame->executor->variables)
    {
        const char *v = variable_store_get_value_cstr(frame->executor->variables, name);
        if (v)
            return v;
    }
    return NULL;
}

const char *exec_get_ps2(const exec_t *executor)
{
    Expects_not_null(executor);
    const char *ps2 = NULL;
    if (executor->current_frame && executor->current_frame->variables)
    {
        ps2 = variable_store_get_value_cstr(executor->current_frame->variables, "PS2");
        if (ps2 && *ps2)
            return ps2;
    }
    if (executor->variables)
    {
        ps2 = variable_store_get_value_cstr(executor->variables, "PS2");
    }
    return (ps2 && *ps2) ? ps2 : "> ";
}

positional_params_t *exec_get_positional_params(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->positional_params;
}

variable_store_t *exec_get_variables(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->variables;
}

alias_store_t *exec_get_aliases(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->aliases;
}

bool exec_is_interactive(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->is_interactive;
}

bool exec_is_login_shell(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->is_login_shell;
}

/* ============================================================================
 * Execution Functions
 * ============================================================================ */

void exec_setup_interactive_execute(exec_t *executor)
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

/**
 * Update the LINENO variable in the frame's variable store.
 *
 * POSIX says LINENO is set by the shell "to a decimal number representing the
 * current sequential line number (numbered starting with 1) within a script or
 * function before it executes each command."
 *
 * LINENO is a regular variable — the user can unset or reset it. If they do,
 * it will be recreated or updated again when the next command executes.
 *
 * We only update when we have a valid source line (> 0) from the AST, and
 * only when executing within a script or function context.
 */
static void exec_update_lineno(exec_frame_t *frame, const ast_node_t *node)
{
    if (!frame || !node)
        return;

    /* Only update for valid source lines (parser-assigned, 1-based) */
    if (node->first_line <= 0)
        return;

    /* Update the frame's internal line tracker */
    frame->source_line = node->first_line;

    /* Write LINENO into the variable store as a regular variable */
    variable_store_t *vars = exec_frame_get_variables(frame);
    if (!vars)
        return;

    if (variable_store_has_name_cstr(vars, "LINENO") &&
        variable_store_is_read_only_cstr(vars, "LINENO"))
        /* You can't make LINENO readonly, permanently. Muhuhuwahaha! */
        variable_store_set_read_only_cstr(vars, "LINENO", false);

    char buf[24];
    snprintf(buf, sizeof(buf), "%d", node->first_line);
    variable_store_add_cstr(vars, "LINENO", buf, false, false);
}

exec_result_t exec_execute_dispatch(exec_frame_t *frame, const ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    exec_result_t result;

    /* Update LINENO before executing this command */
    exec_update_lineno(frame, node);

    switch (node->type)
    {
    case AST_COMMAND_LIST:
        result = exec_compound_list(frame, (ast_node_t *)node);
        break;

    case AST_AND_OR_LIST:
        result = exec_and_or_list(frame, (ast_node_t *)node);
        break;

    case AST_PIPELINE:
        result = exec_pipeline(frame, (ast_node_t *)node);
        break;

    case AST_SIMPLE_COMMAND:
        result = exec_simple_command(frame, (ast_node_t *)node);
        break;

    case AST_SUBSHELL:
        result = exec_subshell(frame, node->data.compound.body);
        break;

    case AST_BRACE_GROUP:
        result = exec_brace_group(frame, node->data.compound.body, NULL);
        break;

    case AST_IF_CLAUSE:
        result = exec_if_clause(frame, (ast_node_t *)node);
        break;

    case AST_WHILE_CLAUSE:
    case AST_UNTIL_CLAUSE:
        result = exec_while_clause(frame, (ast_node_t *)node);
        break;

    case AST_FOR_CLAUSE:
        result = exec_for_clause(frame, (ast_node_t *)node);
        break;

    case AST_REDIRECTED_COMMAND:
        result = exec_redirected_command(frame, (ast_node_t *)node);
        break;

    case AST_CASE_CLAUSE:
        result = exec_case_clause(frame, (ast_node_t *)node);
        break;

    case AST_FUNCTION_DEF:
        result = exec_function_def_clause(frame, (ast_node_t *)node);
        break;

    default:
        exec_set_error(frame->executor, "Unsupported AST node type: %d %s", node->type, ast_node_type_to_string(node->type));
        result.status = EXEC_NOT_IMPL;
        return result;
    }

    /* Update executor's exit status */
    if (result.has_exit_status)
    {
        frame->executor->last_exit_status = result.exit_status;
        frame->executor->last_exit_status_set = true;
        frame->last_exit_status = result.exit_status;
    }

    return result;
}

/* ============================================================================
 * Stream Execution Core
 * ============================================================================ */

/**
 * Initialize the string execution context.
 */
exec_string_ctx_t *exec_string_ctx_create(void)
{
    exec_string_ctx_t *ctx = xcalloc(1, sizeof(exec_string_ctx_t));
    ctx->lexer = lexer_create();
    ctx->accumulated_tokens = NULL;
    ctx->line_num = 0;
    return ctx;
}

/**
 * Destroy the string execution context.
 */
void exec_string_ctx_destroy(exec_string_ctx_t **ctx_ptr)
{
    if (!ctx_ptr || !*ctx_ptr)
        return;

    exec_string_ctx_t *ctx = *ctx_ptr;

    if (ctx->lexer)
        lexer_destroy(&ctx->lexer);
    if (ctx->accumulated_tokens)
        token_list_destroy(&ctx->accumulated_tokens);

    xfree(ctx);
    *ctx_ptr = NULL;
}

/**
 * Reset the context after successful execution.
 */
void exec_string_ctx_reset(exec_string_ctx_t *ctx)
{
    if (ctx->lexer)
        lexer_reset(ctx->lexer);
    if (ctx->accumulated_tokens)
    {
        token_list_destroy(&ctx->accumulated_tokens);
        ctx->accumulated_tokens = NULL;
    }
}

/**
 * Core implementation for executing shell commands from a string.
 *
 * This function processes a single chunk of input (typically one line),
 * handling lexing, tokenization, parsing, and execution. It maintains
 * state in the provided context for handling multi-line constructs.
 *
 * @param frame The execution frame
 * @param input The input string to process
 * @param tokenizer The tokenizer for alias expansion and token processing
 * @param ctx The execution context (maintains lexer and accumulated tokens)
 * @return Status indicating success, need for more input, or error
 */
static exec_string_status_t exec_string_core(exec_frame_t *frame, const char *input,
                                             tokenizer_t *tokenizer, exec_string_ctx_t *ctx)
{
    Expects_not_null(frame);
    Expects_not_null(input);
    Expects_not_null(tokenizer);
    Expects_not_null(ctx);
    Expects_not_null(ctx->lexer);
    Expects_not_null(frame->executor);

    exec_t *executor = frame->executor;
    lexer_t *lx = ctx->lexer;

    ctx->line_num++;
    log_debug("exec_string_core: Processing line %d: %.*s", ctx->line_num,
              (int)strcspn(input, "\r\n"), input);

    lexer_append_input_cstr(lx, input);

    token_list_t *raw_tokens = token_list_create();
    lex_status_t lex_status = lexer_tokenize(lx, raw_tokens, NULL);

    if (lex_status == LEX_ERROR)
    {
        log_debug("exec_string_core: Lexer error at line %d", ctx->line_num);
        const char *err = lexer_get_error(lx);
        frame_set_error_printf(frame, "Lexer error: %s", err ? err : "unknown");
        token_list_destroy(&raw_tokens);
        return EXEC_STRING_ERROR;
    }

    if (lex_status == LEX_INCOMPLETE || lex_status == LEX_NEED_HEREDOC)
    {
        log_debug("exec_string_core: Lexer incomplete/heredoc at line %d", ctx->line_num);

        /* Check if any tokens were produced before the incomplete state.
         * If so, we should accumulate them for parsing when more input arrives. */
        if (token_list_size(raw_tokens) > 0)
        {
            log_debug("exec_string_core: Lexer produced %d tokens before becoming incomplete",
                      token_list_size(raw_tokens));

            /* Process the tokens that were produced */
            token_list_t *processed_tokens = token_list_create();
            tok_status_t tok_status = tokenizer_process(tokenizer, raw_tokens, processed_tokens);
            token_list_destroy(&raw_tokens);

            if (tok_status == TOK_ERROR)
            {
                log_debug("exec_string_core: Tokenizer error on incomplete line %d", ctx->line_num);
                const char *err = tokenizer_get_error(tokenizer);
                frame_set_error_printf(frame, "Tokenizer error: %s", err ? err : "unknown");
                token_list_destroy(&processed_tokens);
                return EXEC_STRING_ERROR;
            }

            if (tok_status == TOK_INCOMPLETE)
            {
                log_debug("exec_string_core: Tokenizer incomplete (compound command) during lexer incomplete at line %d", ctx->line_num);
                /* Tokens are buffered in tokenizer, continue to next line */
                token_list_destroy(&processed_tokens);
                return EXEC_STRING_INCOMPLETE;
            }

            /* Accumulate these tokens for when the lexer completes */
            if (ctx->accumulated_tokens == NULL)
            {
                ctx->accumulated_tokens = processed_tokens;
            }
            else
            {
                if (token_list_append_list_move(ctx->accumulated_tokens, &processed_tokens) != 0)
                {
                    log_debug("exec_string_core: Failed to append incomplete tokens");
                    return EXEC_STRING_ERROR;
                }
            }
        }
        else
        {
            token_list_destroy(&raw_tokens);
        }
        return EXEC_STRING_INCOMPLETE;
    }

    log_debug("exec_string_core: Lexer produced %d raw tokens at line %d",
              token_list_size(raw_tokens), ctx->line_num);

    token_list_t *processed_tokens = token_list_create();
    tok_status_t tok_status = tokenizer_process(tokenizer, raw_tokens, processed_tokens);
    token_list_destroy(&raw_tokens);

    if (tok_status == TOK_ERROR)
    {
        log_debug("exec_string_core: Tokenizer error at line %d", ctx->line_num);
        const char *err = tokenizer_get_error(tokenizer);
        frame_set_error_printf(frame, "Tokenizer error: %s", err ? err : "unknown");
        token_list_destroy(&processed_tokens);
        return EXEC_STRING_ERROR;
    }

    if (tok_status == TOK_INCOMPLETE)
    {
        log_debug("exec_string_core: Tokenizer incomplete (compound command) at line %d, tokens buffered", ctx->line_num);
        /* Tokenizer is buffering tokens for an incomplete compound command.
         * The processed_tokens list will be empty - tokens are held in the tokenizer's buffer.
         * Continue reading more input. */
        token_list_destroy(&processed_tokens);
        return EXEC_STRING_INCOMPLETE;
    }

    if (token_list_size(processed_tokens) == 0)
    {
        log_debug("exec_string_core: No tokens after processing at line %d", ctx->line_num);
        token_list_destroy(&processed_tokens);
        return EXEC_STRING_EMPTY;
    }

    /* Accumulate tokens if we had an incomplete parse previously */
    if (ctx->accumulated_tokens)
    {
        log_debug("exec_string_core: Appending %d new tokens to %d accumulated tokens",
                  token_list_size(processed_tokens), token_list_size(ctx->accumulated_tokens));

        /* Move all tokens from processed_tokens to accumulated_tokens */
        if (token_list_append_list_move(ctx->accumulated_tokens, &processed_tokens) != 0)
        {
            log_debug("exec_string_core: Failed to append tokens");
            return EXEC_STRING_ERROR;
        }

        /* Use the accumulated list for parsing */
        processed_tokens = ctx->accumulated_tokens;
        ctx->accumulated_tokens = NULL;
    }

    log_debug("exec_string_core: Tokenizer produced %d processed tokens at line %d",
              token_list_size(processed_tokens), ctx->line_num);

    /* Debug: print all tokens */
    for (int i = 0; i < token_list_size(processed_tokens); i++)
    {
        const token_t *t = token_list_get(processed_tokens, i);
        log_debug("  Token %d: type=%d, text='%s'", i, token_get_type(t),
                  string_cstr(token_to_string(t)));
    }

    parser_t *parser = parser_create_with_tokens_move(&processed_tokens);
    gnode_t *gnode = NULL;

    log_debug("exec_string_core: Starting parse at line %d", ctx->line_num);
    parse_status_t parse_status = parser_parse_program(parser, &gnode);

    if (parse_status == PARSE_ERROR)
    {
        log_debug("exec_string_core: Parse error at line %d", ctx->line_num);
        const char *err = parser_get_error(parser);
        if (err && err[0])
        {
            frame_set_error_printf(frame, "Parse error at line %d: %s", ctx->line_num, err);
        }
        else
        {
            /* Parser returned error but didn't set error message */
            token_t *curr_tok = token_clone(parser_current_token(parser));
            if (curr_tok)
            {
                string_t *tok_str = token_to_string(curr_tok);
                log_debug("exec_string_core: Current token: type=%d, line=%d, col=%d, text='%s'",
                          token_get_type(curr_tok),
                          token_get_first_line(curr_tok),
                          token_get_first_column(curr_tok),
                          string_cstr(tok_str));
                frame_set_error_printf(frame, "Parse error at line %d, column %d near '%s'",
                                       token_get_first_line(curr_tok),
                                       token_get_first_column(curr_tok),
                                       string_cstr(tok_str));
                string_destroy(&tok_str);
                token_destroy(&curr_tok);
            }
            else
            {
                log_debug("exec_string_core: No current token available");
                frame_set_error_printf(frame, "Parse error at line %d: no error details available", ctx->line_num);
            }
        }
        parser_destroy(&parser);
        return EXEC_STRING_ERROR;
    }

    if (parse_status == PARSE_INCOMPLETE)
    {
        log_debug("exec_string_core: Parse incomplete at line %d, accumulating tokens", ctx->line_num);
        if (gnode)
            g_node_destroy(&gnode);
        /* Clone token list from parser - respect silo boundary */
        ctx->accumulated_tokens = token_list_clone(parser->tokens);
        parser_destroy(&parser);
        return EXEC_STRING_INCOMPLETE;
    }

    if (parse_status == PARSE_EMPTY || !gnode)
    {
        log_debug("exec_string_core: Parse empty at line %d", ctx->line_num);
        parser_destroy(&parser);
        return EXEC_STRING_EMPTY;
    }

    ast_node_t *ast = ast_lower(gnode);
    g_node_destroy(&gnode);
    parser_destroy(&parser);

    if (!ast)
    {
        return EXEC_STRING_EMPTY;
    }

    /* Execute via the dispatch function */
    exec_result_t result = exec_execute_dispatch(frame, ast);

    /* Update frame's exit status */
    if (result.has_exit_status)
    {
        frame->last_exit_status = result.exit_status;
        executor->last_exit_status = result.exit_status;
        executor->last_exit_status_set = true;
    }

    ast_node_destroy(&ast);

    if (result.status == EXEC_ERROR)
    {
        return EXEC_STRING_ERROR;
    }

    /* Reset context after successful execution */
    exec_string_ctx_reset(ctx);

    return EXEC_STRING_OK;
}

/* extracts strings from an input stream to feed to exec_string_core */
exec_status_t exec_stream_core_ex(exec_frame_t* frame, FILE* fp, tokenizer_t* tokenizer,
    exec_string_ctx_t* ctx)
{
    Expects_not_null(frame);
    Expects_not_null(fp);
    Expects_not_null(tokenizer);
    Expects_not_null(ctx);
    Expects_not_null(ctx->lexer);
    Expects_not_null(frame->executor);

    exec_status_t final_status = FRAME_EXEC_OK;

    /* Read a single logical line of any size, efficiently */
#define LINE_CHUNK_SIZE 4096
    char chunk[LINE_CHUNK_SIZE];
    char *line_buf = NULL;
    size_t line_buf_size = 0;
    size_t line_len = 0;

    while (fgets(chunk, sizeof(chunk), fp) != NULL)
    {
        size_t chunk_len = strlen(chunk);

        if (chunk_len > 0 && chunk[chunk_len - 1] == '\n')
        {
            if (line_len == 0)
            {
                /* Fast path: entire line fits in chunk */
                exec_string_status_t status = exec_string_core(frame, chunk, tokenizer, ctx);
                if (ctx->line_num > 0)
                    frame->source_line = ctx->line_num;

                switch (status)
                {
                case EXEC_STRING_OK:
                case EXEC_STRING_EMPTY:
                    final_status = EXEC_OK;
                    break;
                case EXEC_STRING_INCOMPLETE:
                    final_status = EXEC_INCOMPLETE;
                    break;
                case EXEC_STRING_ERROR:
                    final_status = EXEC_ERROR;
                    break;
                }
                return final_status;
            }
            else
            {
                /* Append final chunk to accumulated buffer */
                if (line_len + chunk_len + 1 > line_buf_size)
                {
                    line_buf_size = (line_len + chunk_len + 1) * 2;
                    line_buf = (char *)xrealloc(line_buf, line_buf_size);
                }
                memcpy(line_buf + line_len, chunk, chunk_len);
                line_len += chunk_len;
                line_buf[line_len] = '\0';
                break; /* Line complete — fall through to process it */
            }
        }
        else
        {
            /* No newline yet — accumulate */
            if (line_buf == NULL)
            {
                line_buf_size = (chunk_len + 1) * 2;
                line_buf = (char *)xmalloc(line_buf_size);
                memcpy(line_buf, chunk, chunk_len);
                line_len = chunk_len;
                line_buf[line_len] = '\0';
            }
            else
            {
                if (line_len + chunk_len + 1 > line_buf_size)
                {
                    line_buf_size = (line_len + chunk_len + 1) * 2;
                    line_buf = (char *)xrealloc(line_buf, line_buf_size);
                }
                memcpy(line_buf + line_len, chunk, chunk_len);
                line_len += chunk_len;
                line_buf[line_len] = '\0';
            }
        }
    }

    /* Process whatever we accumulated (may be empty on pure EOF) */
    if (line_len > 0)
    {
        exec_string_status_t status = exec_string_core(frame, line_buf, tokenizer, ctx);
        if (ctx->line_num > 0)
            frame->source_line = ctx->line_num;

        switch (status)
        {
        case EXEC_STRING_OK:
        case EXEC_STRING_EMPTY:
            final_status = EXEC_OK;
            break;
        case EXEC_STRING_INCOMPLETE:
            final_status = EXEC_INCOMPLETE;
            break;
        case EXEC_STRING_ERROR:
            final_status = EXEC_ERROR;
            break;
        }
    }

    if (line_buf)
        xfree(line_buf);

    return final_status;
}

exec_status_t exec_execute_stream_repl(exec_t *executor, FILE *fp, bool interactive)
{
    Expects_not_null(executor);
    Expects_not_null(fp);

    /* ------------------------------------------------------------------
     * Ensure the frame stack is initialized
     * ------------------------------------------------------------------ */
    if (!executor->top_frame_initialized)
    {
        exec_status_t status = exec_setup_lazy(executor, fp);
        if (status != EXEC_OK)
            log_warn("lazy setup failed with status %d", status);
        // Not a fatal error
    }
    /* ------------------------------------------------------------------
     * Create / reuse the persistent tokenizer
     * ------------------------------------------------------------------ */
    if (!executor->tokenizer)
    {
        executor->tokenizer = tokenizer_create(executor->aliases);
        if (!executor->tokenizer)
        {
            exec_set_error(executor, "Failed to create tokenizer");
            return EXEC_ERROR;
        }
    }

    /* ------------------------------------------------------------------
     * Create the persistent string-execution context.
     * This is what survives across INCOMPLETE lines — it holds the lexer
     * (with its quote/heredoc state) and any accumulated tokens from
     * partial parses.
     * ------------------------------------------------------------------ */
    exec_string_ctx_t *ctx = exec_string_ctx_create();
    if (!ctx || !ctx->lexer)
    {
        if (ctx)
            exec_string_ctx_destroy(&ctx);
        exec_set_error(executor, "Failed to create execution context");
        return EXEC_ERROR;
    }

    /* ------------------------------------------------------------------
     * REPL state
     * ------------------------------------------------------------------ */
#define IGNOREEOF_MAX 10
    int consecutive_eof = 0;
    bool need_continuation = false;
    exec_status_t final_result = EXEC_OK;

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
                const char *ps2 = exec_get_ps2(executor);
                fprintf(stderr, "%s", ps2);
            }
            else
            {
                char *ps1 = frame_render_ps1(executor->current_frame);
                fprintf(stderr, "%s", ps1 ? ps1 : "$ ");
                xfree(ps1);
            }
            fflush(stderr);
        }

        /* ---- 2. Read & execute one line ---- */
        exec_status_t line_status =
            exec_stream_core_ex(executor->current_frame, fp, executor->tokenizer, ctx);

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
                final_result = EXEC_ERROR;
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
            final_result = EXEC_OK;
            break;
        }

        /* Got input — reset consecutive-EOF counter */
        consecutive_eof = 0;

        /* ---- 4. Dispatch on line status ---- */
        if (line_status == EXEC_INCOMPLETE)
        {
            /*
             * The lexer or parser needs more input.  The ctx holds the
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
         * The ctx was reset internally by exec_string_core on success
         * (exec_string_ctx_reset clears the lexer and accumulated
         * tokens).  On error the ctx may have stale state, so reset
         * it explicitly to be safe.
         */
        if (line_status == EXEC_ERROR)
        {
            exec_string_ctx_reset(ctx);

            if (interactive)
            {
                const char *err = exec_get_error(executor);
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
                    final_result = EXEC_ERROR;
                    break;
                }
            }
        }

        /* ---- 5. Check for exit / top-level return ---- */
        if (executor->current_frame &&
            executor->current_frame->pending_control_flow == EXEC_FLOW_RETURN &&
            executor->current_frame == executor->top_frame)
        {
            /* A top-level 'return' is equivalent to 'exit' */
            final_result = EXEC_EXIT;
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

            exec_frame_t *trap_frame =
                exec_frame_push(executor->current_frame, EXEC_FRAME_TRAP, executor, NULL);

            exec_result_t trap_result =
                exec_command_string(trap_frame, string_cstr(trap_action->action));

            exec_frame_pop(&executor->current_frame);

            executor->last_exit_status = saved_exit_status;

            if (trap_result.status == EXEC_EXIT)
            {
                final_result = EXEC_EXIT;
                goto done;
            }

            if (trap_result.status == EXEC_ERROR && interactive)
            {
                const char *err = exec_get_error(executor);
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
                 * Discard all partial state: reset the ctx (lexer +
                 * accumulated tokens) and recreate the tokenizer so
                 * any buffered compound-command tokens are flushed.
                 */
                exec_string_ctx_reset(ctx);
                tokenizer_destroy(&executor->tokenizer);
                executor->tokenizer = tokenizer_create(executor->aliases);
            }
            else
            {
                executor->last_exit_status = 128 + SIGINT;
                final_result = EXEC_ERROR;
                break;
            }
        }
    } /* for (;;) */

done:
    exec_string_ctx_destroy(&ctx);
    return final_result;
}

exec_status_t exec_execute_stream(exec_t* executor, FILE* fp)
{
    return exec_execute_stream_repl(executor, fp, executor->is_interactive);
}

/* For non-interactive execution of a named script */
exec_status_t exec_execute_stream_named(exec_t* executor, FILE* fp, const char* filename)
{
    Expects_not_null(executor);
    Expects_not_null(fp);
    Expects_not_null(filename);
    /* Need to make sure a frame exists so set can set the filename */
    if (!executor->top_frame_initialized)
    {
        exec_status_t status = exec_setup_lazy(executor, fp);
        if (status != EXEC_OK)
            log_warn("lazy setup failed with status %d", status);
        // Not a fatal error
    }
    Expects_not_null(executor->current_frame);
    /* Set the source name for error reporting */
    if (executor->current_frame->source_name)
        string_set_cstr(&executor->current_frame->source_name, filename);
    else
        executor->current_frame->source_name = string_create_from_cstr(filename);
    executor->current_frame->source_line = 0;
    exec_status_t status = exec_execute_stream_repl(executor, fp, false);
    return status;
}

/* this version is for executing -c complete strings. Incomplete inputs are errors */
exec_result_t exec_execute_command_string(exec_t *executor, const char *command)
{
    Expects_not_null(executor);
    Expects_not_null(command);

    exec_result_t result = {.status = EXEC_OK, .exit_code = 0};

    /* ------------------------------------------------------------------
     * Ensure the frame stack is initialized.
     * For -c invocations the caller should have called
     * exec_setup_non_interactive() beforehand, but handle the lazy case.
     * ------------------------------------------------------------------ */
    if (!executor->top_frame_initialized)
    {
        exec_status_t setup = exec_setup_core(executor, false);
        if (setup != EXEC_OK)
            log_warn("lazy setup for command string failed with status %d", setup);
        /* Not fatal — proceed with whatever state we have. */
    }

    exec_frame_t *frame = executor->current_frame;
    if (!frame)
    {
        exec_set_error(executor, "No execution frame available");
        result.status = EXEC_ERROR;
        result.exit_code = EXEC_EXIT_FAILURE;
        return result;
    }

    /* ------------------------------------------------------------------
     * Create / reuse the persistent tokenizer.
     * ------------------------------------------------------------------ */
    if (!executor->tokenizer)
    {
        executor->tokenizer = tokenizer_create(executor->aliases);
        if (!executor->tokenizer)
        {
            exec_set_error(executor, "Failed to create tokenizer");
            result.status = EXEC_ERROR;
            result.exit_code = EXEC_EXIT_FAILURE;
            return result;
        }
    }

    /* ------------------------------------------------------------------
     * Create a temporary string-execution context.
     * Unlike the REPL, we feed all input at once, so the context is
     * local to this call rather than persistent across lines.
     * ------------------------------------------------------------------ */
    exec_string_ctx_t *ctx = exec_string_ctx_create();
    if (!ctx || !ctx->lexer)
    {
        if (ctx)
            exec_string_ctx_destroy(&ctx);
        exec_set_error(executor, "Failed to create execution context");
        result.status = EXEC_ERROR;
        result.exit_code = EXEC_EXIT_FAILURE;
        return result;
    }

    /* ------------------------------------------------------------------
     * Feed the entire command string to the core execution engine.
     * ------------------------------------------------------------------ */
    exec_string_status_t str_status = exec_string_core(frame, command, executor->tokenizer, ctx);

    switch (str_status)
    {
    case EXEC_STRING_OK:
        result.status = EXEC_OK;
        result.exit_code = executor->last_exit_status;
        break;

    case EXEC_STRING_EMPTY:
        /* Empty command string (e.g. whitespace only, comments only).
         * POSIX: exit status is zero for an empty command. */
        result.status = EXEC_OK;
        result.exit_code = 0;
        break;

    case EXEC_STRING_INCOMPLETE:
        /* For -c style execution, incomplete input is an error — the
         * caller promised a complete command string. */
        if (!exec_get_error(executor))
        {
            exec_set_error(executor, "Unexpected end of input (unclosed quote, "
                                     "here-document, or compound command)");
        }
        result.status = EXEC_INCOMPLETE_INPUT;
        result.exit_code = EXEC_EXIT_MISUSE;
        break;

    case EXEC_STRING_ERROR:
        result.status = EXEC_ERROR;
        result.exit_code =
            executor->last_exit_status ? executor->last_exit_status : EXEC_EXIT_FAILURE;
        break;
    }

    /* ------------------------------------------------------------------
     * Translate control-flow signals that may have been set during
     * execution into the appropriate top-level result status.
     * ------------------------------------------------------------------ */
    if (frame->pending_control_flow == FRAME_FLOW_TOP || executor->exit_requested)
    {
        result.status = EXEC_EXIT;
        result.exit_code = executor->last_exit_status;
    }
    else if (frame == executor->top_frame && frame->pending_control_flow == FRAME_FLOW_RETURN)
    {
        /* A top-level 'return' is equivalent to 'exit'. */
        result.status = EXEC_EXIT;
        result.exit_code = executor->last_exit_status;
    }

    /* ------------------------------------------------------------------
     * Clean up.
     * ------------------------------------------------------------------ */
    exec_string_ctx_destroy(&ctx);

    return result;
}

/* ============================================================================
 * Partial State Lifecycle
 * ============================================================================ */

size_t exec_partial_state_size(void)
{
    return sizeof(struct exec_partial_state_t);
}

void exec_partial_state_cleanup(exec_partial_state_t *state)
{
    if (!state)
        return;

    if (state->filename)
        string_destroy(&state->filename);
    if (state->string_ctx)
        exec_string_ctx_destroy(&state->string_ctx);
    if (state->tokenizer)
        tokenizer_destroy(&state->tokenizer);

    memset(state, 0, sizeof(*state));
}

/* ============================================================================
 * Incremental Command String Execution
 * ============================================================================ */

exec_status_t exec_execute_command_string_partial(exec_t *executor, const char *command,
                                                  const char *filename, size_t line_number,
                                                  exec_partial_state_t *partial_state_out)
{
    Expects_not_null(executor);
    Expects_not_null(command);
    Expects_not_null(partial_state_out);

    /* ------------------------------------------------------------------
     * Ensure the frame stack is initialized.
     * ------------------------------------------------------------------ */
    if (!executor->top_frame_initialized)
    {
        exec_status_t setup = exec_setup_core(executor, false);
        if (setup != EXEC_OK)
            log_warn("lazy setup for partial execution failed with status %d", setup);
    }

    exec_frame_t *frame = executor->current_frame;
    if (!frame)
    {
        exec_set_error(executor, "No execution frame available");
        return EXEC_ERROR;
    }

    /* ------------------------------------------------------------------
     * If an exit has been requested (e.g. by a previous invocation's
     * exit builtin), honour it immediately without consuming input.
     * ------------------------------------------------------------------ */
    if (executor->exit_requested)
        return EXEC_EXIT;

    /* ------------------------------------------------------------------
     * Source location tracking.
     *
     * If the caller supplies a filename, store it in the partial state
     * so it persists across calls.  The filename is also pushed into the
     * frame for error messages.
     *
     * line_number is updated every call when >= 1.  When the caller
     * does not supply a line number (0), we auto-increment from the
     * value stored in the partial state on previous calls.
     * ------------------------------------------------------------------ */
    if (filename)
    {
        if (!partial_state_out->filename)
            partial_state_out->filename = string_create_from_cstr(filename);
        else
            string_set_cstr(partial_state_out->filename, filename);

        /* Push into the frame for error reporting. */
        if (frame->source_name)
            string_set_cstr(frame->source_name, filename);
        else
            frame->source_name = string_create_from_cstr(filename);
    }
    else if (partial_state_out->filename)
    {
        /* Caller previously supplied a filename — keep using it. */
        if (frame->source_name)
            string_set(frame->source_name, partial_state_out->filename);
        else
            frame->source_name = string_clone(partial_state_out->filename);
    }

    if (line_number >= 1)
    {
        partial_state_out->line_number = line_number;
    }
    else if (partial_state_out->line_number > 0)
    {
        /* Auto-increment from last known line. */
        partial_state_out->line_number++;
    }

    if (partial_state_out->line_number >= 1)
        frame->source_line = (int)partial_state_out->line_number;

    /* ------------------------------------------------------------------
     * Lazily create the tokenizer.
     *
     * The tokenizer lives in the partial state (not the executor) so
     * that each partial-execution sequence has its own compound-command
     * buffering independent of the executor's REPL tokenizer.
     * ------------------------------------------------------------------ */
    if (!partial_state_out->tokenizer)
    {
        partial_state_out->tokenizer = tokenizer_create(executor->aliases);
        if (!partial_state_out->tokenizer)
        {
            exec_set_error(executor, "Failed to create tokenizer");
            return EXEC_ERROR;
        }
    }

    /* ------------------------------------------------------------------
     * Lazily create the string execution context.
     *
     * This context holds the lexer (with its quote / heredoc state) and
     * any accumulated tokens from previous partial calls.
     * ------------------------------------------------------------------ */
    if (!partial_state_out->string_ctx)
    {
        partial_state_out->string_ctx = exec_string_ctx_create();
        if (!partial_state_out->string_ctx || !partial_state_out->string_ctx->lexer)
        {
            if (partial_state_out->string_ctx)
                exec_string_ctx_destroy(&partial_state_out->string_ctx);
            exec_set_error(executor, "Failed to create execution context");
            return EXEC_ERROR;
        }
    }

    /* ------------------------------------------------------------------
     * Synchronise the context's line number with ours so that error
     * messages from exec_string_core reference the correct line.
     * ------------------------------------------------------------------ */
    if (partial_state_out->line_number >= 1)
        partial_state_out->string_ctx->line_num = (int)partial_state_out->line_number - 1;
    /* -1 because exec_string_core increments before processing */

    /* ------------------------------------------------------------------
     * Feed this chunk to the core execution engine.
     * ------------------------------------------------------------------ */
    exec_string_status_t str_status = exec_string_core(frame, command, partial_state_out->tokenizer,
                                                       partial_state_out->string_ctx);

    /* Update the line number from the context (exec_string_core may
       have incremented it). */
    partial_state_out->line_number = (size_t)partial_state_out->string_ctx->line_num;

    /* ------------------------------------------------------------------
     * Translate the internal status to the public exec_status_t.
     * ------------------------------------------------------------------ */
    exec_status_t result;

    switch (str_status)
    {
    case EXEC_STRING_OK:
        partial_state_out->incomplete = false;
        result = EXEC_OK;
        break;

    case EXEC_STRING_EMPTY:
        /* An empty chunk (whitespace, comments) is OK.
         * If we were already mid-continuation, stay incomplete — the
         * empty line is just a blank line inside a multi-line construct.
         * Otherwise report OK. */
        if (partial_state_out->incomplete)
            result = EXEC_INCOMPLETE_INPUT;
        else
            result = EXEC_OK;
        break;

    case EXEC_STRING_INCOMPLETE:
        partial_state_out->incomplete = true;
        result = EXEC_INCOMPLETE_INPUT;
        break;

    case EXEC_STRING_ERROR:
        /* On error, clean up the context so the next call starts fresh.
         * The caller can inspect exec_get_error() for details. */
        exec_string_ctx_reset(partial_state_out->string_ctx);
        partial_state_out->incomplete = false;
        result = EXEC_ERROR;
        break;

    default:
        result = EXEC_ERROR;
        break;
    }

    /* ------------------------------------------------------------------
     * If the command completed (OK or ERROR), check for control flow
     * signals that should be propagated to the caller.
     * ------------------------------------------------------------------ */
    if (result == EXEC_OK || result == EXEC_ERROR)
    {
        if (frame->pending_control_flow == FRAME_FLOW_TOP || executor->exit_requested)
        {
            result = EXEC_EXIT;
        }
        else if (frame == executor->top_frame && frame->pending_control_flow == FRAME_FLOW_RETURN)
        {
            /* Top-level 'return' is equivalent to 'exit'. */
            result = EXEC_EXIT;
        }
    }

    /* ------------------------------------------------------------------
     * If the parse completed (not INCOMPLETE), release the internal
     * resources but keep the partial state struct valid for reuse.
     * The caller can start a new sequence without calling cleanup.
     * ------------------------------------------------------------------ */
    if (result != EXEC_INCOMPLETE_INPUT)
    {
        if (partial_state_out->string_ctx)
            exec_string_ctx_reset(partial_state_out->string_ctx);
        partial_state_out->incomplete = false;
        /* Keep tokenizer and string_ctx allocated for reuse on next call. */
    }

    return result;
}

/* ============================================================================
 * Line-Editor Integration
 * ============================================================================ */

exec_status_t exec_execute_stream_with_line_editor(exec_t *executor, FILE *fp,
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
        exec_set_error(executor, "exec_execute_stream_with_line_editor: "
                                 "only valid after exec_setup_interactive()");
        return EXEC_ERROR;
    }

    /* ------------------------------------------------------------------
     * Ensure the frame stack is initialized.
     * ------------------------------------------------------------------ */
    if (!executor->top_frame_initialized)
    {
        exec_status_t status = exec_setup_core(executor, true);
        if (status != EXEC_OK)
            log_warn("lazy interactive setup failed with status %d", status);
    }

    /* ------------------------------------------------------------------
     * Create / reuse the persistent tokenizer.
     * ------------------------------------------------------------------ */
    if (!executor->tokenizer)
    {
        executor->tokenizer = tokenizer_create(executor->aliases);
        if (!executor->tokenizer)
        {
            exec_set_error(executor, "Failed to create tokenizer");
            return EXEC_ERROR;
        }
    }

    /* ------------------------------------------------------------------
     * Create the persistent string-execution context.
     * ------------------------------------------------------------------ */
    exec_string_ctx_t *ctx = exec_string_ctx_create();
    if (!ctx || !ctx->lexer)
    {
        if (ctx)
            exec_string_ctx_destroy(&ctx);
        exec_set_error(executor, "Failed to create execution context");
        return EXEC_ERROR;
    }

    /* ------------------------------------------------------------------
     * REPL state
     * ------------------------------------------------------------------ */
#define IGNOREEOF_MAX 10
    int consecutive_eof = 0;
    bool need_continuation = false;
    exec_status_t final_result = EXEC_OK;
    string_t *line = NULL;

    /* ------------------------------------------------------------------
     * Main loop — one logical line per iteration
     * ------------------------------------------------------------------ */
    for (;;)
    {
        /* ---- 0. Check for pending exit request ---- */
        if (executor->exit_requested)
        {
            final_result = EXEC_EXIT;
            break;
        }

        /* ---- 1. Build the prompt ---- */
        char *prompt = NULL;
        if (need_continuation)
        {
            const char *ps2 = exec_get_ps2(executor);
            prompt = xstrdup(ps2 ? ps2 : "> ");
        }
        else
        {
            prompt = frame_render_ps1(executor->current_frame);
            if (!prompt)
                prompt = xstrdup("$ ");
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
                final_result = EXEC_ERROR;
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
            final_result = EXEC_OK;
            break;
        }

        if (le_status == LINE_EDIT_ERROR)
        {
            exec_set_error(executor, "Line editor returned fatal error");
            final_result = EXEC_ERROR;
            break;
        }

        if (le_status == LINE_EDIT_INTERRUPT)
        {
            /* The line editor handled SIGINT (cleared the line, etc.).
             * Discard any partial parse state and loop back for a fresh
             * prompt, mirroring the SIGINT handling in the fgets REPL. */
            consecutive_eof = 0;
            need_continuation = false;

            exec_string_ctx_reset(ctx);
            tokenizer_destroy(&executor->tokenizer);
            executor->tokenizer = tokenizer_create(executor->aliases);

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
         * exec_string_core expects input lines to end with '\n' (the
         * lexer uses newlines as token delimiters).  The line-editor
         * contract says the returned string has NO trailing newline,
         * so we append one.
         */
        string_t *input = string_clone(line);
        string_append_char(input, '\n');

        exec_string_status_t str_status =
            exec_string_core(executor->current_frame, string_cstr(input), executor->tokenizer, ctx);

        string_destroy(&input);

        if (ctx->line_num > 0)
            executor->current_frame->source_line = ctx->line_num;

        /* ---- 6. Dispatch on execution status ---- */

        if (str_status == EXEC_STRING_INCOMPLETE)
        {
            need_continuation = true;
            continue;
        }

        /* Command complete (OK, EMPTY, or ERROR) — reset continuation. */
        need_continuation = false;

        if (str_status == EXEC_STRING_ERROR)
        {
            exec_string_ctx_reset(ctx);

            const char *err = exec_get_error(executor);
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
            final_result = EXEC_EXIT;
            break;
        }

        if (executor->current_frame &&
            executor->current_frame->pending_control_flow == FRAME_FLOW_TOP)
        {
            final_result = EXEC_EXIT;
            break;
        }

        if (executor->current_frame && executor->current_frame == executor->top_frame &&
            executor->current_frame->pending_control_flow == FRAME_FLOW_RETURN)
        {
            /* Top-level 'return' is equivalent to 'exit'. */
            final_result = EXEC_EXIT;
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

            exec_frame_t *trap_frame =
                exec_frame_push(executor->current_frame, EXEC_FRAME_TRAP, executor, NULL);

            exec_result_t trap_result =
                exec_execute_command_string(executor, string_cstr(trap_action->action));

            exec_frame_pop(&executor->current_frame);

            executor->last_exit_status = saved_exit_status;

            if (trap_result.status == EXEC_EXIT)
            {
                final_result = EXEC_EXIT;
                goto done;
            }

            if (trap_result.status == EXEC_ERROR)
            {
                const char *err = exec_get_error(executor);
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
         * exec_string_core or between steps.  Handle it here as a
         * fallback.
         */
        if (executor->sigint_received)
        {
            executor->sigint_received = 0;

            fprintf(stderr, "\n");
            need_continuation = false;

            exec_string_ctx_reset(ctx);
            tokenizer_destroy(&executor->tokenizer);
            executor->tokenizer = tokenizer_create(executor->aliases);
        }

    } /* for (;;) */

done:
    if (line)
        string_destroy(&line);
    exec_string_ctx_destroy(&ctx);
    return final_result;
}

/* ============================================================================
 * Global State Queries
 * ============================================================================ */

/* ── Exit status ─────────────────────────────────────────────────────────── */

int exec_get_exit_status(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->last_exit_status;
}

void exec_set_exit_status(exec_t *executor, int status)
{
    Expects_not_null(executor);
    executor->last_exit_status = status;
}

void exec_request_exit(exec_t *executor, int status);
{
    Expects_not_null(executor);
    executor->exit_requested = true;
    executor->last_exit_status = status;
}

bool exec_is_exit_requested(const exec_t* executor)
{
    Expects_not_null(executor);
    return executor->exit_requested;
}

/* ── Error message ───────────────────────────────────────────────────────── */

const char *exec_get_error(const exec_t *executor)\
{
    Expects_not_null(executor);
    return executor->error_message ? string_cstr(executor->error_message) : NULL;
}

void exec_set_error(exec_t* executor, const char* format, ...)
{
    Expects_not_null(executor);
    va_list args;
    va_start(args, format);
    string_set_vprintf(&executor->error_message, format, args);
    va_end(args);
}

void exec_clear_error(exec_t *executor)
{
    Expects_not_null(executor);
    if (executor->error_message)
        string_destroy(&executor->error_message);
}

/* ── Pipe statuses (PIPESTATUS / pipefail) ───────────────────────────────── */

int exec_get_pipe_status_count(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->pipe_status_count;
}

const int *exec_get_pipe_statuses(const exec_t *executor)
{
    Expects_not_null(executor);
    return executor->pipe_statuses;
}

void exec_reset_pipe_statuses(exec_t *executor)
{
    Expects_not_null(executor);
    executor->pipe_status_count = 0;
}

/* ── Prompts ─────────────────────────────────────────────────────────────── */

char *exec_get_ps1(const exec_t *executor)
{
    Expects_not_null(executor);
    // FIXME: first check for a PS1 in the current frame's variable store, then fall back to the
    // global store
    const char *ps1 = var_store_get(executor->variables, "PS1");
    return ps1 ? xstrdup(ps1) : NULL;
}

char *exec_get_rendered_ps1(const exec_t *executor);

char *exec_get_ps2(const exec_t *executor)
{
    Expects_not_null(executor);
    const char *ps2 = var_store_get(executor->variables, "PS2");
    return ps2 ? xstrdup(ps2) : NULL;
}

/* ============================================================================
 * Job Control
 * ============================================================================ */

/**
 * Job state as visible through the public API.
 */
typedef enum exec_job_state_t
{
    EXEC_JOB_UNKNOWN = 0, // No such job
    EXEC_JOB_RUNNING,
    EXEC_JOB_STOPPED,
    EXEC_JOB_DONE,
    EXEC_JOB_TERMINATED,
    EXEC_JOB_UNSPECIFIED // Job exists but state is not specified
} exec_job_state_t;

/* ── Reaping ─────────────────────────────────────────────────────────────── */

void exec_reap_background_jobs(exec_t* executor, bool notify)
{
    Expects_not_null(executor);

    bool any_completed = job_store_update_status(executor->jobs, wait_for_completion);
    if (notify && any_completed)
    {
        job_store_print_completed_jobs(executor->jobs, stdout);
    }
}

/* ── Enumeration ─────────────────────────────────────────────────────────── */

size_t exec_get_job_count(const exec_t *executor)
{
    Expects_not_null(executor);
    return job_store_count(executor->jobs);
}

int exec_get_job_ids(const exec_t* executor, int* job_ids, size_t max_jobs)
{
    Expects_not_null(executor);
    Expects_not_null(job_ids);
    return job_store_get_job_ids(executor->jobs, job_ids, max_jobs);
}

int exec_get_current_job_id(const exec_t* executor)
{
    Expects_not_null(executor);
    // job_t *job_store_get_current(const job_store_t *store);
    job_t *current_job = job_store_get_current(executor->jobs);
    return current_job ? current_job->job_id : -1;
}

int exec_get_previous_job_id(const exec_t* executor)
{
    Expects_not_null(executor);
    job_t *previous_job = job_store_get_previous(executor->jobs);
    return previous_job ? previous_job->job_id : -1;
}

/* ── Per-job queries ─────────────────────────────────────────────────────── */

exec_job_state_t exec_job_get_state(const exec_t *executor, int job_id)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
        return EXEC_JOB_UNKNOWN;
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
        return EXEC_JOB_UNSPECIFIED;
    }
}

const char* exec_job_get_command(const exec_t* executor, int job_id)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    return job ? string_cstr(job->command) : NULL;
}

bool exec_job_is_background(const exec_t* executor, int job_id)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    return job ? job->is_background : false;
}

#ifdef POSIX_API
pid_t exec_job_get_pgid(const exec_t *executor, int job_id)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    return job ? job->pgid : -1;
}
#else
int exec_job_get_pgid(const exec_t* executor, int job_id)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    return job ? job->pgid : -1;
}
#endif

/* ── Per-process queries within a job ────────────────────────────────────── */

size_t exec_job_get_process_count(const exec_t* executor, int job_id)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    if (job)
        return job_process_count(job);
    return 0;
}

exec_job_state_t exec_job_get_process_state(const exec_t *executor, int job_id, size_t index)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
        return EXEC_JOB_UNKNOWN;
    if (index < 0 || index >= job_process_count(job))
        return EXEC_JOB_UNKNOWN;
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
        return EXEC_JOB_UNSPECIFIED;
    }
}

int exec_job_get_process_exit_status(const exec_t* executor, int job_id, size_t index)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
        return -1;
    if (index < 0 || index >= job_process_count(job))
        return -1;
    return job_get_process_exit_status(job, index);
}

#ifdef POSIX_API
pid_t exec_job_get_process_pid(const exec_t *executor, int job_id, size_t index)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
        return -1;
    if (index < 0 || index >= job_process_count(job))
        return -1;
    return job_get_process_pid(job, index);
}
#elif defined(UCRT_API)
int exec_job_get_process_pid(const exec_t *executor, int job_id, size_t index)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
        return -1;
    if (index < 0 || index >= job_process_count(job))
        return -1;
    return (int)job_get_process_pid(job, index);
}

intptr_t exec_job_get_process_handle(const exec_t *executor, int job_id, size_t index)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
        return (uintptr_t)-1;
    if (index < 0 || index >= job_process_count(job))
        return (uintptr_t)-1;
#ifdef UCRT_API
    return job_get_process_handle(job, index);
#else
    return (intptr_t)-1;
#endif
}
#else
int exec_job_get_process_pid(const exec_t* executor, int job_id, size_t index)
{
    Expects_not_null(executor);
    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
        return -1;
    if (index < 0 || index >= job_process_count(job))
        return -1;
    return (int)job_get_process_pid(job, index);
}
#endif

/* ── Job actions ─────────────────────────────────────────────────────────── */

bool exec_job_foreground(exec_t *executor, int job_id, char **cmd)
{
    Expects_not_null(executor);
    if (cmd)
        *cmd = NULL;

    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
    {
        exec_set_error(executor, "fg: no such job");
        return false;
    }

    if (job->state == JOB_STOPPED)
    {
#ifdef POSIX_API
        if (kill(-job->pgid, SIGCONT) < 0)
        {
            exec_set_error(executor, "fg: cannot resume job: %s", strerror(errno));
            return false;
        }
        job_store_set_state(executor->jobs, job_id, JOB_RUNNING);
#else
        exec_set_error(executor, "fg: cannot resume suspended jobs on this platform");
        return false;
#endif
    }

#ifdef POSIX_API
    if (tcsetpgrp(STDIN_FILENO, job->pgid) < 0)
    {
        exec_set_error(executor, "fg: cannot set foreground process group: %s", strerror(errno));
        return false;
    }

    job->is_background = false; // optional but nice for consistency

    if (cmd)
    {
        *cmd = xstrdup(string_cstr(job->command));
    }

    job_store_remove(executor->jobs, job_id);
    return true;
#else
    exec_set_error(executor, "fg: foreground job control not supported on this platform");
    return false;
#endif
}

bool exec_job_kill(exec_t *executor, int job_id, int sig)
{
    Expects_not_null(executor);

    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
    {
        exec_set_error(executor, "kill: no such job");
        return false;
    }

    // Special case: sig == 0 → POSIX check existence/permissions, no signal sent
    if (sig == 0)
    {
        // On POSIX: kill(-pgid, 0) succeeds if we can signal the group
#ifdef POSIX_API
        if (kill(-job->pgid, 0) < 0)
        {
            exec_set_error(executor, "kill: job %d: no such process group: %s", job_id,
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
#ifdef POSIX_API
    // Send to the entire process group (negative pgid)
    if (kill(-job->pgid, sig) < 0)
    {
        // Common errors: ESRCH (no such process), EPERM (permission denied)
        exec_set_error(executor, "kill: cannot send signal %d to job %d: %s", sig, job_id,
                       strerror(errno));
        return false;
    }

    return true;

#elif defined(UCRT_API)
    // Very limited: only attempt forceful termination for common "kill" signals
    if (sig == SIGTERM || sig == SIGKILL || sig == 15 || sig == 9)
    {
        // Each job has a list of process_t, but, in UCRT, there will
        // only ever be one entry. We can get the handle from the process.
        if (job->processes)
        {
            unsigned long long handle;
            // Using the shortcut that we know there can only be one process
            // in a UCRT job.
            handle = job->processes->handle;
            if (handle && TerminateProcess(handle, 1))
            {
                CloseHandle(handle);
                job->processes->handle = NULL;
            }
            else
            {
                exec_set_error(executor, "kill: failed to terminate job %d on Windows", job_id);
                return false;
            }
        }
        // Have to update job store here, since we won't catch a signal or status change from the
        // process itself.
        job_store_set_state(executor->jobs, job_id, JOB_TERMINATED);
    }
    else
    {
        exec_set_error(executor,
                       "kill: only SIGTERM/SIGKILL supported on Windows (job control limited)");
        return false;
    }

#else
    exec_set_error(executor, "kill: job control not supported on this platform");
    return false;
#endif
}

void exec_print_jobs(const exec_t* executor, FILE* output)
{
    // void job_store_print_jobs(const job_store_t *store, FILE *output)
    Expects_not_null(executor);
    Expects_not_null(output);
    job_store_print_jobs(executor->jobs, output);
}

/**
 * Job output format for printing.
 */
typedef enum exec_jobs_format_t
{
    EXEC_JOBS_FORMAT_DEFAULT, /**< [job_id] state command  */
    EXEC_JOBS_FORMAT_LONG,    /**< includes PIDs           */
    EXEC_JOBS_FORMAT_PID_ONLY /**< process group leader PID only */
} exec_jobs_format_t;

/**
 * Parse a job-ID specifier from a string.
 * Accepts: %n, %+, %%, %-, or a plain number.
 *
 * @return Job ID on success, -1 on error.
 */
int exec_parse_job_id(const exec_t* executor, const char* spec)
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
                exec_set_error(executor, "Invalid job specifier: %s", spec);
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
            exec_set_error(executor, "Invalid job ID: %s", spec);
            return -1;
        }
        return (int)id;
    }
}

/**
 * Print a single job.
 *
 * @return true if the job was found and printed.
 */
bool exec_print_job_by_id(const exec_t *executor, int job_id, exec_jobs_format_t format,
                          FILE *output)
{
    Expects_not_null(executor);
    //         const char *state_str = job_state_to_string(job->state);
    // fprintf(output, "[%d] %s\t%s\n", job->job_id, state_str,
    //        job->command_line ? string_cstr(job->command_line) : "(no command)");
    job_t *job = job_store_find(executor->jobs, job_id);
    if (!job)
    {
        exec_set_error(executor, "No such job: %d", job_id);
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
void exec_print_all_jobs(const exec_t* executor, exec_jobs_format_t format, FILE* output)
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
 * Check whether any jobs exist.
 */
bool exec_has_jobs(const exec_t* executor)
{
    Expects_not_null(executor);
    return job_store_count(executor->jobs) > 0;
}

#ifdef KEEP_JUNK


/**
 * Execute a complete command string.
 *
 * This is a simplified wrapper around exec_string_core() for cases where the
 * input string is expected to be a complete, self-contained command that does
 * not require continuation. This is useful for:
 * - Trap handlers (the action string stored with trap)
 * - eval builtin
 * - Any case where you have a complete command as a string
 *
 * If the command string is incomplete (e.g., unclosed quotes, missing 'done'),
 * this function treats it as an error rather than requesting more input.
 *
 * @param frame The execution frame
 * @param command The complete command string to execute
 * @return exec_result_t with execution status and exit code
 */
exec_result_t exec_command_string(exec_frame_t *frame, const char *command)
{
    Expects_not_null(frame);
    Expects_not_null(frame->executor);

    exec_result_t result = {
        .status = EXEC_OK,
        .has_exit_status = true,
        .exit_status = 0,
        .flow = EXEC_FLOW_NORMAL,
        .flow_depth = 0
    };

    /* Handle empty or NULL command */
    if (!command || !*command)
    {
        return result;
    }

    exec_t *executor = frame->executor;

    /* Create a temporary tokenizer for this command */
    tokenizer_t *tokenizer = tokenizer_create(executor->aliases);
    if (!tokenizer)
    {
        frame_set_error_printf(frame, "Failed to create tokenizer for command string");
        result.status = EXEC_ERROR;
        result.exit_status = 1;
        return result;
    }

    /* Create execution context */
    exec_string_ctx_t *ctx = exec_string_ctx_create();
    if (!ctx || !ctx->lexer)
    {
        frame_set_error_printf(frame, "Failed to create execution context");
        if (ctx)
            exec_string_ctx_destroy(&ctx);
        tokenizer_destroy(&tokenizer);
        result.status = EXEC_ERROR;
        result.exit_status = 1;
        return result;
    }

    /* Ensure command ends with newline for proper parsing */
    string_t *cmd_with_newline = string_create_from_cstr(command);
    int len = string_length(cmd_with_newline);
    if (len == 0 || string_at(cmd_with_newline, len - 1) != '\n')
    {
        string_append_char(cmd_with_newline, '\n');
    }

    /* Execute the command string */
    exec_string_status_t status = exec_string_core(frame, string_cstr(cmd_with_newline),
                                                   tokenizer, ctx);

    string_destroy(&cmd_with_newline);

    /* Map status to result */
    switch (status)
    {
    case EXEC_STRING_OK:
        result.status = EXEC_OK;
        result.exit_status = frame->last_exit_status;
        break;

    case EXEC_STRING_EMPTY:
        /* Empty command is not an error, just return success with status 0 */
        result.status = EXEC_OK;
        result.exit_status = 0;
        break;

    case EXEC_STRING_INCOMPLETE:
        /* For a "complete" command string, incomplete is an error */
        frame_set_error_printf(frame, "Incomplete command: unexpected end of input");
        result.status = EXEC_ERROR;
        result.exit_status = 2;
        break;

    case EXEC_STRING_ERROR:
        result.status = EXEC_ERROR;
        result.exit_status = frame->last_exit_status ? frame->last_exit_status : 1;
        break;
    }

    /* Clean up */
    exec_string_ctx_destroy(&ctx);
    tokenizer_destroy(&tokenizer);

    return result;
}

/**
 * Parse a command string into an AST without executing it.
 * This is used by eval to separate parsing from execution.
 */
exec_result_t exec_parse_string(exec_frame_t *frame, const char *command, ast_node_t **out_ast)
{
    Expects_not_null(frame);
    Expects_not_null(out_ast);

    exec_result_t result = {
        .status = EXEC_OK,
        .has_exit_status = true,
        .exit_status = 0,
        .flow = EXEC_FLOW_NORMAL,
        .flow_depth = 0
    };

    *out_ast = NULL;

    /* Handle empty or NULL command */
    if (!command || !*command)
    {
        return result;
    }

    exec_t *executor = frame->executor;

    /* Create a temporary tokenizer for parsing */
    tokenizer_t *tokenizer = tokenizer_create(executor->aliases);
    if (!tokenizer)
    {
        frame_set_error_printf(frame, "Failed to create tokenizer for parsing");
        result.status = EXEC_ERROR;
        result.exit_status = 1;
        return result;
    }

    /* Create lexer */
    lexer_t *lexer = lexer_create();
    if (!lexer)
    {
        frame_set_error_printf(frame, "Failed to create lexer for parsing");
        tokenizer_destroy(&tokenizer);
        result.status = EXEC_ERROR;
        result.exit_status = 1;
        return result;
    }

    /* Ensure command ends with newline for proper parsing */
    string_t *cmd_with_newline = string_create_from_cstr(command);
    int len = string_length(cmd_with_newline);
    if (len == 0 || string_at(cmd_with_newline, len - 1) != '\n')
    {
        string_append_char(cmd_with_newline, '\n');
    }

    /* Lex the input */
    lexer_append_input_cstr(lexer, string_cstr(cmd_with_newline));
    string_destroy(&cmd_with_newline);

    token_list_t *raw_tokens = token_list_create();
    lex_status_t lex_status = lexer_tokenize(lexer, raw_tokens, NULL);

    if (lex_status == LEX_ERROR)
    {
        const char *err = lexer_get_error(lexer);
        frame_set_error_printf(frame, "Lexer error: %s", err ? err : "unknown");
        token_list_destroy(&raw_tokens);
        lexer_destroy(&lexer);
        tokenizer_destroy(&tokenizer);
        result.status = EXEC_ERROR;
        result.exit_status = 2;
        return result;
    }

    if (lex_status == LEX_INCOMPLETE || lex_status == LEX_NEED_HEREDOC)
    {
        frame_set_error_printf(frame, "Incomplete command: unexpected end of input");
        token_list_destroy(&raw_tokens);
        lexer_destroy(&lexer);
        tokenizer_destroy(&tokenizer);
        result.status = EXEC_ERROR;
        result.exit_status = 2;
        return result;
    }

    /* Process tokens */
    token_list_t *processed_tokens = token_list_create();
    tok_status_t tok_status = tokenizer_process(tokenizer, raw_tokens, processed_tokens);
    token_list_destroy(&raw_tokens);
    lexer_destroy(&lexer);

    if (tok_status == TOK_ERROR)
    {
        const char *err = tokenizer_get_error(tokenizer);
        frame_set_error_printf(frame, "Tokenizer error: %s", err ? err : "unknown");
        token_list_destroy(&processed_tokens);
        tokenizer_destroy(&tokenizer);
        result.status = EXEC_ERROR;
        result.exit_status = 2;
        return result;
    }

    if (tok_status == TOK_INCOMPLETE)
    {
        frame_set_error_printf(frame, "Incomplete command: unexpected end of input");
        token_list_destroy(&processed_tokens);
        tokenizer_destroy(&tokenizer);
        result.status = EXEC_ERROR;
        result.exit_status = 2;
        return result;
    }

    if (token_list_size(processed_tokens) == 0)
    {
        /* Empty command */
        token_list_destroy(&processed_tokens);
        tokenizer_destroy(&tokenizer);
        return result;
    }

    /* Parse tokens */
    parser_t *parser = parser_create_with_tokens_move(&processed_tokens);
    gnode_t *gnode = NULL;

    parse_status_t parse_status = parser_parse_program(parser, &gnode);

    if (parse_status == PARSE_ERROR)
    {
        const char *err = parser_get_error(parser);
        if (err && err[0])
        {
            frame_set_error_printf(frame, "Parse error: %s", err);
        }
        else
        {
            frame_set_error_printf(frame, "Parse error");
        }
        parser_destroy(&parser);
        tokenizer_destroy(&tokenizer);
        result.status = EXEC_ERROR;
        result.exit_status = 2;
        return result;
    }

    if (parse_status == PARSE_INCOMPLETE)
    {
        frame_set_error_printf(frame, "Incomplete command: unexpected end of input");
        if (gnode)
            g_node_destroy(&gnode);
        parser_destroy(&parser);
        tokenizer_destroy(&tokenizer);
        result.status = EXEC_ERROR;
        result.exit_status = 2;
        return result;
    }

    if (parse_status == PARSE_EMPTY || !gnode)
    {
        /* Empty command */
        parser_destroy(&parser);
        tokenizer_destroy(&tokenizer);
        return result;
    }

    /* Lower parse tree to AST */
    ast_node_t *ast = ast_lower(gnode);
    g_node_destroy(&gnode);
    parser_destroy(&parser);
    tokenizer_destroy(&tokenizer);

    *out_ast = ast;
    return result;
}

exec_status_t exec_execute_stream(exec_t *executor, FILE *fp)
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

    /* Use the executor's persistent tokenizer (create if needed) */
    if (!executor->tokenizer)
    {
        executor->tokenizer = tokenizer_create(executor->aliases);
        if (!executor->tokenizer)
        {
            exec_set_error(executor, "Failed to create tokenizer");
            return EXEC_ERROR;
        }
    }

    frame_exec_status_t status = exec_stream_core(
        executor->current_frame,
        fp,
        executor->tokenizer
    );

    if (status == FRAME_EXEC_OK)
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
                exec_frame_t *trap_frame =
                    exec_frame_push(executor->current_frame, EXEC_FRAME_TRAP, executor, NULL);

                exec_result_t trap_result = exec_command_string(trap_frame,
                                                                string_cstr(trap_action->action));

                exec_frame_pop(&executor->current_frame);

                /* Restore exit status */
                executor->last_exit_status = saved_exit_status;

                if (trap_result.status == EXEC_ERROR)
                {
                    status = FRAME_EXEC_ERROR;
                    break;
                }
            }
        }
    }

    /* Map frame_exec_status_t to exec_status_t */
    switch (status)
    {
    case FRAME_EXEC_OK:
        return EXEC_OK;
    case FRAME_EXEC_ERROR:
        return EXEC_ERROR;
    case FRAME_EXEC_NOT_IMPL:
        return EXEC_NOT_IMPL;
    default:
        return EXEC_OK;
    }
}

/* ============================================================================
 * Job Control
 * ============================================================================ */

void exec_reap_background_jobs(exec_t *executor, bool notify)
{
    Expects_not_null(executor);

#ifdef POSIX_API
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
            job_store_set_state(executor->jobs, pid, JOB_STATE_TERMINATED);
        else
            job_store_set_state(executor->jobs, pid, JOB_STATE_DONE);
        job_store_set_exit_status(executor->jobs, pid, exit_status);
        any_reaped = true;
    }
    if (any_reaped && notify)
    {
        job_store_print_completed_jobs(executor->jobs, stdout);
    }
    if (any_reaped)
        job_store_remove_completed(executor->jobs);
#elifdef UCRT_API
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

/* ============================================================================
 * AST Visitor Pattern Support
 * ============================================================================ */

static bool ast_traverse_helper(const ast_node_t *node, ast_visitor_fn visitor, void *user_data)
{
    if (node == NULL)
        return true;

    if (!visitor(node, user_data))
        return false;

    switch (node->type)
    {
    case AST_SIMPLE_COMMAND:
        break;

    case AST_PIPELINE:
        if (node->data.pipeline.commands != NULL)
        {
            for (int i = 0; i < node->data.pipeline.commands->size; i++)
            {
                if (!ast_traverse_helper(node->data.pipeline.commands->nodes[i], visitor,
                                         user_data))
                    return false;
            }
        }
        break;

    case AST_AND_OR_LIST:
        if (!ast_traverse_helper(node->data.andor_list.left, visitor, user_data))
            return false;
        if (!ast_traverse_helper(node->data.andor_list.right, visitor, user_data))
            return false;
        break;

    case AST_COMMAND_LIST:
        if (node->data.command_list.items != NULL)
        {
            for (int i = 0; i < node->data.command_list.items->size; i++)
            {
                if (!ast_traverse_helper(node->data.command_list.items->nodes[i], visitor,
                                         user_data))
                    return false;
            }
        }
        break;

    case AST_SUBSHELL:
    case AST_BRACE_GROUP:
        if (!ast_traverse_helper(node->data.compound.body, visitor, user_data))
            return false;
        break;

    case AST_IF_CLAUSE:
        if (!ast_traverse_helper(node->data.if_clause.condition, visitor, user_data))
            return false;
        if (!ast_traverse_helper(node->data.if_clause.then_body, visitor, user_data))
            return false;
        if (node->data.if_clause.elif_list != NULL)
        {
            for (int i = 0; i < node->data.if_clause.elif_list->size; i++)
            {
                if (!ast_traverse_helper(node->data.if_clause.elif_list->nodes[i], visitor,
                                         user_data))
                    return false;
            }
        }
        if (!ast_traverse_helper(node->data.if_clause.else_body, visitor, user_data))
            return false;
        break;

    case AST_WHILE_CLAUSE:
    case AST_UNTIL_CLAUSE:
        if (!ast_traverse_helper(node->data.loop_clause.condition, visitor, user_data))
            return false;
        if (!ast_traverse_helper(node->data.loop_clause.body, visitor, user_data))
            return false;
        break;

    case AST_FOR_CLAUSE:
        if (!ast_traverse_helper(node->data.for_clause.body, visitor, user_data))
            return false;
        break;

    case AST_CASE_CLAUSE:
        if (node->data.case_clause.case_items != NULL)
        {
            for (int i = 0; i < node->data.case_clause.case_items->size; i++)
            {
                if (!ast_traverse_helper(node->data.case_clause.case_items->nodes[i], visitor,
                                         user_data))
                    return false;
            }
        }
        break;

    case AST_CASE_ITEM:
        if (!ast_traverse_helper(node->data.case_item.body, visitor, user_data))
            return false;
        break;

    case AST_FUNCTION_DEF:
        if (!ast_traverse_helper(node->data.function_def.body, visitor, user_data))
            return false;
        if (node->data.function_def.redirections != NULL)
        {
            for (int i = 0; i < node->data.function_def.redirections->size; i++)
            {
                if (!ast_traverse_helper(node->data.function_def.redirections->nodes[i], visitor,
                                         user_data))
                    return false;
            }
        }
        break;

    case AST_REDIRECTED_COMMAND:
        if (!ast_traverse_helper(node->data.redirected_command.command, visitor, user_data))
            return false;
        if (node->data.redirected_command.redirections != NULL)
        {
            for (int i = 0; i < node->data.redirected_command.redirections->size; i++)
            {
                if (!ast_traverse_helper(node->data.redirected_command.redirections->nodes[i],
                                         visitor, user_data))
                    return false;
            }
        }
        break;

    default:
        break;
    }

    return true;
}

bool ast_traverse(const ast_node_t *root, ast_visitor_fn visitor, void *user_data)
{
    Expects_not_null(visitor);
    return ast_traverse_helper(root, visitor, user_data);
}

exec_result_t exec_compound_list(exec_frame_t* frame, const ast_node_t* list)
{
    Expects_not_null(frame);
    Expects_not_null(list);
    Expects(list->type == AST_COMMAND_LIST);

    exec_result_t result = { .status = EXEC_OK, .has_exit_status = false, .exit_status = 0 };

    // Access the command list data
    ast_node_list_t *items = list->data.command_list.items;
    cmd_separator_list_t *separators = list->data.command_list.separators;

    if (!items || items->size == 0) {
        // Empty list: no commands to execute, exit status 0
        result.has_exit_status = true;
        result.exit_status = 0;
        return result;
    }

    // Iterate through each command in the list
    for (int i = 0; i < items->size; i++) {
        ast_node_t *cmd = items->nodes[i];
        if (!cmd)
            continue;  // Skip null commands (shouldn't happen, but defensive)

        /* Update LINENO before executing this command */
        exec_update_lineno(frame, cmd);

        cmd_separator_t sep = cmd_separator_list_get(separators, i);
        if (sep == CMD_EXEC_BACKGROUND)
        {
            /* For POSIX, UCRT, and ISO_C, we'll just call exec_background_job,
             * but that doesn't mean this will actually run in the background.
             * We'll handle platform-specific limitations inside exec_background_job.
             *
             * In POSIX, though, exec_background_job will return quickly.
             * For the others, it depends.
             */
            string_t *command_line = ast_node_to_command_line_full(cmd);
            // Since we're not sending this to spawn, we can keep this as a single string_t
            string_list_t *argv_list = string_list_create();
            string_list_move_push_back(argv_list, &command_line);
            exec_background_job(frame, cmd, argv_list);
            continue;
        }

        // Execute the command based on its type
        exec_result_t cmd_result;
        switch (cmd->type) {
            case AST_PIPELINE:
                cmd_result = exec_pipeline(frame, cmd);
                break;
            case AST_AND_OR_LIST:
                cmd_result = exec_and_or_list(frame, cmd);
                break;
            case AST_COMMAND_LIST:
                cmd_result = exec_compound_list(frame, cmd);
                break;
            case AST_SIMPLE_COMMAND:
                cmd_result = exec_simple_command(frame, cmd);
                break;
            case AST_BRACE_GROUP:
                cmd_result = exec_brace_group(frame, cmd->data.compound.body, NULL);
                break;
            case AST_SUBSHELL:
                cmd_result = exec_subshell(frame, cmd->data.compound.body);
                break;
            case AST_IF_CLAUSE:
                cmd_result = exec_if_clause(frame, cmd);
                break;
            case AST_WHILE_CLAUSE:
            case AST_UNTIL_CLAUSE:
                cmd_result = exec_while_clause(frame, cmd);
                break;
            case AST_FOR_CLAUSE:
                cmd_result = exec_for_clause(frame, cmd);
                break;
            case AST_CASE_CLAUSE:
                cmd_result = exec_case_clause(frame, cmd);
                break;
            case AST_REDIRECTED_COMMAND:
                cmd_result = exec_redirected_command(frame, cmd);
                break;
            case AST_FUNCTION_DEF:
                cmd_result = exec_function_def_clause(frame, cmd);
                break;
            default:
                exec_set_error(frame->executor, "Unsupported command type in compound list: %d (%s)", cmd->type, ast_node_type_to_string(cmd->type));
                result.status = EXEC_NOT_IMPL;
                return result;
        }

        // Propagate errors from command execution
        if (cmd_result.status != EXEC_OK) {
            result.status = cmd_result.status;
            if (cmd_result.has_exit_status) {
                result.has_exit_status = true;
                result.exit_status = cmd_result.exit_status;
            }
            return result;  // Stop on error (unless background)
        }

        // Update the frame's exit status from the command
        if (cmd_result.has_exit_status) {
            frame->last_exit_status = cmd_result.exit_status;
            result.has_exit_status = true;
            result.exit_status = cmd_result.exit_status;
        }

        // Handle the separator after this command
        // cmd_separator_t sep = ast_node_command_list_get_separator(list, i);
        if (sep == CMD_EXEC_BACKGROUND) {
            // Run in background: Add to job store and continue without waiting
            // Note: This assumes the command spawned processes; integrate with job control
            // For simplicity, assume exec_pipeline/exec_and_or_list handle process creation
            // In a full impl, extract PIDs from cmd_result and add to job_store
            // Example: job_store_add(frame->executor->jobs, command_line, true);
            // For now, just log or skip detailed job management
            // TODO: Implement full background job tracking
        } else if (sep == CMD_EXEC_SEQUENTIAL) {
            // Wait for completion (already done above), then proceed to next
        }
        else if (sep == CMD_EXEC_END)
        {
            // End of list: No more commands
            break;
        }
        // Invalid separators are handled by the parser, so no need to check
    }

    return result;
}

exec_result_t exec_and_or_list(exec_frame_t *frame, ast_node_t *list)
{
    Expects_not_null(frame);
    Expects_not_null(list);
    Expects(list->type == AST_AND_OR_LIST);

    exec_result_t result = { .status = EXEC_OK, .has_exit_status = false, .exit_status = 0 };

    ast_node_t *left = list->data.andor_list.left;
    ast_node_t *right = list->data.andor_list.right;
    andor_operator_t op = list->data.andor_list.op;

    /* Update LINENO for left side */
    exec_update_lineno(frame, left);

    // Execute left command
    exec_result_t left_result;
    switch (left->type) {
        case AST_COMMAND_LIST:
            left_result = exec_compound_list(frame, left);
            break;
        case AST_AND_OR_LIST:
            left_result = exec_and_or_list(frame, left);
            break;
        case AST_PIPELINE:
            left_result = exec_pipeline(frame, left);
            break;
        case AST_SIMPLE_COMMAND:
            left_result = exec_simple_command(frame, left);
            break;
        default:
            exec_set_error(frame->executor, "Unsupported AST node type in and_or_list: %d", left->type);
            result.status = EXEC_NOT_IMPL;
            return result;
    }

    if (left_result.status != EXEC_OK) {
        result = left_result;
        return result;
    }

    // Determine if right command should be executed
    bool execute_right = false;
    if (op == ANDOR_OP_AND) {
        if (left_result.exit_status == 0) {
            execute_right = true;
        }
    } else if (op == ANDOR_OP_OR) {
        if (left_result.exit_status != 0) {
            execute_right = true;
        }
    }

    if (execute_right) {
        /* Update LINENO for right side (only if we're going to execute it) */
        exec_update_lineno(frame, right);

        // Execute right command
        exec_result_t right_result;
        switch (right->type) {
            case AST_COMMAND_LIST:
                right_result = exec_compound_list(frame, right);
                break;
            case AST_AND_OR_LIST:
                right_result = exec_and_or_list(frame, right);
                break;
            case AST_PIPELINE:
                right_result = exec_pipeline(frame, right);
                break;
            case AST_SIMPLE_COMMAND:
                right_result = exec_simple_command(frame, right);
                break;
            default:
                exec_set_error(frame->executor, "Unsupported AST node type in and_or_list: %d", right->type);
                result.status = EXEC_NOT_IMPL;
                return result;
        }

        if (right_result.status != EXEC_OK) {
            result = right_result;
            return result;
        }

        result = right_result;
    } else {
        result = left_result;
    }

    // Update frame's last exit status
    frame->last_exit_status = result.exit_status;

    return result;
}

exec_result_t exec_pipeline(exec_frame_t *frame, ast_node_t *list)
{
    Expects_not_null(frame);
    Expects_not_null(list);
    Expects(list->type == AST_PIPELINE);

    ast_node_list_t *commands = list->data.pipeline.commands;
    bool is_negated = list->data.pipeline.is_negated;

    // Execute the pipeline using the frame system
    exec_result_t result = exec_pipeline_group(frame, commands, is_negated);

    return result;
}

exec_result_t exec_simple_command(exec_frame_t *frame, ast_node_t *node)
{
    /* Executing a simple command is so complicated, it gets
     * its own module, lol. Simple, my butt. */
    exec_status_t status = exec_execute_simple_command(frame, node);
    exec_result_t result;
    if (status == EXEC_OK)
    {
        result.status = EXEC_OK;
        result.has_exit_status = true;
        result.exit_status = frame->last_exit_status;
    }
    else
    {
        result.status = status;
        result.has_exit_status = false;
        result.exit_status = 0;
    }

    /* Check if a builtin set pending control flow (return, break, continue) */
    result.flow = frame->pending_control_flow;
    result.flow_depth = frame->pending_flow_depth;

    /* Reset pending flow for next command */
    frame->pending_control_flow = EXEC_FLOW_NORMAL;
    frame->pending_flow_depth = 0;

    return result;
}

exec_result_t exec_if_clause(exec_frame_t *frame, ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects(node->type == AST_IF_CLAUSE);

    exec_result_t result = { .status = EXEC_OK, .has_exit_status = false, .exit_status = 0 };

    // Execute the main condition
    exec_result_t cond_result = exec_execute_dispatch(frame, node->data.if_clause.condition);
    if (cond_result.status != EXEC_OK) {
        result = cond_result;
        return result;
    }

    // If condition succeeds (exit status 0), execute then_body
    if (cond_result.has_exit_status && cond_result.exit_status == 0) {
        exec_result_t then_result = exec_execute_dispatch(frame, node->data.if_clause.then_body);
        if (then_result.status != EXEC_OK) {
            result = then_result;
            return result;
        }
        result = then_result;
        return result;
    }

    // Check elif clauses
    if (node->data.if_clause.elif_list) {
        for (int i = 0; i < node->data.if_clause.elif_list->size; i++) {
            ast_node_t *elif_node = node->data.if_clause.elif_list->nodes[i];
            if (!elif_node || elif_node->type != AST_IF_CLAUSE) {
                // Assuming elif is also AST_IF_CLAUSE with condition and then_body
                continue;
            }
            exec_result_t elif_cond_result = exec_execute_dispatch(frame, elif_node->data.if_clause.condition);
            if (elif_cond_result.status != EXEC_OK) {
                result = elif_cond_result;
                return result;
            }
            if (elif_cond_result.has_exit_status && elif_cond_result.exit_status == 0) {
                exec_result_t elif_then_result = exec_execute_dispatch(frame, elif_node->data.if_clause.then_body);
                if (elif_then_result.status != EXEC_OK) {
                    result = elif_then_result;
                    return result;
                }
                result = elif_then_result;
                return result;
            }
        }
    }

    // Execute else_body if present
    if (node->data.if_clause.else_body) {
        exec_result_t else_result = exec_execute_dispatch(frame, node->data.if_clause.else_body);
        if (else_result.status != EXEC_OK) {
            result = else_result;
            return result;
        }
        result = else_result;
    } else {
        // No else, exit status 0
        result.has_exit_status = true;
        result.exit_status = 0;
    }

    return result;
}

exec_result_t exec_while_clause(exec_frame_t *frame, ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects(node->type == AST_WHILE_CLAUSE || node->type == AST_UNTIL_CLAUSE);

    // Extract the loop components
    ast_node_t *condition = node->data.loop_clause.condition;
    ast_node_t *body = node->data.loop_clause.body;
    bool is_until = (node->type == AST_UNTIL_CLAUSE);

    // Execute the while/until loop using the frame system
    exec_result_t result = exec_while_loop(frame, condition, body, is_until);

    return result;
}

exec_result_t exec_for_clause(exec_frame_t *frame, ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects(node->type == AST_FOR_CLAUSE);

    // Extract the for loop components
    string_t *var_name = node->data.for_clause.variable;
    token_list_t *word_tokens = node->data.for_clause.words;
    ast_node_t *body = node->data.for_clause.body;

    // Build the word list via proper POSIX expansion or positional parameters
    string_list_t *words;
    if (word_tokens && token_list_size(word_tokens) > 0)
    {
        // Perform full word expansion: tilde, parameter, command substitution,
        // arithmetic, field splitting, and pathname expansion
        words = expand_words(frame, word_tokens);
        if (!words)
        {
            words = string_list_create(); // Treat expansion failure as empty list
        }
    }
    else
    {
        // No words provided: iterate over positional parameters ("$@")
        positional_params_t *params = frame->positional_params;
        words = positional_params_get_all(params);
    }

    // Execute the for loop using the frame system
    exec_result_t result = exec_for_loop(frame, var_name, words, body);

    // Clean up
    string_list_destroy(&words);

    return result;
}
exec_result_t exec_function_def_clause(exec_frame_t *frame, ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects_eq(node->type, AST_FUNCTION_DEF);

    ast_node_t *body = node->data.function_def.body;
    ast_node_list_t *ast_redirections = node->data.function_def.redirections;
    string_t *name = node->data.function_def.name;
    exec_result_t result = {.status = EXEC_OK,
                            .has_exit_status = true,
                            .exit_status = 0,
                            .flow = EXEC_FLOW_NORMAL,
                            .flow_depth = 0};

    // Convert AST redirections to exec_redirections_t format
    exec_redirections_t *redirections = NULL;
    if (ast_redirections && ast_node_list_size(ast_redirections) > 0)
    {
        redirections = exec_redirections_from_ast(frame, ast_redirections);
        if (!redirections)
        {
            exec_set_error(frame->executor, "Failed to convert redirections for function '%s'",
                           string_cstr(name));
            result.status = EXEC_ERROR;
            result.exit_status = 1;
            return result;
        }
    }

    func_store_t *func_store = frame->functions;
    func_store_insert_result_t ret = func_store_add_ex(func_store, name, body, redirections);

        if (ret.error != FUNC_STORE_ERROR_NONE)
    {
        exec_set_error(frame->executor, "Failed to define function '%s'", string_cstr(name));
        // Clean up redirections on failure
        if (redirections)
            exec_redirections_destroy(&redirections);
        result.status = EXEC_ERROR;
        result.exit_status = 1;
        return result;
    }

    return result;
}

exec_result_t exec_redirected_command(exec_frame_t *frame, ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects(node->type == AST_REDIRECTED_COMMAND);

    ast_node_t *command = node->data.redirected_command.command;
    ast_node_list_t *ast_redirections = node->data.redirected_command.redirections;

    // Convert AST redirections to exec_redirections_t format
    exec_redirections_t *redirections = exec_redirections_from_ast(frame, ast_redirections);
    if (!redirections && ast_redirections && ast_node_list_size(ast_redirections) > 0)
    {
        // Conversion failed
        exec_result_t error_result = {.status = EXEC_ERROR,
                                      .has_exit_status = true,
                                      .exit_status = 1,
                                      .flow = EXEC_FLOW_NORMAL,
                                      .flow_depth = 0};
        return error_result;
    }

    // Execute the command with redirections using the brace group frame
    // (which shares state but applies/restores redirections)
    exec_result_t result = exec_brace_group(frame, command, redirections);

    // Clean up
    if (redirections)
        exec_redirections_destroy(&redirections);

    return result;
}

/**
 * Execute a while/until loop from within an EXEC_FRAME_LOOP frame.
 *
 * This function performs the actual loop iteration logic for while and until
 * constructs. It must be called from within an EXEC_FRAME_LOOP frame (created
 * by exec_while_loop() in exec_frame.c) so that break/continue semantics work
 * correctly.
 *
 * The EXEC_FRAME_LOOP frame provides:
 * - loop_depth tracking for nested loops
 * - EXEC_LOOP_TARGET policy for handling break/continue with proper depth
 * - EXEC_RETURN_TRANSPARENT policy so return statements propagate up to functions
 *
 * This function evaluates the condition on each iteration and executes the body
 * when appropriate (condition succeeds for while, fails for until). Control flow
 * statements (break, continue, return) in the body are handled by checking the
 * flow field in the result and responding accordingly.
 *
 * @param frame The current execution frame (must be EXEC_FRAME_LOOP)
 * @param params Parameters including condition, body, and until_mode flag
 * @return exec_result_t with final loop status and any pending control flow
 */
exec_result_t exec_condition_loop(exec_frame_t *frame, exec_params_t *params)
{
    Expects_not_null(frame);
    Expects_not_null(params);
    Expects_not_null(params->condition);
    Expects_not_null(params->body);

    exec_result_t result = {.status = EXEC_OK,
                            .has_exit_status = true,
                            .exit_status = 0,
                            .flow = EXEC_FLOW_NORMAL,
                            .flow_depth = 0};

    bool is_until = params->until_mode;

    while (true)
    {
        // Execute condition
        exec_result_t cond_result = exec_execute_dispatch(frame, params->condition);
        if (cond_result.status != EXEC_OK)
        {
            return cond_result;
        }

        // Check if we should exit the loop
        bool condition_met = (cond_result.exit_status == 0);
        if (is_until ? condition_met : !condition_met)
        {
            // Exit loop
            break;
        }

        // Execute body
        exec_result_t body_result;
        if (params->body->type == AST_COMMAND_LIST)
            body_result = exec_compound_list(frame, params->body);
        else
            body_result = exec_execute_dispatch(frame, params->body);

        // Handle control flow
        if (body_result.flow == EXEC_FLOW_BREAK)
        {
            if (body_result.flow_depth > 1)
            {
                // Breaking to outer loop
                body_result.flow_depth--;
                return body_result;
            }
            else
            {
                // Breaking this loop
                result.exit_status = body_result.exit_status;
                result.has_exit_status = body_result.has_exit_status;
                break;
            }
        }
        else if (body_result.flow == EXEC_FLOW_CONTINUE)
        {
            if (body_result.flow_depth > 1)
            {
                // Continuing to outer loop
                body_result.flow_depth--;
                return body_result;
            }
            // Continue this loop - just continue to next iteration
            result.exit_status = body_result.exit_status;
            result.has_exit_status = body_result.has_exit_status;
            continue;
        }
        else if (body_result.flow == EXEC_FLOW_RETURN)
        {
            // Return propagates up
            return body_result;
        }
        else if (body_result.status != EXEC_OK)
        {
            return body_result;
        }

        // Update exit status from body
        result.exit_status = body_result.exit_status;
        result.has_exit_status = body_result.has_exit_status;
    }

    return result;
}


/**
 * Execute a for loop from within an EXEC_FRAME_LOOP frame.
 *
 * This function performs the actual loop iteration logic for for constructs.
 * It must be called from within an EXEC_FRAME_LOOP frame (created by
 * exec_for_loop() in exec_frame.c) so that break/continue semantics work
 * correctly.
 *
 * The EXEC_FRAME_LOOP frame provides:
 * - loop_depth tracking for nested loops
 * - EXEC_LOOP_TARGET policy for handling break/continue with proper depth
 * - EXEC_RETURN_TRANSPARENT policy so return statements propagate up to functions
 *
 * This function iterates over the word list, setting the loop variable to each
 * word in turn and executing the body. Control flow statements (break, continue,
 * return) in the body are handled by checking the flow field in the result and
 * responding accordingly.
 *
 * @param frame The current execution frame (must be EXEC_FRAME_LOOP)
 * @param params Parameters including loop_var_name, iteration_words, and body
 * @return exec_result_t with final loop status and any pending control flow
 */
exec_result_t exec_iteration_loop(exec_frame_t *frame, exec_params_t *params)
{
    Expects_not_null(frame);
    Expects_not_null(params);
    Expects_not_null(params->loop_var_name);
    Expects_not_null(params->iteration_words);
    Expects_not_null(params->body);

    exec_result_t result = {.status = EXEC_OK,
                            .has_exit_status = true,
                            .exit_status = 0,
                            .flow = EXEC_FLOW_NORMAL,
                            .flow_depth = 0};

    // Iterate over each word
    for (size_t i = 0; i < string_list_size(params->iteration_words); i++)
    {
        const string_t *word = string_list_at(params->iteration_words, i);

        // Set loop variable
        variable_store_add(frame->variables, params->loop_var_name, word, false, false);

        // Execute body
        exec_result_t body_result;
        if (params->body->type == AST_COMMAND_LIST)
            body_result = exec_compound_list(frame, params->body);
        else
            body_result = exec_execute_dispatch(frame, params->body);

        // Handle control flow
        if (body_result.flow == EXEC_FLOW_BREAK)
        {
            if (body_result.flow_depth > 1)
            {
                // Breaking to outer loop
                body_result.flow_depth--;
                return body_result;
            }
            else
            {
                // Breaking this loop
                result.exit_status = body_result.exit_status;
                result.has_exit_status = body_result.has_exit_status;
                break;
            }
        }
        else if (body_result.flow == EXEC_FLOW_CONTINUE)
        {
            if (body_result.flow_depth > 1)
            {
                // Continuing to outer loop
                body_result.flow_depth--;
                return body_result;
            }
            // Continue this loop - just continue to next iteration
            result.exit_status = body_result.exit_status;
            result.has_exit_status = body_result.has_exit_status;
            continue;
        }
        else if (body_result.flow == EXEC_FLOW_RETURN)
        {
            // Return propagates up
            return body_result;
        }
        else if (body_result.status != EXEC_OK)
        {
            return body_result;
        }

        // Update exit status from body
        result.exit_status = body_result.exit_status;
        result.has_exit_status = body_result.has_exit_status;
    }

    return result;
}

/* ============================================================================
 * Helper: EINTR-safe waitpid
 * ============================================================================ */

#ifdef POSIX_API
/**
 * waitpid wrapper that retries on EINTR.
 * Returns the pid on success, -1 on real error (with errno set).
 */
static pid_t waitpid_eintr(pid_t pid, int *status, int options)
{
    pid_t ret;
    do
    {
        ret = waitpid(pid, status, options);
    } while (ret == -1 && errno == EINTR);
    return ret;
}
#endif

/* ============================================================================
 * Pipeline Orchestration
 * ============================================================================ */

/**
 * Execute a pipeline from within an EXEC_FRAME_PIPELINE frame.
 *
 * This function orchestrates the execution of multiple commands connected by
 * pipes. It creates pipes, forks child processes for each command, sets up
 * pipe plumbing, and waits for all children to complete.
 *
 * Process group handling follows the standard POSIX practice of calling
 * setpgid() in both parent and child to avoid race conditions. The first
 * child's PID becomes the pipeline's process group ID; subsequent children
 * join that group. Both parent and child call setpgid() because:
 *   - The child might run before the parent records pipeline_pgid
 *   - The parent might run before the child calls setpgid() on itself
 * One of the two calls will succeed; the other will get EACCES/ESRCH
 * harmlessly.
 *
 * @param frame  The current execution frame (must be EXEC_FRAME_PIPELINE)
 * @param params Parameters including pipeline_commands and pipeline_negated
 * @return exec_result_t with final pipeline status
 */
exec_result_t exec_pipeline_orchestrate(exec_frame_t *frame, exec_params_t *params)
{
    Expects_not_null(frame);
    Expects_not_null(params);
    Expects_not_null(params->pipeline_commands);

    ast_node_list_t *commands = params->pipeline_commands;
    bool is_negated = params->pipeline_negated;
    int ncmds = commands->size;

    exec_result_t result = {.status = EXEC_OK,
                            .has_exit_status = true,
                            .exit_status = 0,
                            .flow = EXEC_FLOW_NORMAL,
                            .flow_depth = 0};

    if (ncmds == 0)
    {
        result.exit_status = is_negated ? 1 : 0;
        return result;
    }

    if (ncmds == 1)
    {
        /* Single command — no pipes needed, just execute directly */
        ast_node_t *cmd = commands->nodes[0];
        exec_result_t cmd_result = exec_execute_dispatch(frame, cmd);
        if (is_negated && cmd_result.has_exit_status)
        {
            cmd_result.exit_status = (cmd_result.exit_status == 0) ? 1 : 0;
        }
        return cmd_result;
    }

#ifdef POSIX_API
    /*
     * Allocate pipes and PID array on the heap to avoid VLA stack overflow
     * with pathological input. We need (ncmds - 1) pipes, each being 2 fds.
     */
    int num_pipes = ncmds - 1;
    int *pipes = xcalloc(2 * num_pipes, sizeof(int));
    pid_t *pids = xcalloc(ncmds, sizeof(pid_t));

    /* Initialize all pipe fds to -1 so cleanup knows which were opened */
    for (int i = 0; i < 2 * num_pipes; i++)
    {
        pipes[i] = -1;
    }

    /* Create all pipes up front */
    for (int i = 0; i < num_pipes; i++)
    {
        if (pipe(pipes + 2 * i) == -1)
        {
            exec_set_error(frame->executor, "pipe() failed: %s", strerror(errno));
            result.status = EXEC_ERROR;
            result.exit_status = 1;
            goto cleanup;
        }
    }

    /* Initialize all PIDs to -1 so cleanup knows which children were forked */
    for (int i = 0; i < ncmds; i++)
    {
        pids[i] = -1;
    }

    pid_t pipeline_pgid = 0;

    /* Fork and execute each command */
    for (int i = 0; i < ncmds; i++)
    {
        ast_node_t *cmd = commands->nodes[i];

        pid_t pid = fork();
        if (pid == -1)
        {
            exec_set_error(frame->executor, "fork() failed in pipeline: %s", strerror(errno));

            /* Kill and reap all children we already forked */
            for (int j = 0; j < i; j++)
            {
                kill(pids[j], SIGTERM);
            }
            for (int j = 0; j < i; j++)
            {
                int discard;
                waitpid_eintr(pids[j], &discard, 0);
            }

            result.status = EXEC_ERROR;
            result.exit_status = 1;
            goto cleanup;
        }
        else if (pid == 0)
        {
            /* ---- Child process ---- */

            /*
             * Set up process group (child side).
             * For the first child (i == 0), pipeline_pgid is 0, so
             * setpgid(0, 0) creates a new group with this child as leader.
             * For subsequent children, pipeline_pgid was set by the parent
             * before this fork, so we join the existing group.
             *
             * Errors are ignored: EACCES means the parent already called
             * setpgid for us (or the child has already exec'd), and ESRCH
             * is similarly benign in this context.
             */
            setpgid(0, pipeline_pgid);

            /* Set up stdin from previous pipe */
            if (i > 0)
            {
                dup2(pipes[2 * (i - 1)], STDIN_FILENO);
            }

            /* Set up stdout to next pipe */
            if (i < ncmds - 1)
            {
                dup2(pipes[2 * i + 1], STDOUT_FILENO);
            }

            /* Close all pipe FDs in the child */
            for (int j = 0; j < 2 * num_pipes; j++)
            {
                close(pipes[j]);
            }

            /* Execute the command */
            exec_result_t cmd_result = exec_execute_dispatch(frame, cmd);

            /* Exit the child with the command's exit status */
            _exit(cmd_result.has_exit_status ? cmd_result.exit_status : 0);
        }

        /* ---- Parent process ---- */
        pids[i] = pid;

        if (i == 0)
        {
            /* First child is the group leader */
            pipeline_pgid = pid;
        }

        /*
         * Set up process group (parent side).
         * We call setpgid() here as well to eliminate the race condition
         * where the parent proceeds to fork the next child (which needs
         * pipeline_pgid) before the first child has called setpgid() on
         * itself. One of the parent/child setpgid calls will succeed;
         * the other will get EACCES (child already exec'd) which is fine.
         */
        setpgid(pid, pipeline_pgid);

        /*
         * Eagerly close pipe ends that the parent no longer needs.
         * After forking child i:
         *   - The write end of pipe i (pipes[2*i+1]) has been dup2'd into
         *     child i's stdout. Parent doesn't need it.
         *   - The read end of pipe i-1 (pipes[2*(i-1)]) has been dup2'd
         *     into child i's stdin. Parent doesn't need it.
         * Closing eagerly ensures that:
         *   - Children see EOF on their stdin when the upstream writer exits
         *   - We don't accumulate open fds across the fork loop
         */
        if (i < ncmds - 1)
        {
            close(pipes[2 * i + 1]);
            pipes[2 * i + 1] = -1;
        }
        if (i > 0)
        {
            close(pipes[2 * (i - 1)]);
            pipes[2 * (i - 1)] = -1;
        }
    }

    /* Close any remaining pipe fds the parent still holds */
    for (int i = 0; i < 2 * num_pipes; i++)
    {
        if (pipes[i] >= 0)
        {
            close(pipes[i]);
            pipes[i] = -1;
        }
    }

    /* Wait for all children (collect every status for future pipefail support) */
    int last_status = 0;
    for (int i = 0; i < ncmds; i++)
    {
        int status;
        pid_t waited = waitpid_eintr(pids[i], &status, 0);
        if (waited < 0)
        {
            /*
             * ECHILD means the child was already reaped (e.g. by a signal
             * handler). Treat as exit status 127. For any other error,
             * report but continue waiting for remaining children.
             */
            if (errno != ECHILD)
            {
                exec_set_error(frame->executor, "waitpid() failed for pid %d: %s", (int)pids[i],
                               strerror(errno));
            }
            if (i == ncmds - 1)
            {
                last_status = 127;
            }
            continue;
        }

        int child_status;
        if (WIFEXITED(status))
        {
            child_status = WEXITSTATUS(status);
        }
        else if (WIFSIGNALED(status))
        {
            child_status = 128 + WTERMSIG(status);
        }
        else
        {
            child_status = 127;
        }

        /*
         * TODO: Store child_status in frame->executor->pipe_statuses[i]
         * for pipefail / $PIPESTATUS support.
         */

        if (i == ncmds - 1)
        {
            last_status = child_status;
        }
    }

    result.exit_status = is_negated ? (last_status == 0 ? 1 : 0) : last_status;
    frame->last_exit_status = result.exit_status;

cleanup:
    /* Free heap-allocated arrays */
    for (int i = 0; i < 2 * num_pipes; i++)
    {
        if (pipes[i] >= 0)
        {
            close(pipes[i]);
        }
    }
    xfree(pipes);
    xfree(pids);

    return result;

#else
    /* For non-POSIX systems, multi-command pipelines are not supported */
    exec_set_error(frame->executor, "Pipelines not supported on this platform");
    result.status = EXEC_NOT_IMPL;
    result.exit_status = 1;
    return result;
#endif
}

exec_result_t exec_case_clause(exec_frame_t *frame, ast_node_t *node)
{
    Expects_not_null(frame);
    Expects_not_null(node);
    Expects(node->type == AST_CASE_CLAUSE);

    exec_result_t result = {.status = EXEC_OK,
                            .has_exit_status = true,
                            .exit_status = 0,
                            .flow = EXEC_FLOW_NORMAL,
                            .flow_depth = 0};

    /* Get the word to match against */
    token_t *word_token = node->data.case_clause.word;
    if (!word_token)
    {
        return result;
    }

    /*
     * POSIX XCU 2.9.4.3: The case word undergoes tilde expansion, parameter
     * expansion, command substitution, and arithmetic expansion — but NOT
     * field splitting or pathname expansion.
     */
    string_t *word = expand_word_nosplit(frame, word_token);
    if (!word)
    {
        exec_set_error(frame->executor, "Failed to expand case word");
        result.status = EXEC_ERROR;
        result.exit_status = 1;
        return result;
    }

    /* Iterate through case items */
    ast_node_list_t *case_items = node->data.case_clause.case_items;
    if (!case_items)
    {
        string_destroy(&word);
        return result;
    }

    bool matched = false;
    for (int i = 0; i < case_items->size && !matched; i++)
    {
        ast_node_t *item = case_items->nodes[i];
        if (!item || item->type != AST_CASE_ITEM)
        {
            continue;
        }

        token_list_t *patterns = item->data.case_item.patterns;
        if (!patterns)
        {
            continue;
        }

        for (int j = 0; j < token_list_size(patterns); j++)
        {
            const token_t *pattern_token = token_list_get(patterns, j);

            /*
             * POSIX XCU 2.9.4.3: Each pattern undergoes the same expansions
             * as the case word (tilde, parameter, command subst, arithmetic)
             * but NOT field splitting or pathname expansion. The pattern
             * metacharacters (*, ?, [...]) retain their special meaning for
             * fnmatch-style matching against the case word — they are NOT
             * used for filesystem globbing.
             */
            string_t *pattern = expand_word_nosplit(frame, pattern_token);
            if (!pattern)
            {
                continue;
            }

            /* Match pattern against word */
            bool pattern_matches = false;
#ifdef POSIX_API
            pattern_matches = (fnmatch(string_cstr(pattern), string_cstr(word), 0) == 0);
#else
            pattern_matches = glob_util_match(string_cstr(pattern), string_cstr(word), 0);
#endif

            string_destroy(&pattern);

            if (pattern_matches)
            {
                matched = true;

                /* Execute the body of this case item */
                ast_node_t *body = item->data.case_item.body;
                if (body)
                {
                    result = exec_execute_dispatch(frame, body);
                }

                break;
            }
        }
    }

    string_destroy(&word);

    /* If no match found, exit status remains 0 */
    return result;
}

#endif
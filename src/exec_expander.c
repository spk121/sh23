#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <string.h>
#include "exec_expander.h"
#include "exec_internal.h"
#include "logging.h"
#include "string_t.h"
#include "variable_store.h"

#ifdef POSIX_API
#include <sys/wait.h>
#include <wordexp.h>
#include <pwd.h>
#include <unistd.h>
#endif

#ifdef UCRT_API
#include <io.h>
#include <errno.h>
#include <stdlib.h>
#endif

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

// Record the exit status observed during command substitution. The POSIX rule
// that a simple command with no command name (e.g., pure assignments containing
// substitutions) inherits the last substitution's status is enforced by the
// caller; here we merely stash the status on the executor for later use.
static void exec_record_subst_status(exec_t *executor, int raw_status)
{
    if (executor == NULL)
    {
        return;
    }

#ifdef POSIX_API
    int status = raw_status;
    if (WIFEXITED(raw_status))
    {
        status = WEXITSTATUS(raw_status);
    }
    else if (WIFSIGNALED(raw_status))
    {
        status = 128 + WTERMSIG(raw_status);
    }
#else
    int status = raw_status;
#endif

    exec_set_exit_status(executor, status);
}

/* ============================================================================
 * Command Substitution Callback
 * ============================================================================ */

string_t *exec_command_subst_callback(void *userdata, const string_t *command)
{
#ifdef POSIX_API
    exec_t *executor = (exec_t *)userdata;
    const char *cmd = string_cstr(command);
    if (cmd == NULL || *cmd == '\0')
    {
        exec_record_subst_status(executor, 0);
        return string_create();
    }

    FILE *pipe = popen(cmd, "r");
    if (pipe == NULL)
    {
        log_error("exec_command_subst_callback: popen failed for '%s'", cmd);
        exec_record_subst_status(executor, 1);
        return string_create();
    }

    string_t *output = string_create();
    char buffer[256];

    while (fgets(buffer, sizeof(buffer), pipe) != NULL)
    {
        string_append_cstr(output, buffer);
    }

    int exit_code = pclose(pipe);
    if (exit_code != 0)
    {
        log_debug("exec_command_subst_callback: child exited with code %d for '%s'", exit_code, cmd);
    }
    exec_record_subst_status(executor, exit_code);

    // Trim trailing newlines/carriage returns to approximate shell command substitution behavior
    while (string_length(output) > 0)
    {
        char last = string_back(output);
        if (last == '\n' || last == '\r')
        {
            string_pop_back(output);
        }
        else
        {
            break;
        }
    }

    return output;
#elifdef UCRT_API
    exec_t *executor = (exec_t *)userdata;
    const char *cmd = string_cstr(command);
    if (cmd == NULL || *cmd == '\0')
    {
        exec_record_subst_status(executor, 0);
        return string_create();
    }

    FILE *pipe = _popen(cmd, "r");
    if (pipe == NULL)
    {
        log_error("exec_command_subst_callback: _popen failed for '%s'", cmd);
        exec_record_subst_status(executor, 1);
        return string_create();
    }

    string_t *output = string_create();
    char buffer[256];

    while (fgets(buffer, sizeof(buffer), pipe) != NULL)
    {
        string_append_cstr(output, buffer);
    }

    int exit_code = _pclose(pipe);
    if (exit_code != 0)
    {
        log_debug("exec_command_subst_callback: child exited with code %d for '%s'", exit_code, cmd);
    }
    exec_record_subst_status(executor, exit_code);

    // Trim trailing newlines/carriage returns to approximate shell command substitution behavior
    while (string_length(output) > 0)
    {
        char last = string_back(output);
        if (last == '\n' || last == '\r')
        {
            string_pop_back(output);
        }
        else
        {
            break;
        }
    }

    return output;
#else
    // There is no portable way to do command substitution in ISO_C.
    // You could run a shell process via system(), but, without capturing output.
    (void)command;    // unused
    exec_record_subst_status((exec_t *)userdata, 0);
    return string_create();
#endif
}

/* ============================================================================
 * Pathname Expansion (Glob) Callback
 * ============================================================================ */

string_list_t *exec_pathname_expansion_callback(void *user_data, const string_t *pattern)
{
#ifdef POSIX_API
    (void)user_data;  // unused

    const char *pattern_str = string_data(pattern);
    wordexp_t we;
    int ret;

    /* WRDE_NOCMD: prevent command substitution (security)
     * WRDE_UNDEF: fail on undefined variables (like $foo)
     * No WRDE_SHOWERR — we silence errors about ~nonexistentuser
     */
    ret = wordexp(pattern_str, &we, WRDE_NOCMD | WRDE_UNDEF);

    if (ret != 0 || we.we_wordc == 0) {
        /* Possible return values:
         * WRDE_BADCHAR: illegal char like | or ;
         * WRDE_BADVAL: undefined variable reference (with WRDE_UNDEF)
         * WRDE_CMDSUB: command substitution (blocked by WRDE_NOCMD)
         * WRDE_NOSPACE: out of memory
         * WRDE_SYNTAX: shell syntax error
         * Or we_wordc == 0: no matches
         */
        if (ret != 0) {
            wordfree(&we);  // safe to call even on failure
        }
        return NULL;  // treat errors and no matches the same: keep literal
    }

    /* Success and at least one match */
    string_list_t *result = string_list_create();

    for (size_t i = 0; i < we.we_wordc; i++) {
        string_t *path = string_create_from_cstr(we.we_wordv[i]);
        string_list_move_push_back(result, &path);
    }

    wordfree(&we);
    return result;  

#elifdef UCRT_API
    (void)user_data;  // unused

    const char *pattern_str = string_cstr(pattern);
    log_debug("exec_pathname_expansion_callback: UCRT glob pattern='%s'", pattern_str);
    struct _finddata_t fd;
    intptr_t handle;

    // Attempt to find first matching file
    handle = _findfirst(pattern_str, &fd);
    if (handle == -1L) {
        if (errno == ENOENT) {
            // No matches found
            return NULL;
        }
        // Other error (access denied, etc.)
        return NULL;
    }

    // Create result list
    string_list_t *result = string_list_create();

    // Add all matching files
    do {
        // Skip . and .. entries
        if (strcmp(fd.name, ".") == 0 || strcmp(fd.name, "..") == 0)
            continue;

        // Add the matched filename to the result list
        string_t *filename = string_create_from_cstr(fd.name);
        string_list_move_push_back(result, &filename);

    } while (_findnext(handle, &fd) == 0);

    _findclose(handle);

    // If no files were added (only . and .. were found), return NULL
    if (string_list_size(result) == 0) {
        string_list_destroy(&result);
        return NULL;
    }

    return result;

#else
    /* In ISO_C environments, no glob implementation is available */
    (void)user_data;  // unused
    string_list_t *result = string_list_create();
    string_list_push_back(result, pattern);
    log_warn("exec_pathname_expansion_callback: No glob implementation available");
    return result;
#endif
}

/* ============================================================================
 * Environment Variable Lookup Callback
 * ============================================================================ */

string_t *exec_getenv_callback(void *userdata, const string_t *name)
{
    exec_t *executor = (exec_t *)userdata;
    if (!executor || !executor->variables)
    {
        return NULL;
    }

    const string_t *value = variable_store_get_value(executor->variables, name);
    if (!value)
    {
        return NULL;
    }

    return string_create_from(value);
}

/* ============================================================================
 * Tilde Expansion Callback
 * ============================================================================ */

string_t *exec_tilde_expand_callback(void *userdata, const string_t *username)
{
#ifdef POSIX_API
    (void)userdata; // unused

    struct passwd *pw;
    
    if (username == NULL || string_length(username) == 0)
    {
        // Expand ~ to current user's home
        pw = getpwuid(getuid());
    }
    else
    {
        // Expand ~username to specified user's home
        pw = getpwnam(string_cstr(username));
    }

    if (pw == NULL || pw->pw_dir == NULL)
    {
        return NULL;
    }

    return string_create_from_cstr(pw->pw_dir);

#elifdef UCRT_API
    (void)userdata; // unused

    // Windows tilde expansion
    if (username == NULL || string_length(username) == 0)
    {
        // Expand ~ to current user's home (use USERPROFILE)
        const char *home = getenv("USERPROFILE");
        if (home == NULL)
        {
            home = getenv("HOME");
        }
        
        if (home != NULL)
        {
            return string_create_from_cstr(home);
        }
    }
    // ~username not supported on Windows
    
    return NULL;

#else
    // ISO_C: no tilde expansion available
    (void)userdata;
    (void)username;
    return NULL;
#endif
}
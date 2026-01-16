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
#include "glob_util.h"
#include "xalloc.h"

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

string_t *exec_command_subst_callback(exec_t *executor, const string_t *command)
{
#ifdef POSIX_API
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
    exec_record_subst_status(executor, 0);
    return string_create();
#endif
}

/* ============================================================================
 * Pathname Expansion (Glob) Callback
 * ============================================================================ */

/* ============================================================================
 * Environment Variable Lookup Callback
 * ============================================================================ */

/* ============================================================================
 * Tilde Expansion Callback
 * ============================================================================ */

string_t *exec_expand_tilde(exec_t *userdata, const string_t *username)
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

/* ============================================================================
 * Word Expansion Implementation
 * ============================================================================ */

/**
 * Expand a parameter reference (e.g., $var, $1, $@).
 */
static string_t *expand_parameter(exec_t *executor, const part_t *part)
{
    if (!part->param_name)
    {
        return string_create(); // Empty string if param_name is NULL
    }

    const char *name = string_cstr(part->param_name);
    string_t *result = NULL;

    positional_params_t *params = executor->positional_params;
    variable_store_t *vars = executor->variables;

    // Check for positional parameters
    if (strcmp(name, "#") == 0)
    {
        if (params)
        {
            int count = positional_params_count(params);
            result = string_from_int(count);
        }
    }
    else if (strcmp(name, "@") == 0)
    {
        if (params)
        {
            result = positional_params_get_all_joined(params, ' ');
        }
    }
    else if (strcmp(name, "*") == 0)
    {
        if (params)
        {
            result = positional_params_get_all_joined(params, ' ');
        }
    }
    else
    {
        // Check if name is a digit (positional param like $1, $2, etc.)
        char *endptr;
        int n = (int)strtol(name, &endptr, 10);
        if (*endptr == '\0')
        {
            if (n > 0 && params)
            {
                const string_t *param = positional_params_get(params, n);
                if (param)
                {
                    result = string_create_from(param);
                }
            }
            else if (n == 0 && params)
            {
                result = string_create_from(params->arg0);
            }
        }
    }

    // If not a positional param, check variable store and environment
    if (!result)
    {
        const char *value = NULL;
        if (vars)
        {
            value = variable_store_get_value_cstr(vars, name);
        }
        if (!value)
        {
            string_t *value_str = string_create_from_cstr(getenv(string_cstr(part->param_name)));
            if (value_str)
            {
                result = value_str; // Take ownership
            }
        }
        else
        {
            result = string_create_from_cstr(value);
        }
        
        if (!result)
        {
            result = string_create(); // Empty string if not set
        }
    }

    return result;
}

/**
 * Expand a command substitution.
 */
static string_t *expand_command_subst(exec_t *executor, const part_t *part)
{
    // Convert nested tokens to a command string
    string_t *cmd = string_create();
    for (int i = 0; i < token_list_size(part->nested); i++)
    {
        token_t *t = token_list_get(part->nested, i);
        string_t *ts = token_to_string(t);
        string_append(cmd, ts);
        string_destroy(&ts);
        if (i < token_list_size(part->nested) - 1)
        {
            string_append_cstr(cmd, " ");
        }
    }
    
    string_t *result = exec_command_subst_callback(executor, cmd);
    string_destroy(&cmd);
    return result ? result : string_create();
}

/**
 * Expand an arithmetic expression.
 */
static string_t *expand_arithmetic(exec_t *executor, const part_t *part)
{
    // Stub: For now, return a placeholder
    (void)executor;
    (void)part;
    return string_create_from_cstr("42");
}

/**
 * Expand a tilde prefix.
 */
static string_t *expand_tilde(exec_t *executor, const part_t *part)
{
    return exec_expand_tilde(executor, part->text);
}

/**
 * Expand all parts of a token to a single string.
 */
static string_t *expand_parts_to_string(exec_t *executor, const part_list_t *parts)
{
    string_t *result = string_create();
    for (int i = 0; i < part_list_size(parts); i++)
    {
        const part_t *part = part_list_get(parts, i);
        string_t *part_expanded = NULL;
        
        switch (part->type)
        {
        case PART_LITERAL:
            part_expanded = string_create_from(part->text);
            break;
        case PART_PARAMETER:
            part_expanded = expand_parameter(executor, part);
            break;
        case PART_COMMAND_SUBST:
            part_expanded = expand_command_subst(executor, part);
            break;
        case PART_ARITHMETIC:
            part_expanded = expand_arithmetic(executor, part);
            break;
        case PART_TILDE:
            part_expanded = expand_tilde(executor, part);
            break;
        }
        
        if (part_expanded)
        {
            string_append(result, part_expanded);
            string_destroy(&part_expanded);
        }
    }
    return result;
}

/* ============================================================================
 * Public Expansion Functions
 * ============================================================================ */

string_list_t *exec_expand_word(exec_t *executor, const token_t *tok)
{
    if (!tok || tok->type != TOKEN_WORD)
    {
        return NULL;
    }

    // Check if we need any processing at all
    if (!tok->needs_expansion && !tok->needs_field_splitting && !tok->needs_pathname_expansion)
    {
        // No expansion needed: return literal text as single string
        string_t *text = token_get_all_text(tok);
        string_list_t *result = string_list_create();
        string_list_move_push_back(result, &text);
        return result;
    }

    // Expand each part
    string_t *expanded = expand_parts_to_string(executor, token_get_parts_const(tok));

    // Get IFS for field splitting
    const char *ifs = NULL;
    if (executor->variables)
    {
        ifs = variable_store_get_value_cstr(executor->variables, "IFS");
    }
    if (!ifs)
    {
        ifs = getenv("IFS");
    }
    if (!ifs)
    {
        ifs = " \t\n"; // Default IFS
    }

    // Field splitting
    string_list_t *fields = string_list_create();
    bool do_split = tok->needs_field_splitting && *ifs != '\0';
    
    if (do_split)
    {
        char *str = string_release(&expanded);
        if (str && *str != '\0')
        {
            char *token = strtok(str, ifs);
            while (token)
            {
                string_list_push_back(fields, string_create_from_cstr(token));
                token = strtok(NULL, ifs);
            }
        }
        xfree(str);

        if (string_list_size(fields) == 0)
        {
            string_list_push_back(fields, string_create());
        }
    }
    else
    {
        if (expanded)
        {
            string_list_move_push_back(fields, &expanded);
        }
        else
        {
            string_t *empty_str = string_create();
            string_list_move_push_back(fields, &empty_str);
        }
    }

    // Pathname expansion (globbing)
    if (tok->needs_pathname_expansion)
    {
        string_list_t *globs = string_list_create();
        for (int i = 0; i < string_list_size(fields); i++)
        {
            const string_t *pattern = string_list_at(fields, i);
            string_list_t *matches = glob_util_expand_path(pattern);
            if (matches)
            {
                for (int j = 0; j < string_list_size(matches); j++)
                {
                    string_list_push_back(globs, string_list_at(matches, j));
                }
                string_list_destroy(&matches);
            }
            else
            {
                string_list_push_back(globs, pattern);
            }
        }
        string_list_destroy(&fields);
        return globs;
    }
    else
    {
        return fields;
    }
}

string_list_t *exec_expand_words(exec_t *executor, const token_list_t *tokens)
{
    if (!tokens)
    {
        return NULL;
    }
    
    string_list_t *result = string_list_create();
    for (int i = 0; i < token_list_size(tokens); i++)
    {
        token_t *tok = token_list_get(tokens, i);
        string_list_t *expanded = exec_expand_word(executor, tok);
        if (expanded)
        {
            for (int j = 0; j < string_list_size(expanded); j++)
            {
                string_list_push_back(result, string_list_at(expanded, j));
            }
            string_list_destroy(&expanded);
        }
    }
    return result;
}

string_t *exec_expand_redirection_target(exec_t *executor, const token_t *tok)
{
    if (!tok || tok->type != TOKEN_WORD)
    {
        return NULL;
    }
    // Redirection targets undergo tilde, parameter, command, arithmetic expansion,
    // but no field splitting or pathname expansion
    return expand_parts_to_string(executor, token_get_parts_const(tok));
}

string_t *exec_expand_assignment_value(exec_t *executor, const token_t *tok)
{
    if (!tok || tok->type != TOKEN_ASSIGNMENT_WORD)
    {
        return NULL;
    }
    // Assignment values undergo tilde, parameter, command, arithmetic expansion,
    // but no field splitting or pathname expansion
    return expand_parts_to_string(executor, tok->assignment_value);
}

string_t *exec_expand_heredoc(exec_t *executor, const string_t *body, bool is_quoted)
{
    if (is_quoted)
    {
        return string_create_from(body);
    }
    // For unquoted heredocs, perform parameter, command, arithmetic expansions
    // This is a stub; proper implementation would require tokenizing the heredoc body
    (void)executor;
    return string_create_from(body);
}
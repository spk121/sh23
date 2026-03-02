/**
 * exec_expander.c - Word expansion implementation
 *
 * Performs POSIX word expansion in the context of execution frames.
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "arithmetic.h"
#include "exec_expander.h"
#include "exec_frame.h"
#include "exec_internal.h"
#include "glob_util.h"
#include "logging.h"
#include "pattern_removal.h"
#include "string_t.h"
#include "variable_store.h"
#include "xalloc.h"

#ifdef POSIX_API
#include <pwd.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef UCRT_API
#include <errno.h>
#include <io.h>
#endif

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Get the IFS value from the frame, falling back to default.
 */
static string_t *get_ifs(exec_frame_t *frame)
{
    Expects_not_null(frame);

    string_t *ifs_var = NULL;
    if (frame_has_variable_cstr(frame, "IFS"))
    {
        ifs_var = frame_get_variable_cstr(frame, "IFS");
        /* If IFS is empty, this had to be an explicit choice by the user, since
         * it is initialized to <space><tab><newline> by default. So we intentionally
         * don't check it here. */
    }
    else 
    {
        char *ifs_env = getenv("IFS");
        if (ifs_env)
        {
            ifs_var = string_create_from_cstr(ifs_env);
        }
        else
        {
            /* Default IFS is space, tab, newline */
            ifs_var = string_create_from_cstr(" \t\n");
        }
    }

    return ifs_var;
}

/**
 * Record the exit status from a command substitution.
 */
static void record_subst_status(exec_frame_t *frame, int raw_status)
{
    if (!frame)
        return;

#ifdef POSIX_API
    int status;
    if (WIFEXITED(raw_status))
    {
        status = WEXITSTATUS(raw_status);
    }
    else if (WIFSIGNALED(raw_status))
    {
        status = 128 + WTERMSIG(raw_status);
    }
    else
    {
        status = raw_status;
    }
#else
    int status = raw_status;
#endif

    frame->last_exit_status = status;
}

/**
 * Strip trailing newlines from a string (in-place modification via string API).
 */
static void strip_trailing_newlines(string_t *str)
{
    while (string_length(str) > 0)
    {
        char last = string_back(str);
        if (last == '\n' || last == '\r')
        {
            string_pop_back(str);
        }
        else
        {
            break;
        }
    }
}

/* ============================================================================
 * Tilde Expansion
 * ============================================================================ */

string_t *expand_tilde(exec_frame_t *frame, const string_t *username)
{
#ifdef POSIX_API
    struct passwd *pw;

    if (username == NULL || string_length(username) == 0)
    {
        /* Expand ~ to current user's home */
        pw = getpwuid(getuid());
    }
    else
    {
        const char *uname = string_cstr(username);
        
        /* Check for special ~+ and ~- forms */
        if (strcmp(uname, "+") == 0)
        {
            /* ~+ expands to PWD */
            const char *pwd = frame ? variable_store_get_value_cstr(
                exec_frame_get_variables(frame), "PWD") : NULL;
            if (!pwd)
                pwd = getenv("PWD");
            if (pwd)
                return string_create_from_cstr(pwd);
            return NULL;
        }
        else if (strcmp(uname, "-") == 0)
        {
            /* ~- expands to OLDPWD */
            const char *oldpwd = frame ? variable_store_get_value_cstr(
                exec_frame_get_variables(frame), "OLDPWD") : NULL;
            if (!oldpwd)
                oldpwd = getenv("OLDPWD");
            if (oldpwd)
                return string_create_from_cstr(oldpwd);
            return NULL;
        }
        
        /* Expand ~username to specified user's home */
        pw = getpwnam(uname);
    }

    if (pw == NULL || pw->pw_dir == NULL)
    {
        return NULL;
    }

    return string_create_from_cstr(pw->pw_dir);

#elifdef UCRT_API
    if (username == NULL || string_length(username) == 0)
    {
        /* Expand ~ to current user's home */
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
    else
    {
        const char *uname = string_cstr(username);
        
        /* Check for special ~+ and ~- forms */
        if (strcmp(uname, "+") == 0)
        {
            const char *pwd = frame ? variable_store_get_value_cstr(
                exec_frame_get_variables(frame), "PWD") : NULL;
            if (!pwd)
                pwd = getenv("PWD");
            if (pwd)
                return string_create_from_cstr(pwd);
        }
        else if (strcmp(uname, "-") == 0)
        {
            const char *oldpwd = frame ? variable_store_get_value_cstr(
                exec_frame_get_variables(frame), "OLDPWD") : NULL;
            if (!oldpwd)
                oldpwd = getenv("OLDPWD");
            if (oldpwd)
                return string_create_from_cstr(oldpwd);
        }
    }
    /* ~username not supported on Windows */
    return NULL;

#else
    if (username == NULL || string_length(username) == 0)
    {
        /* ISO C: try HOME environment variable */
        const char *home = getenv("HOME");
        if (home != NULL)
        {
            return string_create_from_cstr(home);
        }
    }
    else
    {
        const char *uname = string_cstr(username);
        
        /* Check for special ~+ and ~- forms */
        if (strcmp(uname, "+") == 0)
        {
            const char *pwd = getenv("PWD");
            if (pwd)
                return string_create_from_cstr(pwd);
        }
        else if (strcmp(uname, "-") == 0)
        {
            const char *oldpwd = getenv("OLDPWD");
            if (oldpwd)
                return string_create_from_cstr(oldpwd);
        }
    }
    return NULL;
#endif
}

/* ============================================================================
 * Parameter Expansion
 * ============================================================================ */

/**
 * Get the value of a parameter (variable or special param).
 * Returns NULL if not set.
 */
static string_t *get_parameter_value(const exec_frame_t *frame, const string_t *name)
{
    if (!name || string_length(name) == 0)
    {
        return NULL;
    }

    const char *name_cstr = string_cstr(name);

    /* Check for special parameters first */
    string_t *special = expand_special_param(frame, name);
    if (special)
    {
        return special;
    }

    /* Look up in variable store */
    if (frame)
    {
        /* Check local variables first (if frame supports them) */
        const string_t *value = exec_frame_get_variable(frame, name);
        if (value)
        {
            return string_create_from(value);
        }
    }

    /* Fall back to environment */
    const char *env_val = getenv(name_cstr);
    if (env_val)
    {
        return string_create_from_cstr(env_val);
    }

    return NULL; /* Not set */
}

/**
 * Set a variable in the frame.
 */
static void set_parameter_value(exec_frame_t *frame, const string_t *name, const string_t *value)
{
    if (!frame || !name || !value)
        return;

    variable_store_t *vars = exec_frame_get_variables(frame);
    if (vars)
    {
        variable_store_add_cstr(vars, string_cstr(name), string_cstr(value), false, false);
    }
}

/**
 * Check if a parameter is set (even if empty).
 */
static bool is_parameter_set(exec_frame_t *frame, const string_t *name)
{
    string_t *value = get_parameter_value(frame, name);
    if (value)
    {
        string_destroy(&value);
        return true;
    }
    return false;
}

/**
 * Expand parameter with modifiers (${var:-word}, ${var#pattern}, etc.)
 * Despite looking like an accessor, this function may have side effects
 * (e.g., modifying the frame's last_exit_status or assigning variables for := modifier).
 */
static string_t *expand_parameter_with_modifier(exec_frame_t *frame, const part_t *part)
{
    string_t *v = get_parameter_value(frame, part->param_name);
    bool is_set = (v != NULL);
    bool is_null = (v == NULL || string_length(v) == 0);

    switch (part->param_kind)
    {
    case PARAM_PLAIN:
        /* Simple parameter expansion */
        if (v)
        {
            return v;
        }
        return string_create();

    case PARAM_LENGTH:
        /* ${#var} - length of parameter */
        if (v)
        {
            int len = string_length(v);
            string_destroy(&v);
            return string_from_int(len);
        }
        return string_create_from_cstr("0");

    case PARAM_USE_DEFAULT:
        /* ${var:-word} - use default if unset or null */
        if (is_null)
        {
            /* Expand the word */
            if (part->word)
            {
                return string_create_from(part->word);
            }
            return string_create();
        }
        return v;

    case PARAM_ASSIGN_DEFAULT:
        /* ${var:=word} - assign default if unset or null */
        if (is_null)
        {
            /* Expand and assign the word */
            if (part->word)
            {
                string_t *value = string_create_from(part->word);
                set_parameter_value(frame, part->param_name, value);
                return value;
            }
            return string_create();
        }
        return v;

    case PARAM_ERROR_IF_UNSET:
        /* ${var:?word} - error if unset or null */
        if (is_null)
        {
            const char *msg = part->word ? string_cstr(part->word) : "parameter null or not set";
            log_error("Parameter expansion error: %s: %s", string_cstr(part->param_name), msg);
            if (frame)
            {
                frame->last_exit_status = 1;
            }
            return string_create();
        }
        return v;

    case PARAM_USE_ALTERNATE:
        /* ${var:+word} - use alternate if set and not null */
        if (!is_null)
        {
            /* Expand the word */
            if (part->word)
            {
                return string_create_from(part->word);
            }
            return string_create();
        }
        return string_create();

    case PARAM_REMOVE_SMALL_SUFFIX:
        /* ${var%pattern} - remove smallest matching suffix */
        if (v)
        {
            if (part->word && string_length(part->word) > 0)
            {
                string_t *result = remove_suffix_smallest(v, part->word);
                string_destroy(&v);
                return result;
            }
            return v;
        }
        return string_create();

    case PARAM_REMOVE_LARGE_SUFFIX:
        /* ${var%%pattern} - remove largest matching suffix */
        if (v)
        {
            if (part->word && string_length(part->word) > 0)
            {
                string_t *result = remove_suffix_largest(v, part->word);
                string_destroy(&v);
                return result;
            }
            return v;
        }
        return string_create();

    case PARAM_REMOVE_SMALL_PREFIX:
        /* ${var#pattern} - remove smallest matching prefix */
        if (v)
        {
            if (part->word && string_length(part->word) > 0)
            {
                string_t *result = remove_prefix_smallest(v, part->word);
                string_destroy(&v);
                return result;
            }
            return v;
        }
        return string_create();

    case PARAM_REMOVE_LARGE_PREFIX:
        /* ${var##pattern} - remove largest matching prefix */
        if (v)
        {
            if (part->word && string_length(part->word) > 0)
            {
                string_t *result = remove_prefix_largest(v, part->word);
                string_destroy(&v);
                return result;
            }
            return v;
        }
        return string_create();

    case PARAM_INDIRECT:
        /* ${!var} - indirect expansion */
        /* First, get the value of the named variable */
        if (v)
        {
            /* The value contains the name of another variable */
            /* Now expand that variable */
            string_t *indirect_value = get_parameter_value(frame, v);
            string_destroy(&v);
            
            if (indirect_value)
            {
                return indirect_value;
            }
            return string_create();
        }
        else
        {
            /* Variable not set, return empty */
            return string_create();
        }

    default:
        return string_create();
    }
}

string_t *expand_parameter(exec_frame_t *frame, const string_t *name)
{
    if (!name || string_length(name) == 0)
    {
        return string_create();
    }

    string_t *value = get_parameter_value(frame, name);
    if (value)
    {
        return value;
    }

    return string_create(); /* Empty string if not set */
}

string_t *expand_special_param(const exec_frame_t *frame, const string_t *name)
{
    if (!name || string_length(name) == 0)
    {
        return NULL;
    }

    const char *n = string_cstr(name);

    /* Single character special parameters */
    if (string_length(name) == 1)
    {
        switch (n[0])
        {
        case '?':
            /* Exit status of last command */
            if (frame)
            {
                return string_from_int(frame->last_exit_status);
            }
            return string_create_from_cstr("0");

        case '$':
            /* PID of shell */
            if (frame && frame->executor)
            {
                return string_from_int(frame->executor->shell_pid);
            }
            return string_create_from_cstr("0");

        case '!':
            /* PID of last background job */
            if (frame)
            {
                return string_from_int(frame->last_bg_pid);
            }
            return string_create_from_cstr("");

        case '#':
            /* Number of positional parameters */
            if (frame && frame->positional_params)
            {
                int count = positional_params_count(frame->positional_params);
                return string_from_int(count);
            }
            return string_create_from_cstr("0");

        case '@':
        case '*':
            /* All positional parameters */
            if (frame && frame->positional_params)
            {
                return positional_params_get_all_joined(frame->positional_params, ' ');
            }
            return string_create();

        case '-':
            /* Current option flags */
            /* TODO: Implement option flags string */
            return string_create();

        case '0':
            /* Shell or script name */
            if (frame && frame->positional_params)
            {
                const string_t *arg0 = positional_params_get_arg0(frame->positional_params);
                if (arg0)
                {
                    return string_create_from(arg0);
                }
            }
            return string_create_from_cstr("mgsh");
        }
    }

    /* Check for numeric positional parameters ($1, $2, ...) */
    char *endptr;
    long n_val = strtol(n, &endptr, 10);
    if (*endptr == '\0' && n_val >= 0)
    {
        if (frame && frame->positional_params)
        {
            if (n_val == 0)
            {
                const string_t *arg0 = positional_params_get_arg0(frame->positional_params);
                return arg0 ? string_create_from(arg0) : string_create();
            }
            else
            {
                const string_t *param = positional_params_get(frame->positional_params, (int)n_val);
                return param ? string_create_from(param) : string_create();
            }
        }
        return string_create();
    }

    return NULL; /* Not a special parameter */
}

bool is_special_param(const string_t *name)
{
    if (!name || string_length(name) == 0)
    {
        return false;
    }

    const char *n = string_cstr(name);

    /* Single character specials */
    if (string_length(name) == 1)
    {
        switch (n[0])
        {
        case '?':
        case '$':
        case '!':
        case '#':
        case '@':
        case '*':
        case '-':
        case '0':
            return true;
        }
    }

    /* Numeric positional parameters */
    char *endptr;
    strtol(n, &endptr, 10);
    return (*endptr == '\0');
}

/* ============================================================================
 * Command Substitution
 * ============================================================================ */

string_t *expand_command_subst(exec_frame_t *frame, const string_t *command)
{
#ifdef POSIX_API
    const char *cmd = string_cstr(command);
    if (cmd == NULL || *cmd == '\0')
    {
        record_subst_status(frame, 0);
        return string_create();
    }

    FILE *pipe = popen(cmd, "r");
    if (pipe == NULL)
    {
        log_error("expand_command_subst: popen failed for '%s'", cmd);
        record_subst_status(frame, 1);
        return string_create();
    }

    string_t *output = string_create();
    char buffer[256];

    while (fgets(buffer, sizeof(buffer), pipe) != NULL)
    {
        string_append_cstr(output, buffer);
    }

    int exit_code = pclose(pipe);
    record_subst_status(frame, exit_code);

    /* Strip trailing newlines per POSIX */
    strip_trailing_newlines(output);

    return output;

#elifdef UCRT_API
    const char *cmd = string_cstr(command);
    if (cmd == NULL || *cmd == '\0')
    {
        record_subst_status(frame, 0);
        return string_create();
    }

    FILE *pipe = _popen(cmd, "r");
    if (pipe == NULL)
    {
        log_error("expand_command_subst: _popen failed for '%s'", cmd);
        record_subst_status(frame, 1);
        return string_create();
    }

    string_t *output = string_create();
    char buffer[256];

    while (fgets(buffer, sizeof(buffer), pipe) != NULL)
    {
        string_append_cstr(output, buffer);
    }

    int exit_code = _pclose(pipe);
    record_subst_status(frame, exit_code);

    strip_trailing_newlines(output);

    return output;

#else
    /* ISO C: no portable way to capture command output */
    (void)command;
    record_subst_status(frame, 0);
    return string_create();
#endif
}

/* ============================================================================
 * Arithmetic Expansion
 * ============================================================================ */

string_t *expand_arithmetic(exec_frame_t *frame, const string_t *expression)
{
    if (!expression)
    {
        return string_create_from_cstr("0");
    }

    if (!frame)
    {
        log_warn("expand_arithmetic: no frame available");
        return string_create_from_cstr("0");
    }

    /* Use the arithmetic module to evaluate the expression */
    ArithmeticResult result = arithmetic_evaluate(frame, expression);

    if (result.failed)
    {
        log_error("Arithmetic expansion error: %s", 
                  result.error ? string_cstr(result.error) : "unknown error");
        arithmetic_result_free(&result);
        frame->last_exit_status = 1;
        return string_create_from_cstr("0");
    }

    string_t *value = string_from_int((int)result.value);
    arithmetic_result_free(&result);
    return value;
}

/* ============================================================================
 * Field Splitting
 * ============================================================================ */

/**
 * Check if a character is IFS whitespace (space, tab, newline).
 */
static bool is_ifs_whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\n';
}

/**
 * Check if a character is in the IFS string.
 */
static bool is_ifs_char(char c, const char *ifs)
{
    for (const char *p = ifs; *p; p++)
    {
        if (*p == c)
            return true;
    }
    return false;
}

/**
 * POSIX-compliant field splitting.
 * 
 * Rules:
 * 1. If IFS is null (empty), no splitting occurs
 * 2. IFS whitespace (space/tab/newline) at start/end is ignored
 * 3. Consecutive IFS whitespace is treated as a single delimiter
 * 4. Non-whitespace IFS characters create empty fields
 * 5. IFS whitespace adjacent to non-whitespace IFS is ignored
 * 6. If the result contains only IFS whitespace, produce zero words (empty list)
 */
string_list_t *expand_field_split(exec_frame_t *frame, const string_t *text)
{
    Expects_not_null(frame);
    Expects_not_null(text);

    string_list_t *fields = string_list_create();

    if (string_empty(text))
        return fields;

    string_t *ifs = get_ifs(frame);
    if (string_empty(ifs))
    {
        // Empty IFS: no splitting, whole text is one field
        string_list_push_back(fields, text);
        string_destroy(&ifs);
        return fields;
    }

    // Split IFS into whitespace and non-whitespace parts for efficient searching
    string_t *ifs_ws = string_create();
    string_t *ifs_nws = string_create();
    for (int k = 0; k < string_length(ifs); k++)
    {
        char c = string_at(ifs, k);
        if (is_ifs_whitespace(c))
            string_append_char(ifs_ws, c);
        else
            string_append_char(ifs_nws, c);
    }

    int len = string_length(text);
    int i = 0;

    // 1. Skip all leading IFS whitespace (never produces fields)
    if (!string_empty(ifs_ws))
    {
        i = string_find_first_not_of_at(text, ifs_ws, 0);
        if (i == -1)
        {
            // Entire input is IFS whitespace → zero fields
            goto cleanup;
        }
    }
    // Now i is either at a non-IFS char or at a hard (non-ws) delimiter

    while (i < len)
    {
        // 2. Collect the field: all characters until any IFS char
        int field_start = i;
        int field_end = string_find_first_of_at(text, ifs, i);
        if (field_end == -1)
            field_end = len;

        string_t *field = string_create_from_range(text, field_start, field_end);
        string_list_move_push_back(fields, &field);

        i = field_end;
        if (i >= len)
            break;

        // 3. Consume the delimiter sequence
        //    - Skip all following IFS whitespace (collapsed)
        //    - Handle each non-whitespace IFS char as a separate delimiter (empty fields)
        bool in_delimiter = true;
        while (in_delimiter && i < len)
        {
            // Skip whitespace part of delimiter
            if (!string_empty(ifs_ws))
            {
                int after_ws = string_find_first_not_of_at(text, ifs_ws, i);
                if (after_ws != -1)
                    i = after_ws;
                else
                {
                    // Only trailing whitespace left → done, no trailing empty from ws-only
                    goto cleanup;
                }
            }

            // Now handle zero or more consecutive hard delimiters
            while (i < len && string_find_first_of_at(text, ifs_nws, i) == i)
            {
                // Consume this hard delimiter
                i++;

                // After each hard delimiter, we conceptually "end" a field.
                // If we're at end or followed by more delimiters → empty field
                if (i >= len || string_find_first_of_at(text, ifs, i) == i)
                {
                    string_t *empty = string_create();
                    string_list_move_push_back(fields, &empty);
                }
                else
                {
                    // Next is start of real field → stop consuming delimiters
                    in_delimiter = false;
                    break;
                }
            }

            // If we didn't hit another hard delimiter and whitespace is done,
            // we're at the start of the next field (or end)
            if (i < len && string_find_first_of_at(text, ifs, i) != i)
                in_delimiter = false;
        }
    }

cleanup:
    string_destroy(&ifs);
    string_destroy(&ifs_ws);
    string_destroy(&ifs_nws);
    return fields;
}

/* ============================================================================
 * Pathname Expansion
 * ============================================================================ */

string_list_t *expand_pathname(exec_frame_t *frame, const string_t *pattern)
{
    (void)frame; /* May use frame for noglob check in future */

    string_list_t *matches = glob_util_expand_path(pattern);

    if (matches && string_list_size(matches) > 0)
    {
        return matches;
    }

    /* No matches: return original pattern */
    if (matches)
    {
        string_list_destroy(&matches);
    }

    string_list_t *result = string_list_create();
    string_list_push_back(result, string_create_from(pattern));
    return result;
}

/* ============================================================================
 * Part Expansion (Internal)
 * ============================================================================ */

/**
 * Expand a single token part.
 * This function may have side effects
 * (e.g., modifying the frame's last_exit_status or assigning variables for := modifier).
 */
static string_t *expand_part(exec_frame_t *frame, const part_t *part)
{
    switch (part->type)
    {
    case PART_LITERAL:
        return string_create_from(part->text);

    case PART_PARAMETER:
        /* Use the full parameter expansion with modifiers */
        return expand_parameter_with_modifier(frame, part);

    case PART_COMMAND_SUBST: {
        /* Convert nested tokens to command string */
        string_t *cmd = string_create();
        int len = part->nested ? token_list_size(part->nested) : 0;
        for (int i = 0; i < len; i++)
        {
            const token_t *t = token_list_get(part->nested, i);
            string_t *ts = token_to_string(t);
            string_append(cmd, ts);
            string_destroy(&ts);
            if (i < token_list_size(part->nested) - 1)
            {
                string_append_cstr(cmd, " ");
            }
        }
        string_t *result = expand_command_subst(frame, cmd);
        string_destroy(&cmd);
        return result;
    }

    case PART_ARITHMETIC:
        return expand_arithmetic(frame, part->text);

    case PART_TILDE:
        return expand_tilde(frame, part->text);

    default:
        return string_create();
    }
}

/**
 * Expand all parts of a token to a single string.
 * Respects quoting: single-quoted parts are literal, double-quoted allow expansion.
 * May have side effects from parameter expansions with modifiers.
 */
static string_t *expand_parts_to_string(exec_frame_t *frame, const part_list_t *parts)
{
    Expects_not_null(frame);
    Expects_not_null(parts);

    string_t *result = string_create();

    for (int i = 0; i < part_list_size(parts); i++)
    {
        const part_t *part = part_list_get(parts, i);
        
        /* Single-quoted parts: no expansion, literal text */
        if (part->was_single_quoted && part->type == PART_LITERAL)
        {
            string_append(result, part->text);
            continue;
        }
        
        /* Expand the part */
        string_t *expanded = expand_part(frame, part);

        if (expanded)
        {
            string_append(result, expanded);
            string_destroy(&expanded);
        }
    }

    return result;
}

/**
 * Remove quote characters from a string (final step of word expansion).
 * Note: This is typically handled during tokenization/parsing, but included
 * for completeness in the expansion sequence.
 */
static string_t *remove_quotes(const string_t *text)
{
    /* In our implementation, quotes are already removed during tokenization.
     * Parts track was_single_quoted and was_double_quoted flags.
     * This function is a no-op but kept for API completeness. */
    return string_create_from(text);
}

/* ============================================================================
 * High-Level Expansion Functions
 * ============================================================================ */

/**
 * Expands a single WORD token into a list of strings, applying all relevant expansions.
 * This may have side effects from parameter expansions with modifiers and command substitutions.
 */
string_list_t *expand_word(exec_frame_t *frame, const token_t *tok)
{
    if (!tok || tok->type != TOKEN_WORD)
    {
        return NULL;
    }

    /* Check if any expansion is needed */
    if (!tok->needs_expansion && !tok->needs_field_splitting && !tok->needs_pathname_expansion)
    {
        /* No expansion: return literal text */
        string_t *text = token_get_all_text(tok);
        string_list_t *result = string_list_create();
        string_list_move_push_back(result, &text);
        return result;
    }

    /* Expand all parts */
    string_t *expanded = expand_parts_to_string(frame, token_get_parts_const(tok));

    /* Field splitting */
    string_list_t *fields;
    if (tok->needs_field_splitting)
    {
        fields = expand_field_split(frame, expanded);
        string_destroy(&expanded);
        
        /* POSIX: If field splitting produced zero words (e.g., input was only
         * IFS whitespace), that's correct - don't add an empty string. */
    }
    else
    {
        fields = string_list_create();
        string_list_move_push_back(fields, &expanded);
    }

    /* Pathname expansion */
    if (tok->needs_pathname_expansion)
    {
        string_list_t *globs = string_list_create();

        for (int i = 0; i < string_list_size(fields); i++)
        {
            const string_t *pattern = string_list_at(fields, i);
            string_list_t *matches = expand_pathname(frame, pattern);

            for (int j = 0; j < string_list_size(matches); j++)
            {
                string_list_push_back(globs, string_create_from(string_list_at(matches, j)));
            }
            string_list_destroy(&matches);
        }

        string_list_destroy(&fields);
        return globs;
    }

    return fields;
}

string_t *expand_word_nosplit(exec_frame_t *frame, const token_t *tok)
{
    if (!tok || tok->type != TOKEN_WORD)
    {
        return NULL;
    }

    /* If no expansion is needed at all, return the literal text */
    if (!tok->needs_expansion)
    {
        return token_get_all_text(tok);
    }

    /* Expand parts (tilde, parameter, command subst, arithmetic)
     * but skip field splitting and pathname expansion */
    return expand_parts_to_string(frame, token_get_parts_const(tok));
}

/**
 * Expand a list of WORD tokens into a list of strings, applying all relevant expansions.
 * This may have side effects from parameter expansions with modifiers and command substitutions.
 */
string_list_t *expand_words(exec_frame_t *frame, const token_list_t *tokens)
{
    if (!tokens)
    {
        return NULL;
    }

    string_list_t *result = string_list_create();

    for (int i = 0; i < token_list_size(tokens); i++)
    {
        const token_t *tok = token_list_get(tokens, i);
        string_list_t *expanded = expand_word(frame, tok);

        if (expanded)
        {
            for (int j = 0; j < string_list_size(expanded); j++)
            {
                string_list_push_back(result, string_create_from(string_list_at(expanded, j)));
            }
            string_list_destroy(&expanded);
        }
    }

    return result;
}

string_t *expand_string(exec_frame_t *frame, const string_t *text, expand_flags_t flags)
{
    /* TODO: Implement proper string expansion with flags */
    /* For now, just return a copy if no expansion requested */
    (void)frame;
    (void)flags;
    return string_create_from(text);
}

string_t *expand_redirection_target(exec_frame_t *frame, const token_t *tok)
{
    if (!tok || tok->type != TOKEN_WORD)
    {
        return NULL;
    }

    /* Redirection targets: tilde, parameter, command, arithmetic expansion
     * but NO field splitting or pathname expansion */
    return expand_parts_to_string(frame, token_get_parts_const(tok));
}

string_t *expand_assignment_value(exec_frame_t *frame, const token_t *tok)
{
    Expects_not_null(frame);
    Expects_not_null(tok);
    Expects_eq(tok->type, TOKEN_ASSIGNMENT_WORD);

    /* Assignment values: tilde, parameter, command, arithmetic expansion
     * but NO field splitting or pathname expansion */
    return expand_parts_to_string(frame, tok->assignment_value);
}

string_t *expand_heredoc(exec_frame_t *frame, const string_t *body, bool is_quoted)
{
    if (is_quoted)
    {
        /* Quoted delimiter: no expansion */
        return string_create_from(body);
    }

    /* Unquoted heredoc: perform parameter, command, and arithmetic expansions.
     * No field splitting or pathname expansion.
     * 
     * We need to scan through the text and expand $var, ${...}, $(...), $((...))
     * but not perform field splitting or globbing.
     */
    
    string_t *result = string_create();
    const char *text = string_cstr(body);
    int len = string_length(body);
    
    for (int i = 0; i < len; i++)
    {
        char c = text[i];
        
        if (c == '\\' && i + 1 < len)
        {
            /* Backslash escapes: \$, \`, \\ */
            char next = text[i + 1];
            if (next == '$' || next == '`' || next == '\\' || next == '\n')
            {
                if (next != '\n')
                {
                    string_append_char(result, next);
                }
                i++; /* Skip escaped character */
                continue;
            }
            /* Other backslashes are literal */
            string_append_char(result, c);
        }
        else if (c == '$')
        {
            /* Parameter or command/arithmetic expansion */
            if (i + 1 < len)
            {
                char next = text[i + 1];
                
                if (next == '(')
                {
                    /* Command substitution $(...) or arithmetic $((...)) */
                    /* This is complex - for now, log a warning and skip */
                    log_warn("Command/arithmetic substitution in heredoc not fully implemented");
                    string_append_char(result, c);
                }
                else if (next == '{')
                {
                    /* Parameter expansion ${...} */
                    /* Find matching } */
                    int j = i + 2;
                    int brace_depth = 1;
                    while (j < len && brace_depth > 0)
                    {
                        if (text[j] == '{')
                            brace_depth++;
                        else if (text[j] == '}')
                            brace_depth--;
                        j++;
                    }
                    
                    if (brace_depth == 0)
                    {
                        /* Extract parameter name (simple case) */
                        string_t *param_name = string_create();
                        for (int k = i + 2; k < j - 1; k++)
                        {
                            string_append_char(param_name, text[k]);
                        }
                        
                        string_t *value = expand_parameter(frame, param_name);
                        if (value)
                        {
                            string_append(result, value);
                            string_destroy(&value);
                        }
                        string_destroy(&param_name);
                        
                        i = j - 1; /* Move past the expansion */
                        continue;
                    }
                    string_append_char(result, c);
                }
                else if (isalnum(next) || next == '_' || next == '?' || next == '$' ||
                         next == '!' || next == '#' || next == '@' || next == '*' ||
                         next == '-' || next == '0')
                {
                    /* Simple parameter: $var or $1, $?, etc. */
                    string_t *param_name = string_create();
                    i++; /* Move to first char of name */
                    
                    /* Special single-char parameters */
                    if (strchr("?$!#@*-0", text[i]))
                    {
                        string_append_char(param_name, text[i]);
                    }
                    else
                    {
                        /* Variable name or positional param */
                        while (i < len && (isalnum(text[i]) || text[i] == '_'))
                        {
                            string_append_char(param_name, text[i]);
                            i++;
                        }
                        i--; /* Back up one for loop increment */
                    }
                    
                    string_t *value = expand_parameter(frame, param_name);
                    if (value)
                    {
                        string_append(result, value);
                        string_destroy(&value);
                    }
                    string_destroy(&param_name);
                }
                else
                {
                    /* Literal $ */
                    string_append_char(result, c);
                }
            }
            else
            {
                /* $ at end of string */
                string_append_char(result, c);
            }
        }
        else if (c == '`')
        {
            /* Backquote command substitution - deprecated but supported */
            log_warn("Backquote command substitution in heredoc not fully implemented");
            string_append_char(result, c);
        }
        else
        {
            /* Regular character */
            string_append_char(result, c);
        }
    }
    
    return result;
}

/* ============================================================================
 * Legacy/Compatibility Functions
 * ============================================================================ */

string_list_t *exec_expand_word(exec_t *executor, const token_t *tok)
{
    if (!executor)
        return NULL;
    return expand_word(executor->current_frame, tok);
}

string_list_t *exec_expand_words(exec_t *executor, const token_list_t *tokens)
{
    if (!executor)
        return NULL;
    return expand_words(executor->current_frame, tokens);
}

string_t *exec_expand_redirection_target(exec_t *executor, const token_t *tok)
{
    if (!executor)
        return NULL;
    return expand_redirection_target(executor->current_frame, tok);
}

string_t *exec_expand_assignment_value(exec_t *executor, const token_t *tok)
{
    if (!executor)
        return NULL;
    return expand_assignment_value(executor->current_frame, tok);
}

string_t *exec_expand_heredoc(exec_t *executor, const string_t *body, bool is_quoted)
{
    if (!executor)
        return NULL;
    return expand_heredoc(executor->current_frame, body, is_quoted);
}

string_t *exec_expand_tilde(exec_t *executor, const string_t *text)
{
    exec_frame_t *frame = executor ? executor->current_frame : NULL;
    return expand_tilde(frame, text);
}

/* ============================================================================
 * Callback for External Use
 * ============================================================================ */

string_t *exec_command_subst_callback(void *userdata, const string_t *command)
{
    exec_t *executor = (exec_t *)userdata;
    exec_frame_t *frame = executor ? executor->current_frame : NULL;
    return expand_command_subst(frame, command);
}

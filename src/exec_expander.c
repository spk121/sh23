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

#include "exec_expander.h"
#include "exec_frame.h"
#include "exec_internal.h"
#include "glob_util.h"
#include "logging.h"
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
static const char *get_ifs(exec_frame_t *frame)
{
    const char *ifs = NULL;

    if (frame)
    {
        variable_store_t *vars = exec_frame_get_variables(frame);
        if (vars)
        {
            ifs = variable_store_get_value_cstr(vars, "IFS");
        }
    }

    if (!ifs)
    {
        ifs = getenv("IFS");
    }

    return ifs ? ifs : " \t\n"; /* Default IFS */
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
    (void)frame; /* May use frame for PWD lookup in future */

#ifdef POSIX_API
    struct passwd *pw;

    if (username == NULL || string_length(username) == 0)
    {
        /* Expand ~ to current user's home */
        pw = getpwuid(getuid());
    }
    else
    {
        /* Expand ~username to specified user's home */
        pw = getpwnam(string_cstr(username));
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
    /* ~username not supported on Windows */
    return NULL;

#else
    /* ISO C: no tilde expansion available */
    (void)username;
    return NULL;
#endif
}

/* ============================================================================
 * Parameter Expansion
 * ============================================================================ */

string_t *expand_parameter(exec_frame_t *frame, const string_t *name)
{
    if (!name || string_length(name) == 0)
    {
        return string_create();
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
        string_t *value = exec_frame_get_variable(frame, name);
        if (value)
        {
            return value;
        }
    }

    /* Fall back to environment */
    const char *env_val = getenv(name_cstr);
    if (env_val)
    {
        return string_create_from_cstr(env_val);
    }

    return string_create(); /* Empty string if not set */
}

string_t *expand_special_param(exec_frame_t *frame, const string_t *name)
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
    /* TODO: Implement proper arithmetic evaluation */
    /* For now, return a placeholder */
    (void)frame;
    (void)expression;
    return string_create_from_cstr("0");
}

/* ============================================================================
 * Field Splitting
 * ============================================================================ */

string_list_t *expand_field_split(exec_frame_t *frame, const string_t *text)
{
    string_list_t *fields = string_list_create();

    if (!text || string_length(text) == 0)
    {
        return fields;
    }

    const char *ifs = get_ifs(frame);

    if (*ifs == '\0')
    {
        /* Empty IFS: no splitting */
        string_list_push_back(fields, string_create_from(text));
        return fields;
    }

    /* Perform IFS-based splitting */
    char *str = xstrdup(string_cstr(text));
    char *token = strtok(str, ifs);

    while (token)
    {
        string_list_push_back(fields, string_create_from_cstr(token));
        token = strtok(NULL, ifs);
    }

    xfree(str);

    /* If no fields resulted, return empty list (not a list with empty string) */
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
 */
static string_t *expand_part(exec_frame_t *frame, const part_t *part)
{
    switch (part->type)
    {
    case PART_LITERAL:
        return string_create_from(part->text);

    case PART_PARAMETER:
        return expand_parameter(frame, part->param_name);

    case PART_COMMAND_SUBST: {
        /* Convert nested tokens to command string */
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
 */
static string_t *expand_parts_to_string(exec_frame_t *frame, const part_list_t *parts)
{
    string_t *result = string_create();

    for (int i = 0; i < part_list_size(parts); i++)
    {
        const part_t *part = part_list_get(parts, i);
        string_t *expanded = expand_part(frame, part);

        if (expanded)
        {
            string_append(result, expanded);
            string_destroy(&expanded);
        }
    }

    return result;
}

/* ============================================================================
 * High-Level Expansion Functions
 * ============================================================================ */

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

        if (string_list_size(fields) == 0)
        {
            /* If splitting produced no fields, add an empty string */
            string_list_push_back(fields, string_create());
        }
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

string_list_t *expand_words(exec_frame_t *frame, const token_list_t *tokens)
{
    if (!tokens)
    {
        return NULL;
    }

    string_list_t *result = string_list_create();

    for (int i = 0; i < token_list_size(tokens); i++)
    {
        token_t *tok = token_list_get(tokens, i);
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
    if (!tok || tok->type != TOKEN_ASSIGNMENT_WORD)
    {
        return NULL;
    }

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

    /* Unquoted: parameter, command, arithmetic expansions */
    /* TODO: Implement proper heredoc expansion by tokenizing the body */
    (void)frame;
    return string_create_from(body);
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

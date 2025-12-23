#include "expander.h"
#include "xalloc.h"
#include <stdlib.h>

/*
 * Internal structure for the expander.
 */
struct expander_t
{
    variable_store_t *vars;
    positional_params_stack_t *params;

    /* System interaction hooks */
    expander_getenv_fn fn_getenv;
    expander_tilde_expand_fn fn_tilde_expand;
    expander_glob_fn fn_glob;
    expander_command_subst_fn fn_command_subst;

    void *userdata;
};

const char *expander_getenv(void *userdata, const char *name);
string_t *expander_tilde_expand(void *userdata, const string_t *text);
string_list_t *expander_glob(void *user_data, const string_t *pattern);
string_t *expander_command_subst(void *user_data, const string_t *command)

    /* ============================================================================
 * Constructor / Destructor
 * ============================================================================
 */

expander_t *expander_create(variable_store_t *vars, positional_params_stack_t *params)
{
    expander_t *exp = xcalloc(1, sizeof(expander_t));
    if (!exp)
        return NULL;

    exp->vars = vars;
    exp->params = params;

    /* Overrideable system interaction hooks with default implementations */
    exp->fn_getenv = expander_getenv;
    exp->fn_tilde_expand = expander_tilde_expand;
    exp->fn_glob = expander_glob;
    exp->fn_command_subst = expander_command_subst;
    exp->userdata = NULL;

    return exp;
}

void expander_destroy(expander_t **exp_ptr)
{
    if (!exp_ptr || !*exp_ptr)
        return;

    xfree(*exp_ptr);
    *exp_ptr = NULL;
}

/* ============================================================================
 * Hook setters
 * ============================================================================
 */

void expander_set_getenv(expander_t *exp, expander_getenv_fn fn)
{
    exp->fn_getenv = fn;
}

void expander_set_tilde_expand(expander_t *exp, expander_tilde_expand_fn fn)
{
    exp->fn_tilde_expand = fn;
}

void expander_set_glob(expander_t *exp, expander_glob_fn fn)
{
    exp->fn_glob = fn;
}

void expander_set_command_substitute(expander_t *exp, expander_command_subst_fn fn)
{
    exp->fn_command_subst = fn;
}

void expander_set_userdata(expander_t *exp, void *userdata)
{
    exp->userdata = userdata;
}

/* ============================================================================
 * Static helper functions for expansion activities
 * ============================================================================ */

static string_t *expand_parameter(expander_t *exp, const part_t *part)
{
    // Basic parameter expansion: handle $var and ${var}
    // For more complex forms (e.g., ${var:-default}), this would need extension
    const char *name = string_cstr(part->param_name);
    const char *value = exp->fn_getenv(exp->userdata, name);
    if (value) {
        return string_create_from_cstr(value);
    } else {
        return string_create(); // Empty string if not set
    }
}

static string_t *expand_command_subst(expander_t *exp, const part_t *part)
{
    // Convert nested tokens to a command string
    string_t *cmd = string_create();
    for (int i = 0; i < token_list_size(part->nested); i++) {
        token_t *t = token_list_get(part->nested, i);
        string_t *ts = token_to_string(t); // Approximate stringification
        string_append(cmd, ts);
        string_destroy(&ts);
        if (i < token_list_size(part->nested) - 1) {
            string_append_cstr(cmd, " "); // Space between tokens
        }
    }
    string_t *result = exp->fn_command_subst(exp->userdata, cmd);
    string_destroy(&cmd);
    return result ? result : string_create();
}

static string_t *expand_arithmetic(expander_t *exp, const part_t *part)
{
    // Stub: For now, return a placeholder. Integrate with arithmetic.c later.
    (void)exp;
    (void)part;
    return string_create_from_cstr("42");
}

static string_t *expand_tilde(expander_t *exp, const part_t *part)
{
    return exp->fn_tilde_expand(exp->userdata, part->text);
}

/* ============================================================================
 * Expansion entry points
 * ============================================================================ */

string_list_t *expander_expand_word(expander_t *exp, const token_t *tok)
{
    if (!tok || tok->type != TOKEN_WORD) {
        return NULL;
    }

    if (!tok->needs_expansion) {
        // No expansion needed: return literal text as single string
        string_t *text = token_get_all_text(tok);
        string_list_t *result = string_list_create();
        string_list_move_push_back(result, text);
        return result;
    }

    // Expand each part
    string_t *expanded = string_create();
    const part_list_t *parts = token_get_parts_const(tok);
    for (int i = 0; i < part_list_size(parts); i++) {
        const part_t *part = part_list_get(parts, i);
        string_t *part_expanded = NULL;
        switch (part->type) {
            case PART_LITERAL:
                part_expanded = string_create_from(part->text);
                break;
            case PART_PARAMETER:
                part_expanded = expand_parameter(exp, part);
                break;
            case PART_COMMAND_SUBST:
                part_expanded = expand_command_subst(exp, part);
                break;
            case PART_ARITHMETIC:
                part_expanded = expand_arithmetic(exp, part);
                break;
            case PART_TILDE:
                part_expanded = expand_tilde(exp, part);
                break;
        }
        if (part_expanded) {
            string_append(expanded, part_expanded);
            string_destroy(&part_expanded);
        }
    }

    // Get IFS for field splitting
    const char *ifs = NULL;
    if (exp->vars) {
        ifs = variable_store_get_value_cstr(exp->vars, "IFS");
    }
    if (!ifs) {
        ifs = exp->fn_getenv(exp->userdata, "IFS");
    }
    if (!ifs) {
        ifs = " \t\n"; // Default IFS
    }

    // Field splitting: only if needs_field_splitting and IFS is not empty
    string_list_t *fields = string_list_create();
    bool do_split = tok->needs_field_splitting && *ifs != '\0';
    if (do_split) {
        char *str = string_release(&expanded);
        char *token = strtok(str, ifs);
        while (token) {
            string_list_push_back(fields, string_create_from_cstr(token));
            token = strtok(NULL, ifs);
        }
        xfree(str);
    } else {
        string_list_move_push_back(fields, expanded);
    }

    // Pathname expansion (globbing)
    if (tok->needs_pathname_expansion) {
        string_list_t *globs = string_list_create();
        for (int i = 0; i < string_list_size(fields); i++) {
            const string_t *pattern = string_list_at(fields, i);
            string_list_t *matches = exp->fn_glob(exp->userdata, pattern);
            if (matches) {
                for (int j = 0; j < string_list_size(matches); j++) {
                    string_list_move_push_back(globs, string_list_at(matches, j));
                }
                string_list_destroy(&matches);
            } else {
                string_list_push_back(globs, pattern);
            }
        }
        string_list_destroy(&fields);
        return globs;
    } else {
        return fields;
    }
}

string_list_t *expander_expand_words(expander_t *exp, const token_list_t *tokens)
{
    (void)exp;
    (void)tokens;
    return NULL; /* TODO */
}

string_t *expander_expand_redirection_target(expander_t *exp, const token_t *tok)
{
    (void)exp;
    (void)tok;
    return NULL; /* TODO */
}

string_t *expander_expand_assignment_value(expander_t *exp, const token_t *tok)
{
    (void)exp;
    (void)tok;
    return NULL; /* TODO */
}

string_t *expander_expand_heredoc(expander_t *exp, const string_t *body, bool is_quoted)
{
    (void)exp;
    (void)body;
    (void)is_quoted;
    return NULL; /* TODO */
}

/* ============================================================================
 * Default implementations of OS-specific functions
 * ============================================================================
 */

const char* expander_getenv(void* userdata, const char* name)
{
    (void)userdata;
#ifdef POSIX_API
    return getenv(name);
#elifdef UCRT_API
    return getenv(name);
#else // ISO_C
    return getenv(name);
#endif
}

string_t *expander_tilde_expand(void *userdata, const string_t *text)
{
    (void)userdata;

    if (string_length(text) == 0 || string_front(text) != '~')
        return string_create_from(text);

    // POSIX_API, UCRT_API, and ISO_C_API share many of the tilde expansion rules

    // ~ alone or ~/...
    if (string_length(text) == 1 || string_at(text, 1) == '/')
    {
        const char *home = getenv("HOME");
        if (home == NULL || home[0] == '\0')
            return string_create_from(text); // No expansion

        string_t *result = string_create_from_cstr(home);
        if (string_at(text, 1) == '/')
            string_append_substring(result, text, 1, string_length(text));
        return result;
    }

    // ~+ expands to PWD
    if (string_at(text, 1) == '+' && (string_length(text) == 2 || string_at(text, 2) == '/'))
    {
        const char *pwd = getenv("PWD");
        if (pwd == NULL || pwd[0] == '\0')
            return string_create_from(text);

        string_t *result = string_create_from_cstr(pwd);
        if (string_at(text, 2) == '/')
            string_append_substring(result, text, 2, string_length(text));
        return result;
    }

    // ~- expands to OLDPWD
    if (string_at(text, 1) == '-' && (string_length(text) == 2 || string_at(text, 2) == '/'))
    {
        const char *oldpwd = getenv("OLDPWD");
        if (oldpwd == NULL || oldpwd[0] == '\0')
            return string_create_from(text);

        string_t *result = string_create_from_cstr(oldpwd);
        if (string_at(text, 2) == '/')
            string_append_substring(result, text, 2, string_length(text));
        return result;
    }

    // ~user or ~user/...
    int slash_index = string_find_cstr(text, "/");
    int name_len = (slash_index == -1) ? string_length(text) : slash_index;

    if (name_len == 0)
        return string_create_from(text);

#ifdef POSIX_API
    string_t *usrname = string_substring(text, 1, name_len);
    struct passwd *pw = getpwnam(string_data(usrname));
    string_destroy(&usrname);

    if (pw == NULL || pw->pw_dir == NULL)
        return string_create_from(text); // No such user

    string_t *result = string_create_from_cstr(pw->pw_dir);
    if (slash_index != -1)
        string_append_substring(result, text, slash_index, string_length(text));
    return result;
#elifdef UCRT_API
    // You could fake this by using Win32 API like GetUserProfileDirectory().
    // But we aren't going to do any Win32 API calls. Only UCRT.
    log_warn("expand tilde: ~user expansion not supported on this platform\n");
    return string_create_from(text);
#else
    log_warn("expand tilde: ~user expansion not supported on this platform\n");
    return string_create_from(text);
#endif
}

string_list_t* expander_glob(void* user_data, const string_t* pattern)
{
#ifdef POSIX_API
    (void)user_data; // unused

    const char *pattern_str = string_data(pattern);
    glob_t glob_result;

    // Perform glob matching
    // GLOB_NOCHECK: If no matches, return the pattern itself
    // GLOB_TILDE: Expand ~ for home directory
    int ret = glob(pattern_str, GLOB_TILDE, NULL, &glob_result);

    if (ret != 0)
    {
        // On error or no matches (when not using GLOB_NOCHECK), return NULL
        if (ret == GLOB_NOMATCH)
        {
            return NULL;
        }
        // GLOB_NOSPACE or GLOB_ABORTED
        return NULL;
    }

    // No matches found
    if (glob_result.gl_pathc == 0)
    {
        globfree(&glob_result);
        return NULL;
    }

    // Create result list
    string_list_t *result = string_list_create();

    // Add all matched paths
    for (size_t i = 0; i < glob_result.gl_pathc; i++)
    {
        string_t *path = string_create_from_cstr(glob_result.gl_pathv[i]);
        string_list_move_push_back(result, path);
    }

    globfree(&glob_result);
    return result;

#elifdef UCRT_API
    (void)user_data; // unused

    const char *pattern_str = string_data(pattern);
    log_debug("glob expansion: glob pattern='%s'", pattern_str);
    struct _finddata_t fd;
    intptr_t handle;

    // Attempt to find first matching file
    handle = _findfirst(pattern_str, &fd);
    if (handle == -1L)
    {
        if (errno == ENOENT)
        {
            // No matches found
            return NULL;
        }
        // Other error (access denied, etc.)
        return NULL;
    }

    // Create result list
    string_list_t *result = string_list_create();

    // Add all matching files
    do
    {
        // Skip . and .. entries
        if (strcmp(fd.name, ".") == 0 || strcmp(fd.name, "..") == 0)
            continue;

        // Add the matched filename to the result list
        string_t *filename = string_create_from_cstr(fd.name);
        string_list_move_push_back(result, filename);

    } while (_findnext(handle, &fd) == 0);

    _findclose(handle);

    // If no files were added (only . and .. were found), return NULL
    if (string_list_size(result) == 0)
    {
        string_list_destroy(&result);
        return NULL;
    }

    return result;

#else
    /* In ISO_C environments, no glob implementation is available
     * since there is no way to enumerate filesystem entries.
     */
    (void)user_data; // unused
    log_warn("glob expansion: no glob implementation available");
    return NULL;
#endif
}

string_t* expander_command_subst(void* user_data, const string_t* command)
{
#ifdef POSIX_API
    (void)user_data; // unused for now

    const char *cmd = string_cstr(command);
    if (cmd == NULL || *cmd == '\0')
    {
        return NULL;
    }

    FILE *pipe = popen(cmd, "r");
    if (pipe == NULL)
    {
        log_error("command substitution: failed to open a pipe to execute '%s'", cmd);
        return NULL;
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
        log_debug("command substitution: child exited with code %d for '%s'", exit_code,
                  cmd);
    }

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
    (void)user_data; // unused for now

    const char *cmd = string_cstr(command);
    if (cmd == NULL || *cmd == '\0')
    {
        return NULL;
    }

    FILE *pipe = _popen(cmd, "r");
    if (pipe == NULL)
    {
        log_error("command substitution: failed to open a pipe to execute '%s'", cmd);
        return NULL;
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
        log_debug("command substitution: child exited with code %d for '%s'", exit_code,
                  cmd);
    }

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
    log_warn("command substitution: no command substitution implementation available");
    (void)command; // unused
    return NULL;
#endif
}

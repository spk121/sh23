/**
 * @file expander.c
 * @brief POSIX shell word expansion implementation
 * 
 * This module implements the word expansion steps according to POSIX:
 * 1. Tilde expansion - expand ~ to home directory
 * 2. Parameter expansion - expand $VAR to variable value
 * 3. Command substitution - execute $(cmd) and replace with output (stub)
 * 4. Arithmetic expansion - evaluate $((expr)) and replace with result
 * 5. Field splitting - split words on IFS characters
 * 6. Pathname expansion - glob patterns (not implemented)
 * 7. Quote removal - handled during parsing
 * 
 * The expansion process respects quoting:
 * - Single quotes prevent all expansions
 * - Double quotes allow selective expansions but prevent field splitting
 * - Unquoted expansions undergo field splitting and pathname expansion
 */

#include "expander.h"
#include "logging.h"
#include "xalloc.h"
#include "string_t.h"
#include "token.h"
#include "arithmetic.h"
#include "positional_params.h"
#include "variable_store.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef POSIX_API
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#endif
#include <stdio.h>

// Internal expander structure
struct expander_t
{   
    string_t *ifs;
    variable_store_t *vars;
    string_t *error_msg;
    int last_exit_status;
#ifdef POSIX_API
    pid_t pid;
    pid_t background_pid;
#else
    int pid;
    int background_pid;
#endif
    bool pid_set;
    bool background_pid_set;
    positional_params_stack_t *pos_stack;
    command_subst_callback_t cmd_subst_callback;
    void *cmd_subst_user_data;
    pathname_expansion_callback_t pathname_expansion_callback;
    void *pathname_expansion_user_data;
};

// ============================================================================
// Lifecycle Functions
// ============================================================================

expander_t *expander_create(void)
{
    expander_t *exp = xcalloc(1, sizeof(expander_t));
    exp->ifs = string_create_from_cstr(" \t\n");
    exp->vars = NULL;
    exp->last_exit_status = 0;
    exp->pos_stack = positional_params_stack_create();
    exp->cmd_subst_callback = NULL;
    exp->cmd_subst_user_data = NULL;
    exp->pathname_expansion_callback = NULL;
    exp->pathname_expansion_user_data = NULL;
    return exp;
}

void expander_destroy(expander_t **expander)
{
    if (!expander) return;
    expander_t *e = *expander;
    
    if (e == NULL)
        return;

    if (e->ifs != NULL)
        string_destroy(&e->ifs);
    positional_params_stack_destroy(&e->pos_stack);
    
    xfree(e);
    *expander = NULL;
}

// ============================================================================
// IFS Management
// ============================================================================

void expander_set_ifs(expander_t *exp, const string_t *ifs)
{
    Expects_not_null(exp);
    Expects_not_null(ifs);

    string_set(exp->ifs, ifs);
}

const string_t *expander_get_ifs(const expander_t *exp)
{
    Expects_not_null(exp);
    return exp->ifs;
}

// ============================================================================
// Variable Store Management
// ============================================================================

void expander_set_variable_store(expander_t *exp, variable_store_t *vars)
{
    Expects_not_null(exp);
    exp->vars = vars;
}

variable_store_t *expander_get_variable_store(const expander_t *exp)
{
    Expects_not_null(exp);
    return exp->vars;
}

void expander_set_last_exit_status(expander_t *exp, int status)
{
    Expects_not_null(exp);
    exp->last_exit_status = status;
}

int expander_get_last_exit_status(const expander_t *exp)
{
    Expects_not_null(exp);
    return exp->last_exit_status;
}

bool expander_set_positionals(expander_t *exp, int argc, const char **argv)
{
    Expects_not_null(exp);
    Expects(argc >= 0);
    
    // Clear any previous error
    if (exp->error_msg)
    {
        string_destroy(&exp->error_msg);
        exp->error_msg = NULL;
    }
    
    // $0 is argv[0]; positional set is argv[1..]
    if (argc > 0 && argv)
    {
        string_t *tmp0 = string_create_from_cstr(argv[0] ? argv[0] : "");
        positional_params_set_zero(exp->pos_stack, tmp0);
        string_destroy(&tmp0);
    }
    
    int count = (argc > 0) ? (argc - 1) : 0;
    
    // Check if count exceeds maximum
    int max_params = positional_params_get_max(exp->pos_stack);
    if (count > max_params)
    {
        exp->error_msg = string_create_from_cstr("too many positional parameters");
        return false;
    }
    
    string_t **params = NULL;
    if (count > 0)
    {
        params = xcalloc((size_t)count, sizeof(string_t *));
        for (int i = 0; i < count; i++)
        {
            const char *src = argv[i + 1] ? argv[i + 1] : "";
            params[i] = string_create_from_cstr(src);
        }
    }
    
    if (!positional_params_replace(exp->pos_stack, params, count))
    {
        // Clean up on failure
        if (params)
        {
            for (int i = 0; i < count; i++)
            {
                if (params[i])
                    string_destroy(&params[i]);
            }
            xfree(params);
        }
        exp->error_msg = string_create_from_cstr("failed to set positional parameters");
        return false;
    }
    
    return true;
}

void expander_clear_positionals(expander_t *exp)
{
    Expects_not_null(exp);
    positional_params_replace(exp->pos_stack, NULL, 0);
    string_t *empty = string_create();
    positional_params_set_zero(exp->pos_stack, empty);
    string_destroy(&empty);
}

const string_t *expander_get_error(const expander_t *exp)
{
    Expects_not_null(exp);
    return exp->error_msg;
}

#ifdef POSIX_API
void expander_set_pid(expander_t *exp, pid_t pid)
{
    Expects_not_null(exp);
    exp->pid = pid;
    exp->pid_set = true;
}

pid_t expander_get_pid(const expander_t *exp)
{
    Expects_not_null(exp);
    return exp->pid;
}

void expander_set_background_pid(expander_t *exp, pid_t pid)
{
    Expects_not_null(exp);
    exp->background_pid = pid;
    exp->background_pid_set = true;
}

pid_t expander_get_background_pid(const expander_t *exp)
{
    Expects_not_null(exp);
    return exp->background_pid;
}
#else
void expander_set_pid(expander_t *exp, int pid)
{
    Expects_not_null(exp);
    exp->pid = pid;
    exp->pid_set = true;
}

int expander_get_pid(const expander_t *exp)
{
    Expects_not_null(exp);
    return exp->pid;
}

void expander_set_background_pid(expander_t *exp, int pid)
{
    Expects_not_null(exp);
    exp->background_pid = pid;
    exp->background_pid_set = true;
}

int expander_get_background_pid(const expander_t *exp)
{
    Expects_not_null(exp);
    return exp->background_pid;
}
#endif

void expander_set_command_subst_callback(expander_t *exp, command_subst_callback_t callback, void *user_data)
{
    Expects_not_null(exp);
    exp->cmd_subst_callback = callback;
    exp->cmd_subst_user_data = user_data;
}

command_subst_callback_t expander_get_command_subst_callback(const expander_t *exp)
{
    Expects_not_null(exp);
    return exp->cmd_subst_callback;
}

void expander_set_pathname_expansion_callback(expander_t *exp, pathname_expansion_callback_t callback, void *user_data)
{
    Expects_not_null(exp);
    exp->pathname_expansion_callback = callback;
    exp->pathname_expansion_user_data = user_data;
}

pathname_expansion_callback_t expander_get_pathname_expansion_callback(const expander_t *exp)
{
    Expects_not_null(exp);
    return exp->pathname_expansion_callback;
}

// ============================================================================
// Helper Functions for Expansion
// ============================================================================

/**
 * Perform tilde expansion on a string.
 * Returns a newly allocated string with tilde expanded, or a copy if no expansion.
 */
static string_t *expand_tilde(const string_t *text)
{
    if (string_length(text) == 0 || string_front(text) != '~')
        return string_create_from(text);
    
    // ~ alone or ~/...
    if (string_length(text) == 1 || string_at(text, 1) == '/')
    {
        const char *home = getenv("HOME");
        if (home == NULL || home[0] == '\0')
            return string_create_from(text);  // No expansion
        
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
        return string_create_from(text);  // No such user
    
    string_t *result = string_create_from_cstr(pw->pw_dir);
    if (slash_index != -1)
        string_append_substring(result, text, slash_index, string_length(text));
    return result;
#else
    // FIXME: add real error handling
    fprintf(stderr, "expand_tilde: ~user expansion not supported on this platform\n");
    return string_create_from(text);
    // expander error
#endif
}

/**
 * Perform parameter expansion on a part.
 * Implements basic expansion by reading environment variables or variable store.
 * Advanced forms (e.g., ${var:-default}) are not yet supported.
 */
static string_t *expand_parameter(expander_t *exp, const part_t *part)
{
    const string_t *param_name = part_get_param_name(part);
    
    if (param_name == NULL)
        return string_create();

    // Special parameter: $?
    if (string_length(param_name) == 1 && string_at(param_name, 0) == '?')
    {
        return string_from_long(exp->last_exit_status);
    }

    // Special parameter: $$
    if (string_length(param_name) == 1 && string_at(param_name, 0) == '$')
    {
        if (exp->pid_set)
        {
#ifdef POSIX_API
            return string_from_long((long)exp->pid);
#else /* UCRT_API and for testing. Not valid ISO_C_API. */
            return string_from_int(exp->pid);
#endif
        }
        else
        {
            // FIXME: need to decide on default behavior if PID not
            // supported or set.
            return string_create_from_cstr("$$");
        }

    }

    // Special parameter: $!
    if (string_length(param_name) == 1 && string_at(param_name, 0) == '!')
    {
        if (exp->background_pid_set)
        {
#ifdef POSIX_API
            return string_from_long((long)exp->background_pid);
#else /* UCRT_API and for testing. Not valid ISO_C_API. */
            return string_from_int(exp->background_pid);
#endif
        }
        else
        {
            // When no background job has been started, return literal $!
            return string_create_from_cstr("$!");
        }
    }

    // Special parameter: $# (number of positionals)
    if (string_length(param_name) == 1 && string_at(param_name, 0) == '#')
    {
        int count = positional_params_count(exp->pos_stack);
        return string_from_long((long)count);
    }

    // Special parameter: $0
    if (string_length(param_name) == 1 && string_at(param_name, 0) == '0')
    {
        const string_t *z = positional_params_get_zero(exp->pos_stack);
        return (z != NULL) ? string_create_from(z) : string_create();
    }

    // Positional parameters $1..$N
    if (string_length(param_name) >= 1 && isdigit((unsigned char)string_at(param_name, 0)))
    {
        // Parse decimal index
        long idx = 0;
        for (int i = 0; i < (int)string_length(param_name); i++)
        {
            char c = string_at(param_name, (int)i);
            if (!isdigit((unsigned char)c)) { idx = -1; break; }
            idx = idx * 10 + (c - '0');
            if (idx > INT_MAX) { idx = -1; break; }
        }
        if (idx == 0)
        {
            const string_t *z = positional_params_get_zero(exp->pos_stack);
            return (z != NULL) ? string_create_from(z) : string_create();
        }
        if (idx > 0)
        {
            const string_t *v = positional_params_get(exp->pos_stack, (int)idx);
            return (v != NULL) ? string_create_from(v) : string_create();
        }
        // Unset positional -> empty string
        return string_create();
    }

    // Special parameter: $* (join all positional params)
    if (string_length(param_name) == 1 && string_at(param_name, 0) == '*')
    {
        char sep = (string_length(exp->ifs) > 0) ? string_at(exp->ifs, 0) : ' ';
        string_t *result = positional_params_get_all_joined(exp->pos_stack, sep);
        return result ? result : string_create();
    }

    // Special parameter: $@ (requires special handling in expander_expand_word)
    if (string_length(param_name) == 1 && string_at(param_name, 0) == '@')
    {
        // When unquoted, $@ behaves like $* (will be field-split later)
        // When quoted ("$@"), it must preserve word boundaries - handled specially below
        char sep = (string_length(exp->ifs) > 0) ? string_at(exp->ifs, 0) : ' ';
        string_t *result = positional_params_get_all_joined(exp->pos_stack, sep);
        return result ? result : string_create();
    }

    string_t *value = NULL;
    const char *ev = NULL;

    // First try the variable store if available
    if (exp->vars != NULL && variable_store_has_name(exp->vars, param_name))
    {
        value = string_create_from(variable_store_get_value(exp->vars, param_name));
    }
    else if ( (ev = getenv(string_cstr(param_name))) != NULL )
    {
        value = string_create_from_cstr(ev);
    }
    else
    {
        // Return empty string for unset variables
        value = string_create();
    }

    return value;
}

/**
 * Perform command substitution.
 * Invokes the callback if set, otherwise returns empty string.
 */
static string_t *expand_command_substitution(expander_t *exp, const part_t *part)
{
    if (exp->cmd_subst_callback == NULL)
    {
        log_debug("expand_command_substitution: no callback set, returning empty");
        return string_create();
    }
    
    const string_t *command = part_get_text(part);
    if (command == NULL || string_length(command) == 0)
    {
        log_debug("expand_command_substitution: empty command");
        return string_create();
    }
    
    log_debug("expand_command_substitution: invoking callback for command: %s", string_cstr(command));
    string_t *result = exp->cmd_subst_callback(command, exp->cmd_subst_user_data);
    
    if (result == NULL)
    {
        log_warn("expand_command_substitution: callback returned NULL, treating as empty");
        return string_create();
    }
    
    return result;
}

/**
 * Perform arithmetic expansion.
 * Evaluates the arithmetic expression and returns the result as a string.
 */
static string_t *expand_arithmetic(expander_t *exp, const part_t *part)
{
    const string_t *expr_text = part_get_text(part);
    
    if (expr_text == NULL || string_length(expr_text) == 0)
    {
        // Empty expression evaluates to 0
        return string_create_from_cstr("0");
    }
    
    // Evaluate the arithmetic expression
    ArithmeticResult result = arithmetic_evaluate(exp, exp->vars, expr_text);
    
    if (result.failed)
    {
        log_warn("expand_arithmetic: evaluation failed: %s", string_cstr(result.error));
        arithmetic_result_free(&result);
        return string_create_from_cstr("0");
    }
    
    // Convert result to string
    long val = result.value;
    arithmetic_result_free(&result);
    
    return string_from_long(val);
}

// Part-level expansion helpers
static void expand_word_part_literal(const part_t *part, string_t *expanded)
{
    const string_t *text = part_get_text(part);
    if (text != NULL)
    {
        string_append(expanded, text);
    }
}

static void expand_word_part_tilde(const part_t *part, string_t *expanded, bool is_quoted, bool at_word_start)
{
    const string_t *text = part_get_text(part);
    if (text == NULL)
        return;

    if (at_word_start && !is_quoted)
    {
        string_t *tilde_expanded = expand_tilde(text);
        string_append(expanded, tilde_expanded);
        string_destroy(&tilde_expanded);
        return;
    }

    string_append(expanded, text);
}

static bool expand_word_part_parameter(expander_t *exp, const part_t *part, string_t *expanded, bool is_quoted)
{
    bool produced_unquoted_expansion = false;

    const string_t *pname = part_get_param_name(part);
    bool is_at = (pname && string_length(pname) == 1 && string_at(pname, 0) == '@');
    bool is_star = (pname && string_length(pname) == 1 && string_at(pname, 0) == '*');

    if (!is_quoted && (is_at || is_star))
    {
        char sep = ' ';
        if (exp->ifs && string_length(exp->ifs) > 0)
            sep = string_at(exp->ifs, 0);

        int pcount = positional_params_count(exp->pos_stack);
        for (int ai = 0; ai < pcount; ai++)
        {
            if (ai > 0)
                string_append_char(expanded, sep);
            const string_t *pv = positional_params_get(exp->pos_stack, ai + 1);
            if (pv)
                string_append(expanded, pv);
        }

        if (is_at)
            produced_unquoted_expansion = true;  // $@ triggers field splitting
    }
    else
    {
        string_t *param_value = expand_parameter(exp, part);
        if (param_value != NULL)
        {
            string_append(expanded, param_value);
            string_destroy(&param_value);
            if (!is_quoted)
                produced_unquoted_expansion = true;
        }
    }

    return produced_unquoted_expansion;
}

static bool expand_word_part_command_subst(expander_t *exp, const part_t *part, string_t *expanded, bool is_quoted)
{
    bool produced_unquoted_expansion = false;

    string_t *cmd_output = expand_command_substitution(exp, part);
    if (cmd_output != NULL)
    {
        while (string_length(cmd_output) > 0 && string_back(cmd_output) == '\n')
        {
            string_pop_back(cmd_output);
        }

        string_append(expanded, cmd_output);
        string_destroy(&cmd_output);

        if (!is_quoted)
            produced_unquoted_expansion = true;
    }

    return produced_unquoted_expansion;
}

static bool expand_word_part_arithmetic(expander_t *exp, const part_t *part, string_t *expanded, bool is_quoted)
{
    bool produced_unquoted_expansion = false;

    string_t *arith_result = expand_arithmetic(exp, part);
    if (arith_result != NULL)
    {
        string_append(expanded, arith_result);
        string_destroy(&arith_result);

        if (!is_quoted)
            produced_unquoted_expansion = true;
    }

    return produced_unquoted_expansion;
}

/**
 * Check if a character is in IFS.
 */
static bool is_ifs_char(char c, const string_t *ifs)
{
    const char str[2] = {c, '\0'};
    if (string_find_cstr(ifs, str) == -1)
        return false;
    return true;
}

/**
 * Check if a character is an IFS whitespace character.
 */
static bool is_ifs_whitespace(char c, const string_t *ifs)
{
    if (c != ' ' && c != '\t' && c != '\n')
        return false;
    
    return is_ifs_char(c, ifs);
}

/**
 * Perform field splitting on a string.
 * Returns a list of strings split according to IFS.
 */
static string_list_t *field_split(const string_t *str, const string_t *ifs, bool was_quoted)
{
    string_list_t *result = string_list_create();
    
    // If the word was quoted, no field splitting
    if (was_quoted)
    {
        string_list_push_back(result, str);
        return result;
    }
    
    // If IFS is NULL or empty, no field splitting
    if (ifs == NULL || string_length(ifs) == 0)
    {
        string_list_push_back(result, str);
        return result;
    }
    
    // If the string is empty, return empty list
    if (string_length(str) == 0)
    {
        return result;
    }
    
    const char *data = string_cstr(str);
    int len = string_length(str);
    string_t *current_field = string_create();
    
    int i = 0;
    
    // Skip leading IFS whitespace
    while (i < len && is_ifs_whitespace(data[i], ifs))
        i++;
    
    while (i < len)
    {
        char c = data[i];
        
        if (is_ifs_char(c, ifs))
        {
            // We hit an IFS character
            if (is_ifs_whitespace(c, ifs))
            {
                // IFS whitespace: finish current field if non-empty
                if (string_length(current_field) > 0)
                {
                    string_list_move_push_back(result, current_field);
                    current_field = string_create();
                }
                
                // Skip consecutive IFS whitespace
                while (i < len && is_ifs_whitespace(data[i], ifs))
                    i++;
            }
            else
            {
                // Non-whitespace IFS character: always creates a field boundary
                string_list_move_push_back(result, current_field);
                current_field = string_create();
                i++;
            }
        }
        else
        {
            // Regular character: add to current field
            string_append_char(current_field, c);
            i++;
        }
    }
    
    // Add final field if non-empty
    if (string_length(current_field) > 0)
    {
        string_list_move_push_back(result, current_field);
    }
    else
    {
        string_destroy(&current_field);
    }
    
    // Per POSIX, if no fields were produced and the input was not empty,
    // return empty list (not a list with one empty field)
    // The check at line 271-274 already handles truly empty input
    
    return result;
}

// ============================================================================
// Main Expansion Entry Points
// ============================================================================

/**
 * Expand an AST node (stub implementation).
 * 
 * In a full implementation, this would:
 * - Traverse the AST recursively
 * - Expand all TOKEN_WORD nodes in commands
 * - Handle parameter expansion, command substitution, arithmetic
 * - Perform field splitting and pathname expansion
 * 
 * For now, returns the node unchanged.
 */
ast_node_t *expander_expand_ast(expander_t *exp, ast_node_t *node)
{
    Expects_not_null(exp);
    Expects_not_null(node);

    log_debug("expander_expand_ast: expanding AST node (stub)");
    
    // TODO: Implement full AST traversal and expansion
    // For now, just return the node unchanged
    return node;
}

/**
 * Expand a single word token into a list of strings.
 * 
 * This performs word expansion steps:
 * 1. Tilde expansion
 * 2. Parameter expansion
 * 3. Command substitution
 * 4. Arithmetic expansion
 * 5. Field splitting (for unquoted expansions)
 * 
 * For now, pathname expansion (globbing) is not implemented.
 */
string_list_t *expander_expand_word(expander_t *exp, token_t *word_token)
{
    Expects_not_null(exp);
    Expects_not_null(word_token);

    log_debug("expander_expand_word: expanding word token");
    
    if (token_get_type(word_token) != TOKEN_WORD)
    {
        // Not a word token, return empty list
        return string_list_create();
    }
    
    // Step 1: Perform expansions and build the expanded string
    string_t *expanded = string_create();
    bool has_unquoted_expansion = false;
    const part_list_t *parts = token_get_parts_const(word_token);
    
    if (parts != NULL)
    {
        int part_count = part_list_size(parts);
        
        // Special case: "$@" must preserve word boundaries and not undergo field splitting
        // This must be detected BEFORE we start building the expanded string
        if (part_count == 1)
        {
            part_t *only_part = part_list_get(parts, 0);
            if (part_get_type(only_part) == PART_PARAMETER &&
                part_was_double_quoted(only_part))
            {
                const string_t *param_name = part_get_param_name(only_part);
                if (param_name && 
                    string_length(param_name) == 1 && 
                    string_at(param_name, 0) == '@')
                {
                    // "$@" - return all positional params as separate fields
                    return positional_params_get_all(exp->pos_stack);
                }
            }
        }
       
        for (int i = 0; i < part_count; i++)
        {
            part_t *part = part_list_get(parts, i);
            part_type_t type = part_get_type(part);
            bool is_quoted = part_was_single_quoted(part) || part_was_double_quoted(part);
            bool part_unquoted_expansion = false;
            
            switch (type)
            {
                case PART_LITERAL:
                    expand_word_part_literal(part, expanded);
                    break;
                
                case PART_TILDE:
                    expand_word_part_tilde(part, expanded, is_quoted, i == 0);
                    break;
                
                case PART_PARAMETER:
                    part_unquoted_expansion = expand_word_part_parameter(exp, part, expanded, is_quoted);
                    break;
                
                case PART_COMMAND_SUBST:
                    part_unquoted_expansion = expand_word_part_command_subst(exp, part, expanded, is_quoted);
                    break;
                
                case PART_ARITHMETIC:
                    part_unquoted_expansion = expand_word_part_arithmetic(exp, part, expanded, is_quoted);
                    break;
                
                default:
                    log_warn("expander_expand_word: unknown part type %d", type);
                    break;
            }

            if (part_unquoted_expansion)
                has_unquoted_expansion = true;
        }
    }
    
    // Step 2: Perform field splitting if there were unquoted expansions
    string_list_t *result;
    if (has_unquoted_expansion)
    {
        result = field_split(expanded, exp->ifs, false);
        string_destroy(&expanded);  // field_split clones, so we need to free
    }
    else
    {
        // No field splitting needed
        result = string_list_create();
        string_list_move_push_back(result, expanded);
        // Ownership transferred to result
    }
    
    // Step 3: Pathname expansion (globbing)
    if (exp->pathname_expansion_callback != NULL)
    {
        string_list_t *globbed = string_list_create();
        int field_count = string_list_size(result);
        
        for (int i = 0; i < field_count; i++)
        {
            const string_t *field = string_list_at(result, i);
            
            // Check if the field contains glob metacharacters
            bool has_glob_chars = false;
            for (int j = 0; j < (int)string_length(field); j++)
            {
                char c = string_at(field, j);
                if (c == '*' || c == '?' || c == '[' || c == ']')
                {
                    has_glob_chars = true;
                    break;
                }
            }
            
            if (has_glob_chars)
            {
                // Invoke callback to expand the pattern
                string_list_t *matches = exp->pathname_expansion_callback(field, exp->pathname_expansion_user_data);
                
                if (matches != NULL && string_list_size(matches) > 0)
                {
                    // Add all matches
                    for (int m = 0; m < string_list_size(matches); m++)
                    {
                        string_list_push_back(globbed, string_list_at(matches, m));
                    }
                    string_list_destroy(&matches);
                }
                else
                {
                    // No matches - keep the pattern literal (POSIX behavior)
                    string_list_push_back(globbed, field);
                    if (matches != NULL)
                        string_list_destroy(&matches);
                }
            }
            else
            {
                // No glob characters - keep as-is
                string_list_push_back(globbed, field);
            }
        }
        
        string_list_destroy(&result);
        result = globbed;
    }
    
    return result;
}

/**
 * Expand a string directly (for arithmetic evaluation).
 * 
 * This performs parameter expansion on a raw string by scanning for $VAR
 * and ${VAR} patterns and replacing them with variable values. This is a
 * simplified expansion used by the arithmetic evaluator.
 * 
 * Note: This is a basic implementation that only handles simple parameter
 * expansion. It does not support:
 * - Special parameters ($?, $#, $$, $@, $*, $1, etc.)
 * - Command substitution
 * - Recursive expansion in braced forms (e.g., ${var:-word})
 * 
 * @param exp The expander instance (currently unused, reserved for future use)
 * @param vars The variable store to use for parameter expansion
 * @param input The input string to expand
 * @return A newly allocated string with expansions applied (caller must free),
 *         or NULL on error (e.g., unclosed braced expansion).
 */
char *expand_string(expander_t *exp, variable_store_t *vars, const char *input)
{
    (void)exp;  // Not currently used, but kept for future extensions
    
    if (input == NULL)
        return NULL;
    
    string_t *result = string_create();
    size_t i = 0;
    size_t len = strlen(input);
    
    while (i < len) {
        if (input[i] == '$') {
            i++;  // Skip the $
            
            if (i >= len) {
                // Lone $ at end of string
                string_append_char(result, '$');
                break;
            }
            
            // Check for ${VAR} or $VAR
            bool braced = false;
            if (input[i] == '{') {
                braced = true;
                i++;  // Skip the {
            }
            
            // Extract variable name
            string_t *var_name = string_create();
            size_t var_start_pos = i;
            bool found_closing_brace = false;
            while (i < len) {
                char c = input[i];
                if (braced) {
                    if (c == '}') {
                        i++;  // Skip the }
                        found_closing_brace = true;
                        break;
                    }
                    string_append_char(var_name, c);
                    i++;
                } else {
                    // For unbraced variables, first char must be letter/underscore
                    if (i == var_start_pos) {
                        if (isalpha(c) || c == '_') {
                            string_append_char(var_name, c);
                            i++;
                        } else {
                            break;
                        }
                    } else {
                        // Subsequent characters can be alphanumeric or underscore
                        if (isalnum(c) || c == '_') {
                            string_append_char(var_name, c);
                            i++;
                        } else {
                            break;
                        }
                    }
                }
            }
            
            // Check for unclosed braced variable expansion
            if (braced && !found_closing_brace) {
                // Unclosed braced expansion - handle error
                string_destroy(&var_name);
                string_destroy(&result);
                return NULL;
            }
            
            // Look up the variable value
            if (string_length(var_name) > 0) {
                const char *value = variable_store_get_value_cstr(vars, string_data(var_name));
                if (value != NULL) {
                    string_append_cstr(result, value);
                }
                // If variable doesn't exist, expand to empty string
            } else {
                // Empty variable name: ${}  or $ followed by non-identifier
                string_append_char(result, '$');
                if (braced) {
                    string_append_cstr(result, "{}");
                }
            }
            
            string_destroy(&var_name);
        } else {
            // Regular character
            string_append_char(result, input[i]);
            i++;
        }
    }
    
    // Convert to C string and free the string_t
    char *cstr = xstrdup(string_data(result));
    string_destroy(&result);
    
    return cstr;
}

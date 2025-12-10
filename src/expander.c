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
};

// ============================================================================
// Lifecycle Functions
// ============================================================================

expander_t *expander_create(void)
{
    expander_t *exp = xcalloc(1, sizeof(expander_t));
    exp->ifs = string_create_from_cstr(" \t\n");
    exp->vars = NULL;
    return exp;
}

void expander_destroy(expander_t *expander)
{
    if (expander == NULL)
        return;

    if (expander->ifs != NULL)
        string_destroy(&expander->ifs);
    
    xfree(expander);
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
 * For now, returns empty string (stub for future implementation).
 */
static string_t *expand_command_substitution(expander_t *exp, const part_t *part)
{
    (void)exp;
    (void)part;
    
    log_debug("expand_command_substitution: command substitution not yet implemented");
    return string_create();
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
        
        for (int i = 0; i < part_count; i++)
        {
            part_t *part = part_list_get(parts, i);
            part_type_t type = part_get_type(part);
            bool is_quoted = part_was_single_quoted(part) || part_was_double_quoted(part);
            
            switch (type)
            {
                case PART_LITERAL:
                {
                    const string_t *text = part_get_text(part);
                    if (text != NULL)
                    {
                        string_append(expanded, text);
                    }
                    break;
                }
                
                case PART_TILDE:
                {
                    const string_t *text = part_get_text(part);
                    if (text != NULL)
                    {
                        // Tilde expansion occurs at the beginning of a word or after : or =
                        // For now, only handle beginning of word
                        // TODO: Handle tilde after : or = in assignments (e.g., PATH=~:~/bin)
                        if (i == 0 && !is_quoted)
                        {
                            string_t *tilde_expanded = expand_tilde(text);
                            string_append(expanded, tilde_expanded);
                            string_destroy(&tilde_expanded);
                            
                            // Tilde expansion does NOT undergo field splitting per POSIX
                            // Do NOT set has_unquoted_expansion here
                        }
                        else
                        {
                            string_append(expanded, text);
                        }
                    }
                    break;
                }
                
                case PART_PARAMETER:
                {
                    string_t *param_value = expand_parameter(exp, part);
                    if (param_value != NULL)
                    {
                        string_append(expanded, param_value);
                        string_destroy(&param_value);
                        
                        // Track if this was an unquoted expansion for field splitting
                        if (!is_quoted)
                            has_unquoted_expansion = true;
                    }
                    break;
                }
                
                case PART_COMMAND_SUBST:
                {
                    string_t *cmd_output = expand_command_substitution(exp, part);
                    if (cmd_output != NULL)
                    {
                        // Remove trailing newlines (POSIX requirement)
                        while (string_length(cmd_output) > 0 && 
                               string_back(cmd_output) == '\n')
                        {
                            string_pop_back(cmd_output);
                        }
                        
                        string_append(expanded, cmd_output);
                        string_destroy(&cmd_output);
                        
                        if (!is_quoted)
                            has_unquoted_expansion = true;
                    }
                    break;
                }
                
                case PART_ARITHMETIC:
                {
                    string_t *arith_result = expand_arithmetic(exp, part);
                    if (arith_result != NULL)
                    {
                        string_append(expanded, arith_result);
                        string_destroy(&arith_result);
                        
                        if (!is_quoted)
                            has_unquoted_expansion = true;
                    }
                    break;
                }
                
                default:
                    log_warn("expander_expand_word: unknown part type %d", type);
                    break;
            }
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
    
    // Step 3: Pathname expansion (globbing) would go here
    // For now, we skip it
    
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

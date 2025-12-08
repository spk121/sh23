#include "expander.h"
#include "logging.h"
#include "xalloc.h"
#include "string_t.h"
#include "token.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>

// Internal expander structure
struct expander_t
{   
    string_t *ifs;
    // Variable store can be added here when needed for parameter expansion
    // variable_store_t *vars;
};

// ============================================================================
// Lifecycle Functions
// ============================================================================

expander_t *expander_create(void)
{
    expander_t *exp = xcalloc(1, sizeof(expander_t));
    exp->ifs = string_create_from_cstr(" \t\n");
    return exp;
}

void expander_destroy(expander_t *expander)
{
    if (expander == NULL)
        return;

    if (expander->ifs != NULL)
        string_destroy(expander->ifs);
    
    xfree(expander);
}

// ============================================================================
// IFS Management
// ============================================================================

void expander_set_ifs(expander_t *exp, const string_t *ifs)
{
    Expects_not_null(exp);
    Expects_not_null(ifs);

    if (exp->ifs != NULL)
    {
        string_destroy(exp->ifs);
    }
    exp->ifs = string_clone(ifs);
}

const string_t *expander_get_ifs(const expander_t *exp)
{
    Expects_not_null(exp);
    return exp->ifs;
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
    const char *str = string_data(text);
    
    if (str == NULL || str[0] != '~')
        return string_clone(text);
    
    // ~ alone or ~/...
    if (str[1] == '/' || str[1] == '\0')
    {
        const char *home = getenv("HOME");
        if (home == NULL || home[0] == '\0')
            return string_clone(text);  // No expansion
        
        string_t *result = string_create_from_cstr(home);
        if (str[1] == '/')
            string_append_cstr(result, str + 1);
        return result;
    }
    
    // ~+ expands to PWD
    if (str[1] == '+' && (str[2] == '/' || str[2] == '\0'))
    {
        const char *pwd = getenv("PWD");
        if (pwd == NULL || pwd[0] == '\0')
            return string_clone(text);
        
        string_t *result = string_create_from_cstr(pwd);
        if (str[2] == '/')
            string_append_cstr(result, str + 2);
        return result;
    }
    
    // ~- expands to OLDPWD
    if (str[1] == '-' && (str[2] == '/' || str[2] == '\0'))
    {
        const char *oldpwd = getenv("OLDPWD");
        if (oldpwd == NULL || oldpwd[0] == '\0')
            return string_clone(text);
        
        string_t *result = string_create_from_cstr(oldpwd);
        if (str[2] == '/')
            string_append_cstr(result, str + 2);
        return result;
    }
    
    // ~user or ~user/...
    const char *slash = strchr(str, '/');
    size_t name_len = slash ? (size_t)(slash - str - 1) : strlen(str + 1);
    
    if (name_len == 0)
        return string_clone(text);
    
    char *username = xmalloc(name_len + 1);
    memcpy(username, str + 1, name_len);
    username[name_len] = '\0';
    
    struct passwd *pw = getpwnam(username);
    xfree(username);
    
    if (pw == NULL || pw->pw_dir == NULL)
        return string_clone(text);  // No such user
    
    string_t *result = string_create_from_cstr(pw->pw_dir);
    if (slash != NULL)
        string_append_cstr(result, slash);
    
    return result;
}

/**
 * Perform parameter expansion on a part.
 * For now, returns empty string (stub for future implementation).
 */
static string_t *expand_parameter(expander_t *exp, const part_t *part)
{
    (void)exp;
    const string_t *param_name = part_get_param_name(part);
    
    if (param_name == NULL)
        return string_create_empty(0);
    
    // For now, check a few common environment variables
    const char *name = string_data(param_name);
    const char *value = getenv(name);
    
    if (value != NULL)
        return string_create_from_cstr(value);
    
    // Return empty string for unset variables
    return string_create_empty(0);
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
    return string_create_empty(0);
}

/**
 * Perform arithmetic expansion.
 * For now, returns "0" (stub for future implementation).
 */
static string_t *expand_arithmetic(expander_t *exp, const part_t *part)
{
    (void)exp;
    (void)part;
    
    log_debug("expand_arithmetic: arithmetic expansion not yet implemented");
    return string_create_from_cstr("0");
}

/**
 * Check if a character is an IFS whitespace character.
 */
static bool is_ifs_whitespace(char c, const string_t *ifs)
{
    if (c != ' ' && c != '\t' && c != '\n')
        return false;
    
    const char *ifs_str = string_data(ifs);
    int ifs_len = string_length(ifs);
    
    for (int i = 0; i < ifs_len; i++)
    {
        if (ifs_str[i] == c)
            return true;
    }
    
    return false;
}

/**
 * Check if a character is in IFS.
 */
static bool is_ifs_char(char c, const string_t *ifs)
{
    const char *ifs_str = string_data(ifs);
    int ifs_len = string_length(ifs);
    
    for (int i = 0; i < ifs_len; i++)
    {
        if (ifs_str[i] == c)
            return true;
    }
    
    return false;
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
        string_list_clone_append(result, str);
        return result;
    }
    
    // If IFS is null (empty), no field splitting
    if (ifs == NULL || string_length(ifs) == 0)
    {
        string_list_clone_append(result, str);
        return result;
    }
    
    // If the string is empty, return empty list
    if (string_length(str) == 0)
    {
        return result;
    }
    
    const char *data = string_data(str);
    int len = string_length(str);
    string_t *current_field = string_create_empty(64);
    
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
                    string_list_take_append(result, current_field);
                    current_field = string_create_empty(64);
                }
                
                // Skip consecutive IFS whitespace
                while (i < len && is_ifs_whitespace(data[i], ifs))
                    i++;
            }
            else
            {
                // Non-whitespace IFS character: always creates a field boundary
                string_list_take_append(result, current_field);
                current_field = string_create_empty(64);
                i++;
            }
        }
        else
        {
            // Regular character: add to current field
            string_append_ascii_char(current_field, c);
            i++;
        }
    }
    
    // Add final field if non-empty
    if (string_length(current_field) > 0)
    {
        string_list_take_append(result, current_field);
    }
    else
    {
        string_destroy(current_field);
    }
    
    // If no fields were produced, add an empty field
    if (string_list_size(result) == 0)
    {
        string_list_take_append(result, string_create_empty(0));
    }
    
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
    string_t *expanded = string_create_empty(128);
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
                        // Only expand tilde if it's the first part and unquoted
                        if (i == 0 && !is_quoted)
                        {
                            string_t *tilde_expanded = expand_tilde(text);
                            string_append(expanded, tilde_expanded);
                            string_destroy(tilde_expanded);
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
                        string_destroy(param_value);
                        
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
                               string_char_at(cmd_output, string_length(cmd_output) - 1) == '\n')
                        {
                            string_drop_back(cmd_output, 1);
                        }
                        
                        string_append(expanded, cmd_output);
                        string_destroy(cmd_output);
                        
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
                        string_destroy(arith_result);
                        
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
        string_destroy(expanded);  // field_split clones, so we need to free
    }
    else
    {
        // No field splitting needed
        result = string_list_create();
        string_list_take_append(result, expanded);
        // Ownership transferred to result
    }
    
    // Step 3: Pathname expansion (globbing) would go here
    // For now, we skip it
    
    return result;
}

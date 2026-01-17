#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "exec_expander.h"
#include "expander.h"
#include "positional_params.h"
#include "string_t.h"
#include "token.h"
#include "variable_store.h"
#include "xalloc.h"

#ifdef POSIX_API
#include <glob.h>
#include <pwd.h>
#include <unistd.h>
#endif

/**
 * Internal structure for the expander.
 */
struct expander_t
{
    exec_t *executor;  // Executor context for callbacks
};

/* ============================================================================
 * Constructor / Destructor
 * ============================================================================ */

expander_t *expander_create(exec_t *executor)
{
    expander_t *exp = xcalloc(1, sizeof(expander_t));
    exp->executor = executor;
    return exp;
}

void expander_destroy(expander_t **exp_ptr)
{
    if (!exp_ptr || !*exp_ptr)
        return;
    expander_t *exp = *exp_ptr;
    // exec_destroy(&exp->executor);
    xfree(exp);
    *exp_ptr = NULL;
}

/* ============================================================================
 * Static helper functions for expansion activities
 * ============================================================================ */

static string_t *expand_parameter(expander_t *exp, const part_t *part)
{
    if (!part->param_name)
    {
        return string_create(); // Empty string if param_name is NULL
    }

    const char *name = string_cstr(part->param_name);
    string_t *result = NULL;

    // Get positional params and variables from executor
    positional_params_t *params = exec_get_positional_params(exp->executor);
    variable_store_t *vars = exec_get_variables(exp->executor);

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
            char *val;
            val = getenv(name);
            if (val)
            {
                result = string_create_from_cstr(val);
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

static string_t *expand_command_subst(expander_t *exp, const part_t *part)
{
    // Convert nested tokens to a command string
    string_t *cmd = string_create();
    for (int i = 0; i < token_list_size(part->nested); i++)
    {
        token_t *t = token_list_get(part->nested, i);
        string_t *ts = token_to_string(t); // Approximate stringification
        string_append(cmd, ts);
        string_destroy(&ts);
        if (i < token_list_size(part->nested) - 1)
        {
            string_append_cstr(cmd, " "); // Space between tokens
        }
    }
    
    string_t *result = exec_command_subst_callback(exp->executor, cmd);
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
    return exec_tilde_expand_callback(exp->executor, part->text);
}

static string_t *expand_parts_to_string(expander_t *exp, const part_list_t *parts)
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
        if (part_expanded)
        {
            string_append(result, part_expanded);
            string_destroy(&part_expanded);
        }
    }
    return result;
}

/* ============================================================================
 * Expansion entry points
 * ============================================================================ */

string_list_t *exec_expand_word(expander_t *exp, const token_t *tok)
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
    string_t *expanded = expand_parts_to_string(exp, token_get_parts_const(tok));

    // Get IFS for field splitting
    variable_store_t *vars = exec_get_variables(exp->executor);
    const char *ifs = NULL;
    if (vars)
    {
        ifs = variable_store_get_value_cstr(vars, "IFS");
    }
    if (!ifs)
    {
        ifs = getenv("IFS");
    }
    if (!ifs)
    {
        ifs = " \t\n"; // Default IFS
    }

    // Field splitting: only if needs_field_splitting and IFS is not empty
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

        // If field splitting produced zero fields, add one empty field
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
            string_list_t *matches = glob_until_expand_path(exp->executor, pattern);
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
                const string_t *pat = string_list_at(fields, i);
                string_list_push_back(globs, pat);
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

string_list_t *expander_expand_words(expander_t *exp, const token_list_t *tokens)
{
    if (!tokens)
    {
        return NULL;
    }
    string_list_t *result = string_list_create();
    for (int i = 0; i < token_list_size(tokens); i++)
    {
        token_t *tok = token_list_get(tokens, i);
        string_list_t *expanded = exec_expand_word(exp, tok);
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

string_t *exec_expand_redirection_target(expander_t *exp, const token_t *tok)
{
    if (!tok || tok->type != TOKEN_WORD)
    {
        return NULL;
    }
    // Redirection targets undergo tilde, parameter, command, arithmetic expansion, but no field
    // splitting or pathname expansion
    return expand_parts_to_string(exp, token_get_parts_const(tok));
}

string_t *expander_expand_assignment_value(expander_t *exp, const token_t *tok)
{
    if (!tok || tok->type != TOKEN_ASSIGNMENT_WORD)
    {
        return NULL;
    }
    // Assignment values undergo tilde, parameter, command, arithmetic expansion, but no field
    // splitting or pathname expansion
    return expand_parts_to_string(exp, tok->assignment_value);
}

string_t *exec_expand_heredoc(expander_t *exp, const string_t *body, bool is_quoted)
{
    if (is_quoted)
    {
        return string_create_from(body);
    }
    // For unquoted heredocs, perform parameter, command, arithmetic expansions
    // This is a stub; proper implementation would require tokenizing the heredoc body
    (void)exp;
    return string_create_from(body);
}
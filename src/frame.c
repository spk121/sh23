/**
 * frame.c - Public API implementation for exec_frame_t
 *
 * This file implements the public API defined in frame.h, providing a clean
 * interface to execution frames without exposing internal implementation details.
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "frame.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "alias_store.h"
#include "exec.h"
#include "exec_expander.h"
#include "exec_frame.h"
#include "exec_internal.h"
#include "func_store.h"
#include "gnode.h"
#include "job_store.h"
#include "logging.h"
#include "lower.h"
#include "parser.h"
#include "positional_params.h"
#include "string_list.h"
#include "string_t.h"
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
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <process.h>
#endif

/* ============================================================================
 * Error Handling
 * ============================================================================ */

bool frame_has_error(const exec_frame_t *frame)
{
    Expects_not_null(frame);
    Expects_not_null(frame->executor);

    const char *err = exec_get_error(frame->executor);
    return (err != NULL && err[0] != '\0');
}

const string_t *frame_get_error_message(const exec_frame_t *frame)
{
    Expects_not_null(frame);
    Expects_not_null(frame->executor);

    return frame->executor->error_msg;
}

void frame_clear_error(exec_frame_t *frame)
{
    Expects_not_null(frame);
    Expects_not_null(frame->executor);

    exec_clear_error(frame->executor);
}

void frame_set_error(exec_frame_t *frame, const string_t *error)
{
    Expects_not_null(frame);
    Expects_not_null(frame->executor);
    Expects_not_null(error);

    exec_set_error(frame->executor, "%s", string_cstr(error));
}

void frame_set_error_printf(exec_frame_t *frame, const char *format, ...)
{
    Expects_not_null(frame);
    Expects_not_null(frame->executor);
    Expects_not_null(format);

    va_list args;
    va_start(args, format);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);

    va_end(args);

    exec_set_error(frame->executor, "%s", buffer);
}

/* ============================================================================
 * Variable Access
 * ============================================================================ */

bool frame_has_variable(const exec_frame_t *frame, const string_t *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    const string_t *value = exec_frame_get_variable(frame, name);
    return (value != NULL);
}

bool frame_has_variable_cstr(const exec_frame_t *frame, const char *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    bool result = frame_has_variable(frame, name_str);
    string_destroy(&name_str);
    return result;
}

string_t *frame_get_variable_value(exec_frame_t *frame, const string_t *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    const string_t *value = exec_frame_get_variable(frame, name);
    if (value)
    {
        return string_create_from(value);
    }
    return string_create();
}

string_t *frame_get_variable_cstr(exec_frame_t *frame, const char *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    string_t *result = frame_get_variable_value(frame, name_str);
    string_destroy(&name_str);
    return result;
}

bool frame_variable_is_exported(exec_frame_t *frame, const string_t *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(frame->variables);

    variable_view_t view;
    if (variable_store_get_variable(frame->variables, name, &view))
    {
        return view.exported;
    }
    return false;
}

bool frame_variable_is_exported_cstr(exec_frame_t *frame, const char *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    bool result = frame_variable_is_exported(frame, name_str);
    string_destroy(&name_str);
    return result;
}

bool frame_variable_is_readonly(exec_frame_t *frame, const string_t *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(frame->variables);

    variable_view_t view;
    if (variable_store_get_variable(frame->variables, name, &view))
    {
        return view.read_only;
    }
    return false;
}

bool frame_variable_is_readonly_cstr(exec_frame_t *frame, const char *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    bool result = frame_variable_is_readonly(frame, name_str);
    string_destroy(&name_str);
    return result;
}

string_t *frame_get_ps1(exec_frame_t *frame)
{
    Expects_not_null(frame);

    string_t *name = string_create_from_cstr("PS1");
    string_t *value = frame_get_variable_value(frame, name);
    string_destroy(&name);

    if (value && string_length(value) > 0)
    {
        return value;
    }

    if (value)
    {
        string_destroy(&value);
    }
    return string_create_from_cstr("$ ");
}

string_t *frame_get_ps2(exec_frame_t *frame)
{
    Expects_not_null(frame);

    string_t *name = string_create_from_cstr("PS2");
    string_t *value = frame_get_variable_value(frame, name);
    string_destroy(&name);

    if (value && string_length(value) > 0)
    {
        return value;
    }

    if (value)
    {
        string_destroy(&value);
    }
    return string_create_from_cstr("> ");
}

var_store_error_t frame_set_variable(exec_frame_t *frame, const string_t *name,
                                     const string_t *value)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(value);

    variable_store_t *vars = frame->local_variables ? frame->local_variables : frame->variables;
    if (!vars)
    {
        return VAR_STORE_ERROR_NOT_FOUND;
    }

    variable_view_t view;
    bool exists = variable_store_get_variable(vars, name, &view);

    if (exists && view.read_only)
    {
        if (string_compare(view.value, value) != 0)
        {
            return VAR_STORE_ERROR_READ_ONLY;
        }
        return VAR_STORE_ERROR_NONE;
    }

    bool exported = exists ? view.exported : false;
    bool read_only = exists ? view.read_only : false;

    return variable_store_add(vars, name, value, exported, read_only);
}

var_store_error_t frame_set_variable_cstr(exec_frame_t *frame, const char *name, const char *value)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(value);

    string_t *name_str = string_create_from_cstr(name);
    string_t *value_str = string_create_from_cstr(value);
    var_store_error_t result = frame_set_variable(frame, name_str, value_str);
    string_destroy(&name_str);
    string_destroy(&value_str);
    return result;
}

var_store_error_t frame_set_variable_exported(exec_frame_t *frame, const string_t *name,
                                              bool exported)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(frame->variables);

    return variable_store_set_exported(frame->variables, name, exported);
}

var_store_error_t frame_set_variable_readonly(exec_frame_t *frame, const string_t *name,
                                              bool readonly)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(frame->variables);

    return variable_store_set_read_only(frame->variables, name, readonly);
}

var_store_error_t frame_unset_variable(exec_frame_t *frame, const string_t *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    variable_store_t *vars = frame->local_variables ? frame->local_variables : frame->variables;
    if (!vars)
    {
        return VAR_STORE_ERROR_NOT_FOUND;
    }

    variable_view_t view;
    if (!variable_store_get_variable(vars, name, &view))
    {
        return VAR_STORE_ERROR_NOT_FOUND;
    }

    if (view.read_only)
    {
        return VAR_STORE_ERROR_READ_ONLY;
    }

    variable_store_remove(vars, name);

#if defined(POSIX_API) || defined(UCRT_API)
    if (view.exported)
    {
#ifdef POSIX_API
        unsetenv(string_cstr(name));
#elifdef UCRT_API
        _putenv_s(string_cstr(name), "");
#endif
    }
#endif

    return VAR_STORE_ERROR_NONE;
}

var_store_error_t frame_unset_variable_cstr(exec_frame_t *frame, const char *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    var_store_error_t result = frame_unset_variable(frame, name_str);
    string_destroy(&name_str);
    return result;
}

static void print_exported_var_callback(const string_t *name, const string_t *value, bool exported,
                                       bool read_only, void *user_data)
{
    (void)read_only;
    (void)user_data;

    if (exported)
    {
        printf("export %s=%s\n", string_cstr(name), string_cstr(value));
    }
}

void frame_print_exported_variables_in_export_format(exec_frame_t *frame)
{
    Expects_not_null(frame);

    if (frame->variables)
    {
        variable_store_for_each(frame->variables, print_exported_var_callback, NULL);
    }
}

static void print_var_callback(const string_t *name, const string_t *value, bool exported,
                               bool read_only, void *user_data)
{
    bool reusable = *(bool *)user_data;

    if (reusable)
    {
        if (exported && read_only)
        {
            printf("export -r %s=%s\n", string_cstr(name), string_cstr(value));
        }
        else if (exported)
        {
            printf("export %s=%s\n", string_cstr(name), string_cstr(value));
        }
        else if (read_only)
        {
            printf("readonly %s=%s\n", string_cstr(name), string_cstr(value));
        }
        else
        {
            printf("%s=%s\n", string_cstr(name), string_cstr(value));
        }
    }
    else
    {
        printf("%s=%s", string_cstr(name), string_cstr(value));
        if (exported || read_only)
        {
            printf(" [");
            if (exported)
                printf("exported");
            if (exported && read_only)
                printf(", ");
            if (read_only)
                printf("readonly");
            printf("]");
        }
        printf("\n");
    }
}

void frame_print_variables(exec_frame_t *frame, bool reusable_format)
{
    Expects_not_null(frame);

    if (frame->variables)
    {
        variable_store_for_each(frame->variables, print_var_callback, &reusable_format);
    }
}

/* ============================================================================
 * Word and String Expansion
 * ============================================================================ */

string_t *frame_expand_string(exec_frame_t *frame, const string_t *text, expand_flags_t flags)
{
    Expects_not_null(frame);
    Expects_not_null(text);

    return expand_string(frame, text, flags);
}

string_list_t *frame_expand_word_token(exec_frame_t *frame, const token_t *tok)
{
    Expects_not_null(frame);
    Expects_not_null(tok);

    return expand_word(frame, (const token_t *)tok);
}

/* ============================================================================
 * Positional Parameters
 * ============================================================================ */

bool frame_has_positional_params(const exec_frame_t *frame)
{
    Expects_not_null(frame);

    return (frame->positional_params != NULL);
}

int frame_count_positional_params(const exec_frame_t *frame)
{
    Expects_not_null(frame);

    if (!frame->positional_params)
    {
        return 0;
    }

    return positional_params_count(frame->positional_params);
}

void frame_shift_positional_params(exec_frame_t *frame, int shift_count)
{
    Expects_not_null(frame);

    if (!frame->positional_params)
    {
        return;
    }

    positional_params_shift(frame->positional_params, shift_count);
}

void frame_replace_positional_params(exec_frame_t *frame, const string_list_t *new_params)
{
    Expects_not_null(frame);

    if (!frame->positional_params)
    {
        return;
    }

    int count = new_params ? string_list_size(new_params) : 0;
    string_t **params = NULL;

    if (count > 0)
    {
        params = xcalloc((size_t)count, sizeof(string_t *));
        for (int i = 0; i < count; i++)
        {
            params[i] = string_create_from(string_list_at(new_params, i));
        }
    }

    positional_params_replace(frame->positional_params, params, count);
}

string_t *frame_get_positional_param(const exec_frame_t *frame, int index)
{
    Expects_not_null(frame);

    if (!frame->positional_params)
    {
        return string_create();
    }

    if (index == 0)
    {
        const string_t *arg0 = positional_params_get_arg0(frame->positional_params);
        if (arg0)
        {
            return string_create_from(arg0);
        }
        return string_create();
    }

    const string_t *param = positional_params_get(frame->positional_params, index - 1);
    if (param)
    {
        return string_create_from(param);
    }

    return string_create();
}

string_list_t *frame_get_all_positional_params(const exec_frame_t *frame)
{
    Expects_not_null(frame);

    if (!frame->positional_params)
    {
        return string_list_create();
    }

    int count = positional_params_count(frame->positional_params);
    string_list_t *result = string_list_create();

    for (int i = 0; i < count; i++)
    {
        const string_t *param = positional_params_get(frame->positional_params, i);
        if (param)
        {
            string_list_push_back(result, param);
        }
    }

    return result;
}

/* ============================================================================
 * Named Options
 * ============================================================================ */

bool frame_has_named_option(const exec_frame_t *frame, const string_t *option_name)
{
    Expects_not_null(frame);
    Expects_not_null(option_name);

    if (!frame->opt_flags)
    {
        return false;
    }

    const char *name = string_cstr(option_name);

    if (strcmp(name, "allexport") == 0 || strcmp(name, "a") == 0)
        return true;
    if (strcmp(name, "errexit") == 0 || strcmp(name, "e") == 0)
        return true;
    if (strcmp(name, "ignoreeof") == 0)
        return true;
    if (strcmp(name, "noclobber") == 0 || strcmp(name, "C") == 0)
        return true;
    if (strcmp(name, "noglob") == 0 || strcmp(name, "f") == 0)
        return true;
    if (strcmp(name, "noexec") == 0 || strcmp(name, "n") == 0)
        return true;
    if (strcmp(name, "nounset") == 0 || strcmp(name, "u") == 0)
        return true;
    if (strcmp(name, "pipefail") == 0)
        return true;
    if (strcmp(name, "verbose") == 0 || strcmp(name, "v") == 0)
        return true;
    if (strcmp(name, "vi") == 0)
        return true;
    if (strcmp(name, "xtrace") == 0 || strcmp(name, "x") == 0)
        return true;

    return false;
}

bool frame_has_named_option_cstr(const exec_frame_t *frame, const char *option_name)
{
    Expects_not_null(frame);
    Expects_not_null(option_name);

    string_t *name_str = string_create_from_cstr(option_name);
    bool result = frame_has_named_option(frame, name_str);
    string_destroy(&name_str);
    return result;
}

bool frame_get_named_option(const exec_frame_t *frame, const string_t *option_name)
{
    Expects_not_null(frame);
    Expects_not_null(option_name);

    if (!frame->opt_flags)
    {
        return false;
    }

    const char *name = string_cstr(option_name);
    const exec_opt_flags_t *opts = frame->opt_flags;

    if (strcmp(name, "allexport") == 0 || strcmp(name, "a") == 0)
        return opts->allexport;
    if (strcmp(name, "errexit") == 0 || strcmp(name, "e") == 0)
        return opts->errexit;
    if (strcmp(name, "ignoreeof") == 0)
        return opts->ignoreeof;
    if (strcmp(name, "noclobber") == 0 || strcmp(name, "C") == 0)
        return opts->noclobber;
    if (strcmp(name, "noglob") == 0 || strcmp(name, "f") == 0)
        return opts->noglob;
    if (strcmp(name, "noexec") == 0 || strcmp(name, "n") == 0)
        return opts->noexec;
    if (strcmp(name, "nounset") == 0 || strcmp(name, "u") == 0)
        return opts->nounset;
    if (strcmp(name, "pipefail") == 0)
        return opts->pipefail;
    if (strcmp(name, "verbose") == 0 || strcmp(name, "v") == 0)
        return opts->verbose;
    if (strcmp(name, "vi") == 0)
        return opts->vi;
    if (strcmp(name, "xtrace") == 0 || strcmp(name, "x") == 0)
        return opts->xtrace;

    return false;
}

bool frame_get_named_option_cstr(const exec_frame_t *frame, const char *option_name)
{
    Expects_not_null(frame);
    Expects_not_null(option_name);

    string_t *name_str = string_create_from_cstr(option_name);
    bool result = frame_get_named_option(frame, name_str);
    string_destroy(&name_str);
    return result;
}

bool frame_set_named_option(exec_frame_t *frame, const string_t *option_name, bool value,
                            bool plus_prefix)
{
    Expects_not_null(frame);
    Expects_not_null(option_name);

    (void)plus_prefix;

    if (!frame->opt_flags)
    {
        return false;
    }

    const char *name = string_cstr(option_name);
    exec_opt_flags_t *opts = frame->opt_flags;

    if (strcmp(name, "allexport") == 0 || strcmp(name, "a") == 0)
    {
        opts->allexport = value;
        return true;
    }
    if (strcmp(name, "errexit") == 0 || strcmp(name, "e") == 0)
    {
        opts->errexit = value;
        return true;
    }
    if (strcmp(name, "ignoreeof") == 0)
    {
        opts->ignoreeof = value;
        return true;
    }
    if (strcmp(name, "noclobber") == 0 || strcmp(name, "C") == 0)
    {
        opts->noclobber = value;
        return true;
    }
    if (strcmp(name, "noglob") == 0 || strcmp(name, "f") == 0)
    {
        opts->noglob = value;
        return true;
    }
    if (strcmp(name, "noexec") == 0 || strcmp(name, "n") == 0)
    {
        opts->noexec = value;
        return true;
    }
    if (strcmp(name, "nounset") == 0 || strcmp(name, "u") == 0)
    {
        opts->nounset = value;
        return true;
    }
    if (strcmp(name, "pipefail") == 0)
    {
        opts->pipefail = value;
        return true;
    }
    if (strcmp(name, "verbose") == 0 || strcmp(name, "v") == 0)
    {
        opts->verbose = value;
        return true;
    }
    if (strcmp(name, "vi") == 0)
    {
        opts->vi = value;
        return true;
    }
    if (strcmp(name, "xtrace") == 0 || strcmp(name, "x") == 0)
    {
        opts->xtrace = value;
        return true;
    }

    return false;
}

bool frame_set_named_option_cstr(exec_frame_t *frame, const char *option_name, bool value,
                                 bool plus_prefix)
{
    Expects_not_null(frame);
    Expects_not_null(option_name);

    string_t *name_str = string_create_from_cstr(option_name);
    bool result = frame_set_named_option(frame, name_str, value, plus_prefix);
    string_destroy(&name_str);
    return result;
}

/* ============================================================================
 * Functions
 * ============================================================================ */

bool frame_has_function(const exec_frame_t *frame, const string_t *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    if (!frame->functions)
    {
        return false;
    }

    return func_store_has_name(frame->functions, name);
}

string_t *frame_get_function(exec_frame_t *frame, const string_t *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    if (!frame->functions)
    {
        return NULL;
    }

    const ast_node_t *func_def = func_store_get_def(frame->functions, name);
    if (!func_def)
    {
        return NULL;
    }

    return ast_node_to_string(func_def);
}

func_store_error_t frame_get_function_cstr(exec_frame_t *frame, const char *name, string_t **value)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(value);

    if (!frame->functions)
    {
        *value = NULL;
        return FUNC_STORE_ERROR_NOT_FOUND;
    }

    string_t *name_str = string_create_from_cstr(name);
    const ast_node_t *func_def = func_store_get_def(frame->functions, name_str);
    string_destroy(&name_str);

    if (!func_def)
    {
        *value = NULL;
        return FUNC_STORE_ERROR_NOT_FOUND;
    }

    *value = ast_node_to_string(func_def);
    return FUNC_STORE_ERROR_NONE;
}

func_store_error_t frame_set_function(exec_frame_t *frame, const string_t *name,
                                      const ast_node_t *func_def)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(func_def);
    Expects_not_null(frame->functions);

    func_store_error_t result = func_store_add(frame->functions, name, func_def);

    return result;
}

func_store_error_t frame_set_function_cstr(exec_frame_t *frame, const char *name,
                                           const char *value)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(value);

    string_t *name_str = string_create_from_cstr(name);
    gnode_t *root = NULL;
    parse_status_t status = parser_string_to_gnodes(value, &root);
    if (status != PARSE_OK)
    {
        string_destroy(&name_str);
        g_node_destroy(&root);
        return FUNC_STORE_ERROR_PARSE_FAILURE;
    }
    ast_t *node = ast_lower(root);
    func_store_error_t result = frame_set_function(frame, name_str, node);
    string_destroy(&name_str);
    return result;
}

func_store_error_t frame_unset_function(exec_frame_t *frame, const string_t *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    if (!frame->functions)
    {
        return FUNC_STORE_ERROR_NOT_FOUND;
    }

    return func_store_remove(frame->functions, name);
}

func_store_error_t frame_unset_function_cstr(exec_frame_t *frame, const char *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    func_store_error_t result = frame_unset_function(frame, name_str);
    string_destroy(&name_str);
    return result;
}

exec_status_t frame_call_function(exec_frame_t *frame, const string_t *name,
                                  const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    (void)args;

    if (!frame->functions)
    {
        return EXEC_ERROR;
    }

    const ast_node_t *func_def = func_store_get_def(frame->functions, name);
    if (!func_def)
    {
        return EXEC_ERROR;
    }

    return EXEC_NOT_IMPL;
}

/* ============================================================================
 * Exit Status
 * ============================================================================ */

int frame_get_last_exit_status(const exec_frame_t *frame)
{
    Expects_not_null(frame);

    return frame->last_exit_status;
}

void frame_set_last_exit_status(exec_frame_t *frame, int status)
{
    Expects_not_null(frame);

    frame->last_exit_status = status;
}

/* ============================================================================
 * Control Flow
 * ============================================================================ */

void frame_set_pending_control_flow(exec_frame_t *frame, exec_control_flow_t flow, int depth)
{
    Expects_not_null(frame);

    frame->pending_control_flow = flow;
    frame->pending_flow_depth = depth;
}

/* ============================================================================
 * Traps
 * ============================================================================ */

void frame_run_exit_traps(const trap_store_t *store, exec_frame_t *frame)
{
    Expects_not_null(store);
    Expects_not_null(frame);

    if (!trap_store_is_exit_set(store))
    {
        return;
    }

    const string_t *exit_action = trap_store_get_exit(store);
    if (!exit_action || string_length(exit_action) == 0)
    {
        return;
    }

    trap_store_run_exit_trap(store, frame);
}

/* ============================================================================
 * Background Jobs
 * ============================================================================ */

static bool job_store_update_status(job_store_t *jobs, bool wait_for_completion)
{
    Expects_not_null(jobs);

#ifdef POSIX_API
    int status;
    pid_t pid;
    bool any_completed = false;

    if (!wait_for_completion)
    {
        /* -1 and WNOHANG mean we return the PID of any completed child process or -1 
         * if
         * no child process has recently completed */
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        {
            // Determine if the job is done, or if it was terminated by a signal
            bool terminated = WIFSIGNALED(status);
            int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            if (terminated)
                job_store_set_state(jobs, pid, JOB_STATE_TERMINATED);
            else
                job_store_set_state(jobs, pid, JOB_STATE_DONE);
            job_store_set_exit_status(jobs, pid, exit_status);
            any_completed = true;
        }
    }
    else
    {
        while (1)
        {
            pid_t w = waitpid(-1, &status, 0);

            if (w == -1)
            {
                if (errno == ECHILD)
                {
                    // No more child processes
                    break;
                }
                // Some other error occurred
                break;
            }
            // Determine if the job is done, or if it was terminated by a signal
            bool terminated = WIFSIGNALED(status);
            int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            if (terminated)
                job_store_set_state(jobs, pid, JOB_STATE_TERMINATED);
            else
                job_store_set_state(jobs, pid, JOB_STATE_DONE);
            job_store_set_exit_status(jobs, pid, exit_status);
            any_completed = true;
        }
    }
    if (any_completed)
        job_store_remove_completed(jobs);
    return any_completed;
#elifdef UCRT_API
    bool any_completed = false;
    job_process_iterator_t iter = job_store_active_processes_begin(jobs);
    const DWORD one_hour_ms = 60 * 60 * 1000; // 1 hour in milliseconds

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
        else if (WaitForSingleObject((HANDLE)h, (DWORD)(wait_for_completion ? one_hour_ms : 0)) == WAIT_OBJECT_0)
        {
            DWORD exit_code = 0;
            if (GetExitCodeProcess((HANDLE)h, &exit_code))
            {
                // No precise way to distinguish between normal exit and termination by signal.
                // So we infer heuristically by exit code.
                if (exit_code == 1 || exit_code == 2 || exit_code > 0xC0000000) 
                    job_store_iter_set_state(&iter, JOB_TERMINATED, (int)exit_code);
                else
                    job_store_iter_set_state(&iter, JOB_DONE, (int)exit_code);
            }

            // Check if job is now complete
            job_state_t state = job_store_iter_get_job_state(&iter);
            if (state == JOB_DONE || state == JOB_TERMINATED)
                any_completed = true;
        }
        else
        {
            // Process is still active or an error occurred. We can check for errors if needed.
        }
    }
    if (any_completed)
        job_store_remove_completed(jobs);
    return any_completed;
#else
    (void)jobs;
    (void)wait_for_completion;
    return false;
#endif
}

bool frame_reap_background_jobs(exec_frame_t *frame, bool wait_for_completion)
{
    Expects_not_null(frame);
    Expects_not_null(frame->executor);
    Expects_not_null(frame->executor->jobs);

    return job_store_update_status(frame->executor->jobs, wait_for_completion);
}

bool frame_print_completed_background_jobs(exec_frame_t *frame)
{
    Expects_not_null(frame);
    Expects_not_null(frame->executor);
    Expects_not_null(frame->executor->jobs);
    return job_store_print_completed_jobs(frame->executor->jobs, stdout);
}

void frame_print_background_jobs(exec_frame_t *frame)
{
    Expects_not_null(frame);
    Expects_not_null(frame->executor);
    Expects_not_null(frame->executor->jobs);

    job_store_print_jobs(frame->executor->jobs, stdout);
}

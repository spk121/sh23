/**
 * frame.c - Public API implementation for miga_frame_t
 *
 * This file implements the public API defined in frame.h, providing a clean
 * interface to execution frames without exposing internal implementation details.
 *
 * Internal frame management (push, pop, exec_in_frame, etc.) is in exec_frame.c
 * and exec_frame.h.
 *
 * Whenever possible, frame.c functions should delegate to exec_frame.c for
 * logic and to avoid duplication.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef MIGA_POSIX_API
#define _POSIX_C_SOURCE 202405L
#endif
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif


#include "miga/frame.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef MIGA_POSIX_API
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef MIGA_UCRT_API
#if defined(_WIN64)
#define _AMD64_
#elif defined(_WIN32)
#define _X86_
#endif
#include <direct.h>
#include <io.h>
#include <process.h>
#include <search.h>
#endif


// FIXME: This module should not need internal headers.
// It should be delegating down to functions from exec_frame.h.
#include "alias_store.h"
#include "ast.h"
#include "miga/exec.h"
#include "exec_frame.h"
#include "exec_frame_expander.h"
#include "exec_frame_policy.h"
#include "exec_types_internal.h"
#include "miga/type_pub.h"
#include "func_store.h"
#include "gnode.h"
#include "lib.h"
#include "logging.h"
#include "lower.h"
#include "parser.h"
#include "positional_params.h"
#include "miga/strlist.h"
#include "miga/string_t.h"
#include "trap_store.h"
#include "variable_store.h"
#include "miga/xalloc.h"


/* ============================================================================
 * Error Handling
 * ============================================================================ */

bool frame_has_error(const miga_frame_t *frame)
{
    Expects_not_null(frame);
    Expects_not_null(frame->executor);

    const char *err = exec_get_error_cstr(frame->executor);
    return (err != NULL && err[0] != '\0');
}

const string_t *frame_get_error_message(const miga_frame_t *frame)
{
    Expects_not_null(frame);
    Expects_not_null(frame->executor);

    return exec_get_error(frame->executor);
}

void frame_clear_error(miga_frame_t *frame)
{
    Expects_not_null(frame);
    Expects_not_null(frame->executor);

    exec_clear_error(frame->executor);
}

void frame_set_error(miga_frame_t *frame, const string_t *error)
{
    Expects_not_null(frame);
    Expects_not_null(frame->executor);
    Expects_not_null(error);

    exec_set_error_printf(frame->executor, "%s", string_cstr(error));
}

void frame_set_error_printf(miga_frame_t *frame, const char *format, ...)
{
    Expects_not_null(frame);
    Expects_not_null(frame->executor);
    Expects_not_null(format);

    va_list args;
    va_start(args, format);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);

    va_end(args);

    exec_set_error_printf(frame->executor, "%s", buffer);
}

/* ============================================================================
 * Variable Access
 * ============================================================================ */

bool frame_has_variable(const miga_frame_t *frame, const string_t *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    const string_t *value = exec_frame_get_variable(frame, name);
    return (value != NULL);
}

bool frame_has_variable_cstr(const miga_frame_t *frame, const char *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    bool result = frame_has_variable(frame, name_str);
    string_destroy(&name_str);
    return result;
}

string_t *frame_get_variable_value(miga_frame_t *frame, const string_t *name)
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

string_t *frame_get_variable_cstr(miga_frame_t *frame, const char *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    string_t *result = frame_get_variable_value(frame, name_str);
    string_destroy(&name_str);
    return result;
}

bool frame_variable_is_exported(miga_frame_t *frame, const string_t *name)
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

bool frame_variable_is_exported_cstr(miga_frame_t *frame, const char *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    bool result = frame_variable_is_exported(frame, name_str);
    string_destroy(&name_str);
    return result;
}

bool frame_variable_is_readonly(miga_frame_t *frame, const string_t *name)
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

bool frame_variable_is_readonly_cstr(miga_frame_t *frame, const char *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    bool result = frame_variable_is_readonly(frame, name_str);
    string_destroy(&name_str);
    return result;
}

string_t *frame_get_ps1(miga_frame_t *frame)
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

string_t *frame_get_ps2(miga_frame_t *frame)
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

miga_var_status_t frame_set_variable(miga_frame_t *frame, const string_t *name,
                                     const string_t *value)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(value);

    variable_store_t *vars = frame->local_variables ? frame->local_variables : frame->variables;
    if (!vars)
    {
        return MIGA_VAR_STATUS_NOT_FOUND;
    }

    variable_view_t view;
    bool exists = variable_store_get_variable(vars, name, &view);

    if (exists && view.read_only)
    {
        if (string_compare(view.value, value) != 0)
        {
            return MIGA_VAR_STATUS_READ_ONLY;
        }
        return MIGA_VAR_STATUS_OK;
    }

    bool exported = exists ? view.exported : false;
    bool read_only = exists ? view.read_only : false;

    var_store_error_t internal_result = variable_store_add(vars, name, value, exported, read_only);

    /* Map internal error codes to public error codes */
    switch (internal_result)
    {
    case VAR_STORE_ERROR_NONE:
        return MIGA_VAR_STATUS_OK;
    case VAR_STORE_ERROR_READ_ONLY:
        return MIGA_VAR_STATUS_READ_ONLY;
    case VAR_STORE_ERROR_NOT_FOUND:
        return MIGA_VAR_STATUS_NOT_FOUND;
    default:
        return MIGA_VAR_STATUS_OK;
    }
}

miga_var_status_t frame_set_variable_cstr(miga_frame_t *frame, const char *name, const char *value)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(value);

    string_t *name_str = string_create_from_cstr(name);
    string_t *value_str = string_create_from_cstr(value);
    miga_var_status_t result = frame_set_variable(frame, name_str, value_str);
    string_destroy(&name_str);
    string_destroy(&value_str);
    return result;
}

miga_var_status_t frame_set_persistent_variable(miga_frame_t *frame, const string_t *name,
                                                const string_t *value)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(value);

    variable_store_t *vars = frame->saved_variables ? frame->saved_variables : frame->variables;
    if (!vars)
    {
        return MIGA_VAR_STATUS_NOT_FOUND;
    }

    variable_view_t view;
    bool exists = variable_store_get_variable(vars, name, &view);

    if (exists && view.read_only)
    {
        if (string_compare(view.value, value) != 0)
        {
            return MIGA_VAR_STATUS_READ_ONLY;
        }
        return MIGA_VAR_STATUS_OK;
    }

    bool exported = exists ? view.exported : false;
    bool read_only = exists ? view.read_only : false;

    var_store_error_t internal_result = variable_store_add(vars, name, value, exported, read_only);

    switch (internal_result)
    {
    case VAR_STORE_ERROR_NONE:
        return MIGA_VAR_STATUS_OK;
    case VAR_STORE_ERROR_READ_ONLY:
        return MIGA_VAR_STATUS_READ_ONLY;
    case VAR_STORE_ERROR_NOT_FOUND:
        return MIGA_VAR_STATUS_NOT_FOUND;
    default:
        return MIGA_VAR_STATUS_OK;
    }
}

miga_var_status_t frame_set_persistent_variable_cstr(miga_frame_t *frame, const char *name,
                                                     const char *value)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(value);

    string_t *name_str = string_create_from_cstr(name);
    string_t *value_str = string_create_from_cstr(value);
    miga_var_status_t result = frame_set_persistent_variable(frame, name_str, value_str);
    string_destroy(&name_str);
    string_destroy(&value_str);
    return result;
}

miga_var_status_t frame_set_variable_exported(miga_frame_t *frame, const string_t *name,
                                              bool exported)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(frame->variables);

    var_store_error_t internal_result =
        variable_store_set_exported(frame->variables, name, exported);
    switch (internal_result)
    {
    case VAR_STORE_ERROR_NONE:
        return MIGA_VAR_STATUS_OK;
    case VAR_STORE_ERROR_NOT_FOUND:
        return MIGA_VAR_STATUS_NOT_FOUND;
    default:
        return MIGA_VAR_STATUS_OK;
    }
}

miga_export_status_t frame_export_variable(miga_frame_t *frame, const string_t *name,
                                            const string_t *value)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(frame->variables);

    variable_store_t *vars = frame->variables;

    /* Validate variable name */
    if (string_empty(name))
    {
        return MIGA_EXPORT_STATUS_INVALID_NAME;
    }

    /* Check for invalid characters in variable name */
    const char *name_cstr = string_cstr(name);
    for (const char *p = name_cstr; *p; p++)
    {
        /* POSIX: name must start with letter or underscore, contain only
         * alphanumeric and underscore */
        if (p == name_cstr)
        {
            if (!(*p == '_' || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')))
            {
                return MIGA_EXPORT_STATUS_INVALID_NAME;
            }
        }
        else
        {
            if (!(*p == '_' || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                  (*p >= '0' && *p <= '9')))
            {
                return MIGA_EXPORT_STATUS_INVALID_NAME;
            }
        }
    }

    /* Check if variable exists and is readonly */
    variable_view_t view;
    bool exists = variable_store_get_variable(vars, name, &view);

    if (exists && view.read_only)
    {
        /* Check if we're trying to change the value */
        if (value && !string_eq(view.value, value))
        {
            return MIGA_EXPORT_STATUS_READONLY;
        }
    }

    /* Set or update the variable value if provided */
    if (value)
    {
        miga_var_status_t set_result = frame_set_variable(frame, name, value);
        if (set_result != MIGA_VAR_STATUS_OK)
        {
            if (set_result == MIGA_VAR_STATUS_READ_ONLY)
            {
                return MIGA_EXPORT_STATUS_READONLY;
            }
            return MIGA_EXPORT_STATUS_SYSTEM_ERROR;
        }
    }
    else if (!exists)
    {
        /* If value is NULL and variable doesn't exist, create with empty value */
        string_t *empty = string_create();
        miga_var_status_t set_result = frame_set_variable(frame, name, empty);
        string_destroy(&empty);
        if (set_result != MIGA_VAR_STATUS_OK)
        {
            return MIGA_EXPORT_STATUS_SYSTEM_ERROR;
        }
    }

    /* Mark variable as exported */
    var_store_error_t export_result = variable_store_set_exported(vars, name, true);
    if (export_result != VAR_STORE_ERROR_NONE)
    {
        return MIGA_EXPORT_STATUS_SYSTEM_ERROR;
    }

    /* Export to system environment if supported */
#if defined(MIGA_POSIX_API) || defined(MIGA_UCRT_API)
    /* Get the current value (which might be the value we just set, or the
     * existing value) */
    variable_view_t current_view;
    if (!variable_store_get_variable(vars, name, &current_view))
    {
        return MIGA_EXPORT_STATUS_SYSTEM_ERROR;
    }

    const char *name_str = string_cstr(name);
    const char *value_str = string_cstr(current_view.value);

#ifdef MIGA_POSIX_API
    if (setenv(name_str, value_str, 1) != 0)
    {
        return MIGA_EXPORT_STATUS_SYSTEM_ERROR;
    }
#elifdef MIGA_UCRT_API
    if (_putenv_s(name_str, value_str) != 0)
    {
        return MIGA_EXPORT_STATUS_SYSTEM_ERROR;
    }
#endif

#else
    /* Platform doesn't support environment export */
    return MIGA_EXPORT_STATUS_NOT_SUPPORTED;
#endif

    return MIGA_EXPORT_STATUS_SUCCESS;
}

miga_var_status_t frame_set_variable_readonly(miga_frame_t *frame, const string_t *name,
                                              bool readonly)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(frame->variables);

    var_store_error_t internal_result =
        variable_store_set_read_only(frame->variables, name, readonly);
    switch (internal_result)
    {
    case VAR_STORE_ERROR_NONE:
        return MIGA_VAR_STATUS_OK;
    case VAR_STORE_ERROR_NOT_FOUND:
        return MIGA_VAR_STATUS_NOT_FOUND;
    default:
        return MIGA_VAR_STATUS_OK;
    }
}

miga_var_status_t frame_unset_variable(miga_frame_t *frame, const string_t *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    variable_store_t *vars = frame->local_variables ? frame->local_variables : frame->variables;
    if (!vars)
    {
        return MIGA_VAR_STATUS_NOT_FOUND;
    }

    variable_view_t view;
    if (!variable_store_get_variable(vars, name, &view))
    {
        return MIGA_VAR_STATUS_NOT_FOUND;
    }

    if (view.read_only)
    {
        return MIGA_VAR_STATUS_READ_ONLY;
    }

    variable_store_remove(vars, name);

#if defined(MIGA_POSIX_API) || defined(MIGA_UCRT_API)
    if (view.exported)
    {
#ifdef MIGA_POSIX_API
        unsetenv(string_cstr(name));
#elifdef MIGA_UCRT_API
        _putenv_s(string_cstr(name), "");
#endif
    }
#endif

    return MIGA_VAR_STATUS_OK;
}

miga_var_status_t frame_unset_variable_cstr(miga_frame_t *frame, const char *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    miga_var_status_t result = frame_unset_variable(frame, name_str);
    string_destroy(&name_str);
    return result;
}

/* -- Variable printing helpers --------------------------------------------- */

typedef struct
{
    FILE *output;
} print_var_context_t;

static void print_exported_var_callback(const string_t *name, const string_t *value, bool exported,
                                        bool read_only, void *user_data)
{
    (void)read_only;
    print_var_context_t *ctx = user_data;

    if (exported)
    {
        fprintf(ctx->output, "export %s=%s\n", string_cstr(name), string_cstr(value));
    }
}

void frame_print_exported_variables_in_export_format(miga_frame_t *frame, FILE *output)
{
    Expects_not_null(frame);
    Expects_not_null(output);

    if (frame->variables)
    {
        print_var_context_t ctx = {.output = output};
        variable_store_for_each(frame->variables, print_exported_var_callback, &ctx);
    }
}

static void print_readonly_var_callback(const string_t *name, const string_t *value, bool exported,
                                        bool read_only, void *user_data)
{
    (void)exported;
    print_var_context_t *ctx = user_data;

    if (read_only)
    {
        fprintf(ctx->output, "readonly %s=%s\n", string_cstr(name), string_cstr(value));
    }
}

void frame_print_readonly_variables(miga_frame_t *frame, FILE *output)
{
    Expects_not_null(frame);
    Expects_not_null(output);

    if (frame->variables)
    {
        print_var_context_t ctx = {.output = output};
        variable_store_for_each(frame->variables, print_readonly_var_callback, &ctx);
    }
}

/* Helper structure for sorting variables */
typedef struct
{
    const string_t *key;
    const string_t *value;
    bool exported;
    bool read_only;
} frame_var_entry_t;

/* Collector context for variable_store_for_each */
typedef struct
{
    frame_var_entry_t *vars;
    int count;
    int capacity;
} frame_collect_ctx_t;

static void frame_collect_var(const string_t *name, const string_t *value, bool exported,
                              bool read_only, void *user_data)
{
    frame_collect_ctx_t *ctx = user_data;

    if (ctx->count >= ctx->capacity)
    {
        ctx->capacity = ctx->capacity ? ctx->capacity * 2 : 32;
        ctx->vars = xrealloc(ctx->vars, ctx->capacity * sizeof(frame_var_entry_t));
    }

    ctx->vars[ctx->count].key = name;
    ctx->vars[ctx->count].value = value;
    ctx->vars[ctx->count].exported = exported;
    ctx->vars[ctx->count].read_only = read_only;
    ctx->count++;
}

/* Comparison function for sorting variables by name using locale collation */
static int frame_compare_vars(const void *a, const void *b)
{
    const frame_var_entry_t *va = (const frame_var_entry_t *)a;
    const frame_var_entry_t *vb = (const frame_var_entry_t *)b;
    return lib_strcoll(string_cstr(va->key), string_cstr(vb->key));
}

typedef struct
{
    bool reusable;
    FILE *output;
} print_all_var_context_t;

static void print_var_callback(const string_t *name, const string_t *value, bool exported,
                               bool read_only, void *user_data)
{
    print_all_var_context_t *ctx = user_data;

    if (ctx->reusable)
    {
        if (exported && read_only)
        {
            fprintf(ctx->output, "export -r %s=%s\n", string_cstr(name), string_cstr(value));
        }
        else if (exported)
        {
            fprintf(ctx->output, "export %s=%s\n", string_cstr(name), string_cstr(value));
        }
        else if (read_only)
        {
            fprintf(ctx->output, "readonly %s=%s\n", string_cstr(name), string_cstr(value));
        }
        else
        {
            fprintf(ctx->output, "%s=%s\n", string_cstr(name), string_cstr(value));
        }
    }
    else
    {
        fprintf(ctx->output, "%s=%s", string_cstr(name), string_cstr(value));
        if (exported || read_only)
        {
            fprintf(ctx->output, " [");
            if (exported)
                fprintf(ctx->output, "exported");
            if (exported && read_only)
                fprintf(ctx->output, ", ");
            if (read_only)
                fprintf(ctx->output, "readonly");
            fprintf(ctx->output, "]");
        }
        fprintf(ctx->output, "\n");
    }
}

void frame_print_variables(miga_frame_t *frame, bool reusable_format, FILE *output)
{
    Expects_not_null(frame);
    Expects_not_null(output);

    if (!frame->variables)
    {
        return;
    }

    if (reusable_format)
    {
        /* Collect all variables for sorting */
        frame_collect_ctx_t ctx = {NULL, 0, 0};
        variable_store_for_each(frame->variables, frame_collect_var, &ctx);

        if (ctx.count == 0)
            return;

        /* Sort by name using locale collation */
        qsort(ctx.vars, ctx.count, sizeof(frame_var_entry_t), frame_compare_vars);

        /* Print each variable using lib_quote for proper quoting */
        for (int i = 0; i < ctx.count; i++)
        {
            string_t *quoted = lib_quote(ctx.vars[i].key, ctx.vars[i].value);
            fprintf(output, "%s\n", string_cstr(quoted));
            string_destroy(&quoted);
        }

        xfree(ctx.vars);
    }
    else
    {
        /* Non-reusable format: print without sorting */
        print_all_var_context_t pctx = {.reusable = false, .output = output};
        variable_store_for_each(frame->variables, print_var_callback, &pctx);
    }
}

/* -- IFS convenience ------------------------------------------------------- */

string_t *frame_get_ifs(miga_frame_t *frame)
{
    Expects_not_null(frame);

    string_t *ifs_name = string_create_from_cstr("IFS");
    bool has_ifs = frame_has_variable(frame, ifs_name);

    if (has_ifs)
    {
        string_t *value = frame_get_variable_value(frame, ifs_name);
        string_destroy(&ifs_name);
        return value;
    }

    string_destroy(&ifs_name);
    /* POSIX default: space, tab, newline */
    return string_create_from_cstr(" \t\n");
}

char *frame_get_ifs_cstr(miga_frame_t *frame)
{
    string_t *ifs = frame_get_ifs(frame);
    char *result = string_release(&ifs);
    return result;
}

/* -- Working directory ----------------------------------------------------- */

bool frame_change_directory(miga_frame_t *frame, const string_t *path)
{
    Expects_not_null(frame);
    Expects_not_null(path);

    return frame_change_directory_cstr(frame, string_cstr(path));
}

// This stub is just to avoid a warning.
static int chdir_stub(const char *path)
{
    (void)path;
    return -1;
}

bool frame_change_directory_cstr(miga_frame_t *frame, const char *path)
{
    Expects_not_null(frame);
    Expects_not_null(path);

#if defined(MIGA_POSIX_API)
    int (*const chdir_func)(const char *) = chdir;
#elif defined(MIGA_UCRT_API)
    int (*const chdir_func)(const char *) = _chdir;
#else
    (void)frame;
    (void)path;
    int (*const chdir_func)(const char *) = chdir_stub;
    return false;
#endif

    string_t *old_pwd = frame_get_variable_cstr(frame, "PWD");

    if (chdir_func(path) != 0)
    {
        string_destroy(&old_pwd);
        return false;
    }

    string_t *new_pwd = lib_getcwd();

    // OLDPWD ← previous PWD (only if it was non-empty)
    if (old_pwd && string_length(old_pwd) > 0)
    {
        frame_set_variable_cstr(frame, "OLDPWD", string_cstr(old_pwd));
    }
    string_destroy(&old_pwd);

    // PWD ← new absolute path
    frame_set_variable_cstr(frame, "PWD", string_cstr(new_pwd));
    string_destroy(&new_pwd);

    return true;
}

/* ============================================================================
 * Word and String Expansion
 * ============================================================================ */

static expand_flags_t convert_frame_expand_flags(miga_expand_flags_t frame_flags)
{
    expand_flags_t flags = EXPAND_NONE;
    if (frame_flags & MIGA_EXPAND_TILDE)
        flags |= EXPAND_TILDE;
    if (frame_flags & MIGA_EXPAND_PARAMETER)
        flags |= EXPAND_PARAMETER;
    if (frame_flags & MIGA_EXPAND_COMMAND_SUBST)
        flags |= EXPAND_COMMAND_SUBST;
    if (frame_flags & MIGA_EXPAND_ARITHMETIC)
        flags |= EXPAND_ARITHMETIC;
    if (frame_flags & MIGA_EXPAND_FIELD_SPLIT)
        flags |= EXPAND_FIELD_SPLIT;
    if (frame_flags & MIGA_EXPAND_PATHNAME)
        flags |= EXPAND_PATHNAME;
    return flags;
}

string_t *frame_expand_string(miga_frame_t *frame, const string_t *text, miga_expand_flags_t flags)
{
    Expects_not_null(frame);
    Expects_not_null(text);

    return expand_string(frame, text, convert_frame_expand_flags(flags));
}

/* ============================================================================
 * Positional Parameters
 * ============================================================================ */

bool frame_has_positional_params(const miga_frame_t *frame)
{
    Expects_not_null(frame);

    return (frame->positional_params != NULL);
}

int frame_count_positional_params(const miga_frame_t *frame)
{
    Expects_not_null(frame);

    if (!frame->positional_params)
    {
        return 0;
    }

    return positional_params_count(frame->positional_params);
}

void frame_shift_positional_params(miga_frame_t *frame, int shift_count)
{
    Expects_not_null(frame);

    if (!frame->positional_params)
    {
        return;
    }

    positional_params_shift(frame->positional_params, shift_count);
}

void frame_replace_positional_params(miga_frame_t *frame, const strlist_t *new_params)
{
    Expects_not_null(frame);

    if (!frame->positional_params)
    {
        return;
    }

    int count = new_params ? strlist_size(new_params) : 0;
    string_t **params = NULL;

    if (count > 0)
    {
        params = xcalloc((size_t)count, sizeof(string_t *));
        for (int i = 0; i < count; i++)
        {
            params[i] = string_create_from(strlist_at(new_params, i));
        }
    }

    positional_params_replace(frame->positional_params, params, count);
}

void frame_set_arg0(miga_frame_t *frame, const string_t *new_arg0)
{
    Expects_not_null(frame);

    if (!frame->positional_params)
    {
        return;
    }

    positional_params_set_arg0(frame->positional_params, new_arg0);
}

void frame_set_arg0_cstr(miga_frame_t *frame, const char *new_arg0)
{
    Expects_not_null(frame);
    if (!frame->positional_params)
    {
        return;
    }
    string_t *new_arg0_str = string_create_from_cstr(new_arg0);
    positional_params_set_arg0(frame->positional_params, new_arg0_str);
    string_destroy(&new_arg0_str);
}

string_t *frame_get_positional_param(const miga_frame_t *frame, int index)
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

strlist_t *frame_get_all_positional_params(const miga_frame_t *frame)
{
    Expects_not_null(frame);

    if (!frame->positional_params)
    {
        return strlist_create();
    }

    int count = positional_params_count(frame->positional_params);
    strlist_t *result = strlist_create();

    for (int i = 0; i < count; i++)
    {
        const string_t *param = positional_params_get(frame->positional_params, i);
        if (param)
        {
            strlist_push_back(result, param);
        }
    }

    return result;
}

/* ============================================================================
 * Named Options
 * ============================================================================ */

bool frame_has_named_option(const miga_frame_t *frame, const string_t *option_name)
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

bool frame_has_named_option_cstr(const miga_frame_t *frame, const char *option_name)
{
    Expects_not_null(frame);
    Expects_not_null(option_name);

    string_t *name_str = string_create_from_cstr(option_name);
    bool result = frame_has_named_option(frame, name_str);
    string_destroy(&name_str);
    return result;
}

bool frame_get_named_option(const miga_frame_t *frame, const string_t *option_name)
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

bool frame_get_named_option_cstr(const miga_frame_t *frame, const char *option_name)
{
    Expects_not_null(frame);
    Expects_not_null(option_name);

    string_t *name_str = string_create_from_cstr(option_name);
    bool result = frame_get_named_option(frame, name_str);
    string_destroy(&name_str);
    return result;
}

bool frame_set_named_option(miga_frame_t *frame, const string_t *option_name, bool value,
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

bool frame_set_named_option_cstr(miga_frame_t *frame, const char *option_name, bool value,
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
 * Shell Functions
 * ============================================================================ */

bool frame_has_function(const miga_frame_t *frame, const string_t *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    if (!frame->functions)
    {
        return false;
    }

    return func_store_has_name(frame->functions, name);
}

miga_func_status_t frame_get_function(miga_frame_t *frame, const string_t *name,
                                      string_t **out_body)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(out_body);

    if (!frame->functions)
    {
        *out_body = NULL;
        return MIGA_FUNC_STATUS_NOT_FOUND;
    }

    const ast_node_t *func_def = func_store_get_def(frame->functions, name);
    if (!func_def)
    {
        *out_body = NULL;
        return MIGA_FUNC_STATUS_NOT_FOUND;
    }

    *out_body = ast_node_to_string(func_def);
    return MIGA_FUNC_STATUS_OK;
}

miga_func_status_t frame_get_function_cstr(miga_frame_t *frame, const char *name, char **out_body)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(out_body);

    string_t *name_str = string_create_from_cstr(name);
    string_t *body = NULL;
    miga_func_status_t result = frame_get_function(frame, name_str, &body);
    string_destroy(&name_str);

    if (result == MIGA_FUNC_STATUS_OK && body)
    {
        *out_body = string_release(&body);
    }
    else
    {
        *out_body = NULL;
        if (body)
            string_destroy(&body);
    }

    return result;
}

miga_func_status_t frame_set_function(miga_frame_t *frame, const string_t *name,
                                      const string_t *value)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(value);
    Expects_not_null(frame->functions);

    /* Parse the string into an AST for storage */
    gnode_t *root = NULL;
    parse_status_t status = parser_string_to_gnodes(string_cstr(value), &root);
    if (status != PARSE_OK)
    {
        g_node_destroy(&root);
        return MIGA_FUNC_STATUS_PARSE_FAILURE;
    }
    ast_t *node = ast_lower(root);

    func_store_error_t internal_result = func_store_add(frame->functions, name, node);

    switch (internal_result)
    {
    case FUNC_STORE_ERROR_NONE:
        return MIGA_FUNC_STATUS_OK;
    case FUNC_STORE_ERROR_NOT_FOUND:
        return MIGA_FUNC_STATUS_NOT_FOUND;
    case FUNC_STORE_ERROR_READONLY:
        return MIGA_FUNC_STATUS_READONLY;
    default:
        return MIGA_FUNC_STATUS_SYSTEM_ERROR;
    }
}

miga_func_status_t frame_set_function_cstr(miga_frame_t *frame, const char *name, const char *value)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(value);

    string_t *name_str = string_create_from_cstr(name);
    string_t *value_str = string_create_from_cstr(value);
    miga_func_status_t result = frame_set_function(frame, name_str, value_str);
    string_destroy(&name_str);
    string_destroy(&value_str);
    return result;
}

miga_func_status_t frame_unset_function(miga_frame_t *frame, const string_t *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    if (!frame->functions)
    {
        return MIGA_FUNC_STATUS_NOT_FOUND;
    }

    func_store_error_t internal_result = func_store_remove(frame->functions, name);

    switch (internal_result)
    {
    case FUNC_STORE_ERROR_NONE:
        return MIGA_FUNC_STATUS_OK;
    case FUNC_STORE_ERROR_NOT_FOUND:
        return MIGA_FUNC_STATUS_NOT_FOUND;
    case FUNC_STORE_ERROR_READONLY:
        return MIGA_FUNC_STATUS_READONLY;
    default:
        return MIGA_FUNC_STATUS_SYSTEM_ERROR;
    }
}

miga_func_status_t frame_unset_function_cstr(miga_frame_t *frame, const char *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    string_t *name_str = string_create_from_cstr(name);
    miga_func_status_t result = frame_unset_function(frame, name_str);
    string_destroy(&name_str);
    return result;
}

miga_exec_status_t frame_call_function(miga_frame_t *frame, const string_t *name,
                                  const strlist_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    if (!frame->functions)
    {
        return MIGA_EXEC_STATUS_ERROR;
    }

    const ast_node_t *func_def = func_store_get_def(frame->functions, name);
    if (!func_def)
    {
        return MIGA_EXEC_STATUS_ERROR;
    }

    /* Execute the function body in a new function frame */
    exec_frame_execute_result_t result =
        exec_frame_execute_function_body(frame, func_def, (strlist_t *)args, NULL);

    if (result.status == MIGA_EXEC_STATUS_ERROR)
        return MIGA_EXEC_STATUS_ERROR;

    return MIGA_EXEC_STATUS_OK;
}

/* ============================================================================
 * Exit Status
 * ============================================================================ */

int frame_get_last_exit_status(const miga_frame_t *frame)
{
    Expects_not_null(frame);

    return frame->last_exit_status;
}

void frame_set_last_exit_status(miga_frame_t *frame, int status)
{
    Expects_not_null(frame);

    frame->last_exit_status = status;
}

/* ============================================================================
 * Control Flow
 * ============================================================================ */

void frame_set_pending_control_flow(miga_frame_t *frame, miga_frame_flow_t flow, int depth)
{
    Expects_not_null(frame);

    if (flow == MIGA_FRAME_FLOW_TOP)
    {
        /* Unwind all frames to top level � request exit on the executor */
        exec_request_exit(frame->executor, frame->last_exit_status);
    }
    frame->pending_control_flow = flow;
    frame->pending_flow_depth = depth;
}

miga_frame_t *frame_find_return_target(miga_frame_t *frame)
{
    /* Just delegate to the internal API function */
    return exec_frame_find_return_target(frame);
}

/* ============================================================================
 * Traps
 * ============================================================================ */

/* Helper: Get trap store from frame */
static trap_store_t *get_trap_store(miga_frame_t *frame)
{
    if (frame->traps)
        return frame->traps;
    if (frame->executor && frame->executor->traps)
        return frame->executor->traps;
    return NULL;
}

void frame_run_exit_traps(miga_frame_t *frame)
{
    Expects_not_null(frame);

    trap_store_t *traps = get_trap_store(frame);
    if (!traps)
        return;

    if (!trap_store_is_exit_set(traps))
    {
        return;
    }

    const string_t *exit_action = trap_store_get_exit(traps);
    if (!exit_action || string_length(exit_action) == 0)
    {
        return;
    }

    trap_store_run_exit_trap(traps, (void *)frame);
}

/* Callback adapter for frame_for_each_set_trap */
typedef struct
{
    frame_trap_callback_t callback;
    void *context;
} frame_trap_callback_adapter_t;

static void trap_callback_adapter(int signal_number, const trap_action_t *trap, void *context)
{
    frame_trap_callback_adapter_t *adapter = (frame_trap_callback_adapter_t *)context;
    adapter->callback(signal_number, trap->action, trap->is_ignored, adapter->context);
}

void frame_for_each_set_trap(miga_frame_t *frame, frame_trap_callback_t callback, void *context)
{
    Expects_not_null(frame);
    Expects_not_null(callback);

    trap_store_t *traps = get_trap_store(frame);
    if (!traps)
        return;

    frame_trap_callback_adapter_t adapter = {.callback = callback, .context = context};
    trap_store_for_each_set_trap(traps, trap_callback_adapter, &adapter);
}

const string_t *frame_get_trap(miga_frame_t *frame, int signal_number, bool *out_is_ignored)
{
    Expects_not_null(frame);

    trap_store_t *traps = get_trap_store(frame);
    if (!traps)
    {
        if (out_is_ignored)
            *out_is_ignored = false;
        return NULL;
    }

    const trap_action_t *trap = trap_store_get(traps, signal_number);
    if (!trap)
    {
        if (out_is_ignored)
            *out_is_ignored = false;
        return NULL;
    }

    if (out_is_ignored)
        *out_is_ignored = trap->is_ignored;

    return trap->action;
}

const string_t *frame_get_exit_trap(miga_frame_t *frame)
{
    Expects_not_null(frame);

    trap_store_t *traps = get_trap_store(frame);
    if (!traps)
        return NULL;

    return trap_store_get_exit(traps);
}

bool frame_set_trap(miga_frame_t *frame, int signal_number, const string_t *action, bool is_ignored,
                    bool is_reset)
{
    Expects_not_null(frame);

    trap_store_t *traps = get_trap_store(frame);
    if (!traps)
        return false;

    return trap_store_set(traps, signal_number, (string_t *)action, is_ignored, is_reset);
}

bool frame_set_exit_trap(miga_frame_t *frame, const string_t *action, bool is_ignored,
                         bool is_reset)
{
    Expects_not_null(frame);

    trap_store_t *traps = get_trap_store(frame);
    if (!traps)
        return false;

    return trap_store_set_exit(traps, (string_t *)action, is_ignored, is_reset);
}

int frame_trap_name_to_number(const char *name)
{
    if (!name)
        return -1;

    return trap_signal_name_to_number(name);
}

const char *frame_trap_number_to_name(int signal_number)
{
    return trap_signal_number_to_name(signal_number);
}

bool frame_trap_name_is_unsupported(const char *name)
{
    if (!name)
        return false;

    return trap_signal_name_is_unsupported(name);
}

/* ============================================================================
 * Aliases
 * ============================================================================ */

/* Helper: Get alias store from frame */
static alias_store_t *get_alias_store(miga_frame_t *frame)
{
    if (frame->aliases)
        return frame->aliases;
    if (frame->executor && frame->executor->aliases)
        return frame->executor->aliases;
    return NULL;
}

/* Helper: Get alias store from frame (const version) */
static const alias_store_t *get_alias_store_const(const miga_frame_t *frame)
{
    if (frame->aliases)
        return frame->aliases;
    if (frame->executor && frame->executor->aliases)
        return frame->executor->aliases;
    return NULL;
}

bool frame_has_alias(const miga_frame_t *frame, const string_t *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    const alias_store_t *aliases = get_alias_store_const(frame);
    if (!aliases)
        return false;

    return alias_store_has_name(aliases, name);
}

bool frame_has_alias_cstr(const miga_frame_t *frame, const char *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    const alias_store_t *aliases = get_alias_store_const(frame);
    if (!aliases)
        return false;

    return alias_store_has_name_cstr(aliases, name);
}

const string_t *frame_get_alias(const miga_frame_t *frame, const string_t *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    const alias_store_t *aliases = get_alias_store_const(frame);
    if (!aliases)
        return NULL;

    return alias_store_get_value(aliases, name);
}

const char *frame_get_alias_cstr(const miga_frame_t *frame, const char *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    const alias_store_t *aliases = get_alias_store_const(frame);
    if (!aliases)
        return NULL;

    return alias_store_get_value_cstr(aliases, name);
}

bool frame_set_alias(miga_frame_t *frame, const string_t *name, const string_t *value)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(value);

    alias_store_t *aliases = get_alias_store(frame);
    if (!aliases)
        return false;

    alias_store_add(aliases, name, value);
    return true;
}

bool frame_set_alias_cstr(miga_frame_t *frame, const char *name, const char *value)
{
    Expects_not_null(frame);
    Expects_not_null(name);
    Expects_not_null(value);

    alias_store_t *aliases = get_alias_store(frame);
    if (!aliases)
        return false;

    alias_store_add_cstr(aliases, name, value);
    return true;
}

bool frame_remove_alias(miga_frame_t *frame, const string_t *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    alias_store_t *aliases = get_alias_store(frame);
    if (!aliases)
        return false;

    return alias_store_remove(aliases, name);
}

bool frame_remove_alias_cstr(miga_frame_t *frame, const char *name)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    alias_store_t *aliases = get_alias_store(frame);
    if (!aliases)
        return false;

    return alias_store_remove_cstr(aliases, name);
}

int frame_alias_count(const miga_frame_t *frame)
{
    Expects_not_null(frame);

    const alias_store_t *aliases = get_alias_store_const(frame);
    if (!aliases)
        return 0;

    return alias_store_size(aliases);
}

void frame_for_each_alias(const miga_frame_t *frame, frame_alias_callback_t callback, void *context)
{
    Expects_not_null(frame);
    Expects_not_null(callback);

    const alias_store_t *aliases = get_alias_store_const(frame);
    if (!aliases)
        return;

    /* The alias_store_foreach callback signature matches frame_alias_callback_t */
    alias_store_foreach(aliases, (alias_store_foreach_fn)callback, context);
}

void frame_clear_all_aliases(miga_frame_t *frame)
{
    Expects_not_null(frame);

    alias_store_t *aliases = get_alias_store(frame);
    if (!aliases)
        return;

    alias_store_clear(aliases);
}

bool frame_alias_name_is_valid(const char *name)
{
    if (!name)
        return false;

    return alias_name_is_valid(name);
}

/* ============================================================================
 * Frame-Level Command Execution
 * ============================================================================ */

 /**
 * Execute a complete command string.
 *
 * This is a simplified wrapper around exec_frame_string_core() for cases where the
 * input string is expected to be a complete, self-contained command that does
 * not require continuation.  Useful for trap handlers, eval, and any case
 * where you have a complete command as a string.
 *
 * A terminal newline is appended if not already present.  Any result other
 * than MIGA_EXEC_STATUS_OK (including MIGA_EXEC_STATUS_INCOMPLETE, MIGA_EXEC_STATUS_EMPTY, MIGA_EXEC_STATUS_ERROR) is
 * promoted to MIGA_EXEC_STATUS_ERROR in the returned status.
 *
 * @param frame   The execution frame.
 * @param command The complete command string to execute.
 * @return miga_exec_result_t with .status == MIGA_EXEC_STATUS_OK or MIGA_EXEC_STATUS_ERROR.
 */
static miga_exec_result_t execute_command_string(miga_frame_t *frame, const char *command)
{
    Expects_not_null(frame);
    Expects_not_null(frame->executor);

    miga_exec_result_t result = {.status = MIGA_EXEC_STATUS_OK, .exit_code = 0};

    /* Empty or NULL command is success */
    if (!command || !*command)
        return result;

    miga_exec_t *executor = frame->executor;

    /* Create a temporary parse session */
    parse_session_t *session = exec_create_parse_session(executor);
    if (!session)
    {
        frame_set_error_printf(frame, "Failed to create parse session for command string");
        result.status = MIGA_EXEC_STATUS_ERROR;
        result.exit_code = EXEC_EXIT_FAILURE;
        return result;
    }

    /* Ensure command ends with newline for proper parsing */
    string_t *cmd_with_newline = string_create_from_cstr(command);
    int len = string_length(cmd_with_newline);
    if (len == 0 || string_at(cmd_with_newline, len - 1) != '\n')
    {
        string_append_char(cmd_with_newline, '\n');
    }

    /* Execute the command string */
    miga_exec_status_t status = exec_frame_string_core(frame, string_cstr(cmd_with_newline), session);

    string_destroy(&cmd_with_newline);
    parse_session_destroy(&session);

    /* Map result: only MIGA_EXEC_STATUS_OK passes through; everything else is an error */
    if (status == MIGA_EXEC_STATUS_OK)
    {
        result.status = MIGA_EXEC_STATUS_OK;
        result.exit_code = frame->last_exit_status;
    }
    else
    {
        result.status = MIGA_EXEC_STATUS_ERROR;
        result.exit_code = frame->last_exit_status ? frame->last_exit_status : EXEC_EXIT_FAILURE;
    }

    return result;
}


miga_exec_status_t frame_execute_string(miga_frame_t *frame, const string_t *command)
{
    Expects_not_null(frame);

    if (!command || string_empty(command))
        return MIGA_EXEC_STATUS_OK;

    return frame_execute_string_cstr(frame, string_cstr(command));
}

miga_exec_status_t frame_execute_string_cstr(miga_frame_t *frame, const char *command)
{
    Expects_not_null(frame);

    if (!command || !*command)
        return MIGA_EXEC_STATUS_OK;

    miga_exec_result_t result = execute_command_string(frame, command);

    switch(result.status)
    {
        case MIGA_EXEC_STATUS_OK:
        case MIGA_EXEC_STATUS_EMPTY:
        case MIGA_EXEC_STATUS_EXIT:
            return MIGA_EXEC_STATUS_OK;
        case MIGA_EXEC_STATUS_ERROR:
        case MIGA_EXEC_STATUS_NOT_IMPL:
        case MIGA_EXEC_STATUS_INCOMPLETE:
        default:
            return MIGA_EXEC_STATUS_ERROR;
    }
}

miga_exec_status_t frame_execute_eval_string_cstr(miga_frame_t *frame, const char *command)
{
    Expects_not_null(frame);

    if (!command || !*command)
        return MIGA_EXEC_STATUS_OK;

    /* Push an EXEC_FRAME_EVAL frame so that control flow (return, break,
     * continue) passes through to enclosing contexts correctly. */
    miga_frame_t *eval_frame = exec_frame_push(frame, EXEC_FRAME_EVAL, frame->executor, NULL);

    miga_exec_result_t result = execute_command_string(eval_frame, command);

    exec_frame_pop(&eval_frame);

    /* Update caller frame's exit status */
    frame->last_exit_status = result.exit_code;

    if (result.status == MIGA_EXEC_STATUS_ERROR)
        return MIGA_EXEC_STATUS_ERROR;

    return MIGA_EXEC_STATUS_OK;
}

miga_exec_status_t frame_execute_string_as_eval(miga_frame_t *frame, const string_t *command)
{
    Expects_not_null(frame);

    if (!command || string_empty(command))
        return MIGA_EXEC_STATUS_OK;

    return frame_execute_eval_string_cstr(frame, string_cstr(command));
}

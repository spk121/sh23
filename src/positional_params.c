/**
 * @file positional_params.c
 * @brief Implementation of positional parameters management
 */

#include "positional_params.h"
#include "xalloc.h"
#include "string_t.h"
#include "string_list.h"
#include "logging.h"
#include <string.h>
#include <assert.h>

// ============================================================================
// Lifecycle Management
// ============================================================================

positional_params_t *positional_params_create(void)
{
    positional_params_t *p = xcalloc(1, sizeof(positional_params_t));
    p->params = NULL;
    p->count = 0;
    p->max_params = POSITIONAL_PARAMS_MAX;
    return p;
}

positional_params_t *positional_params_create_from_array(const string_t *arg0, int count, const string_t **params)
{
    Expects(count >= 0);
    if (count > POSITIONAL_PARAMS_MAX)
        return NULL;

    if (count > 0)
        Expects_not_null(params);

    positional_params_t *p = xcalloc(1, sizeof(positional_params_t));
    p->count = count;
    p->arg0 = string_create_from(arg0);
    p->params = xcalloc((size_t)count + 1, sizeof(string_t *));
    for (int i = 0; i < count; i++)
        p->params[i] = string_create_from(params[i]);
    p->params[count] = NULL;
    p->max_params = POSITIONAL_PARAMS_MAX;
    return p;
}

positional_params_t* positional_params_create_from_string_list(const string_t* arg0, const string_list_t* params)
{
    Expects_not_null(params);
    int count = string_list_size(params);
    if (count > POSITIONAL_PARAMS_MAX)
        return NULL;
    positional_params_t *p = xcalloc(1, sizeof(positional_params_t));
    p->count = count;
    p->arg0 = string_create_from(arg0);
    p->params = xcalloc((size_t)count + 1, sizeof(string_t *));
    for (int i = 0; i < count; i++)
        p->params[i] = string_create_from(string_list_at(params, i));
    p->params[count] = NULL;
    p->max_params = POSITIONAL_PARAMS_MAX;
    return p;
}

positional_params_t *positional_params_create_from_argv(const char *arg0, int argc, const char **argv)
{
    Expects(argc >= 0);
    if (argc > 0)
        Expects_not_null(argv);

    if (argc > POSITIONAL_PARAMS_MAX)
        return NULL;

    positional_params_t *p = xcalloc(1, sizeof(positional_params_t));
    p->max_params = POSITIONAL_PARAMS_MAX;
    p->arg0 = string_create_from_cstr(arg0);
    if (argc == 0)
    {
        p->params = NULL;
        p->count = 0;
        return p;
    }

    p->params = xcalloc((size_t)argc + 1, sizeof(string_t *));
    p->count = argc;

    for (int i = 0; i < argc; i++)
        p->params[i] = string_create_from_cstr(argv[i]);
    p->params[argc] = NULL;
    return p;
}

positional_params_t *positional_params_clone(const positional_params_t *src)
{
    Expects_not_null(src);

    positional_params_t *p = xcalloc(1, sizeof(positional_params_t));
    p->max_params = src->max_params;
    p->count = src->count;
    p->arg0 = src->arg0 ? string_create_from(src->arg0) : string_create_from_cstr("mgsh");

    if (src->count == 0 || src->params == NULL)
    {
        p->params = NULL;
        return p;
    }

    p->params = xcalloc((size_t)src->count, sizeof(string_t *));
    for (int i = 0; i < src->count; i++)
    {
        p->params[i] = string_create_from(src->params[i]);
    }

    return p;
}

void positional_params_destroy(positional_params_t **params)
{
    if (params == NULL || *params == NULL)
        return;

    positional_params_t *p = *params;

    if (p->params != NULL)
    {
        for (int i = 0; i < p->count; i++)
        {
            if (p->params[i] != NULL)
                string_destroy(&p->params[i]);
        }
        if (p->arg0 != NULL)
            string_destroy(&p->arg0);
        xfree(p->params);
    }

    xfree(p);
    *params = NULL;
}

// ============================================================================
// Parameter Access
// ============================================================================

const string_t *positional_params_get(const positional_params_t *params, int n)
{
    Expects_not_null(params);
    Expects(n >= 1);

    if (n < 1 || n > params->count)
        return NULL;

    return params->params[n - 1];
}

const string_t *positional_params_get_arg0(const positional_params_t *params)
{
    Expects_not_null(params);
    return params->arg0;
}

int positional_params_count(const positional_params_t *params)
{
    Expects_not_null(params);
    return params->count;
}

string_list_t *positional_params_get_all(const positional_params_t *params)
{
    Expects_not_null(params);

    string_list_t *list = string_list_create();

    if (params->params == NULL || params->count == 0)
        return list;

    for (int i = 0; i < params->count; i++)
    {
        string_list_push_back(list, params->params[i]);
    }

    return list;
}

string_t *positional_params_get_all_joined(const positional_params_t *params, char sep)
{
    Expects_not_null(params);
    Expects(sep != '\0');

    string_t *result = string_create();

    if (params->params == NULL || params->count == 0)
        return result;

    for (int i = 0; i < params->count; i++)
    {
        if (i > 0)
            string_append_char(result, sep);
        string_append(result, params->params[i]);
    }

    return result;
}

// ============================================================================
// Parameter Modification
// ============================================================================

void positional_params_set_arg0(positional_params_t* params, const string_t* arg0)
{
    Expects_not_null(params);
    Expects_not_null(arg0);

    if (params->arg0 != NULL)
        string_destroy(&params->arg0);
    params->arg0 = string_create_from(arg0);
}

bool positional_params_replace(positional_params_t *params,
                               string_t **new_params, int count)
{
    Expects_not_null(params);
    Expects(count >= 0);

    if (count > 0)
        Expects_not_null(new_params);

    if (count > params->max_params)
        return false;

    // Free old parameters
    if (params->params != NULL)
    {
        for (int i = 0; i < params->count; i++)
        {
            if (params->params[i] != NULL)
                string_destroy(&params->params[i]);
        }
        xfree(params->params);
    }

    // Set new parameters
    params->params = new_params;
    params->count = count;

    return true;
}

bool positional_params_shift(positional_params_t *params, int n)
{
    Expects_not_null(params);
    Expects(n >= 0);

    if (n > params->count)
        return false;

    if (n == 0)
        return true;

    // Free the shifted-out parameters
    for (int i = 0; i < n; i++)
    {
        if (params->params[i] != NULL)
            string_destroy(&params->params[i]);
    }

    int new_count = params->count - n;

    if (new_count == 0)
    {
        xfree(params->params);
        params->params = NULL;
        params->count = 0;
        return true;
    }

    // Allocate new array and copy remaining parameters
    string_t **new_params = xcalloc((size_t)new_count, sizeof(string_t *));
    for (int i = 0; i < new_count; i++)
    {
        new_params[i] = params->params[i + n];
    }

    xfree(params->params);
    params->params = new_params;
    params->count = new_count;

    return true;
}

// ============================================================================
// Configuration
// ============================================================================

void positional_params_set_max(positional_params_t *params, int max_params)
{
    Expects_not_null(params);
    Expects(max_params > 0);
    Expects(max_params <= POSITIONAL_PARAMS_MAX);

    params->max_params = max_params;
}

int positional_params_get_max(const positional_params_t *params)
{
    Expects_not_null(params);
    return params->max_params;
}


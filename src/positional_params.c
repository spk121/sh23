#include "positional_params.h"
#include "xalloc.h"
#include "string_t.h"
#include "logging.h"
#include <string.h>
#include <assert.h>


static positional_params_t *positional_params_create_frame(string_t **params, int count)
{
    positional_params_t *f = xcalloc(1, sizeof(positional_params_t));
    f->params = params;
    f->count = count;
    return f;
}

static void positional_params_destroy_frame(positional_params_t **pf)
{
    Expects_not_null(pf);
    Expects_not_null(*pf);

    positional_params_t *f = *pf;
    if (f->params) {
        for (int i = 0; i < f->count; i++) {
            if (f->params[i])
                string_destroy(&f->params[i]);
        }
        xfree(f->params);
    }
    xfree(f);
    *pf = NULL;
}

static void positional_params_stack_ensure_capacity(positional_params_stack_t *stack, int needed)
{
    Expects_not_null(stack);
    
    if (stack->capacity >= needed) return;
    int newcap = stack->capacity ? stack->capacity * 2 : 4;
    if (newcap < needed) newcap = needed;
    if (stack->frames == NULL)
        stack->frames = xcalloc((size_t)newcap, sizeof(positional_params_t *));
    else
        stack->frames = xrealloc(stack->frames, (size_t)newcap * sizeof(positional_params_t *));
    stack->capacity = newcap;
}

positional_params_stack_t *positional_params_stack_create(void)
{
    positional_params_stack_t *s = xcalloc(1, sizeof(positional_params_stack_t));
    s->capacity = 0;
    s->depth = 0;
    s->frames = NULL;
    s->zero = string_create();
    s->max_params = POSITIONAL_PARAMS_MAX;
    // initialize with an empty frame
    positional_params_stack_ensure_capacity(s, 1);
    s->frames[0] = positional_params_create_frame(NULL, 0);
    s->depth = 1;
    return s;
}

void positional_params_stack_destroy(positional_params_stack_t **stack)
{
    if (!stack || !*stack) return;
    positional_params_stack_t *s = *stack;
    for (int i = 0; i < s->depth; i++) {
        positional_params_destroy_frame(&s->frames[i]);
    }
    xfree(s->frames);
    string_destroy(&s->zero);
    xfree(s);
    *stack = NULL;
}

bool positional_params_push(positional_params_stack_t *stack, string_t **params, int count)
{
    Expects_not_null(stack);
    Expects(count >= 0);
    if (count > 0)
        Expects_not_null(params);

    if (count > stack->max_params)
        return false;

    positional_params_stack_ensure_capacity(stack, stack->depth + 1);
    stack->frames[stack->depth] = positional_params_create_frame(params, count);
    stack->depth += 1;
    return true;
}

void positional_params_pop(positional_params_stack_t *stack)
{
    Expects_not_null(stack);

    if (stack->depth <= 1)
        return; // keep at least one frame
    stack->depth -= 1;
    positional_params_destroy_frame(&stack->frames[stack->depth]);
}

int positional_params_stack_depth(const positional_params_stack_t *stack)
{
    Expects_not_null(stack);

    return stack->depth;
}

static positional_params_t *positional_params_current(const positional_params_stack_t *stack)
{
    Expects_not_null(stack);
    return stack->frames[stack->depth - 1];
}

const string_t *positional_params_get(const positional_params_stack_t *stack, int n)
{
    Expects_not_null(stack);
    Expects_ge(n, 1);

    positional_params_t *cur = positional_params_current(stack);
    Expects_not_null(cur);
    Expects_ge(cur->count, 0);
    Expects_le(n, cur->count);
    return cur->params[n - 1];
}

int positional_params_count(const positional_params_stack_t *stack)
{
    Expects_not_null(stack);

    positional_params_t *cur = positional_params_current(stack);
    return cur ? cur->count : 0;
}

string_list_t *positional_params_get_all(const positional_params_stack_t *stack)
{
    Expects_not_null(stack);

    positional_params_t *cur = positional_params_current(stack);
    string_list_t *list = string_list_create();
    if (!cur || !cur->params) return list;
    for (int i = 0; i < cur->count; i++)
    {
        string_list_push_back(list, cur->params[i]);
    }
    return list;
}

string_t *positional_params_get_all_joined(const positional_params_stack_t *stack, char sep)
{
    Expects_not_null(stack);
    Expects(sep != '\0');

    positional_params_t *cur = positional_params_current(stack);
    string_t *out = string_create();
    if (!cur || !cur->params) return out;
    for (int i = 0; i < cur->count; i++)
    {
        if (i > 0)
            string_append_char(out, sep);
        string_append(out, cur->params[i]);
    }
    return out;
}

static bool positional_params_replace_frame(positional_params_stack_t *stack, string_t **params, int count)
{
    Expects_not_null(stack);
    Expects(count >= 0);
    if (count > 0)
    {
        Expects_not_null(params);
        Expects_not_null(*params);
    }
    
    if (count > stack->max_params)
        return false;
    
    positional_params_t *cur = positional_params_current(stack);
    if (!cur) return false;
    positional_params_destroy_frame(&stack->frames[stack->depth - 1]);
    stack->frames[stack->depth - 1] = positional_params_create_frame(params, count);
    return true;
}

bool positional_params_replace(positional_params_stack_t *stack, string_t **params, int count)
{
    Expects_not_null(stack);
    Expects_ge(count, 0);
    if (count > 0)
    {
        Expects_not_null(params);
        Expects_not_null(*params);
    }

    return positional_params_replace_frame(stack, params, count);
}

bool positional_params_shift(positional_params_stack_t *stack, int n)
{
    Expects_not_null(stack);
    Expects_ge(n, 0);

    positional_params_t *cur = positional_params_current(stack);
    if (!cur) return false;
    if (n > cur->count) return false;
    if (n == 0) return true;
    int new_count = cur->count - n;
    string_t **new_params = NULL;
    if (new_count > 0)
    {
        new_params = xcalloc((size_t)new_count, sizeof(string_t *));
        for (int i = 0; i < new_count; i++)
        {
            new_params[i] = cur->params[i + n];
        }
    }
    // free shifted-out strings
    for (int i = 0; i < n; i++)
    {
        string_destroy(&cur->params[i]);
    }
    xfree(cur->params);
    cur->params = new_params;
    cur->count = new_count;
    return true;
}

void positional_params_set_max(positional_params_stack_t *stack, int max_params)
{
    Expects_not_null(stack);
    Expects(max_params > 0);
    
    stack->max_params = max_params;
}

int positional_params_get_max(const positional_params_stack_t *stack)
{
    Expects_not_null(stack);
    
    return stack->max_params;
}

void positional_params_set_zero(positional_params_stack_t *stack, const string_t *name)
{
    Expects_not_null(name);
    Expects_not_null(stack);
    
    if (stack->zero)
        string_destroy(&stack->zero);
    stack->zero = string_create_from(name);
}

bool positional_params_has_zero(const positional_params_stack_t *stack)
{
    Expects_not_null(stack);
    Expects_not_null(stack->zero);

    return string_length(stack->zero) > 0;
}

const string_t *positional_params_get_zero(const positional_params_stack_t *stack)
{
    Expects_not_null(stack);
    Expects_not_null(stack->zero);

    return stack->zero;
}

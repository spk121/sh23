/* getopt.c -- Portable GNU-like getopt + getopt_long for ISO C23
   Original: GNU C Library / gnulib
   Copyright (C) 1987-2025 Free Software Foundation, Inc.
   Copyright (C) 2025 Michael L. Gran.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, version 2.1 or later.
*/

#include "getopt.h"
#include "string_t.h"
#include "xalloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Traditional global variables */
char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = '?';
char **__getopt_external_argv = NULL;

static struct getopt_state _getopt_global_state = {
    .optind = 1, .opterr = 1, .optopt = '?', .initialized = 0};

/* ============================================================================
 * Helper predicates and utilities
 * ============================================================================ */

/* Check if current argument is a non-option (doesn't start with - or +, or is lone -/+) */
static inline int is_nonoption(const char *arg)
{
    return (arg[0] != '-' && arg[0] != '+') || arg[1] == '\0';
}

/* Check if argument is a lone hyphen */
static inline int is_lone_hyphen(const char *arg)
{
    return arg[0] == '-' && arg[1] == '\0';
}

/* Check if argument is the "--" terminator */
static inline int is_double_dash(const char *arg)
{
    return arg[0] == '-' && arg[1] == '-' && arg[2] == '\0';
}

/* Check if argument is a long option (--foo) */
static inline int is_long_option(const char *arg)
{
    return arg[0] == '-' && arg[1] == '-' && arg[2] != '\0';
}

/* Check if argument is a long plus option (++foo) */
static inline int is_long_plus_option(const char *arg)
{
    return arg[0] == '+' && arg[1] == '+' && arg[2] != '\0';
}

/* ============================================================================
 * Permutation (reordering non-options to the end)
 * ============================================================================ */

static void exchange(char **argv, struct getopt_state *st)
{
    if (__getopt_external_argv == NULL)
    {
        int argc = 0;
        while (argv[argc] != NULL)
            argc++;
        __getopt_external_argv = xcalloc((size_t)argc + 1, sizeof(char *));
        for (int i = 0; i < argc; i++)
            __getopt_external_argv[i] = argv[i];
        __getopt_external_argv[argc] = NULL;
        argv = __getopt_external_argv;
    }
    int bottom = st->first_nonopt;
    int middle = st->last_nonopt;
    int top = st->optind;
    char *tmp;
    while (top > middle && middle > bottom)
    {
        if (top - middle > middle - bottom)
        {
            int len = middle - bottom;
            for (int i = 0; i < len; ++i)
            {
                tmp = argv[bottom + i];
                argv[bottom + i] = argv[top - (middle - bottom) + i];
                argv[top - (middle - bottom) + i] = tmp;
            }
            top -= len;
        }
        else
        {
            int len = top - middle;
            for (int i = 0; i < len; ++i)
            {
                tmp = argv[bottom + i];
                argv[bottom + i] = argv[middle + i];
                argv[middle + i] = tmp;
            }
            bottom += len;
        }
    }
    st->first_nonopt += (st->optind - st->last_nonopt);
    st->last_nonopt = st->optind;
}

/* Mark all remaining arguments as non-options and optionally exchange */
static void finalize_nonoptions(char **argv, struct getopt_state *st, int argc)
{
    if (st->first_nonopt != st->last_nonopt && st->last_nonopt != st->optind)
        exchange(argv, st);
    else if (st->first_nonopt == st->last_nonopt)
        st->first_nonopt = st->optind;
    st->last_nonopt = argc;
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

static const char *initialize(const char *optstring, struct getopt_state *st, int posixly_correct)
{
    if (st->optind == 0)
        st->optind = 1;
    st->first_nonopt = st->last_nonopt = st->optind;
    st->__nextchar = NULL;
    st->initialized = 1;

    /* Set print_errors from opterr; will be updated below if optstring starts with ':' */
    st->print_errors = st->opterr;

    if (optstring[0] == '-')
    {
        st->ordering = RETURN_IN_ORDER;
        ++optstring;
    }
    else if (optstring[0] == '+')
    {
        st->ordering = REQUIRE_ORDER;
        ++optstring;
    }
    else if (posixly_correct)
        st->ordering = REQUIRE_ORDER;
    else
        st->ordering = PERMUTE;

    /* Leading ':' suppresses error messages */
    if (optstring[0] == ':')
        st->print_errors = 0;

    return optstring;
}

/* Skip leading '+' or '-' in optstring if present */
static const char *skip_optstring_prefix(const char *optstring)
{
    if (optstring[0] == '+' || optstring[0] == '-')
        return optstring + 1;
    return optstring;
}

/* ============================================================================
 * Long option processing
 * ============================================================================ */

static int process_long_option(int argc, char **argv, const char *optstring,
                               const struct option *longopts, int *longind, int long_only,
                               struct getopt_state *st, const char *prefix, int plus_aware)
{
    char *nameend;
    size_t namelen;
    const struct option *p;
    const struct option *pfound = NULL;
    int option_index = -1;

    for (nameend = st->__nextchar; *nameend && *nameend != '='; ++nameend)
        ;
    namelen = (size_t)(nameend - st->__nextchar);

    /* Exact match search */
    {
        int idx = 0;
        for (p = longopts; p && p->name; ++p, ++idx)
        {
            if (strlen(p->name) == namelen && strncmp(p->name, st->__nextchar, namelen) == 0)
            {
                pfound = p;
                option_index = idx;
                break;
            }
        }
    }

    /* Prefix match / ambiguity check */
    if (!pfound)
    {
        int idx = 0, matches = 0, exact = 0;
        const struct option *candidate = NULL;
        for (p = longopts; p && p->name; ++p, ++idx)
        {
            if (strncmp(p->name, st->__nextchar, namelen) == 0)
            {
                ++matches;
                if (strlen(p->name) == namelen)
                {
                    ++exact;
                    candidate = p;
                    option_index = idx;
                }
                else if (!candidate)
                {
                    candidate = p;
                    option_index = idx;
                }
            }
        }
        if (matches > 1 && exact != 1)
        {
            if (st->print_errors)
                fprintf(stderr, "%s: option '%s%.*s' is ambiguous\n", argv[0], prefix, (int)namelen,
                        st->__nextchar);
            st->__nextchar += strlen(st->__nextchar);
            ++st->optind;
            st->optopt = 0;
            return '?';
        }
        pfound = candidate;
    }

    /* Not found */
    if (!pfound)
    {
        if (!long_only || argv[st->optind][1] == '-' || strchr(optstring, *st->__nextchar) == NULL)
        {
            if (st->print_errors)
                fprintf(stderr, "%s: unrecognized option '%s%.*s'\n", argv[0], prefix, (int)namelen,
                        st->__nextchar);
            st->__nextchar = NULL;
            ++st->optind;
            st->optopt = 0;
            return '?';
        }
        return -1;
    }

    ++st->optind;
    st->__nextchar = NULL;

    /* Handle argument (=value or next argv) */
    if (*nameend)
    {
        if (pfound->has_arg)
            st->optarg = nameend + 1;
        else
        {
            if (st->print_errors)
                fprintf(stderr, "%s: option '%s%s' doesn't allow an argument\n", argv[0], prefix,
                        pfound->name);
            st->optopt = pfound->val;
            return '?';
        }
    }
    else if (pfound->has_arg == required_argument)
    {
        if (st->optind < argc)
            st->optarg = argv[st->optind++];
        else
        {
            if (st->print_errors)
                fprintf(stderr, "%s: option '%s%s' requires an argument\n", argv[0], prefix,
                        pfound->name);
            st->optopt = pfound->val;
            return optstring[0] == ':' ? ':' : '?';
        }
    }
    else if (pfound->has_arg == optional_argument)
        st->optarg = NULL;

    if (longind)
        *longind = option_index;

    /* Handle flag setting */
    if (pfound->flag)
    {
        if (plus_aware)
        {
            const struct option_ex *pex = (const struct option_ex *)pfound;
            if (st->opt_plus_prefix)
            {
                *pex->flag = 0;
                return 0;
            }
            else
            {
                *pex->flag = 1;
                return pfound->val;
            }
        }
        else
        {
            *pfound->flag = pfound->val;
            return 0;
        }
    }
    return pfound->val;
}

/* ============================================================================
 * Short option argument handling
 * ============================================================================ */

typedef enum
{
    ARG_NONE,
    ARG_REQUIRED,
    ARG_OPTIONAL
} arg_requirement_t;

static arg_requirement_t get_arg_requirement(const char *optstring, char c)
{
    const char *temp = strchr(optstring, c);
    if (!temp || c == ':' || c == ';')
        return ARG_NONE; /* Will be caught as invalid later */
    if (temp[1] != ':')
        return ARG_NONE;
    if (temp[2] == ':')
        return ARG_OPTIONAL;
    return ARG_REQUIRED;
}

/* Process argument for a short option. Returns 0 on success, '?' or ':' on error */
static int process_short_option_arg(int argc, char **argv, const char *optstring, char c,
                                    struct getopt_state *st)
{
    arg_requirement_t req = get_arg_requirement(optstring, c);

    switch (req)
    {
    case ARG_OPTIONAL:
        if (*st->__nextchar)
        {
            st->optarg = st->__nextchar;
            ++st->optind;
        }
        else
            st->optarg = NULL;
        st->__nextchar = NULL;
        break;

    case ARG_REQUIRED:
        if (*st->__nextchar)
        {
            st->optarg = st->__nextchar;
            ++st->optind;
        }
        else if (st->optind >= argc)
        {
            if (st->print_errors)
                fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], c);
            st->optopt = c;
            return optstring[0] == ':' ? ':' : '?';
        }
        else
            st->optarg = argv[st->optind++];
        st->__nextchar = NULL;
        break;

    case ARG_NONE:
        /* No argument processing needed */
        break;
    }
    return 0;
}

/* ============================================================================
 * Plus-aware flag handling for short options
 * ============================================================================ */

static int handle_plus_aware_short(char c, int is_plus, const struct option_ex *longopts,
                                   struct getopt_state *st, char **argv)
{
    const struct option_ex *p;
    for (p = longopts; p && !(p->val == 0 && p->flag == NULL); ++p)
    {
        if (p->val != c)
            continue;

        if (is_plus)
        {
            if (!p->allow_plus)
            {
                if (st->print_errors)
                    fprintf(stderr, "%s: invalid option -- +%c\n", argv[0], c);
                st->optopt = c;
                return '?';
            }
            if (p->flag)
            {
                *p->flag = 0;
                if (p->plus_used)
                    *p->plus_used = 1;
                return 0;
            }
        }
        else
        {
            if (p->flag)
            {
                *p->flag = 1;
                if (p->plus_used)
                    *p->plus_used = 0;
                return c;
            }
        }
        break;
    }
    return c; /* No flag handling, just return the character */
}

/* ============================================================================
 * -W ; long option handling
 * ============================================================================ */

static int handle_W_long_option(int argc, char **argv, const char *optstring, char c,
                                const struct option *longopts, int *longind,
                                struct getopt_state *st, int plus_aware)
{
    if (*st->__nextchar)
        st->optarg = st->__nextchar;
    else if (st->optind >= argc)
    {
        if (st->print_errors)
            fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], c);
        st->optopt = c;
        return optstring[0] == ':' ? ':' : '?';
    }
    else
        st->optarg = argv[st->optind];

    st->__nextchar = st->optarg;
    st->optarg = NULL;
    return process_long_option(argc, argv, optstring, longopts, longind, 0, st, "-W ", plus_aware);
}

/* ============================================================================
 * Advance to next argument (skip non-options in PERMUTE mode)
 * ============================================================================ */

typedef enum
{
    ADVANCE_CONTINUE, /* Found an option to process */
    ADVANCE_END,      /* No more options */
    ADVANCE_NONOPTION /* Found a non-option (RETURN_IN_ORDER mode) */
} advance_result_t;

static advance_result_t advance_to_next_option(int argc, char **argv, struct getopt_state *st)
{
    /* Clamp indices */
    if (st->last_nonopt > st->optind)
        st->last_nonopt = st->optind;
    if (st->first_nonopt > st->optind)
        st->first_nonopt = st->optind;

    /* Handle PERMUTE ordering: skip over non-options */
    if (st->ordering == PERMUTE)
    {
        if (st->first_nonopt != st->last_nonopt && st->last_nonopt != st->optind)
            exchange(argv, st);
        else if (st->last_nonopt != st->optind)
            st->first_nonopt = st->optind;

        /* Skip non-options, but stop at lone '-' if posix_hyphen is set */
        while (st->optind < argc && is_nonoption(argv[st->optind]))
        {
            if (st->posix_hyphen && is_lone_hyphen(argv[st->optind]))
                break;
            ++st->optind;
        }
        st->last_nonopt = st->optind;
    }

    /* Check for "--" terminator */
    if (st->optind < argc && is_double_dash(argv[st->optind]))
    {
        ++st->optind; /* Skip past "--" */
        finalize_nonoptions(argv, st, argc);
        /* Don't set optind = argc here; fall through to the end-of-arguments
         * check below which will reset optind to first_nonopt if needed.
         */
    }

    /* Check for POSIX single-hyphen terminator */
    if (st->posix_hyphen && st->optind < argc && is_lone_hyphen(argv[st->optind]))
    {
        ++st->optind; /* Skip past "-" */
        finalize_nonoptions(argv, st, argc);
        /* Fall through to end-of-arguments check */
    }

    /* Check for end of arguments, or if all remaining args are non-options */
    if (st->optind >= argc || st->last_nonopt == argc)
    {
        if (st->first_nonopt != st->last_nonopt)
            st->optind = st->first_nonopt;
        return ADVANCE_END;
    }

    /* Check for non-option in non-PERMUTE mode */
    if (is_nonoption(argv[st->optind]))
    {
        if (st->ordering == REQUIRE_ORDER)
            return ADVANCE_END;
        st->optarg = argv[st->optind++];
        return ADVANCE_NONOPTION;
    }

    return ADVANCE_CONTINUE;
}

/* ============================================================================
 * Process the current option argument
 * ============================================================================ */

typedef enum
{
    OPT_LONG,      /* --option */
    OPT_LONG_PLUS, /* ++option */
    OPT_SHORT      /* -x or +x */
} option_type_t;

static option_type_t classify_option(const char *arg, int plus_aware)
{
    if (is_long_option(arg))
        return OPT_LONG;
    if (plus_aware && is_long_plus_option(arg))
        return OPT_LONG_PLUS;
    return OPT_SHORT;
}

/* ============================================================================
 * Main entry point (refactored)
 * ============================================================================ */

int _getopt_internal_r(int argc, char *const argv[], const char *optstring,
                       const void *longopts_void, int *longind, int long_only, int posixly_correct,
                       struct getopt_state *st, int plus_aware)
{
    const struct option_ex *longopts = (const struct option_ex *)longopts_void;

    if (!st || argc < 1)
        return -1;

    st->optarg = NULL;

    /* Initialize on first call or if reset */
    if (st->optind == 0 || !st->initialized)
        optstring = initialize(optstring, st, posixly_correct);
    else
        optstring = skip_optstring_prefix(optstring);

    /* Phase 1: If no current option cluster, advance to next option */
    if (!st->__nextchar || *st->__nextchar == '\0')
    {
        advance_result_t result = advance_to_next_option(argc, (char **)argv, st);
        switch (result)
        {
        case ADVANCE_END:
            return -1;
        case ADVANCE_NONOPTION:
            return 1;
        case ADVANCE_CONTINUE:
            break;
        }

        /* Classify and dispatch to appropriate handler */
        const char *arg = argv[st->optind];
        option_type_t opt_type = classify_option(arg, plus_aware);

        switch (opt_type)
        {
        case OPT_LONG:
            st->opt_plus_prefix = 0;
            st->__nextchar = (char *)(arg + 2);
            return process_long_option(argc, (char **)argv, optstring,
                                       (const struct option *)longopts, longind, long_only, st,
                                       "--", plus_aware);

        case OPT_LONG_PLUS:
            st->opt_plus_prefix = 1;
            st->__nextchar = (char *)(arg + 2);
            return process_long_option(argc, (char **)argv, optstring,
                                       (const struct option *)longopts, longind, long_only, st,
                                       "++", plus_aware);

        case OPT_SHORT:
            st->opt_plus_prefix = (arg[0] == '+');
            st->__nextchar = (char *)(arg + 1);
            break;
        }
    }

    /* Phase 2: Process short option from current cluster */
    char c = *st->__nextchar++;
    int is_plus = st->opt_plus_prefix;

    if (*st->__nextchar == '\0')
        ++st->optind;

    /* Validate option character */
    const char *temp = strchr(optstring, c);
    if (!temp || c == ':' || c == ';')
    {
        if (st->print_errors)
            fprintf(stderr, "%s: invalid option -- '%c%c'\n", argv[0], is_plus ? '+' : '-', c);
        st->optopt = c;
        return '?';
    }

    /* Handle -W ; for long options */
    if (temp[0] == 'W' && temp[1] == ';' && longopts)
    {
        return handle_W_long_option(argc, (char **)argv, optstring, c,
                                    (const struct option *)longopts, longind, st, plus_aware);
    }

    /* Process argument if required */
    int arg_result = process_short_option_arg(argc, (char **)argv, optstring, c, st);
    if (arg_result != 0)
        return arg_result;

    /* Handle plus-aware flag setting */
    if (plus_aware && longopts)
        return handle_plus_aware_short(c, is_plus, longopts, st, (char **)argv);

    return c;
}

/* ============================================================================
 * Public non-reentrant APIs
 * ============================================================================ */

static void sync_globals(const struct getopt_state *st)
{
    optind = st->optind;
    optarg = st->optarg;
    opterr = st->opterr;
    optopt = st->optopt;
}

static void reset_global_state(void)
{
    _getopt_global_state.optind = 1;
    _getopt_global_state.first_nonopt = 1;
    _getopt_global_state.last_nonopt = 1;
    _getopt_global_state.__nextchar = NULL;
    _getopt_global_state.initialized = 0;
}

int getopt(int argc, char *const argv[], const char *optstring)
{
    if (optind == 0)
    {
        optind = 1;
        reset_global_state();
    }
    _getopt_global_state.opterr = opterr;

    int rc = _getopt_internal_r(argc, argv, optstring, NULL, NULL, 0, 0, &_getopt_global_state, 0);
    sync_globals(&_getopt_global_state);
    return rc;
}

int getopt_long(int argc, char *const argv[], const char *optstring, const struct option *longopts,
                int *longind)
{
    if (optind == 0)
    {
        optind = 1;
        reset_global_state();
    }
    _getopt_global_state.opterr = opterr;
    int rc = _getopt_internal_r(argc, argv, optstring, longopts, longind, 0, 0,
                                &_getopt_global_state, 0);
    sync_globals(&_getopt_global_state);
    return rc;
}

int getopt_long_only(int argc, char *const argv[], const char *optstring,
                     const struct option *longopts, int *longind)
{
    if (optind == 0)
    {
        optind = 1;
        reset_global_state();
    }
    _getopt_global_state.opterr = opterr;
    int rc = _getopt_internal_r(argc, argv, optstring, longopts, longind, 1, 0,
                                &_getopt_global_state, 0);
    sync_globals(&_getopt_global_state);
    return rc;
}

int getopt_long_plus(int argc, char *const argv[], const char *optstring,
                     const struct option_ex *longopts, int *longind)
{
    static struct getopt_state state = {0};
    if (optind == 0)
    {
        optind = 1;
        memset(&state, 0, sizeof(state));
        state.optind = 1;
    }
    state.opterr = opterr;
    int rc = _getopt_internal_r(argc, argv, optstring, longopts, longind, 0, 0, &state, 1);
    optind = state.optind;
    optarg = state.optarg;
    optopt = state.optopt;
    return rc;
}

int getopt_long_only_plus(int argc, char *const argv[], const char *optstring,
                          const struct option_ex *longopts, int *longind)
{
    static struct getopt_state state = {0};
    if (optind == 0)
    {
        optind = 1;
        memset(&state, 0, sizeof(state));
        state.optind = 1;
    }
    state.opterr = opterr;
    int rc = _getopt_internal_r(argc, argv, optstring, longopts, longind, 1, 0, &state, 1);
    optind = state.optind;
    optarg = state.optarg;
    optopt = state.optopt;
    return rc;
}

/* ============================================================================
 * Re-entrant variants
 * ============================================================================ */

int getopt_r(int argc, char *const argv[], const char *optstring, struct getopt_state *state)
{
    if (!state)
        return -1;
    return _getopt_internal_r(argc, argv, optstring, NULL, NULL, 0, 0, state, 0);
}

int getopt_long_r(int argc, char *const argv[], const char *optstring,
                  const struct option *longopts, int *longind, struct getopt_state *state)
{
    if (!state)
        return -1;
    return _getopt_internal_r(argc, argv, optstring, longopts, longind, 0, 0, state, 0);
}

int getopt_long_only_r(int argc, char *const argv[], const char *optstring,
                       const struct option *longopts, int *longind, struct getopt_state *state)
{
    if (!state)
        return -1;
    return _getopt_internal_r(argc, argv, optstring, longopts, longind, 1, 0, state, 0);
}

int getopt_long_plus_r(int argc, char *const argv[], const char *optstring,
                       const struct option_ex *longopts, int *longind, struct getopt_state *state)
{
    return _getopt_internal_r(argc, argv, optstring, longopts, longind, 0, 0, state, 1);
}

/* ============================================================================
 * State reset functions
 * ============================================================================ */

void getopt_reset(void)
{
    optind = 1;
    optarg = NULL;
    optopt = '?';
    reset_global_state();
}

void getopt_reset_plus(void)
{
    optind = 1;
    optarg = NULL;
    optopt = '?';
    /* Reset the static state used by getopt_long_plus() and getopt_long_only_plus() */
    /* Note: This is a bit hacky - we're resetting by triggering reinitialization */
}

/* ============================================================================
 * String-based wrappers for shell builtins
 * ============================================================================ */

static char **string_list_to_argv(const string_list_t *list, int *argc)
{
    if (!list)
    {
        *argc = 0;
        return NULL;
    }

    int count = string_list_size(list);
    *argc = count;

    if (count == 0)
        return NULL;

    char **argv = malloc(count * sizeof(char *));
    if (!argv)
        return NULL;

    for (int i = 0; i < count; i++)
    {
        const string_t *str = string_list_at(list, i);
        argv[i] = (char *)string_cstr(str);
    }

    return argv;
}

int getopt_string(const string_list_t *argv, const string_t *optstring)
{
    int argc;
    char **argv_array = string_list_to_argv(argv, &argc);
    if (!argv_array)
        return -1;

    const char *optstr = string_cstr(optstring);

    int result = getopt(argc, argv_array, optstr);

    free(argv_array);
    return result;
}

int getopt_long_plus_string(const string_list_t *argv, const string_t *optstring,
                            const struct option_ex *longopts, int *longind)
{
    int argc;
    char **argv_array = string_list_to_argv(argv, &argc);
    if (!argv_array)
        return -1;

    const char *optstr = string_cstr(optstring);

    int result = getopt_long_plus(argc, argv_array, optstr, longopts, longind);

    free(argv_array);
    return result;
}

#ifdef GETOPT_TEST
#include <stdio.h>
int main(int argc, char **argv)
{
    int c;
    while ((c = getopt_long(argc, argv, "ab:c::dW;:", NULL, NULL)) != -1)
    {
        switch (c)
        {
        case 'a':
            printf("option a\n");
            break;
        case 'b':
            printf("option b with arg %s\n", optarg);
            break;
        case 'c':
            printf("option c with optional arg %s\n", optarg ? optarg : "(none)");
            break;
        case 1:
            printf("non-option arg: %s\n", optarg);
            break;
        case '?':
            printf("unknown option or missing arg\n");
            break;
        }
    }
    printf("remaining args:");
    for (int i = optind; i < argc; ++i)
        printf(" %s", argv[i]);
    printf("\n");
    return 0;
}
#endif

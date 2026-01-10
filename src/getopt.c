/* getopt.c -- Portable GNU-like getopt + getopt_long for ISO C23
   Original: GNU C Library / gnulib
   Copyright (C) 1987-2025 Free Software Foundation, Inc.
   Copyright (C) 2025 Michael L. Gran.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, version 2.1 or later.
*/

#include "getopt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xalloc.h"
#include "string_t.h"


/* Traditional global variables */
char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = '?';
char **__getopt_external_argv = NULL;

static struct getopt_state _getopt_global_state = {
    .optind = 1, .opterr = 1, .optopt = '?', .initialized = 0};

/* Internal helpers */
static void exchange(char **argv, struct getopt_state *st)
{
    if (__getopt_external_argv == NULL)
    {
        // Can't permute the main-provided argv, so
        // copy them to __getopt_external_argv on first use.
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

static int process_long_option(int argc, char **argv, const char *optstring,
                               const struct option *longopts, int *longind, int long_only,
                               struct getopt_state *st, int print_errors, const char *prefix,
                               int plus_aware)
{
    char *nameend;
    size_t namelen;
    const struct option *p;
    const struct option *pfound = NULL;
    int option_index = -1;

    for (nameend = st->__nextchar; *nameend && *nameend != '='; ++nameend)
    {
    }
    namelen = (size_t)(nameend - st->__nextchar);

    /* Exact matches */
    {
        int idx = 0;
        for (p = longopts; p && p->name; ++p, ++idx)
        {
            size_t plen = strlen(p->name);
            if (plen == namelen && strncmp(p->name, st->__nextchar, namelen) == 0)
            {
                pfound = p;
                option_index = idx;
                break;
            }
        }
    }
    /* Prefix matching / ambiguity */
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
            if (print_errors)
                fprintf(stderr, "%s: option '%s%.*s' is ambiguous\n", argv[0], prefix, (int)namelen,
                        st->__nextchar);
            st->__nextchar += strlen(st->__nextchar);
            ++st->optind;
            st->optopt = 0;
            return '?';
        }
        pfound = candidate;
    }

    if (!pfound)
    {
        if (!long_only || argv[st->optind][1] == '-' || strchr(optstring, *st->__nextchar) == NULL)
        {
            if (print_errors)
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

    if (*nameend)
    {
        if (pfound->has_arg)
            st->optarg = nameend + 1;
        else
        {
            if (print_errors)
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
            if (print_errors)
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
    if (pfound->flag)
    {
        if (plus_aware)
        {
            /* For option_ex: handle +/- prefix like short options */
            const struct option_ex *pex = (const struct option_ex *)pfound;
            if (st->opt_plus_prefix)
            {
                *pex->flag = 0; /* unset */
                return 0;
            }
            else
            {
                *pex->flag = 1; /* set */
                return pfound->val;
            }
        }
        else
        {
            /* Standard option behavior */
            *pfound->flag = pfound->val;
            return 0;
        }
    }
    return pfound->val;
}

static const char *initialize(const char *optstring, struct getopt_state *st, int posixly_correct)
{
    if (st->optind == 0)
        st->optind = 1;
    st->first_nonopt = st->last_nonopt = st->optind;
    st->__nextchar = NULL;
    st->initialized = 1;
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
    return optstring;
}

int _getopt_internal_r(int argc, char *const argv[], const char *optstring,
                       const void *longopts_void, int *longind, int long_only, int posixly_correct,
                       struct getopt_state *st, int plus_aware)
{
    const struct option_ex *longopts = (const struct option_ex *)longopts_void;

    if (!st || argc < 1)
        return -1;
    int print_errors = st->opterr;

    st->optarg = NULL;

    if (st->optind == 0 || !st->initialized)
        optstring = initialize(optstring, st, posixly_correct);
    else if (optstring[0] == '+' || optstring[0] == '-')
        ++optstring;

    if (optstring[0] == ':')
        print_errors = 0;

#define NONOPTION_P                                                                                \
    ((argv[st->optind][0] != '-' && argv[st->optind][0] != '+') || argv[st->optind][1] == '\0')

    if (!st->__nextchar || *st->__nextchar == '\0')
    {
        if (st->last_nonopt > st->optind)
            st->last_nonopt = st->optind;
        if (st->first_nonopt > st->optind)
            st->first_nonopt = st->optind;

        if (st->ordering == PERMUTE)
        {
            if (st->first_nonopt != st->last_nonopt && st->last_nonopt != st->optind)
                exchange((char **)argv, st);
            else if (st->last_nonopt != st->optind)
                st->first_nonopt = st->optind;
            while (st->optind < argc && NONOPTION_P &&
                   !(st->posix_hyphen && argv[st->optind][0] == '-' && argv[st->optind][1] == '\0'))
                ++st->optind;
            st->last_nonopt = st->optind;
        }

        if (st->optind != argc && strcmp(argv[st->optind], "--") == 0)
        {
            ++st->optind;
            if (st->first_nonopt != st->last_nonopt && st->last_nonopt != st->optind)
                exchange((char **)argv, st);
            else if (st->first_nonopt == st->last_nonopt)
                st->first_nonopt = st->optind;
            st->last_nonopt = argc;
            st->optind = argc;
        }

        /* POSIX shell single-hyphen handling:
         * Per POSIX.1-2024: "A single <hyphen-minus> shall be treated as the
         * first operand and then ignored."
         * Unlike "--", the lone '-' is skipped entirely (not an operand).
         * This behavior is only enabled when posix_hyphen is set.
         */
        if (st->posix_hyphen && st->optind != argc && argv[st->optind][0] == '-' &&
            argv[st->optind][1] == '\0')
        {
            ++st->optind; /* Skip past the hyphen */
            /* Mark remaining arguments as operands, terminate option processing */
            if (st->first_nonopt != st->last_nonopt && st->last_nonopt != st->optind)
                exchange((char **)argv, st);
            else if (st->first_nonopt == st->last_nonopt)
                st->first_nonopt = st->optind;
            st->last_nonopt = argc;
            /* Return -1 to signal end of options, with optind pointing to
             * the first operand (or argc if there are no more arguments).
             */
            return -1;
        }

        if (st->optind == argc)
        {
            if (st->first_nonopt != st->last_nonopt)
                st->optind = st->first_nonopt;
            return -1;
        }

        if (NONOPTION_P)
        {
            if (st->ordering == REQUIRE_ORDER)
                return -1;
            st->optarg = argv[st->optind++];
            return 1;
        }

        char prefix_char = argv[st->optind][0];
        int is_plus = (prefix_char == '+');

        /* Handle long options if applicable */
        if (longopts && argv[st->optind][1] == '-')
        {
            st->opt_plus_prefix = 0;
            st->__nextchar = argv[st->optind] + 2;
            return process_long_option(argc, (char **)argv, optstring,
                                       (const struct option *)longopts, longind, long_only, st,
                                       print_errors, "--", plus_aware);
        }
        if (plus_aware && is_plus && argv[st->optind][1] == '+')
        {
            st->opt_plus_prefix = 1;
            st->__nextchar = argv[st->optind] + 2;
            return process_long_option(argc, (char **)argv, optstring,
                                       (const struct option *)longopts, longind, long_only, st,
                                       print_errors, "++", plus_aware);
        }

        /* Short option cluster or single */
        st->opt_plus_prefix = is_plus;
        st->__nextchar = argv[st->optind] + 1;
    }

    /* Short options in cluster */
    {
        char c = *st->__nextchar++;
        int is_plus = st->opt_plus_prefix;

        if (*st->__nextchar == '\0')
            ++st->optind;

        /* Find short option in optstring */
        const char *temp = strchr(optstring, c);
        if (!temp || c == ':' || c == ';')
        {
            if (print_errors)
                fprintf(stderr, "%s: invalid option -- '%c%c'\n", argv[0], is_plus ? '+' : '-', c);
            st->optopt = c;
            return '?';
        }

        if (temp[0] == 'W' && temp[1] == ';' && longopts)
        {
            if (*st->__nextchar)
                st->optarg = st->__nextchar;
            else if (st->optind == argc)
            {
                if (print_errors)
                    fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], c);
                st->optopt = c;
                return optstring[0] == ':' ? ':' : '?';
            }
            else
                st->optarg = argv[st->optind];
            st->__nextchar = st->optarg;
            st->optarg = NULL;
            return process_long_option(argc, (char **)argv, optstring,
                                       (const struct option *)longopts, longind, 0, st,
                                       print_errors, "-W ", plus_aware);
        }

        if (temp[1] == ':')
        {
            if (temp[2] == ':') /* optional argument */
            {
                if (*st->__nextchar)
                {
                    st->optarg = st->__nextchar;
                    ++st->optind;
                }
                else
                    st->optarg = NULL;
                st->__nextchar = NULL;
            }
            else /* required argument */
            {
                if (*st->__nextchar)
                {
                    st->optarg = st->__nextchar;
                    ++st->optind;
                }
                else if (st->optind == argc)
                {
                    if (print_errors)
                        fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], c);
                    st->optopt = c;
                    return optstring[0] == ':' ? ':' : '?';
                }
                else
                    st->optarg = argv[st->optind++];
                st->__nextchar = NULL;
            }
        }
        if (plus_aware && longopts)
        {
            const struct option_ex *p;
            int idx = 0;
            /* Loop through option_ex array until terminator (val == 0 and flag == NULL) */
            for (p = longopts; p && !(p->val == 0 && p->flag == NULL); ++p, ++idx)
            {
                if (p->val == c)
                {
                    if (is_plus)
                    {
                        if (!p->allow_plus)
                        {
                            if (print_errors)
                                fprintf(stderr, "%s: invalid option -- +%c\n", argv[0], c);
                            st->optopt = c;
                            return '?';
                        }
                        if (p->flag)
                        {
                            *p->flag = 0; /* unset */
                            if (p->plus_used)
                                *p->plus_used = 1;
                            return 0;
                        }
                        /* fall through to return val if no flag */
                    }
                    else
                    {
                        /* For minus prefix */
                        if (p->flag)
                        {
                            *p->flag = 1; /* set flag to 1 (true/on) */
                            if (p->plus_used)
                                *p->plus_used = 0;
                            /* Return the character to indicate which option was set */
                            return c;
                        }
                    }
                    break;
                }
            }
        }
        return c;
    }
}

/* Public non-reentrant APIs use the persistent global state */

static void sync_globals(const struct getopt_state *st)
{
    optind = st->optind;
    optarg = st->optarg;
    opterr = st->opterr;
    optopt = st->optopt;
}

int getopt(int argc, char *const argv[], const char *optstring)
{
    /* Reset if user set optind = 0 */
    if (optind == 0)
    {
        optind = 1;
        _getopt_global_state.optind = 1;
        _getopt_global_state.first_nonopt = 1;
        _getopt_global_state.last_nonopt = 1;
        _getopt_global_state.__nextchar = NULL;
        _getopt_global_state.initialized = 0;
    }
    /* Keep opterr from global */
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
        _getopt_global_state.optind = 1;
        _getopt_global_state.first_nonopt = 1;
        _getopt_global_state.last_nonopt = 1;
        _getopt_global_state.__nextchar = NULL;
        _getopt_global_state.initialized = 0;
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
        _getopt_global_state.optind = 1;
        _getopt_global_state.first_nonopt = 1;
        _getopt_global_state.last_nonopt = 1;
        _getopt_global_state.__nextchar = NULL;
        _getopt_global_state.initialized = 0;
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

/* Re-entrant variants unchanged */
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

/* ============================================================================
 * String-based wrappers for shell builtins
 * ============================================================================ */


/**
 * Convert string_list_t to char** for getopt
 * The caller must free the returned array (but not the individual strings)
 */
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
        argv[i] = (char *)string_cstr(str);  // Cast away const for getopt compatibility
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

/* getopt.h -- Portable GNU-like getopt + getopt_long for ISO C23 and for POSIX shell environments
   Copyright (C) 1987-2025 Free Software Foundation, Inc.
   Copyright (C) 2025 Michael L. Gran.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, version 2.1 or later.
*/

/* getopt.h -- Portable GNU-like getopt + getopt_long for ISO C23
   Enhanced with +prefix support for POSIX shell
*/

#ifndef SH23_GETOPT_H
#define SH23_GETOPT_H

#include "string_t.h"

enum
{
    no_argument = 0,
    required_argument = 1,
    optional_argument = 2
};

/* Standard struct option */
struct option
{
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

/* Extended struct for +prefix-aware options (used by _plus variants) */
struct option_ex
{
    const char *name; /* long name, e.g. "verbose" */
    int has_arg;      /* no_argument, required_argument, optional_argument */
    int allow_plus;   /* 1 if +name or +short is allowed (means "unset") */
    int *flag;        /* if non-NULL: set to val on -, clear on + */
    int val;          /* value to store in *flag when setting */
    int *plus_used;   /* optional: set to 1 if +prefix used */
};

/* Traditional globals */
extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;

/* Standard interfaces */
int getopt(int argc, char *const argv[], const char *optstring);
int getopt_long(int argc, char *const argv[], const char *optstring, const struct option *longopts,
                int *longind);
int getopt_long_only(int argc, char *const argv[], const char *optstring,
                     const struct option *longopts, int *longind);

/* prefix-aware interfaces */
int getopt_long_plus(int argc, char *const argv[], const char *optstring,
                     const struct option_ex *longopts, int *longind);
int getopt_long_only_plus(int argc, char *const argv[], const char *optstring,
                          const struct option_ex *longopts, int *longind);

/* Re-entrant versions */
typedef enum
{
    REQUIRE_ORDER,
    PERMUTE,
    RETURN_IN_ORDER
} getopt_ordering_t;

struct getopt_state
{
    int optind;
    int opterr;
    int optopt;
    char *optarg;
    int initialized;
    char *__nextchar;
    getopt_ordering_t ordering;
    int first_nonopt;
    int last_nonopt;
    int opt_plus_prefix;

    /* Derived from opterr and optstring[0]==':' during initialization.
     * If 0, suppress error messages to stderr.
     */
    int print_errors;

    /* POSIX shell extension: if set, a lone '-' argument terminates option
     * processing (like '--') but is itself skipped rather than preserved
     * as an operand. Per POSIX.1-2024 sh utility specification:
     * "A single <hyphen-minus> shall be treated as the first operand and
     * then ignored."
     */
    int posix_hyphen;
};

int getopt_r(int argc, char *const argv[], const char *optstring, struct getopt_state *state);
int getopt_long_r(int argc, char *const argv[], const char *optstring,
                  const struct option *longopts, int *longind, struct getopt_state *state);
int getopt_long_only_r(int argc, char *const argv[], const char *optstring,
                       const struct option *longopts, int *longind, struct getopt_state *state);

int getopt_long_plus_r(int argc, char *const argv[], const char *optstring,
                       const struct option_ex *longopts, int *longind, struct getopt_state *state);

/* Internal core */
int _getopt_internal_r(int argc, char *const argv[], const char *optstring, const void *longopts,
                       int *longind, int long_only, int posixly_correct, struct getopt_state *state,
                       int plus_aware); /* 1 if using option_ex */

/* ============================================================================
 * String-based wrappers for shell builtins
 * ============================================================================
 * These wrappers allow using string_list_t and string_t directly with getopt,
 * which is convenient for shell builtins that already work with these types.
 */

/**
 * String-based wrapper for getopt()
 *
 * @param argv The argument list as a string_list_t
 * @param optstring The option string as a string_t
 * @return Same as getopt(): option character, -1 for end, '?' for error
 */
int getopt_string(const string_list_t *argv, const string_t *optstring);

/**
 * String-based wrapper for getopt_long_plus()
 *
 * @param argv The argument list as a string_list_t
 * @param optstring The option string as a string_t
 * @param longopts Long options array (NULL-terminated)
 * @param longind Pointer to store index of long option (can be NULL)
 * @return Same as getopt_long_plus(): option character, -1 for end, '?' for error
 */
int getopt_long_plus_string(const string_list_t *argv, const string_t *optstring,
                            const struct option_ex *longopts, int *longind);

/* State reset functions */
void getopt_reset(void);
void getopt_reset_plus(void);

#endif /* GETOPT_H */

/* getopt.h -- Portable GNU-like getopt + getopt_long tweaked for
   use with ISO C and POSIX shell environments.

   Copyright (C) 1987-2025 Free Software Foundation, Inc.
   Copyright (C) 2025 Michael L. Gran.

   This program is free software: you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License
   as published by the Free Software Foundation, version 2.1 or later. */

/**
 * @file getopt.h
 * @brief Portable GNU-like getopt, getopt_long, and +prefix extensions for
 *        ISO C23 and POSIX shell environments.
 *
 * This header provides command-line option parsing for the migash shell,
 * including extensions that recognize a @c + prefix on options.  The +prefix
 * convention is used in POSIX shell builtins (e.g. @c set) where @c -x
 * enables an option and @c +x disables (unsets) it.
 *
 * @par Public API
 * The only exported (MIGA_API) functions are the reentrant, strlist-based
 * wrappers.  These take a @c strlist_t argument vector, a @c string_t
 * optstring, and a @c struct @c getopt_state for reentrant parsing:
 *
 *   - getopt_r_string()
 *   - getopt_long_r_string()
 *   - getopt_long_only_r_string()
 *   - getopt_long_plus_r_string()
 *   - getopt_long_only_plus_r_string()
 *   - getopt_state_init()
 *
 * All other functions (non-reentrant globals-based APIs, raw @c char** reentrant
 * APIs, and internal helpers) are @c MIGA_LOCAL and not part of the public ABI.
 *
 * @par Optstring syntax
 * The @p optstring argument follows the GNU/POSIX convention:
 *   - A letter alone (e.g. @c "v") is a flag with no argument.
 *   - A letter followed by @c ':' (e.g. @c "o:") requires an argument.
 *   - A letter followed by @c '::' (e.g. @c "c::") accepts an optional
 *     argument (the argument must be attached: @c -cARG, not @c -c @c ARG).
 *   - A leading @c '+' in @p optstring selects REQUIRE_ORDER (stop at the
 *     first non-option).
 *   - A leading @c '-' selects RETURN_IN_ORDER (return non-options as if they
 *     were arguments to option character @c \\1).
 *   - A leading @c ':' (after any @c '+' or @c '-') suppresses error messages
 *     and causes a missing required argument to return @c ':' instead of
 *     @c '?'.
 *   - The sequence @c "W;" causes @c -W @c foo to be treated as the long
 *     option @c --foo.
 *
 * @par Option termination
 *   - The argument @c "--" terminates option scanning; @c optind is advanced
 *     past it and the remaining elements are operands.
 *   - A lone @c "-" is not treated as an option; it is an operand.
 *
 * @par Plus-prefix semantics
 * When using the plus-aware functions:
 *   - Short options may be prefixed with @c + instead of @c - (e.g. @c +v).
 *     A @c + prefix means "unset" or "disable".
 *   - Long options may use @c ++ instead of @c -- (e.g. @c ++verbose).
 *   - Whether a particular option accepts a @c + prefix is controlled by the
 *     @c allow_plus field in @c struct @c option_ex.
 *   - When a @c + prefix is used on an option whose @c flag pointer is
 *     non-NULL, the flag is cleared to 0 (instead of being set to @c val).
 */

#ifndef MIGA_GETOPT_H
#define MIGA_GETOPT_H

#include "miga/api.h"
#include "miga/migaconf.h"
#include "miga/strlist.h"
#include "miga/string_t.h"

MIGA_EXTERN_C_START

/**
 * @name Argument-requirement constants
 * Used in the @c has_arg field of @c struct @c option and @c struct
 * @c option_ex.
 * @{
 */
enum
{
    no_argument = 0,       /**< The option takes no argument. */
    required_argument = 1, /**< The option requires an argument. */
    optional_argument = 2  /**< The option accepts an optional argument
                                (must be attached: @c --opt=ARG or @c -oARG). */
};
/** @} */

/**
 * @brief Describes a single long option for getopt_long() and
 *        getopt_long_only().
 *
 * An array of these structures, terminated by an entry whose @c name is
 * @c NULL, is passed to the long-option functions.
 *
 * @par Example
 * @code
 * static struct option longopts[] = {
 *     { "verbose", no_argument,       &verbose_flag, 1   },
 *     { "output",  required_argument, NULL,          'o' },
 *     { "count",   optional_argument, NULL,          'c' },
 *     { NULL,      0,                 NULL,           0  }
 * };
 * @endcode
 */
struct option
{
    const char *name; /**< Long option name (without leading dashes). */
    int has_arg;      /**< One of @c no_argument, @c required_argument,
                           or @c optional_argument. */
    int *flag;        /**< If non-NULL, getopt_long() sets @c *flag to @c val
                           and returns 0.  If NULL, getopt_long() returns
                           @c val. */
    int val;          /**< Value to return (or to store in @c *flag). */
};

/**
 * @brief Extended option descriptor for the plus-aware long-option functions.
 *
 * This structure extends @c struct @c option with fields that control the
 * @c + prefix ("unset") behaviour used by POSIX shell builtins.
 *
 * @par Behaviour with @c + prefix
 * When the user supplies a @c + prefix (e.g. @c +v or @c ++verbose):
 *   - If @c allow_plus is 0, the option is rejected and @c '?' is returned.
 *   - If @c allow_plus is 1 and @c flag is non-NULL, @c *flag is set to 0
 *     (cleared) and the function returns 0.
 *   - If @c plus_used is non-NULL, @c *plus_used is set to 1.
 *
 * When the user supplies the normal @c - prefix:
 *   - If @c flag is non-NULL, @c *flag is set to 1 and the function returns
 *     @c val.
 *   - If @c plus_used is non-NULL, @c *plus_used is set to 0.
 *
 * @par Example
 * @code
 * int verbose_flag = 0;
 * int verbose_plus = 0;
 * static struct option_ex longopts[] = {
 *     //  name       has_arg          allow_plus flag           val  plus_used
 *     { "verbose", no_argument,       1,         &verbose_flag, 'v', &verbose_plus },
 *     { "output",  required_argument, 0,         NULL,          'o', NULL          },
 *     { NULL,      0,                 0,         NULL,           0,  NULL          }
 * };
 * @endcode
 */
struct option_ex
{
    const char *name; /**< Long option name (without leading dashes or plus signs). */
    int has_arg;      /**< One of @c no_argument, @c required_argument,
                           or @c optional_argument. */
    int allow_plus;   /**< If 1, this option may be prefixed with @c + (short)
                           or @c ++ (long) to mean "unset / disable". */
    int *flag;        /**< If non-NULL: set to @c val on @c - prefix, cleared
                           to 0 on @c + prefix.  If NULL, @c val is returned
                           directly. */
    int val;          /**< Value to store in @c *flag (or to return). */
    int *plus_used;   /**< Optional output: set to 1 if the @c + prefix was
                           used, 0 if the @c - prefix was used.  May be NULL. */
};

/*---------------------------------------------------------------------------
 * Ordering mode
 *---------------------------------------------------------------------------*/

/**
 * @brief Ordering mode for argument permutation in reentrant getopt.
 *
 * Controls how non-option arguments are handled relative to options:
 *   - @c REQUIRE_ORDER — stop processing at the first non-option argument
 *     (POSIX-conforming behaviour; also selected by a leading @c '+' in
 *     @p optstring).
 *   - @c PERMUTE — reorder @p argv so that all options come before all
 *     non-options (the GNU default).
 *   - @c RETURN_IN_ORDER — return non-option arguments as if they were
 *     arguments to option character @c \\1 (selected by a leading @c '-' in
 *     @p optstring).
 */
typedef enum
{
    REQUIRE_ORDER,
    PERMUTE,
    RETURN_IN_ORDER
} getopt_ordering_t;

/*---------------------------------------------------------------------------
 * Reentrant state
 *---------------------------------------------------------------------------*/

/**
 * @brief Per-invocation state for the reentrant getopt functions.
 *
 * Initialise with getopt_state_init() before the first call, then pass it
 * to every subsequent call in the same parsing session.
 *
 * @par Example
 * @code
 * struct getopt_state st;
 * getopt_state_init(&st);
 * int c;
 * while ((c = getopt_r_string(argv_list, optstr, &st)) != -1) {
 *     switch (c) {
 *     case 'a': ... break;
 *     case 'b': printf("arg = %s\n", st.optarg); break;
 *     }
 * }
 * @endcode
 */
struct getopt_state
{
    int optind;      /**< Index of the next argv element to process.
                          Initialise to 1 (or 0 to force full reinit). */
    int opterr;      /**< If non-zero, print diagnostics to stderr. */
    int optopt;      /**< Character that caused the most recent error. */
    char *optarg;    /**< Points to the argument of the current option,
                          or NULL if the option has no argument. */
    int initialized; /**< @private Non-zero after first call. */
    char *__nextchar;       /**< @private Pointer within current argv cluster. */
    getopt_ordering_t ordering; /**< @private Current ordering mode. */
    int first_nonopt;       /**< @private Start of non-option span. */
    int last_nonopt;        /**< @private End of non-option span. */
    int opt_plus_prefix;    /**< @private 1 if the current option used a
                                 @c + prefix. */
    int print_errors;       /**< @private Derived from @c opterr and leading
                                 @c ':' in optstring. */

    /**
     * @brief POSIX shell extension: treat a lone @c '-' as an option
     *        terminator.
     *
     * When set to 1, a bare @c '-' argument terminates option processing
     * (like @c '--') but is itself consumed rather than left as an operand.
     * This matches POSIX.1-2024 shell behaviour where "a single hyphen-minus
     * shall be treated as the first operand and then ignored."
     *
     * Defaults to 0 (standard behaviour where @c '-' is a normal operand).
     */
    int posix_hyphen;

    char **__getopt_external_argv; /**< @private Copy of argv for PERMUTE
                                        reordering. */
};

/*---------------------------------------------------------------------------
 * State initialisation
 *---------------------------------------------------------------------------*/

/**
 * @brief Initialise a @c getopt_state to its default starting values.
 *
 * Zero-fills the structure, then sets the fields needed to begin a fresh
 * parsing pass:
 *   - @c optind  = 1
 *   - @c opterr  = 1  (print diagnostics)
 *   - @c optopt  = '?'
 *
 * All private fields are zeroed.
 *
 * @param state  Pointer to the state to initialise.  Must not be NULL.
 *
 * @par Example
 * @code
 * struct getopt_state st;
 * getopt_state_init(&st);
 * @endcode
 */
MIGA_API void getopt_state_init(struct getopt_state *state);

/*---------------------------------------------------------------------------
 * Internal APIs used by Miga Shell builtins
 * Not public because of collision risk with standard getopt APIs.
 *---------------------------------------------------------------------------*/

 /* Traditional getopt APIs */
MIGA_LOCAL int getopt(int argc, char *const argv[], const char *optstring);
MIGA_LOCAL int getopt_long(int argc, char *const argv[], const char *optstring,
                           const struct option *longopts, int *longind);
MIGA_LOCAL int getopt_long_only(int argc, char *const argv[], const char *optstring,
                                const struct option *longopts, int *longind);
MIGA_LOCAL int getopt_long_plus(int argc, char *const argv[], const char *optstring,
                                const struct option_ex *longopts, int *longind);
MIGA_LOCAL int getopt_long_only_plus(int argc, char *const argv[], const char *optstring,
                                     const struct option_ex *longopts, int *longind);

/* Resets the internal state of the traditional getopt APIs */
MIGA_LOCAL void getopt_reset(void);

/* Output-only traditional global variables. Updated by the traditional getopt APIs. */
MIGA_LOCAL extern int optind;
MIGA_LOCAL extern char *optarg;
MIGA_LOCAL extern int opterr;
MIGA_LOCAL extern int optopt;

/* Reentrant versions */
MIGA_LOCAL int getopt_r(int argc, char *const argv[], const char *optstring,
                        struct getopt_state *state);
MIGA_LOCAL int getopt_long_r(int argc, char *const argv[], const char *optstring,
                             const struct option *longopts, int *longind,
                             struct getopt_state *state);
MIGA_LOCAL int getopt_long_only_r(int argc, char *const argv[], const char *optstring,
                                  const struct option *longopts, int *longind,
                                  struct getopt_state *state);
MIGA_LOCAL int getopt_long_plus_r(int argc, char *const argv[], const char *optstring,
                                  const struct option_ex *longopts, int *longind,
                                  struct getopt_state *state);
MIGA_LOCAL int getopt_long_only_plus_r(int argc, char *const argv[], const char *optstring,
                                       const struct option_ex *longopts, int *longind,
                                       struct getopt_state *state);

/*---------------------------------------------------------------------------
 * Public API — reentrant, strlist-based wrappers
 *---------------------------------------------------------------------------*/

/**
 * @brief Reentrant, strlist-based short-option parser.
 *
 * Equivalent to getopt() but uses @p state instead of global variables and
 * accepts a @c strlist_t argument vector and @c string_t option string.
 *
 * @param argv      Argument list as a strlist_t.
 * @param optstring Short-option specification string as a string_t.
 * @param state     Parsing state (initialise with getopt_state_init()).
 *
 * @return The next option character on success.
 * @retval '?'  Unrecognised option or missing required argument.
 * @retval ':'  Missing required argument (when optstring begins with ':').
 * @retval -1   All options have been parsed, or @p state is NULL.
 */
MIGA_API int getopt_r_string(const strlist_t *argv, const string_t *optstring,
                             struct getopt_state *state);

/**
 * @brief Reentrant, strlist-based long-option parser.
 *
 * @param argv      Argument list as a strlist_t.
 * @param optstring Short-option specification string as a string_t.
 * @param longopts  Array of @c struct @c option (NULL-terminated).
 * @param longind   If non-NULL, receives the matched long-option index.
 * @param state     Parsing state.
 *
 * @return Same as getopt_long().
 * @retval -1 If @p state is NULL.
 */
MIGA_API int getopt_long_r_string(const strlist_t *argv,
                                  const string_t *optstring,
                                  const struct option *longopts, int *longind,
                                  struct getopt_state *state);

/**
 * @brief Reentrant, strlist-based long-only option parser.
 *
 * Like getopt_long_r_string(), but a long option may also be introduced
 * with a single @c '-'.
 *
 * @param argv      Argument list as a strlist_t.
 * @param optstring Short-option specification string as a string_t.
 * @param longopts  Array of @c struct @c option (NULL-terminated).
 * @param longind   If non-NULL, receives the matched long-option index.
 * @param state     Parsing state.
 *
 * @return Same as getopt_long_only().
 * @retval -1 If @p state is NULL.
 */
MIGA_API int getopt_long_only_r_string(const strlist_t *argv,
                                       const string_t *optstring,
                                       const struct option *longopts,
                                       int *longind,
                                       struct getopt_state *state);

/**
 * @brief Reentrant, strlist-based long-option parser with +prefix support.
 *
 * Combines reentrant state with strlist arguments and plus-prefix semantics.
 *
 * @param argv      Argument list as a strlist_t.
 * @param optstring Short-option specification string as a string_t.
 * @param longopts  Array of @c struct @c option_ex (terminated by an entry
 *                  with @c val == 0 and @c flag == NULL).
 * @param longind   If non-NULL, receives the matched long-option index.
 * @param state     Parsing state.
 *
 * @return Same as getopt_long_plus().
 * @retval -1 If @p state is NULL.
 */
MIGA_API int getopt_long_plus_r_string(const strlist_t *argv,
                                       const string_t *optstring,
                                       const struct option_ex *longopts,
                                       int *longind,
                                       struct getopt_state *state);

/**
 * @brief Reentrant, strlist-based long-only option parser with +prefix
 *        support.
 *
 * Combines reentrant state with strlist arguments, plus-prefix semantics,
 * and single-dash long-option recognition.
 *
 * @param argv      Argument list as a strlist_t.
 * @param optstring Short-option specification string as a string_t.
 * @param longopts  Array of @c struct @c option_ex (terminated by an entry
 *                  with @c val == 0 and @c flag == NULL).
 * @param longind   If non-NULL, receives the matched long-option index.
 * @param state     Parsing state.
 *
 * @return Same as getopt_long_only_plus().
 * @retval -1 If @p state is NULL.
 */
MIGA_API int getopt_long_only_plus_r_string(const strlist_t *argv,
                                            const string_t *optstring,
                                            const struct option_ex *longopts,
                                            int *longind,
                                            struct getopt_state *state);

MIGA_EXTERN_C_END

#endif /* MIGA_GETOPT_H */

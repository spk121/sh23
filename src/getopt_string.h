/* getopt_string.h -- String-based wrappers for getopt functions
   These wrappers allow using string_list_t and string_t directly with getopt,
   which is convenient for shell builtins that already work with these types.

   This module depends on string_t and string_list_t, so it is part of the
   logic library (libmgshlogic) rather than the base library.

   Copyright (C) 2025 Michael L. Gran.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, version 2.1 or later.
*/

#ifndef SH23_GETOPT_STRING_H
#define SH23_GETOPT_STRING_H

#include "getopt.h"
#include "string_t.h"
#include "string_list.h"

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

#endif /* SH23_GETOPT_STRING_H */

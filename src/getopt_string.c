/* getopt_string.c -- String-based wrappers for getopt functions
   These wrappers allow using string_list_t and string_t directly with getopt,
   which is convenient for shell builtins that already work with these types.

   This module depends on string_t and string_list_t, so it is part of the
   logic library (libmgshlogic) rather than the base library.

   Copyright (C) 2025 Michael L. Gran.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, version 2.1 or later.
*/

#include "getopt_string.h"
#include <stdlib.h>

/* Helper to convert string_list_t to argv array
   Caller owns the returned array but NOT the strings it points to (they're
   owned by the string_list_t).
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

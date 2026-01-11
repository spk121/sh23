#ifndef BUILTINS_H
#define BUILTINS_H

#include "exec.h"
#include "string_t.h"

/**
 * Builtin command classification
 */
typedef enum
{
    BUILTIN_NONE,    /* Not a builtin - external command */
    BUILTIN_SPECIAL, /* POSIX special builtin */
    BUILTIN_REGULAR  /* Regular (non-special) builtin */
} builtin_class_t;

typedef int (*builtin_func_t)(exec_t *ex, const string_list_t *args);

/**
 * Classify a command name as a builtin type.
 *
 * @param name  The command name to classify
 * @return      BUILTIN_SPECIAL, BUILTIN_REGULAR, or BUILTIN_NONE
 */
builtin_class_t builtin_classify(const string_t *name);
builtin_class_t builtin_classify_cstr(const char *name);

bool builtin_is_special(const string_t *name);
bool builtin_is_special_cstr(const char *name);

bool builtin_is_defined(const string_t *name);
bool builtin_is_defined_cstr(const char *name);

/**
 * Get the function pointer for a builtin command.
 *
 * @param name  The command name
 * @return      The function pointer, or NULL if not a builtin
 */
builtin_func_t builtin_get_function(const string_t *name);
builtin_func_t builtin_get_function_cstr(const char *name);

/* ============================================================================
 * Builtin Commands
 * ============================================================================
 *
 * Builtin commands are implemented at the shell abstraction level and have
 * access to the full shell context. They return an exit status code.
 *
 * Return values:
 *   0 - success
 *   1 - general error
 *   2 - misuse of shell builtin
 *   other - command-specific error codes
 */

/**
 * set - Set or unset shell options and positional parameters
 *
 * @param ex The executor context
 * @param args The argument list (including "set" as args[0])
 * @return Exit status code
 */
int builtin_set(exec_t *ex, const string_list_t *args);

#endif /* BUILTINS_H */

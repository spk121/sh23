#ifndef BUILTINS_H
#define BUILTINS_H

#include "exec.h"
#include "string_t.h"

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

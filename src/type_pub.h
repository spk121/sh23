/* type_pub.h - Public type definitions

  Copyright (c) 2026 by Michael Gran.

  This file is part of Miga Shell.

  GPL3+ license. See LICENSE.txt for details.
*/
#ifndef MIGA_TYPE_PUB_H
#define MIGA_TYPE_PUB_H

#include "miga/api.h"
#include "miga/migaconf.h"
#include "miga/strlist.h"

MIGA_EXTERN_C_START

 /* This opaque type represents a full POSIX Shell execution context.  */
typedef struct miga_exec_t miga_exec_t;

/* This opaque type represents a single execution stack frame.  There are
   several kinds of frames: loops, traps, functions, subshells, brace groups,
   etc. Each frame has its own variable scope, set of traps, and so on. */
typedef struct miga_frame_t miga_frame_t;

/* This enumeration describes the outcome of an attempt to execute
   input data by the shell. */
typedef enum miga_exec_status_t
{
    MIGA_EXEC_STATUS_OK = 0,         /* Successful completion                       */
    MIGA_EXEC_STATUS_ERROR = 1,      /* Execution error                             */
    MIGA_EXEC_STATUS_NOT_IMPL = 2,   /* Feature not implemented                     */
    MIGA_EXEC_STATUS_INCOMPLETE = 3, /* Input ended but command was incomplete      */
    MIGA_EXEC_STATUS_EMPTY = 4,      /* No commands to execute (empty/comment-only) */
    MIGA_EXEC_STATUS_EXIT = 5        /* Exit requested (shell is done)              */
} miga_exec_status_t;

/* This structure describes the outcome of an attempt to execute input data
   by the shell, but, in a context where an exit code or return value is also
   relevant. */
typedef struct miga_exec_result_t
{
    miga_exec_status_t status;
    int exit_code;
} miga_exec_result_t;

/* POSIX-defined exit status values for use by builtins and the executor.
   A builtin returns one of these (or any value 0–255) from its function. */
#define EXEC_EXIT_SUCCESS 0       /* Successful completion                    */
#define EXEC_EXIT_FAILURE 1       /* General failure / catchall               */
#define EXEC_EXIT_MISUSE 2        /* Incorrect usage (bad options / arguments) */
#define EXEC_EXIT_CANNOT_EXEC 126 /* Command found but not executable         */
#define EXEC_EXIT_NOT_FOUND 127   /* Command not found                        */

/* This enumeration indicates the category of a builtin command.  POSIX
   specifies that some builtins are "special" and must be executed in the
   context of the current shell (not a subshell), while others are "regular"
   and can be executed in a subshell.  This affects how they interact with
   variables, traps, and other shell state. */
typedef enum miga_builtin_category_t
{
    MIGA_BUILTIN_CATEGORY_NONE = 0,    /**< Not a builtin (used internally) */
    MIGA_BUILTIN_CATEGORY_SPECIAL = 1, /**< POSIX special builtin           */
    MIGA_BUILTIN_CATEGORY_REGULAR = 2  /**< POSIX regular builtin           */
} miga_builtin_category_t;

/* This is the function signature of a builtin function.  The FRAME
   parameter indicates the execution frame in which the builtin is being executed,
   and the ARGS parameter is the list of arguments passed to the builtin (the first
   argument is the name of the builtin itself, by convention.)
   It returns an integer exit status where 0 indicates success, and
   non-zero indicates failure. See EXEC_EXIT_* for standard values. */
 typedef int (*miga_builtin_fn_t)(miga_frame_t *frame, strlist_t *args);

/* This set of logical flags describe the different types of expansions that
   can be performed on a string. */
typedef enum miga_expand_flags_t
{
    MIGA_EXPAND_NONE = 0,
    MIGA_EXPAND_TILDE = (1 << 0),         /* Expand tildes into directory names */
    MIGA_EXPAND_PARAMETER = (1 << 1),     /* Expand variables into their values */
    MIGA_EXPAND_COMMAND_SUBST = (1 << 2), /* Expand commands into their output */
    MIGA_EXPAND_ARITHMETIC = (1 << 3),    /* Expand arithmetic expressions into their values */
    MIGA_EXPAND_FIELD_SPLIT = (1 << 4),   /* Split fields based on IFS */
    MIGA_EXPAND_PATHNAME = (1 << 5),      /* Perform pathname expansion */

    /* Common combinations */
    MIGA_EXPAND_ALL = (MIGA_EXPAND_TILDE | MIGA_EXPAND_PARAMETER | MIGA_EXPAND_COMMAND_SUBST |
                        MIGA_EXPAND_ARITHMETIC | MIGA_EXPAND_FIELD_SPLIT | MIGA_EXPAND_PATHNAME),

    /* For assignments and redirections: no field splitting or globbing */
    MIGA_EXPAND_NO_SPLIT_GLOB = (MIGA_EXPAND_TILDE | MIGA_EXPAND_PARAMETER |
                                  MIGA_EXPAND_COMMAND_SUBST | MIGA_EXPAND_ARITHMETIC),

    /* For here-documents: parameter, command, arithmetic only */
    MIGA_EXPAND_HEREDOC =
        (MIGA_EXPAND_PARAMETER | MIGA_EXPAND_COMMAND_SUBST | MIGA_EXPAND_ARITHMETIC)
} miga_expand_flags_t;

/* This enumeration describes the result of an attempt to export a variable. */
typedef enum miga_export_status_t
{
    MIGA_EXPORT_STATUS_SUCCESS = 0,   /**< Export succeeded */
    MIGA_EXPORT_STATUS_INVALID_NAME,  /**< Invalid variable name */
    MIGA_EXPORT_STATUS_INVALID_VALUE, /**< Invalid variable value */
    MIGA_EXPORT_STATUS_READONLY,      /**< Variable is readonly */
    MIGA_EXPORT_STATUS_NOT_SUPPORTED, /**< Export not supported on platform */
    MIGA_EXPORT_STATUS_SYSTEM_ERROR   /**< System error during export */
} miga_export_status_t;

/* This enumeration describes the control flow state after executing a
   frame or command. */
typedef enum miga_frame_flow_t
{
    MIGA_FRAME_FLOW_NORMAL,   /**< Normal execution */
    MIGA_FRAME_FLOW_RETURN,   /**< 'return' executed */
    MIGA_FRAME_FLOW_BREAK,    /**< 'break' executed */
    MIGA_FRAME_FLOW_CONTINUE, /**< 'continue' executed */
    MIGA_FRAME_FLOW_TOP       /**< Unwind all frames to top level (used by 'exit') */
} miga_frame_flow_t;

/* These error codes are returned by operations on shell-defined functions. */
typedef enum miga_func_status_t
{
    MIGA_FUNC_STATUS_OK = 0,
    MIGA_FUNC_STATUS_NOT_FOUND,
    MIGA_FUNC_STATUS_EMPTY_NAME,
    MIGA_FUNC_STATUS_INVALID_NAME,
    MIGA_FUNC_STATUS_PARSE_FAILURE,
    MIGA_FUNC_STATUS_READONLY,
    MIGA_FUNC_STATUS_SYSTEM_ERROR
} miga_func_status_t;

/* These error codes are returned by shell variable operations.*/
typedef enum miga_var_status_t
{
    MIGA_VAR_STATUS_OK = 0,
    MIGA_VAR_STATUS_NOT_FOUND,
    MIGA_VAR_STATUS_READ_ONLY,
    MIGA_VAR_STATUS_EMPTY_NAME,
    MIGA_VAR_STATUS_INVALID_NAME,
} miga_var_status_t;

MIGA_EXTERN_C_END

#endif /* MIGA_TYPE_PUB_H */

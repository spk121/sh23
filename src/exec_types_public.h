#ifndef EXEC_TYPES_PUBLIC_H
#define EXEC_TYPES_PUBLIC_H

#include "string_list.h"
/* ============================================================================
 * Opaque Types
 * ============================================================================
 *
 * The concrete definitions of these structures live in exec_internal.h and are
 * not part of the public API.  All access goes through the functions declared
 * in this header and in frame.h.
 */

typedef struct exec_t exec_t;
typedef struct exec_frame_t exec_frame_t;

/* ============================================================================
 * Status / Result Types
 * ============================================================================ */

/**
 * Unified status codes returned by executor and frame operations.
 *
 * These describe the outcome of an execution attempt (success, failure,
 * incomplete input, etc.).  Frame-level control flow state (break, continue,
 * return) is represented separately by frame_control_flow_t.  EXEC_EXIT is
 * a top-level status indicating the shell has been asked to terminate.
 */
typedef enum exec_status_t
{
    EXEC_OK = 0,         /**< Successful completion                       */
    EXEC_ERROR = 1,      /**< Execution error                             */
    EXEC_NOT_IMPL = 2,   /**< Feature not implemented                     */
    EXEC_INCOMPLETE = 3, /**< Input ended but command was incomplete      */
    EXEC_EMPTY = 4,      /**< No commands to execute (empty/comment-only) */
    EXEC_EXIT = 5        /**< Exit requested (shell is done)              */
} exec_status_t;

/**
 * Result of executing a command string at top-level.
 * Bundles the execution status together with the exit code that was produced.
 */
typedef struct exec_result_t
{
    exec_status_t status;
    int exit_code;
} exec_result_t;

/* ============================================================================
 * Standard Exit Codes
 * ============================================================================
 *
 * POSIX-defined exit status values for use by builtins and the executor.
 * A builtin returns one of these (or any value 0–255) from its function.
 */

#define EXEC_EXIT_SUCCESS 0       /**< Successful completion                    */
#define EXEC_EXIT_FAILURE 1       /**< General failure / catchall               */
#define EXEC_EXIT_MISUSE 2        /**< Incorrect usage (bad options / arguments) */
#define EXEC_EXIT_CANNOT_EXEC 126 /**< Command found but not executable         */
#define EXEC_EXIT_NOT_FOUND 127   /**< Command not found                        */

/* ============================================================================
 * Builtin Types
 * ============================================================================ */

/**
 * Builtin category per POSIX XCU 2.14.
 *
 * The distinction matters for error handling and variable assignment
 * semantics.  Special builtins cause prefix assignments to persist and
 * errors in a non-interactive shell to exit; regular builtins do not.
 */
typedef enum builtin_category_t
{
    BUILTIN_NONE = 0,    /**< Not a builtin (used internally) */
    BUILTIN_SPECIAL = 1, /**< POSIX special builtin           */
    BUILTIN_REGULAR = 2  /**< POSIX regular builtin           */
} builtin_category_t;

/**
 * Builtin function signature.
 *
 * @param frame  The current execution frame.
 * @param args   Argument list (first element is the builtin name).
 * @return       Exit status (0 = success).
 */
typedef int (*builtin_fn_t)(exec_frame_t *frame, string_list_t *args);

/* ============================================================================
 * Frame Opaque Type
 * ============================================================================ */

typedef struct exec_frame_t exec_frame_t;

/* ============================================================================
 * Frame Expansion Flags
 * ============================================================================ */

typedef enum frame_expand_flags_t
{
    FRAME_EXPAND_NONE = 0,
    FRAME_EXPAND_TILDE = (1 << 0),
    FRAME_EXPAND_PARAMETER = (1 << 1),
    FRAME_EXPAND_COMMAND_SUBST = (1 << 2),
    FRAME_EXPAND_ARITHMETIC = (1 << 3),
    FRAME_EXPAND_FIELD_SPLIT = (1 << 4),
    FRAME_EXPAND_PATHNAME = (1 << 5),

    /* Common combinations */
    FRAME_EXPAND_ALL = (FRAME_EXPAND_TILDE | FRAME_EXPAND_PARAMETER | FRAME_EXPAND_COMMAND_SUBST |
                        FRAME_EXPAND_ARITHMETIC | FRAME_EXPAND_FIELD_SPLIT | FRAME_EXPAND_PATHNAME),

    /* For assignments and redirections: no field splitting or globbing */
    FRAME_EXPAND_NO_SPLIT_GLOB = (FRAME_EXPAND_TILDE | FRAME_EXPAND_PARAMETER |
                                  FRAME_EXPAND_COMMAND_SUBST | FRAME_EXPAND_ARITHMETIC),

    /* For here-documents: parameter, command, arithmetic only */
    FRAME_EXPAND_HEREDOC =
        (FRAME_EXPAND_PARAMETER | FRAME_EXPAND_COMMAND_SUBST | FRAME_EXPAND_ARITHMETIC)
} frame_expand_flags_t;

/* ============================================================================
 * Frame Error Types
 * ============================================================================ */

/**
 * Export variable status codes.
 */
typedef enum frame_export_status_t
{
    FRAME_EXPORT_SUCCESS = 0,   /**< Export succeeded */
    FRAME_EXPORT_INVALID_NAME,  /**< Invalid variable name */
    FRAME_EXPORT_INVALID_VALUE, /**< Invalid variable value */
    FRAME_EXPORT_READONLY,      /**< Variable is readonly */
    FRAME_EXPORT_NOT_SUPPORTED, /**< Export not supported on platform */
    FRAME_EXPORT_SYSTEM_ERROR   /**< System error during export */
} frame_export_status_t;

/**
 * Control flow state after executing a frame or command.
 */
typedef enum frame_control_flow_t
{
    FRAME_FLOW_NORMAL,   /**< Normal execution */
    FRAME_FLOW_RETURN,   /**< 'return' executed */
    FRAME_FLOW_BREAK,    /**< 'break' executed */
    FRAME_FLOW_CONTINUE, /**< 'continue' executed */
    FRAME_FLOW_TOP       /**< Unwind all frames to top level (used by 'exit') */
} frame_control_flow_t;

/**
 * Error codes returned by shell function operations.
 */
typedef enum frame_func_error_t
{
    FRAME_FUNC_ERROR_NONE = 0,               /**< No error */
    FRAME_FUNC_ERROR_NOT_FOUND,              /**< Function not found */
    FRAME_FUNC_ERROR_EMPTY_NAME,             /**< Function name is empty */
    FRAME_FUNC_ERROR_NAME_TOO_LONG,          /**< Function name is too long */
    FRAME_FUNC_ERROR_NAME_INVALID_CHARACTER, /**< Function name contains invalid character */
    FRAME_FUNC_ERROR_NAME_STARTS_WITH_DIGIT, /**< Function name starts with a digit */
    FRAME_FUNC_ERROR_PARSE_FAILURE,          /**< Invalid function body */
    FRAME_FUNC_ERROR_READONLY,               /**< Function is readonly and cannot be modified */
    FRAME_FUNC_ERROR_SYSTEM_ERROR            /**< System error (e.g. memory allocation failure) */
} frame_func_error_t;

/**
 * Error codes returned by variable store operations.
 */
typedef enum frame_var_error_t
{
    FRAME_VAR_ERROR_NONE = 0,               /**< Operation succeeded. */
    FRAME_VAR_ERROR_NOT_FOUND,              /**< Variable does not exist. */
    FRAME_VAR_ERROR_READ_ONLY,              /**< Variable is read-only. */
    FRAME_VAR_ERROR_EMPTY_NAME,             /**< Variable name is empty. */
    FRAME_VAR_ERROR_NAME_TOO_LONG,          /**< Variable name exceeds limits. */
    FRAME_VAR_ERROR_NAME_STARTS_WITH_DIGIT, /**< Variable name begins with a digit. */
    FRAME_VAR_ERROR_NAME_INVALID_CHARACTER, /**< Variable name contains invalid characters. */
    FRAME_VAR_ERROR_VALUE_TOO_LONG          /**< Variable value exceeds limits. */
} frame_var_error_t;

#endif

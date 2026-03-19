#ifndef LOGGING_H
#define LOGGING_H

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @file logging.h
 * @brief Logging and precondition checking utilities
 *
 * This header provides a logging system with multiple levels and precondition
 * checking macros that log failures with detailed information.
 */

/**
 * @enum LogLevel
 * @brief Enumerates available logging levels
 */
typedef enum
{
    LOG_LEVEL_DEBUG = 0, /**< Debug level - detailed diagnostic information */
    LOG_LEVEL_WARN = 1,  /**< Warning level - potential issues */
    LOG_LEVEL_ERROR = 2, /**< Error level - error conditions */
    LOG_LEVEL_FATAL = 3, /**< Fatal level - unrecoverable errors, aborts program */
    LOG_LEVEL_NONE = 4   /**< No logging - disable all output */
} LogLevel;

/**
 * @brief Initialize the logging system
 *
 * Reads the LOG_LEVEL environment variable to set the logging threshold.
 * Valid values are "DEBUG", "WARN", "ERROR", "FATAL", or "NONE".
 * Also reads LOG_ABORT_LEVEL to determine if WARN or ERROR should abort.
 * Should be called once at program startup.
 */
void log_init(void);

/**
 * Returns the current log level.
 */
LogLevel log_level(void);

/**
 * Overrides the LOG_LEVEL environment variable to set the logging
 * threshold.
 * Messages below this threshold are suppressed.
 */
void log_set_level(LogLevel lv);

/**
 * @brief Disable abort on fatal messages
 *
 * Set the abort level to LOG_LEVEL_NONE so that log_fatal calls
 * will not call abort(). Useful for testing.
 */
void log_disable_abort(void);

/**
 * @brief Set the abort level for logging
 *
 * Messages at or above this level will trigger abort().
 * Use LOG_LEVEL_NONE to disable aborting entirely.
 */
void log_set_abort_level(LogLevel lv);

/**
 * @brief Setup for fatal "try" region (internal use).
 *
 * Temporarily disables abort so that LOG_LEVEL_FATAL will longjmp
 * to the provided buffer instead of aborting. Aborting is restored
 * by log_fatal_try_end().
 *
 * Do not call directly - use log_fatal_try_begin() macro instead.
 */
void log_fatal_try_setup(jmp_buf *env);

/**
 * @brief Begin a fatal "try" region using setjmp/longjmp.
 *
 * Returns 0 on initial call, non-zero after longjmp.
 *
 * Note: This must be a macro so that setjmp is called in the caller's
 * stack frame, not in a function that returns before longjmp occurs.
 */
#define log_fatal_try_begin(env_ptr) \
    (log_fatal_try_setup(env_ptr), setjmp(*(env_ptr)))

/**
 * @brief End a fatal "try" region.
 */
void log_fatal_try_end(void);

/**
 * @brief Log a debug message
 * @param format Format string (printf-style)
 * @param ... Variable arguments for the format string
 */
void log_debug(const char *format, ...);

/**
 * @brief Log a warning message
 * @param format Format string (printf-style)
 * @param ... Variable arguments for the format string
 */
void log_warn(const char *format, ...);

/**
 * @brief Log an error message
 * @param format Format string (printf-style)
 * @param ... Variable arguments for the format string
 */
void log_error(const char *format, ...);

/**
 * @brief Log a fatal message and abort
 * @param format Format string (printf-style)
 * @param ... Variable arguments for the format string
 */
void log_fatal(const char *format, ...);

// Existing recoverable precondition helpers

#define return_if_null(ptr)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((ptr) == NULL)                                                                                             \
        {                                                                                                              \
            log_error("Precondition failed at %s:%d - %s is NULL", __func__, __LINE__, #ptr);                          \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#define return_val_if_null(ptr, val)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((ptr) == NULL)                                                                                             \
        {                                                                                                              \
            log_error("Precondition failed at %s:%d - %s is NULL", __func__, __LINE__, #ptr);                          \
            return (val);                                                                                              \
        }                                                                                                              \
    } while (0)

#define return_if(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (condition)                                                                                                 \
        {                                                                                                              \
            log_error("Precondition failed at %s:%d - %s", __func__, __LINE__, #condition);                            \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#define return_val_if(condition, val)                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if (condition)                                                                                                 \
        {                                                                                                              \
            log_error("Precondition failed at %s:%d - %s", __func__, __LINE__, #condition);                            \
            return (val);                                                                                              \
        }                                                                                                              \
    } while (0)

#define return_if_eq(a, b)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((a) == (b))                                                                                                \
        {                                                                                                              \
            _Generic((a),                                                                                              \
                char: log_error("Precondition failed at %s:%d - %s == %s (%c == %c)", __func__, __LINE__, #a, #b, (a), \
                                (b)),                                                                                  \
                short: log_error("Precondition failed at %s:%d - %s == %s (%hd == %hd)", __func__, __LINE__, #a, #b,   \
                                 (a), (b)),                                                                            \
                int: log_error("Precondition failed at %s:%d - %s == %s (%d == %d)", __func__, __LINE__, #a, #b, (a),  \
                               (b)),                                                                                   \
                long: log_error("Precondition failed at %s:%d - %s == %s (%ld == %ld)", __func__, __LINE__, #a, #b,    \
                                (a), (b)),                                                                             \
                float: log_error("Precondition failed at %s:%d - %s == %s (%f == %f)", __func__, __LINE__, #a, #b,     \
                                 (a), (b)),                                                                            \
                double: log_error("Precondition failed at %s:%d - %s == %s (%lf == %lf)", __func__, __LINE__, #a, #b,  \
                                  (a), (b)),                                                                           \
                default: log_error("Precondition failed at %s:%d - %s == %s (unknown type)", __func__, __LINE__, #a,   \
                                   #b));                                                                               \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#define return_val_if_eq(a, b, val)                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((a) == (b))                                                                                                \
        {                                                                                                              \
            _Generic((a),                                                                                              \
                char: log_error("Precondition failed at %s:%d - %s == %s (%c == %c)", __func__, __LINE__, #a, #b, (a), \
                                (b)),                                                                                  \
                short: log_error("Precondition failed at %s:%d - %s == %s (%hd == %hd)", __func__, __LINE__, #a, #b,   \
                                 (a), (b)),                                                                            \
                int: log_error("Precondition failed at %s:%d - %s == %s (%d == %d)", __func__, __LINE__, #a, #b, (a),  \
                               (b)),                                                                                   \
                long: log_error("Precondition failed at %s:%d - %s == %s (%ld == %ld)", __func__, __LINE__, #a, #b,    \
                                (a), (b)),                                                                             \
                float: log_error("Precondition failed at %s:%d - %s == %s (%f == %f)", __func__, __LINE__, #a, #b,     \
                                 (a), (b)),                                                                            \
                double: log_error("Precondition failed at %s:%d - %s == %s (%lf == %lf)", __func__, __LINE__, #a, #b,  \
                                  (a), (b)),                                                                           \
                default: log_error("Precondition failed at %s:%d - %s == %s (unknown type)", __func__, __LINE__, #a,   \
                                   #b));                                                                               \
            return (val);                                                                                              \
        }                                                                                                              \
    } while (0)

#define return_if_ne(a, b)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((a) != (b))                                                                                                \
        {                                                                                                              \
            _Generic((a),                                                                                              \
                char: log_error("Precondition failed at %s:%d - %s != %s (%c != %c)", __func__, __LINE__, #a, #b, (a), \
                                (b)),                                                                                  \
                short: log_error("Precondition failed at %s:%d - %s != %s (%hd != %hd)", __func__, __LINE__, #a, #b,   \
                                 (a), (b)),                                                                            \
                int: log_error("Precondition failed at %s:%d - %s != %s (%d != %d)", __func__, __LINE__, #a, #b, (a),  \
                               (b)),                                                                                   \
                long: log_error("Precondition failed at %s:%d - %s != %s (%ld != %ld)", __func__, __LINE__, #a, #b,    \
                                (a), (b)),                                                                             \
                float: log_error("Precondition failed at %s:%d - %s != %s (%f != %f)", __func__, __LINE__, #a, #b,     \
                                 (a), (b)),                                                                            \
                double: log_error("Precondition failed at %s:%d - %s != %s (%lf != %lf)", __func__, __LINE__, #a, #b,  \
                                  (a), (b)),                                                                           \
                default: log_error("Precondition failed at %s:%d - %s != %s (unknown type)", __func__, __LINE__, #a,   \
                                   #b));                                                                               \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#define return_val_if_ne(a, b, val)                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((a) != (b))                                                                                                \
        {                                                                                                              \
            _Generic((a),                                                                                              \
                char: log_error("Precondition failed at %s:%d - %s != %s (%c != %c)", __func__, __LINE__, #a, #b, (a), \
                                (b)),                                                                                  \
                short: log_error("Precondition failed at %s:%d - %s != %s (%hd != %hd)", __func__, __LINE__, #a, #b,   \
                                 (a), (b)),                                                                            \
                int: log_error("Precondition failed at %s:%d - %s != %s (%d != %d)", __func__, __LINE__, #a, #b, (a),  \
                               (b)),                                                                                   \
                long: log_error("Precondition failed at %s:%d - %s != %s (%ld != %ld)", __func__, __LINE__, #a, #b,    \
                                (a), (b)),                                                                             \
                float: log_error("Precondition failed at %s:%d - %s != %s (%f != %f)", __func__, __LINE__, #a, #b,     \
                                 (a), (b)),                                                                            \
                double: log_error("Precondition failed at %s:%d - %s != %s (%lf != %lf)", __func__, __LINE__, #a, #b,  \
                                  (a), (b)),                                                                           \
                default: log_error("Precondition failed at %s:%d - %s != %s (unknown type)", __func__, __LINE__, #a,   \
                                   #b));                                                                               \
            return (val);                                                                                              \
        }                                                                                                              \
    } while (0)

#define return_if_lt(a, b)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((a) < (b))                                                                                                 \
        {                                                                                                              \
            _Generic((a),                                                                                              \
                char: log_error("Precondition failed at %s:%d - %s < %s (%c < %c)", __func__, __LINE__, #a, #b, (a),   \
                                (b)),                                                                                  \
                short: log_error("Precondition failed at %s:%d - %s < %s (%hd < %hd)", __func__, __LINE__, #a, #b,     \
                                 (a), (b)),                                                                            \
                int: log_error("Precondition failed at %s:%d - %s < %s (%d < %d)", __func__, __LINE__, #a, #b, (a),    \
                               (b)),                                                                                   \
                long: log_error("Precondition failed at %s:%d - %s < %s (%ld < %ld)", __func__, __LINE__, #a, #b, (a), \
                                (b)),                                                                                  \
                float: log_error("Precondition failed at %s:%d - %s < %s (%f < %f)", __func__, __LINE__, #a, #b, (a),  \
                                 (b)),                                                                                 \
                double: log_error("Precondition failed at %s:%d - %s < %s (%lf < %lf)", __func__, __LINE__, #a, #b,    \
                                  (a), (b)),                                                                           \
                default: log_error("Precondition failed at %s:%d - %s < %s (unknown type)", __func__, __LINE__, #a,    \
                                   #b));                                                                               \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#define return_val_if_lt(a, b, val)                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((a) < (b))                                                                                                 \
        {                                                                                                              \
            _Generic((a),                                                                                              \
                char: log_error("Precondition failed at %s:%d - %s < %s (%c < %c)", __func__, __LINE__, #a, #b, (a),   \
                                (b)),                                                                                  \
                short: log_error("Precondition failed at %s:%d - %s < %s (%hd < %hd)", __func__, __LINE__, #a, #b,     \
                                 (a), (b)),                                                                            \
                int: log_error("Precondition failed at %s:%d - %s < %s (%d < %d)", __func__, __LINE__, #a, #b, (a),    \
                               (b)),                                                                                   \
                long: log_error("Precondition failed at %s:%d - %s < %s (%ld < %ld)", __func__, __LINE__, #a, #b, (a), \
                                (b)),                                                                                  \
                float: log_error("Precondition failed at %s:%d - %s < %s (%f < %f)", __func__, __LINE__, #a, #b, (a),  \
                                 (b)),                                                                                 \
                double: log_error("Precondition failed at %s:%d - %s < %s (%lf < %lf)", __func__, __LINE__, #a, #b,    \
                                  (a), (b)),                                                                           \
                default: log_error("Precondition failed at %s:%d - %s < %s (unknown type)", __func__, __LINE__, #a,    \
                                   #b));                                                                               \
            return (val);                                                                                              \
        }                                                                                                              \
    } while (0)

#define return_if_gt(a, b)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((a) > (b))                                                                                                 \
        {                                                                                                              \
            _Generic((a),                                                                                              \
                char: log_error("Precondition failed at %s:%d - %s > %s (%c > %c)", __func__, __LINE__, #a, #b, (a),   \
                                (b)),                                                                                  \
                short: log_error("Precondition failed at %s:%d - %s > %s (%hd > %hd)", __func__, __LINE__, #a, #b,     \
                                 (a), (b)),                                                                            \
                int: log_error("Precondition failed at %s:%d - %s > %s (%d > %d)", __func__, __LINE__, #a, #b, (a),    \
                               (b)),                                                                                   \
                long: log_error("Precondition failed at %s:%d - %s > %s (%ld > %ld)", __func__, __LINE__, #a, #b, (a), \
                                (b)),                                                                                  \
                float: log_error("Precondition failed at %s:%d - %s > %s (%f > %f)", __func__, __LINE__, #a, #b, (a),  \
                                 (b)),                                                                                 \
                double: log_error("Precondition failed at %s:%d - %s > %s (%lf > %lf)", __func__, __LINE__, #a, #b,    \
                                  (a), (b)),                                                                           \
                default: log_error("Precondition failed at %s:%d - %s > %s (unknown type)", __func__, __LINE__, #a,    \
                                   #b));                                                                               \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#define return_val_if_gt(a, b, val)                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((a) > (b))                                                                                                 \
        {                                                                                                              \
            _Generic((a),                                                                                              \
                char: log_error("Precondition failed at %s:%d - %s > %s (%c > %c)", __func__, __LINE__, #a, #b, (a),   \
                                (b)),                                                                                  \
                short: log_error("Precondition failed at %s:%d - %s > %s (%hd > %hd)", __func__, __LINE__, #a, #b,     \
                                 (a), (b)),                                                                            \
                int: log_error("Precondition failed at %s:%d - %s > %s (%d > %d)", __func__, __LINE__, #a, #b, (a),    \
                               (b)),                                                                                   \
                long: log_error("Precondition failed at %s:%d - %s > %s (%ld > %ld)", __func__, __LINE__, #a, #b, (a), \
                                (b)),                                                                                  \
                float: log_error("Precondition failed at %s:%d - %s > %s (%f > %f)", __func__, __LINE__, #a, #b, (a),  \
                                 (b)),                                                                                 \
                double: log_error("Precondition failed at %s:%d - %s > %s (%lf > %lf)", __func__, __LINE__, #a, #b,    \
                                  (a), (b)),                                                                           \
                default: log_error("Precondition failed at %s:%d - %s > %s (unknown type)", __func__, __LINE__, #a,    \
                                   #b));                                                                               \
            return (val);                                                                                              \
        }                                                                                                              \
    } while (0)

#define return_if_le(a, b)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((a) <= (b))                                                                                                \
        {                                                                                                              \
            _Generic((a),                                                                                              \
                char: log_error("Precondition failed at %s:%d - %s <= %s (%c <= %c)", __func__, __LINE__, #a, #b, (a), \
                                (b)),                                                                                  \
                short: log_error("Precondition failed at %s:%d - %s <= %s (%hd <= %hd)", __func__, __LINE__, #a, #b,   \
                                 (a), (b)),                                                                            \
                int: log_error("Precondition failed at %s:%d - %s <= %s (%d <= %d)", __func__, __LINE__, #a, #b, (a),  \
                               (b)),                                                                                   \
                long: log_error("Precondition failed at %s:%d - %s <= %s (%ld <= %ld)", __func__, __LINE__, #a, #b,    \
                                (a), (b)),                                                                             \
                float: log_error("Precondition failed at %s:%d - %s <= %s (%f <= %f)", __func__, __LINE__, #a, #b,     \
                                 (a), (b)),                                                                            \
                double: log_error("Precondition failed at %s:%d - %s <= %s (%lf <= %lf)", __func__, __LINE__, #a, #b,  \
                                  (a), (b)),                                                                           \
                default: log_error("Precondition failed at %s:%d - %s <= %s (unknown type)", __func__, __LINE__, #a,   \
                                   #b));                                                                               \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#define return_val_if_le(a, b, val)                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((a) <= (b))                                                                                                \
        {                                                                                                              \
            _Generic((a),                                                                                              \
                char: log_error("Precondition failed at %s:%d - %s <= %s (%c <= %c)", __func__, __LINE__, #a, #b, (a), \
                                (b)),                                                                                  \
                short: log_error("Precondition failed at %s:%d - %s <= %s (%hd <= %hd)", __func__, __LINE__, #a, #b,   \
                                 (a), (b)),                                                                            \
                int: log_error("Precondition failed at %s:%d - %s <= %s (%d <= %d)", __func__, __LINE__, #a, #b, (a),  \
                               (b)),                                                                                   \
                long: log_error("Precondition failed at %s:%d - %s <= %s (%ld <= %ld)", __func__, __LINE__, #a, #b,    \
                                (a), (b)),                                                                             \
                float: log_error("Precondition failed at %s:%d - %s <= %s (%f <= %f)", __func__, __LINE__, #a, #b,     \
                                 (a), (b)),                                                                            \
                double: log_error("Precondition failed at %s:%d - %s <= %s (%lf <= %lf)", __func__, __LINE__, #a, #b,  \
                                  (a), (b)),                                                                           \
                default: log_error("Precondition failed at %s:%d - %s <= %s (unknown type)", __func__, __LINE__, #a,   \
                                   #b));                                                                               \
            return (val);                                                                                              \
        }                                                                                                              \
    } while (0)

#define return_if_ge(a, b)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((a) >= (b))                                                                                                \
        {                                                                                                              \
            _Generic((a),                                                                                              \
                char: log_error("Precondition failed at %s:%d - %s >= %s (%c >= %c)", __func__, __LINE__, #a, #b, (a), \
                                (b)),                                                                                  \
                short: log_error("Precondition failed at %s:%d - %s >= %s (%hd >= %hd)", __func__, __LINE__, #a, #b,   \
                                 (a), (b)),                                                                            \
                int: log_error("Precondition failed at %s:%d - %s >= %s (%d >= %d)", __func__, __LINE__, #a, #b, (a),  \
                               (b)),                                                                                   \
                long: log_error("Precondition failed at %s:%d - %s >= %s (%ld >= %ld)", __func__, __LINE__, #a, #b,    \
                                (a), (b)),                                                                             \
                float: log_error("Precondition failed at %s:%d - %s >= %s (%f >= %f)", __func__, __LINE__, #a, #b,     \
                                 (a), (b)),                                                                            \
                double: log_error("Precondition failed at %s:%d - %s >= %s (%lf >= %lf)", __func__, __LINE__, #a, #b,  \
                                  (a), (b)),                                                                           \
                default: log_error("Precondition failed at %s:%d - %s >= %s (unknown type)", __func__, __LINE__, #a,   \
                                   #b));                                                                               \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#define return_val_if_ge(a, b, val)                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((a) >= (b))                                                                                                \
        {                                                                                                              \
            _Generic((a),                                                                                              \
                char: log_error("Precondition failed at %s:%d - %s >= %s (%c >= %c)", __func__, __LINE__, #a, #b, (a), \
                                (b)),                                                                                  \
                short: log_error("Precondition failed at %s:%d - %s >= %s (%hd >= %hd)", __func__, __LINE__, #a, #b,   \
                                 (a), (b)),                                                                            \
                int: log_error("Precondition failed at %s:%d - %s >= %s (%d >= %d)", __func__, __LINE__, #a, #b, (a),  \
                               (b)),                                                                                   \
                long: log_error("Precondition failed at %s:%d - %s >= %s (%ld >= %ld)", __func__, __LINE__, #a, #b,    \
                                (a), (b)),                                                                             \
                float: log_error("Precondition failed at %s:%d - %s >= %s (%f >= %f)", __func__, __LINE__, #a, #b,     \
                                 (a), (b)),                                                                            \
                double: log_error("Precondition failed at %s:%d - %s >= %s (%lf >= %lf)", __func__, __LINE__, #a, #b,  \
                                  (a), (b)),                                                                           \
                default: log_error("Precondition failed at %s:%d - %s >= %s (unknown type)", __func__, __LINE__, #a,   \
                                   #b));                                                                               \
            return (val);                                                                                              \
        }                                                                                                              \
    } while (0)

// Non-recoverable precondition macros using log_fatal

#ifdef _MSC_VER
// For MSVC, use _Analysis_assume_ to help static analysis tools stop warning about null pointers.
#define Expects_not_null(ptr)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((ptr) == NULL)                                                                                             \
        {                                                                                                              \
            log_fatal("Contract violation at %s:%d - %s is NULL", __func__, __LINE__, #ptr);\
        }                                                                                                              \
        _Analysis_assume_((ptr) != NULL);                                                          \
        _Analysis_assume_((ptr) != 0);                                                               \
    } while (0)
#else
#define Expects_not_null(ptr)                                                                      \
        do {                                                                                       \
            if ((ptr) == NULL) {                                                                     \
                log_fatal("Contract violation at %s:%d - %s is NULL", __func__, __LINE__, #ptr);   \
            } \
        } while (0)
#endif


#define Expects(condition)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            log_fatal("Contract violation at %s:%d - %s", __func__, __LINE__, #condition);                             \
        }                                                                                                              \
    } while (0)

#define Expects_eq(a, b)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((a) != (b))                                                                                                \
        {                                                                                                              \
            _Generic((a),                                                                                              \
                char: log_fatal("Contract violation at %s:%d - %s != %s (%c != %c)", __func__, __LINE__, #a, #b, (a),  \
                                (b)),                                                                                  \
                short: log_fatal("Contract violation at %s:%d - %s != %s (%hd != %hd)", __func__, __LINE__, #a, #b,    \
                                 (a), (b)),                                                                            \
                int: log_fatal("Contract violation at %s:%d - %s != %s (%d != %d)", __func__, __LINE__, #a, #b, (a),   \
                               (b)),                                                                                   \
                long: log_fatal("Contract violation at %s:%d - %s != %s (%ld != %ld)", __func__, __LINE__, #a, #b,     \
                                (a), (b)),                                                                             \
                float: log_fatal("Contract violation at %s:%d - %s != %s (%f != %f)", __func__, __LINE__, #a, #b, (a), \
                                 (b)),                                                                                 \
                double: log_fatal("Contract violation at %s:%d - %s != %s (%lf != %lf)", __func__, __LINE__, #a, #b,   \
                                  (a), (b)),                                                                           \
                default: log_fatal("Contract violation at %s:%d - %s != %s (unknown type)", __func__, __LINE__, #a,    \
                                   #b));                                                                               \
        }                                                                                                              \
    } while (0)

#define Expects_ne(a, b)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((a) == (b))                                                                                                \
        {                                                                                                              \
            _Generic((a),                                                                                              \
                char: log_fatal("Contract violation at %s:%d - %s == %s (%c == %c)", __func__, __LINE__, #a, #b, (a),  \
                                (b)),                                                                                  \
                short: log_fatal("Contract violation at %s:%d - %s == %s (%hd == %hd)", __func__, __LINE__, #a, #b,    \
                                 (a), (b)),                                                                            \
                int: log_fatal("Contract violation at %s:%d - %s == %s (%d == %d)", __func__, __LINE__, #a, #b, (a),   \
                               (b)),                                                                                   \
                long: log_fatal("Contract violation at %s:%d - %s == %s (%ld == %ld)", __func__, __LINE__, #a, #b,     \
                                (a), (b)),                                                                             \
                float: log_fatal("Contract violation at %s:%d - %s == %s (%f == %f)", __func__, __LINE__, #a, #b, (a), \
                                 (b)),                                                                                 \
                double: log_fatal("Contract violation at %s:%d - %s == %s (%lf == %lf)", __func__, __LINE__, #a, #b,   \
                                  (a), (b)),                                                                           \
                default: log_fatal("Contract violation at %s:%d - %s == %s (unknown type)", __func__, __LINE__, #a,    \
                                   #b));                                                                               \
        }                                                                                                              \
    } while (0)

#define Expects_lt(a, b)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!((a) < (b)))                                                                                              \
        {                                                                                                              \
            _Generic((a),                                                                                              \
                char: log_fatal("Contract violation at %s:%d - %s >= %s (%c >= %c)", __func__, __LINE__, #a, #b, (a),  \
                                (b)),                                                                                  \
                short: log_fatal("Contract violation at %s:%d - %s >= %s (%hd >= %hd)", __func__, __LINE__, #a, #b,    \
                                 (a), (b)),                                                                            \
                int: log_fatal("Contract violation at %s:%d - %s >= %s (%d >= %d)", __func__, __LINE__, #a, #b, (a),   \
                               (b)),                                                                                   \
                long: log_fatal("Contract violation at %s:%d - %s >= %s (%ld >= %ld)", __func__, __LINE__, #a, #b,     \
                                (a), (b)),                                                                             \
                float: log_fatal("Contract violation at %s:%d - %s >= %s (%f >= %f)", __func__, __LINE__, #a, #b, (a), \
                                 (b)),                                                                                 \
                double: log_fatal("Contract violation at %s:%d - %s >= %s (%lf >= %lf)", __func__, __LINE__, #a, #b,   \
                                  (a), (b)),                                                                           \
                default: log_fatal("Contract violation at %s:%d - %s >= %s (unknown type)", __func__, __LINE__, #a,    \
                                   #b));                                                                               \
        }                                                                                                              \
    } while (0)

#define Expects_gt(a, b)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!((a) > (b)))                                                                                              \
        {                                                                                                              \
            _Generic((a),                                                                                              \
                char: log_fatal("Contract violation at %s:%d - %s <= %s (%c <= %c)", __func__, __LINE__, #a, #b, (a),  \
                                (b)),                                                                                  \
                short: log_fatal("Contract violation at %s:%d - %s <= %s (%hd <= %hd)", __func__, __LINE__, #a, #b,    \
                                 (a), (b)),                                                                            \
                int: log_fatal("Contract violation at %s:%d - %s <= %s (%d <= %d)", __func__, __LINE__, #a, #b, (a),   \
                               (b)),                                                                                   \
                long: log_fatal("Contract violation at %s:%d - %s <= %s (%ld <= %ld)", __func__, __LINE__, #a, #b,     \
                                (a), (b)),                                                                             \
                float: log_fatal("Contract violation at %s:%d - %s <= %s (%f <= %f)", __func__, __LINE__, #a, #b, (a), \
                                 (b)),                                                                                 \
                double: log_fatal("Contract violation at %s:%d - %s <= %s (%lf <= %lf)", __func__, __LINE__, #a, #b,   \
                                  (a), (b)),                                                                           \
                default: log_fatal("Contract violation at %s:%d - %s <= %s (unknown type)", __func__, __LINE__, #a,    \
                                   #b));                                                                               \
        }                                                                                                              \
    } while (0)

#define Expects_le(a, b)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!((a) <= (b)))                                                                                             \
        {                                                                                                              \
            _Generic((a),                                                                                              \
                char: log_fatal("Contract violation at %s:%d - %s > %s (%c > %c)", __func__, __LINE__, #a, #b, (a),    \
                                (b)),                                                                                  \
                short: log_fatal("Contract violation at %s:%d - %s > %s (%hd > %hd)", __func__, __LINE__, #a, #b, (a), \
                                 (b)),                                                                                 \
                int: log_fatal("Contract violation at %s:%d - %s > %s (%d > %d)", __func__, __LINE__, #a, #b, (a),     \
                               (b)),                                                                                   \
                long: log_fatal("Contract violation at %s:%d - %s > %s (%ld > %ld)", __func__, __LINE__, #a, #b, (a),  \
                                (b)),                                                                                  \
                float: log_fatal("Contract violation at %s:%d - %s > %s (%f > %f)", __func__, __LINE__, #a, #b, (a),   \
                                 (b)),                                                                                 \
                double: log_fatal("Contract violation at %s:%d - %s > %s (%lf > %lf)", __func__, __LINE__, #a, #b,     \
                                  (a), (b)),                                                                           \
                default: log_fatal("Contract violation at %s:%d - %s > %s (unknown type)", __func__, __LINE__, #a,     \
                                   #b));                                                                               \
        }                                                                                                              \
    } while (0)

#define Expects_ge(a, b)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!((a) >= (b)))                                                                                             \
        {                                                                                                              \
            _Generic((a),                                                                                              \
                char: log_fatal("Contract violation at %s:%d - %s < %s (%c < %c)", __func__, __LINE__, #a, #b, (a),    \
                                (b)),                                                                                  \
                short: log_fatal("Contract violation at %s:%d - %s < %s (%hd < %hd)", __func__, __LINE__, #a, #b, (a), \
                                 (b)),                                                                                 \
                int: log_fatal("Contract violation at %s:%d - %s < %s (%d < %d)", __func__, __LINE__, #a, #b, (a),     \
                               (b)),                                                                                   \
                long: log_fatal("Contract violation at %s:%d - %s < %s (%ld < %ld)", __func__, __LINE__, #a, #b, (a),  \
                                (b)),                                                                                  \
                float: log_fatal("Contract violation at %s:%d - %s < %s (%f < %f)", __func__, __LINE__, #a, #b, (a),   \
                                 (b)),                                                                                 \
                double: log_fatal("Contract violation at %s:%d - %s < %s (%lf < %lf)", __func__, __LINE__, #a, #b,     \
                                  (a), (b)),                                                                           \
                default: log_fatal("Contract violation at %s:%d - %s < %s (unknown type)", __func__, __LINE__, #a,     \
                                   #b));                                                                               \
        }                                                                                                              \
    } while (0)

#endif // LOGGING_H

#include "logging.h"
#include <stdarg.h>
#include <stdlib.h>

#ifdef POSIX_API
#include <strings.h> // For strcasecmp
#elifdef UCRT_API
#include <string.h> // For _stricmp
#else
#include "lib.h" // For ascii_strcasecmp
#endif

// Constants
#define LOG_MESSAGE_BUFFER_SIZE 1024

// Define the global threshold
LogLevel g_log_threshold = LOG_LEVEL_ERROR; // Default to ERROR level

// Define the abort level
static LogLevel g_log_abort_level = LOG_LEVEL_FATAL; // Default to no aborting, except for FATAL

// Internal logging function
static void log_message(LogLevel level, const char *level_str, const char *format, va_list args)
{
    if (level < g_log_threshold)
    {
        return;
    }

    char *buffer = malloc(LOG_MESSAGE_BUFFER_SIZE);
    if (buffer == NULL)
    {
        fprintf(stderr, "[%s] Failed to allocate log buffer\n", level_str);
        fflush(stderr);
        if (level >= g_log_abort_level && level != LOG_LEVEL_NONE)
        {
            abort();
        }
        return;
    }

    int written = vsnprintf(buffer, LOG_MESSAGE_BUFFER_SIZE, format, args);
    if (written < 0)
    {
        fprintf(stderr, "[%s] Failed to format log message\n", level_str);
    }
    else if (written >= LOG_MESSAGE_BUFFER_SIZE)
    {
        fprintf(stderr, "[%s] Log message truncated (needed %d bytes)\n", level_str, written + 1);
        fprintf(stderr, "[%s] %s\n", level_str, buffer);
    }
    else
    {
        fprintf(stderr, "[%s] %s\n", level_str, buffer);
    }
    fflush(stderr);
    free(buffer);

    // Check if this level should trigger abort
    if (level >= g_log_abort_level && level != LOG_LEVEL_NONE)
    {
        abort();
    }
}

static int strcompare(const char *s1, const char *s2)
{
#ifdef POSIX_API
    return strcasecmp(s1, s2);
#elifdef UCRT_API
    return _stricmp(s1, s2);
#else
    // Fallback for ISO_C: ASCII-only case-sensitive comparison
    return ascii_strcasecmp(s1, s2);
#endif
}

// Initialization function checking environment variables
void log_init(void)
{
    // Set logging threshold
    const char *env_level = getenv("LOG_LEVEL");

    if (env_level != NULL)
    {
        if (strcompare(env_level, "DEBUG") == 0)
        {
            g_log_threshold = LOG_LEVEL_DEBUG;
        }
        else if (strcompare(env_level, "WARN") == 0)
        {
            g_log_threshold = LOG_LEVEL_WARN;
        }
        else if (strcompare(env_level, "ERROR") == 0)
        {
            g_log_threshold = LOG_LEVEL_ERROR;
        }
        else if (strcompare(env_level, "FATAL") == 0)
        {
            g_log_threshold = LOG_LEVEL_FATAL;
        }
        else if (strcompare(env_level, "NONE") == 0)
        {
            g_log_threshold = LOG_LEVEL_NONE;
        }
    }
    else
    {
        g_log_threshold = LOG_LEVEL_ERROR;
    }

    // Set abort level
    const char *env_abort_level = getenv("LOG_ABORT_LEVEL");

    if (env_abort_level != NULL)
    {
        if (strcompare(env_abort_level, "WARN") == 0)
        {
            g_log_abort_level = LOG_LEVEL_WARN;
        }
        else if (strcompare(env_abort_level, "ERROR") == 0)
        {
            g_log_abort_level = LOG_LEVEL_ERROR;
        }
        else if (strcompare(env_abort_level, "FATAL") == 0)
        {
            g_log_abort_level = LOG_LEVEL_FATAL;
        }
        else if (strcompare(env_abort_level, "NONE") == 0)
        {
            g_log_abort_level = LOG_LEVEL_NONE;
        }
    }
    else
    {
        g_log_abort_level = LOG_LEVEL_FATAL;
    }
}

LogLevel log_level(void)
{
    return g_log_threshold;
}

void log_set_level(LogLevel lv)
{
    Expects_ge(lv, LOG_LEVEL_DEBUG);
    Expects_le(lv, LOG_LEVEL_NONE);

    g_log_threshold = lv;
}

// Public logging functions
void log_debug(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_message(LOG_LEVEL_DEBUG, "DEBUG", format, args);
    va_end(args);
}

void log_warn(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_message(LOG_LEVEL_WARN, "WARN", format, args);
    va_end(args);
}

void log_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_message(LOG_LEVEL_ERROR, "ERROR", format, args);
    va_end(args);
}

void log_fatal(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_message(LOG_LEVEL_FATAL, "FATAL", format, args);
    va_end(args);
}

#ifdef EXAMPLE_USAGE
int test_function(float fvalue, double dvalue, int ivalue)
{
    char c1 = 'a', c2 = 'a';
    short s1 = 5, s2 = 10;
    long l1 = 100L, l2 = 50L;

    return_if_eq(c1, c2);              // char comparison
    return_val_if_lt(s1, s2, -1);      // short comparison
    return_if_gt(fvalue, 0.0f);        // float comparison
    return_val_if_le(dvalue, 1.0, 42); // double comparison
    return_if_ge(l1, l2);              // long comparison
    return_val_if_eq(ivalue, 5, 99);   // int comparison

    // Example usage of Expects
    Expects_not_null(&ivalue);
    Expects(ivalue > 0);
    Expects_eq(ivalue, 5);

    return 0;
}

int main(void)
{
    log_init();

    test_function(1.5f, 0.5, 5);

    return 0;
}
#endif

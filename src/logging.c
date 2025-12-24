#include <stdarg.h>
// #include <strings.h> // For strcasecmp
#include "logging.h"

// Define the global threshold
LogLevel g_log_threshold = LOG_ERROR; // Default to ERROR level

// Define the abort level
static LogLevel g_log_abort_level = LOG_NONE; // Default to no aborting

// Internal logging function
static void log_message(LogLevel level, const char *level_str, const char *format, va_list args)
{
    if (level < g_log_threshold)
    {
        return;
    }

    char *buffer = malloc(1024); // Mutable buffer for vsnprintf
    if (buffer == NULL)
    {
        fprintf(stderr, "[%s] Failed to allocate log buffer\n", level_str);
        return;
    }

    vsnprintf(buffer, 1024, format, args);

    fprintf(stderr, "[%s] %s\n", level_str, buffer);
    fflush(stderr);
    free(buffer);

    // Check if this level should trigger abort
    if (level >= g_log_abort_level && level != LOG_NONE)
    {
        abort();
    }
}

// Initialization function checking environment variables
void log_init(void)
{
    // Set logging threshold
    const char *env_level = getenv("LOG_LEVEL");
    // FIXME: on Windows, strcasecmp is _stricmp
#ifdef _WIN32
#define strcasecmp _stricmp
#endif
    if (env_level != NULL)
    {
        if (strcasecmp(env_level, "DEBUG") == 0)
        {
            g_log_threshold = LOG_DEBUG;
        }
        else if (strcasecmp(env_level, "WARN") == 0)
        {
            g_log_threshold = LOG_WARN;
        }
        else if (strcasecmp(env_level, "ERROR") == 0)
        {
            g_log_threshold = LOG_ERROR;
        }
        else if (strcasecmp(env_level, "FATAL") == 0)
        {
            g_log_threshold = LOG_FATAL;
        }
        else if (strcasecmp(env_level, "NONE") == 0)
        {
            g_log_threshold = LOG_NONE;
        }
    }
    else
    {
        // g_log_threshold = LOG_ERROR;
        g_log_threshold = LOG_DEBUG;
    }

    // Set abort level
    const char *env_abort_level = getenv("LOG_ABORT_LEVEL");

    if (env_abort_level != NULL)
    {
        if (strcasecmp(env_abort_level, "WARN") == 0)
        {
            g_log_abort_level = LOG_WARN;
        }
        else if (strcasecmp(env_abort_level, "ERROR") == 0)
        {
            g_log_abort_level = LOG_ERROR;
        }
        else if (strcasecmp(env_abort_level, "FATAL") == 0)
        {
            g_log_abort_level = LOG_FATAL;
        }
        else if (strcasecmp(env_abort_level, "NONE") == 0)
        {
            g_log_abort_level = LOG_NONE;
        }
    }
    else
    {
        g_log_abort_level = LOG_FATAL;
    }
}

LogLevel log_level(void)
{
    return g_log_threshold;
}

void log_set_level(LogLevel lv)
{
    Expects_ge(lv, LOG_DEBUG);
    Expects_le(lv, LOG_NONE);

    g_log_threshold = lv;
}

// Public logging functions
void log_debug(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_message(LOG_DEBUG, "DEBUG", format, args);
    va_end(args);
}

void log_warn(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_message(LOG_WARN, "WARN", format, args);
    va_end(args);
}

void log_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_message(LOG_ERROR, "ERROR", format, args);
    va_end(args);
}

void log_fatal(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_message(LOG_FATAL, "FATAL", format, args);
    va_end(args);
    abort(); // Ensure abort even if LOG_ABORT_LEVEL is not set
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

#define _CRT_SECURE_NO_WARNINGS

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include "builtins.h"

#include "exec_internal.h"
#include "frame.h"
#include "func_store.h"
#include "getopt.h"
#include "getopt_string.h"
#include "job_store.h"
#include "lib.h"
#include "logging.h"
#include "positional_params.h"
#include "string_list.h"
#include "string_t.h"
#include "variable_store.h"
#include "xalloc.h"
#ifdef UCRT_API
#include <direct.h>
#include <io.h>
#include <time.h>
#if defined(_WIN64)
#define _AMD64_
#elif defined(_WIN32)
#define _X86_
#endif
#include <processthreadsapi.h>   // TerminateProcess, OpenProcess, etc.
#include <synchapi.h>            // WaitForSingleObject
#include <handleapi.h>           // CloseHandle
/* Constants not always available without full Windows.h */
#ifndef INFINITE
#define INFINITE 0xFFFFFFFF
#endif
#ifndef WAIT_OBJECT_0
#define WAIT_OBJECT_0 0
#endif
#endif
#ifdef POSIX_API
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <termios.h>
#endif

typedef struct builtin_implemented_function_map_t
{
    const char *name;
    builtin_class_t class;
    builtin_func_t func;
} builtin_implemented_function_map_t;

builtin_implemented_function_map_t builtin_implemented_functions[] = {
    { "break", BUILTIN_SPECIAL, builtin_break},
    { ":", BUILTIN_SPECIAL, builtin_colon},
    { "continue", BUILTIN_SPECIAL, builtin_continue},
    { ".", BUILTIN_SPECIAL, builtin_dot},
    /* { "eval", BUILTIN_SPECIAL, builtin_eval}, */
    /* { "exec", BUILTIN_SPECIAL, builtin_exec}, */
    /* { "exit", BUILTIN_SPECIAL, builtin_exit}, */
    { "export", BUILTIN_SPECIAL, builtin_export},
    { "readonly", BUILTIN_SPECIAL, builtin_readonly},
    { "return", BUILTIN_SPECIAL, builtin_return},
    {"set", BUILTIN_SPECIAL, builtin_set},
    { "shift", BUILTIN_SPECIAL, builtin_shift},
    /* { "times", BUILTIN_SPECIAL, builtin_times}, */
    { "trap", BUILTIN_SPECIAL, builtin_trap},
    { "unset", BUILTIN_SPECIAL, builtin_unset},

#ifdef UCRT_API
    {"cd", BUILTIN_REGULAR, builtin_cd },
    {"pwd", BUILTIN_REGULAR, builtin_pwd},
#endif
    /* { "cd", BUILTIN_REGULAR, builtin_cd}, */
    /* { "pwd", BUILTIN_REGULAR, builtin_pwd}, */
    { "echo", BUILTIN_REGULAR, builtin_echo},
    { "printf", BUILTIN_REGULAR, builtin_printf},
    /* { "test", BUILTIN_REGULAR, builtin_test}, */
    { "[", BUILTIN_REGULAR, builtin_bracket},
    /* { "read", BUILTIN_REGULAR, builtin_read}, */
    /* { "alias", BUILTIN_REGULAR, builtin_alias}, */
    /* { "unalias", BUILTIN_REGULAR, builtin_unalias}, */
    /* { "type", BUILTIN_REGULAR, builtin_type}, */
    /* { "command", BUILTIN_REGULAR, builtin_command}, */
    /* { "getopts", BUILTIN_REGULAR, builtin_getopts}, */
    /* { "hash", BUILTIN_REGULAR, builtin_hash}, */
    /* { "umask", BUILTIN_REGULAR, builtin_umask}, */
    /* { "ulimit", BUILTIN_REGULAR, builtin_ulimit}, */
    { "jobs", BUILTIN_REGULAR, builtin_jobs},
    { "kill", BUILTIN_REGULAR, builtin_kill},
    { "wait", BUILTIN_REGULAR, builtin_wait},
    { "fg", BUILTIN_REGULAR, builtin_fg},
    { "bg", BUILTIN_REGULAR, builtin_bg},
#ifdef UCRT_API
    {"ls", BUILTIN_REGULAR, builtin_ls},
#endif
    { "basename", BUILTIN_REGULAR, builtin_basename},
    { "dirname", BUILTIN_REGULAR, builtin_dirname},
    { "true", BUILTIN_REGULAR, builtin_true},
    { "false", BUILTIN_REGULAR, builtin_false},
    {"mgsh_dirnamevar", BUILTIN_REGULAR, builtin_mgsh_dirnamevar},
    {"mgsh_printfvar", BUILTIN_REGULAR, builtin_mgsh_printfvar},
    {NULL, BUILTIN_NONE, NULL} // Sentinel
};

/* ============================================================================
 * colon - do nothing builtin
 * ============================================================================
 */

int builtin_colon(exec_frame_t *frame, const string_list_t *args)
{
    /* Suppress unused parameter warnings */
    (void)frame;
    (void)args;

    getopt_reset();

    /* Do nothing, return success */
    return 0;
}

/* ============================================================================
 * break - exit from a loop
 * ============================================================================
 */

int builtin_break(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    /* Parse optional loop count argument (default 1) */
    int loop_count = 1;

    if (string_list_size(args) > 1)
    {
        const string_t *arg_str = string_list_at(args, 1);
        int endpos = 0;
        long val = string_atol_at(arg_str, 0, &endpos);

        if (endpos != string_length(arg_str) || val <= 0)
        {
            frame_set_error_printf(frame, "break: argument '%s' is not a positive number", string_cstr(arg_str));
            return 2;
        }

        loop_count = (int)val;
    }

    if (string_list_size(args) > 2)
    {
        frame_set_error_printf(frame, "break: invalid number of arguments (%d)", string_list_size(args));
        return 1;
    }

    frame_set_pending_control_flow(frame, FRAME_FLOW_BREAK, loop_count - 1);

    return 0;
}

/* ============================================================================
 * continue - skip to next iteration of a loop
 * ============================================================================
 */

int builtin_continue(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    /* Parse optional loop count argument (default 1) */
    int loop_count = 1;

    if (string_list_size(args) > 1)
    {
        const string_t *arg_str = string_list_at(args, 1);
        int endpos = 0;
        long val = string_atol_at(arg_str, 0, &endpos);

        if (endpos != string_length(arg_str) || val <= 0)
        {
            frame_set_error_printf(frame, "continue: argument '%s' is not a positive number", string_cstr(arg_str));
            return 2;
        }

        loop_count = (int)val;
    }

    if (string_list_size(args) > 2)
    {
        frame_set_error_printf(frame, "continue: too many arguments (%d)", string_list_size(args));
        return 1;
    }

    frame_set_pending_control_flow(frame, FRAME_FLOW_CONTINUE, loop_count - 1);

    return 0;
}

/* ============================================================================
 * shift - remove positional parameters from the beginning
 * ============================================================================
 */

int builtin_shift(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    if (!frame_has_positional_params(frame))
    {
        /* No positional parameters to shift */
        return 0;
    }

    /* Parse optional shift count argument (default 1) */
    int shift_count = 1;

    if (string_list_size(args) > 1)
    {
        const string_t *arg_str = string_list_at(args, 1);
        int endpos = 0;
        long val = string_atol_at(arg_str, 0, &endpos);

        if (endpos != string_length(arg_str) || val < 0)
        {
            frame_set_error_printf(frame, "shift: argument '%s' is not a positive number", string_cstr(arg_str));
            return 2;
        }

        shift_count = (int)val;
    }

    if (string_list_size(args) > 2)
    {
        frame_set_error_printf(frame, "shift: too many arguments (%d)", string_list_size(args));
        return 1;
    }

    /* Verify shift count doesn't exceed parameter count */
    int param_count = frame_count_positional_params(frame);
    if (shift_count > param_count)
    {
        frame_set_error_printf(frame, "shift: shift count (%d) exceeds number of positional parameters (%d)",
                       shift_count, param_count);
        return 1;
    }

    /* Perform the shift */
    frame_shift_positional_params(frame, shift_count);

    return 0;
}

/* ============================================================================
 * dot - run file contents in current environment
 * 
 * POSIX Synopsis: . filename [argument...]
 * 
 * The dot utility shall execute commands from filename in the current 
 * environment. If filename does not contain a slash, the shell searches
 * PATH for filename.
 * 
 * If arguments are given, they become the positional parameters for the
 * duration of the sourced file execution.
 * ============================================================================
 */

/**
 * Helper: Check if a filename contains a path separator
 */
static bool dot_has_path_separator(const char *filename)
{
    return strchr(filename, '/') != NULL
#ifdef UCRT_API
           || strchr(filename, '\\') != NULL
#endif
        ;
}

/**
 * Helper: Search PATH for a file and return the full path if found
 * Returns NULL if not found. Caller must free the returned string.
 */
static string_t *dot_search_path(exec_frame_t *frame, const char *filename)
{
    string_t *path_var = frame_get_variable_cstr(frame, "PATH");
    if (!path_var || string_empty(path_var))
    {
        if (path_var)
            string_destroy(&path_var);
        return NULL;
    }

    const char *path_str = string_cstr(path_var);
    const char *start = path_str;
    const char *end;

#ifdef UCRT_API
    const char path_sep = ';';
    const char dir_sep = '\\';
#else
    const char path_sep = ':';
    const char dir_sep = '/';
#endif

    while (*start)
    {
        /* Find end of this PATH component */
        end = strchr(start, path_sep);
        if (!end)
            end = start + strlen(start);

        /* Build full path: dir + separator + filename */
        string_t *full_path = string_create();

        if (end > start)
        {
            /* Append the PATH component character by character */
            for (const char *p = start; p < end; p++)
            {
                string_append_char(full_path, *p);
            }
            /* Add directory separator if needed */
            int len = string_length(full_path);
            if (len > 0)
            {
                char last = string_at(full_path, len - 1);
                if (last != '/' && last != '\\')
                {
                    string_append_char(full_path, dir_sep);
                }
            }
        }
        else
        {
            /* Empty component means current directory */
            string_append_char(full_path, '.');
            string_append_char(full_path, dir_sep);
        }

        string_append_cstr(full_path, filename);

        /* Check if file exists and is readable */
        FILE *test_fp = fopen(string_cstr(full_path), "r");
        if (test_fp)
        {
            fclose(test_fp);
            string_destroy(&path_var);
            return full_path;
        }

        string_destroy(&full_path);

        /* Move to next PATH component */
        if (*end)
            start = end + 1;
        else
            break;
    }

    string_destroy(&path_var);
    return NULL;
}

int builtin_dot(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    int argc = string_list_size(args);

    if (argc < 2)
    {
        fprintf(stderr, "dot: filename argument required\n");
        return 2; /* misuse of shell builtin */
    }

    const char *filename = string_cstr(string_list_at(args, 1));
    string_t *resolved_path = NULL;
    FILE *fp = NULL;

    /* If filename contains no path separator, search PATH */
    if (!dot_has_path_separator(filename))
    {
        resolved_path = dot_search_path(frame, filename);
        if (resolved_path)
        {
            fp = fopen(string_cstr(resolved_path), "r");
        }
    }
    else
    {
        /* Filename has a path, open directly */
        fp = fopen(filename, "r");
    }

    if (!fp)
    {
        char cwd[1024];
        if (filename[0] == '.' && getcwd(cwd, sizeof(cwd)))
            fprintf(stderr, "dot: %s: not found relative to '%s'", filename, cwd);
        else
            fprintf(stderr, "dot: %s: not found\n", filename);
        if (resolved_path)
            string_destroy(&resolved_path);
        return 1;
    }

    /* Save current positional parameters if we have new ones */
    string_list_t *saved_params = NULL;
    if (argc > 2)
    {
        saved_params = frame_get_all_positional_params(frame);

        /* Build new positional parameters from args[2..] */
        string_list_t *new_params = string_list_create();
        for (int i = 2; i < argc; i++)
        {
            string_list_push_back(new_params, string_list_at(args, i));
        }
        frame_replace_positional_params(frame, new_params);
        string_list_destroy(&new_params);
    }

    /* Execute the file in the current environment */
    frame_exec_status_t status = frame_execute_stream(frame, fp);
    fclose(fp);

    /* Get the exit status from the last command executed */
    int exit_status = frame_get_last_exit_status(frame);

    /* Restore positional parameters if we saved them */
    if (saved_params)
    {
        frame_replace_positional_params(frame, saved_params);
        string_list_destroy(&saved_params);
    }

    if (resolved_path)
        string_destroy(&resolved_path);

    /* Return the exit status of the last command in the sourced file,
     * unless there was a framework error */
    if (status == FRAME_EXEC_ERROR && exit_status == 0)
        return 1;

    return exit_status;
}

/* ============================================================================
 * export - export variables to environment
 * ============================================================================
 */

int builtin_export(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    /* No arguments → print exported variables */
    if (string_list_size(args) == 1)
    {
        frame_print_exported_variables_in_export_format(frame);
        return 0;
    }

    int exit_status = 0;

    for (int i = 1; i < string_list_size(args); i++)
    {
        const string_t *arg = string_list_at(args, i);
        if (!arg || string_empty(arg))
        {
            frame_set_error_printf(frame, "export: invalid variable name '%s'", string_cstr(arg));
            exit_status = 2;
            continue;
        }

        int eq_pos = string_find_cstr(arg, "=");

        string_t *name = NULL;
        string_t *value = NULL;

        /* eq_pos == 0 should never happen. */
        if (eq_pos > 0)
        {
            /* VAR=value */
            name = string_substring(arg, 0, eq_pos);
            value = string_substring(arg, eq_pos + 1, string_length(arg));
        }
        else
        {
            /* VAR */
            name = string_create_from(arg);
            value = NULL;
        }

        {
            frame_export_status_t res = frame_export_variable(frame, name, value);

            /* Note: name and value are freed inside frame_export_variable if needed,
             * but we still own our copies */
            string_destroy(&name);
            if (value)
                string_destroy(&value);

            switch (res)
            {
            case FRAME_EXPORT_SUCCESS:
                break;
            case FRAME_EXPORT_INVALID_NAME:
                frame_set_error_printf(frame, "export: variable name is invalid");
                exit_status = 1;
                break;
            case FRAME_EXPORT_INVALID_VALUE:
                frame_set_error_printf(frame, "export: variable value is invalid");
                exit_status = 1;
                break;
            case FRAME_EXPORT_READONLY:
                frame_set_error_printf(frame, "export: variable is readonly, cannot change value");
                exit_status = 1;
                break;
            case FRAME_EXPORT_NOT_SUPPORTED:
                frame_set_error_printf(frame, "export: failed to export to system, not supported");
                exit_status = 1;
                break;
            case FRAME_EXPORT_SYSTEM_ERROR:
            default:
                frame_set_error_printf(frame, "export: failed to export to system");
                exit_status = 1;
                break;
            }
        }
    }

    return exit_status;
}

/* ============================================================================
 * readonly - Mark variables as read-only
 * 
 * POSIX Synopsis:
 *   readonly name[=value]...
 *   readonly -p
 * 
 * The readonly utility shall mark each specified variable as read-only.
 * If a value is specified, the variable is set to that value before being
 * marked readonly. A readonly variable cannot be unset or have its value
 * changed.
 * 
 * Options:
 *   -p    Print all readonly variables in a format that can be re-input
 * 
 * Returns:
 *   0     Success
 *   >0    An error occurred
 * ============================================================================
 */

int builtin_readonly(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    int argc = string_list_size(args);
    bool print_mode = false;
    int first_operand = 1;

    /* Parse options */
    for (int i = 1; i < argc; i++)
    {
        const char *arg = string_cstr(string_list_at(args, i));

        if (arg[0] != '-')
        {
            first_operand = i;
            break;
        }

        if (strcmp(arg, "--") == 0)
        {
            first_operand = i + 1;
            break;
        }

        /* Parse option characters */
        for (const char *p = arg + 1; *p; p++)
        {
            switch (*p)
            {
            case 'p':
                print_mode = true;
                break;
            default:
                fprintf(stderr, "readonly: -%c: invalid option\n", *p);
                fprintf(stderr, "readonly: usage: readonly [-p] [name[=value] ...]\n");
                return 2;
            }
        }

        first_operand = i + 1;
    }

    /* No arguments or -p only: print readonly variables */
    if (first_operand >= argc)
    {
        frame_print_readonly_variables(frame);
        return 0;
    }

    int exit_status = 0;

    /* Process each name[=value] argument */
    for (int i = first_operand; i < argc; i++)
    {
        const string_t *arg = string_list_at(args, i);
        if (!arg || string_empty(arg))
        {
            frame_set_error_printf(frame, "readonly: invalid variable name '%s'", 
                                  arg ? string_cstr(arg) : "");
            exit_status = 1;
            continue;
        }

        int eq_pos = string_find_cstr(arg, "=");

        string_t *name = NULL;
        string_t *value = NULL;

        if (eq_pos > 0)
        {
            /* VAR=value */
            name = string_substring(arg, 0, eq_pos);
            value = string_substring(arg, eq_pos + 1, string_length(arg));
        }
        else if (eq_pos == 0)
        {
            /* =value - invalid */
            fprintf(stderr, "readonly: '=': not a valid identifier\n");
            exit_status = 1;
            continue;
        }
        else
        {
            /* VAR only */
            name = string_create_from(arg);
            value = NULL;
        }

        /* Validate variable name */
        const char *name_cstr = string_cstr(name);
        bool valid_name = true;

        if (string_empty(name))
        {
            valid_name = false;
        }
        else
        {
            for (const char *p = name_cstr; *p && valid_name; p++)
            {
                if (p == name_cstr)
                {
                    /* First character: must be letter or underscore */
                    if (!(*p == '_' || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')))
                    {
                        valid_name = false;
                    }
                }
                else
                {
                    /* Subsequent characters: letter, digit, or underscore */
                    if (!(*p == '_' || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                          (*p >= '0' && *p <= '9')))
                    {
                        valid_name = false;
                    }
                }
            }
        }

        if (!valid_name)
        {
            fprintf(stderr, "readonly: '%s': not a valid identifier\n", name_cstr);
            string_destroy(&name);
            if (value)
                string_destroy(&value);
            exit_status = 1;
            continue;
        }

        /* Check if variable is already readonly - can't change value if so */
        if (value && frame_variable_is_readonly(frame, name))
        {
            fprintf(stderr, "readonly: %s: readonly variable\n", name_cstr);
            string_destroy(&name);
            string_destroy(&value);
            exit_status = 1;
            continue;
        }

        /* Set value if provided */
        if (value)
        {
            var_store_error_t set_result = frame_set_variable(frame, name, value);
            if (set_result != VAR_STORE_ERROR_NONE)
            {
                fprintf(stderr, "readonly: failed to set variable '%s'\n", name_cstr);
                string_destroy(&name);
                string_destroy(&value);
                exit_status = 1;
                continue;
            }
        }
        else if (!frame_has_variable(frame, name))
        {
            /* Variable doesn't exist and no value provided - create with empty value */
            string_t *empty = string_create();
            frame_set_variable(frame, name, empty);
            string_destroy(&empty);
        }

        /* Mark variable as readonly */
        var_store_error_t ro_result = frame_set_variable_readonly(frame, name, true);
        if (ro_result != VAR_STORE_ERROR_NONE)
        {
            fprintf(stderr, "readonly: failed to mark '%s' as readonly\n", name_cstr);
            exit_status = 1;
        }

        string_destroy(&name);
        if (value)
            string_destroy(&value);
    }

    return exit_status;
}


/* ============================================================================
 * trap - Set or display signal handlers
 * 
 * POSIX Synopsis:
 *   trap [action condition ...]
 *   trap
 * 
 * The trap utility shall control the execution of commands when the shell
 * receives signals or other conditions.
 * 
 * If action is absent or is '-', each condition shall be reset to its
 * default value. If action is null (''), the shell shall ignore each
 * specified condition if it arises.
 * 
 * Options:
 *   -l    List signal names (extension)
 *   -p    Print trap commands for the named signals
 * 
 * Special conditions:
 *   EXIT (or 0)    - Executed when the shell exits
 *   ERR           - Executed when a command fails (bash extension)
 *   DEBUG         - Executed before each command (bash extension)
 *   RETURN        - Executed when a function or sourced script returns
 * 
 * Returns:
 *   0     Success
 *   >0    An error occurred
 * ============================================================================
 */

/**
 * Callback for printing traps in "trap -- 'action' SIGNAL" format
 */
static void trap_print_callback(int signal_number, const string_t *action, bool is_ignored, void *context)
{
    (void)context;

    const char *sig_name = frame_trap_number_to_name(signal_number);

    if (is_ignored)
    {
        printf("trap -- '' %s\n", sig_name);
    }
    else if (action && !string_empty(action))
    {
        /* Escape single quotes in the action string */
        const char *action_str = string_cstr(action);
        printf("trap -- '");
        for (const char *p = action_str; *p; p++)
        {
            if (*p == '\'')
            {
                /* End single quote, add escaped quote, start new single quote */
                printf("'\\''");
            }
            else
            {
                putchar(*p);
            }
        }
        printf("' %s\n", sig_name);
    }
}

/**
 * Parse a signal specification (name or number)
 * Returns signal number or -1 on error
 */
static int trap_parse_signal_spec(const char *spec)
{
    if (!spec || !*spec)
        return -1;

    /* Check for numeric signal */
    char *endptr;
    long val = strtol(spec, &endptr, 10);
    if (*endptr == '\0')
    {
        /* Numeric specification */
        if (val < 0 || val >= NSIG)
            return -1;
        return (int)val;
    }

    /* Try as signal name */
    return frame_trap_name_to_number(spec);
}

int builtin_trap(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    int argc = string_list_size(args);
    bool list_signals = false;
    bool print_traps = false;
    int first_operand = 1;

    /* Parse options */
    for (int i = 1; i < argc; i++)
    {
        const char *arg = string_cstr(string_list_at(args, i));

        if (arg[0] != '-')
        {
            first_operand = i;
            break;
        }

        if (strcmp(arg, "--") == 0)
        {
            first_operand = i + 1;
            break;
        }

        /* Check for special case: "-" as action (reset to default) */
        if (strcmp(arg, "-") == 0 && i == 1)
        {
            first_operand = i;
            break;
        }

        /* Parse option characters */
        for (const char *p = arg + 1; *p; p++)
        {
            switch (*p)
            {
            case 'l':
                list_signals = true;
                break;
            case 'p':
                print_traps = true;
                break;
            default:
                fprintf(stderr, "trap: -%c: invalid option\n", *p);
                fprintf(stderr, "trap: usage: trap [-lp] [[arg] signal_spec ...]\n");
                return 2;
            }
        }

        first_operand = i + 1;
    }

    /* Handle -l option: list signal names */
    if (list_signals)
    {
        /* Print common signal names */
        printf("EXIT\n");
#ifdef SIGHUP
        printf("HUP\n");
#endif
#ifdef SIGINT
        printf("INT\n");
#endif
#ifdef SIGQUIT
        printf("QUIT\n");
#endif
#ifdef SIGILL
        printf("ILL\n");
#endif
#ifdef SIGTRAP
        printf("TRAP\n");
#endif
#ifdef SIGABRT
        printf("ABRT\n");
#endif
#ifdef SIGFPE
        printf("FPE\n");
#endif
#ifdef SIGKILL
        printf("KILL\n");
#endif
#ifdef SIGBUS
        printf("BUS\n");
#endif
#ifdef SIGSEGV
        printf("SEGV\n");
#endif
#ifdef SIGSYS
        printf("SYS\n");
#endif
#ifdef SIGPIPE
        printf("PIPE\n");
#endif
#ifdef SIGALRM
        printf("ALRM\n");
#endif
#ifdef SIGTERM
        printf("TERM\n");
#endif
#ifdef SIGUSR1
        printf("USR1\n");
#endif
#ifdef SIGUSR2
        printf("USR2\n");
#endif
#ifdef SIGCHLD
        printf("CHLD\n");
#endif
#ifdef SIGCONT
        printf("CONT\n");
#endif
#ifdef SIGSTOP
        printf("STOP\n");
#endif
#ifdef SIGTSTP
        printf("TSTP\n");
#endif
#ifdef SIGTTIN
        printf("TTIN\n");
#endif
#ifdef SIGTTOU
        printf("TTOU\n");
#endif
        return 0;
    }

    /* No arguments: print all traps */
    if (first_operand >= argc)
    {
        frame_for_each_set_trap(frame, trap_print_callback, NULL);
        return 0;
    }

    /* Handle -p option: print traps for specific signals */
    if (print_traps)
    {
        int exit_status = 0;

        for (int i = first_operand; i < argc; i++)
        {
            const char *spec = string_cstr(string_list_at(args, i));
            int signo = trap_parse_signal_spec(spec);

            if (signo < 0)
            {
                fprintf(stderr, "trap: %s: invalid signal specification\n", spec);
                exit_status = 1;
                continue;
            }

            if (signo == 0)
            {
                /* EXIT trap */
                const string_t *exit_action = frame_get_exit_trap(frame);
                if (exit_action)
                {
                    printf("trap -- '");
                    const char *action = string_cstr(exit_action);
                    for (const char *p = action; *p; p++)
                    {
                        if (*p == '\'')
                            printf("'\\''");
                        else
                            putchar(*p);
                    }
                    printf("' EXIT\n");
                }
            }
            else
            {
                bool is_ignored;
                const string_t *trap_action = frame_get_trap(frame, signo, &is_ignored);
                if (trap_action || is_ignored)
                {
                    trap_print_callback(signo, trap_action, is_ignored, NULL);
                }
            }
        }
        return exit_status;
    }

    /* Parse action and signals */
    const char *action_str = string_cstr(string_list_at(args, first_operand));
    bool is_reset = false;
    bool is_ignore = false;
    string_t *action = NULL;
    int signal_start = first_operand + 1;

    /* Check for special action values */
    if (strcmp(action_str, "-") == 0)
    {
        /* Reset to default */
        is_reset = true;
    }
    else if (action_str[0] == '\0')
    {
        /* Empty string = ignore signal */
        is_ignore = true;
    }
    else
    {
        /* Check if first argument looks like a signal name/number */
        /* If there's only one operand, it could be a signal (print trap) */
        /* If first operand is a valid signal and there are more operands, first is action */

        int test_signo = trap_parse_signal_spec(action_str);

        if (test_signo >= 0 && signal_start >= argc)
        {
            /* Single argument that's a valid signal - print that trap */
            if (test_signo == 0)
            {
                const string_t *exit_action = frame_get_exit_trap(frame);
                if (exit_action)
                {
                    printf("trap -- '");
                    const char *act = string_cstr(exit_action);
                    for (const char *p = act; *p; p++)
                    {
                        if (*p == '\'')
                            printf("'\\''");
                        else
                            putchar(*p);
                    }
                    printf("' EXIT\n");
                }
            }
            else
            {
                bool is_ignored;
                const string_t *trap_action = frame_get_trap(frame, test_signo, &is_ignored);
                if (trap_action || is_ignored)
                {
                    trap_print_callback(test_signo, trap_action, is_ignored, NULL);
                }
            }
            return 0;
        }

        /* First argument is the action command */
        action = string_create_from_cstr(action_str);
    }

    /* Process each signal specification */
    if (signal_start >= argc)
    {
        fprintf(stderr, "trap: usage: trap [-lp] [[arg] signal_spec ...]\n");
        if (action)
            string_destroy(&action);
        return 2;
    }

    int exit_status = 0;

    for (int i = signal_start; i < argc; i++)
    {
        const char *spec = string_cstr(string_list_at(args, i));
        int signo = trap_parse_signal_spec(spec);

        if (signo < 0)
        {
            fprintf(stderr, "trap: %s: invalid signal specification\n", spec);
            exit_status = 1;
            continue;
        }

        /* Check if signal is valid but unsupported */
        if (signo > 0 && frame_trap_name_is_unsupported(frame_trap_number_to_name(signo)))
        {
            fprintf(stderr, "trap: %s: signal not supported on this platform\n", spec);
            exit_status = 1;
            continue;
        }

        bool success;
        if (signo == 0)
        {
            /* EXIT trap */
            success = frame_set_exit_trap(frame, action, is_ignore, is_reset);
        }
        else
        {
            success = frame_set_trap(frame, signo, action, is_ignore, is_reset);
        }

        if (!success)
        {
            fprintf(stderr, "trap: failed to set trap for %s\n", spec);
            exit_status = 1;
        }
    }

    if (action)
        string_destroy(&action);

    return exit_status;
}


/* ============================================================================
 * set - Set or unset shell options and positional parameters
 * ============================================================================
 */

static void builtin_set_print_options(exec_frame_t *frame, bool reusable_format);

/* Valid -o/+o option arguments for the set builtin */
static const char *builtin_set_valid_o_args[] = {
    "allexport", "errexit", "ignoreeof", "monitor", "noclobber",
    "noglob", "noexec", "nounset", "pipefail", "verbose",
    "vi", "xtrace", NULL
};

/* Check if an -o argument is valid */
static bool builtin_set_is_valid_o_arg(const char *arg)
{
    if (!arg)
        return false;

    for (int i = 0; builtin_set_valid_o_args[i]; i++)
    {
        if (strcmp(arg, builtin_set_valid_o_args[i]) == 0)
            return true;
    }
    return false;
}

/* Print all shell options (set -o) */
static void builtin_set_print_options(exec_frame_t *frame, bool reusable_format)
{
    Expects_not_null(frame);

    // TODO: Print actual option values from shell
    // For now, just print the option names
    for (int i = 0; builtin_set_valid_o_args[i]; i++)
    {
        if (reusable_format)
        {
            // +o format: set -o name / set +o name
            fprintf(stdout, "set -o %s\n", builtin_set_valid_o_args[i]);
        }
        else
        {
            // -o format: name [on/off]
            fprintf(stdout, "%-12s off\n", builtin_set_valid_o_args[i]);
        }
    }
}

int builtin_set(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    // Shell option flags - initialized to -1 so we can detect "not mentioned"
    int flag_a = -1; /* allexport */
    int flag_b = -1; /* notify (job control) - not yet implemented */
    int flag_C = -1; /* noclobber */
    int flag_e = -1; /* errexit */
    int flag_f = -1; /* noglob */
    int flag_h = -1; /* remember command locations - not yet implemented */
    int flag_m = -1; /* monitor (job control) - not yet implemented */
    int flag_n = -1; /* noexec */
    int flag_u = -1; /* nounset */
    int flag_v = -1; /* verbose */
    int flag_x = -1; /* xtrace */

    /* Configure option_ex array for getopt_long_plus */
    struct option_ex long_options[] = {
        /* Short options that support both - and + with automatic flag handling */
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_a, .val = 'a'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_b, .val = 'b'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_C, .val = 'C'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_e, .val = 'e'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_f, .val = 'f'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_h, .val = 'h'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_m, .val = 'm'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_n, .val = 'n'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_u, .val = 'u'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_v, .val = 'v'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_x, .val = 'x'},

        /* 'o' needs special handling - it takes an argument */
        {.name = NULL, .has_arg = required_argument, .allow_plus = 1, .flag = NULL, .val = 'o'},

        /* Terminator */
        {0}
    };

    /* Convert string_list_t to char** for getopt */
    int argc = string_list_size(args);
    char **argv = xmalloc(argc * sizeof(char *));

    for (int i = 0; i < argc; i++)
    {
        const string_t *str = string_list_at(args, i);
        argv[i] = (char *)string_cstr(str);  // Cast away const for getopt compatibility
    }

    const char *optstring = "abCefhmnuvxo:";

    /* Track whether the user supplied "--" so we know to clear positional params */
    bool saw_double_dash = false;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--") == 0)
        {
            saw_double_dash = true;
            break;
        }
    }

    int c;
    int longind = 0;
    bool print_o_options = false;
    bool reusable_format = false;
    bool options_changed = false;

    /* Initialize getopt state for re-entrant parsing */
    struct getopt_state state = {0};
    state.optind = 1;
    state.opterr = 1;

    /* Use getopt_long_plus_r to parse options with explicit state */
    while ((c = getopt_long_plus_r(argc, argv, optstring, long_options, &longind, &state)) != -1)
    {
        switch (c)
        {
        case 0:
            /* Long option set a flag, nothing else to do */
            break;

        case 'a':
        case 'b':
        case 'C':
        case 'e':
        case 'f':
        case 'h':
        case 'm':
        case 'n':
        case 'u':
        case 'v':
        case 'x':
            /* These are already handled by option_ex - flags are automatically set/unset */
            /* getopt validated allow_plus for us */
            break;

        case 'o':
            /* -o/+o option handling */
            if (!state.optarg)
            {
                /* "set -o" or "set +o" with no argument - print options */
                print_o_options = true;
                reusable_format = state.opt_plus_prefix; /* +o uses reusable format */
                break;
            }

            if (!builtin_set_is_valid_o_arg(state.optarg))
            {
                fprintf(stderr, "set: invalid -o option: %s\n", state.optarg);
                xfree(argv);
                return 2;
            }

            /* Set or unset the named option based on prefix */
            /* state.opt_plus_prefix = true means +o (unset), false means -o (set) */
            bool value = !state.opt_plus_prefix;
            if (!frame_set_named_option_cstr(frame, state.optarg, value, state.opt_plus_prefix))
            {
                fprintf(stderr, "set: option '%s' not supported yet\n", state.optarg);
                xfree(argv);
                return 1;
            }
            options_changed = true;
            break;

        case '?':
            /* Error - getopt already printed error message */
            xfree(argv);
            return 2;

        default:
            fprintf(stderr, "set: internal error in option parsing\n");
            xfree(argv);
            return 2;
        }
    }

    /* Handle special cases: set -o or set +o */
    if (print_o_options)
    {
        builtin_set_print_options(frame, reusable_format);
        xfree(argv);
        return 0;
    }

    /* Remaining args start at state.optind */
    int new_param_count = argc - state.optind;
    bool have_positional_request = (new_param_count > 0) || saw_double_dash;

    /* Handle "set" with no options or arguments - print all variables */
    if (!have_positional_request)
    {
        /* No remaining arguments after options */
        bool any_flags = (flag_a != -1 || flag_b != -1 || flag_C != -1 || flag_e != -1 ||
                          flag_f != -1 || flag_h != -1 || flag_m != -1 || flag_n != -1 ||
                          flag_u != -1 || flag_v != -1 || flag_x != -1 || options_changed);

        if (!any_flags)
        {
            /* Pure "set" with no arguments - print all variables */
            frame_print_variables(frame, true);
            xfree(argv);
            return 0;
        }
    }

    /* Apply collected short options using frame API */
    if (flag_a != -1) { frame_set_named_option_cstr(frame, "allexport", (flag_a != 0), false); options_changed = true; }
    if (flag_C != -1) { frame_set_named_option_cstr(frame, "noclobber", (flag_C != 0), false); options_changed = true; }
    if (flag_e != -1) { frame_set_named_option_cstr(frame, "errexit", (flag_e != 0), false); options_changed = true; }
    if (flag_f != -1) { frame_set_named_option_cstr(frame, "noglob", (flag_f != 0), false); options_changed = true; }
    if (flag_n != -1) { frame_set_named_option_cstr(frame, "noexec", (flag_n != 0), false); options_changed = true; }
    if (flag_u != -1) { frame_set_named_option_cstr(frame, "nounset", (flag_u != 0), false); options_changed = true; }
    if (flag_v != -1) { frame_set_named_option_cstr(frame, "verbose", (flag_v != 0), false); options_changed = true; }
    if (flag_x != -1) { frame_set_named_option_cstr(frame, "xtrace", (flag_x != 0), false); options_changed = true; }

    /* Flags not yet implemented: b (notify), h (hashall), m (monitor) */

    /* Replace positional parameters if requested (includes explicit "set --") */
    if (have_positional_request)
    {
        /* Build a string_list_t from the new parameters */
        string_list_t *new_params = string_list_create();
        for (int i = 0; i < new_param_count; i++)
        {
            string_t *param = string_create_from_cstr(argv[state.optind + i]);
            string_list_push_back(new_params, param);
            string_destroy(&param);
        }

        /* Use frame API to replace positional parameters */
        frame_replace_positional_params(frame, new_params);
        string_list_destroy(&new_params);
    }

    xfree(argv);
    return 0;
}

/* ============================================================================
 * unset - unset values and attributes of variables and functions
 * ============================================================================
 */
int builtin_unset(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    int flag_f = 0;
    int flag_v = 0;
    int flag_err = 0;
    int err_count = 0;
    int c;
    string_t *opts = string_create_from_cstr("fv");

    while ((c = getopt_string(args, opts)) != -1)
    {
        switch (c)
        {
        case 'f':
            if (flag_v)
                flag_err++;
            else
                flag_f++;
            break;
        case 'v':
            if (flag_f)
                flag_err++;
            else
                flag_v++;
            break;
        case '?':
            fprintf(stderr, "unset: Unrecognized option: '-%c'\n", optopt);
            flag_err++;
            break;
        }
    }
    string_destroy(&opts);
    if (flag_err)
    {
        fprintf(stderr, "usage:");
        return 2;
    }
    for (; optind < string_list_size(args); optind++)
    {
        if (flag_f)
        {
            /* Unset function using frame API */
            func_store_error_t err = frame_unset_function(frame, string_list_at(args, optind));
            if (err == FUNC_STORE_ERROR_NOT_FOUND)
            {
                fprintf(stderr, "unset: function '%s' not found\n",
                        string_cstr(string_list_at(args, optind)));
                err_count++;
            }
            else if (err == FUNC_STORE_ERROR_EMPTY_NAME || err == FUNC_STORE_ERROR_NAME_TOO_LONG ||
                     err == FUNC_STORE_ERROR_NAME_INVALID_CHARACTER ||
                     err == FUNC_STORE_ERROR_NAME_STARTS_WITH_DIGIT)
            {
                fprintf(stderr, "unset: invalid function name '%s'\n",
                        string_cstr(string_list_at(args, optind)));
                err_count++;
            }
        }
        else
        {
            /* Unset variable using frame API */
            if (!frame_has_variable(frame, string_list_at(args, optind)))
            {
                fprintf(stderr, "unset: variable '%s' not found\n",
                        string_cstr(string_list_at(args, optind)));
                err_count++;
            }
            else if (frame_variable_is_readonly(frame, string_list_at(args, optind)))
            {
                fprintf(stderr, "unset: variable '%s' is read-only\n",
                        string_cstr(string_list_at(args, optind)));
                err_count++;
            }
            else
            {
                frame_unset_variable(frame, string_list_at(args, optind));
            }
        }
    }
    if (err_count > 0)
        return 1;
    return 0;
}

/* ============================================================================
 * cd - Change the shell working directory
 * ============================================================================
 */
#if defined(POSIX_API)
int builtin_cd(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    int flag_err = 0;
    int c;

    string_t *opts = string_create_from_cstr("LP");

    while ((c = getopt_string(args, opts)) != -1)
    {
        switch (c)
        {
        case 'L':
        case 'P':
            /* -L and -P options are accepted but currently ignored */
            break;
        case '?':
            fprintf(stderr, "cd: unrecognized option: '-%c'\n", optopt);
            flag_err++;
            break;
        }
    }
    string_destroy(&opts);

    if (flag_err)
    {
        fprintf(stderr, "usage: cd [-L|-P] [directory]\n");
        return 2;
    }

    const char *target_dir = NULL;

    int remaining = string_list_size(args) - optind;

    if (remaining > 1)
    {
        fprintf(stderr, "cd: too many arguments\n");
        return 1;
    }

    if (remaining == 0)
    {
        /* No argument: go to HOME */
        string_t *home = frame_get_variable_cstr(frame, "HOME");
        if (!home || string_empty(home))
        {
            if (home)
                string_destroy(&home);
            /* Try environment variable as fallback */
            const char *env_home = getenv("HOME");
            if (!env_home || env_home[0] == '\0')
            {
                fprintf(stderr, "cd: HOME not set\n");
                return 1;
            }
            target_dir = env_home;
        }
        else
        {
            target_dir = string_cstr(home);
        }
        
        /* Get current directory before changing (for OLDPWD) */
        char *old_cwd = getcwd(NULL, 0);
        if (!old_cwd)
        {
            if (home && !string_empty(home))
                string_destroy(&home);
            fprintf(stderr, "cd: cannot determine current directory: %s\n", strerror(errno));
            return 1;
        }

        /* Attempt to change directory */
        if (chdir(target_dir) != 0)
        {
            int saved_errno = errno;
            free(old_cwd);
            if (home && !string_empty(home))
                string_destroy(&home);

            switch (saved_errno)
            {
            case ENOENT:
                fprintf(stderr, "cd: %s: No such file or directory\n", target_dir);
                break;
            case EACCES:
                fprintf(stderr, "cd: %s: Permission denied\n", target_dir);
                break;
            case ENOTDIR:
                fprintf(stderr, "cd: %s: Not a directory\n", target_dir);
                break;
            default:
                fprintf(stderr, "cd: %s: %s\n", target_dir, strerror(saved_errno));
                break;
            }
            return 1;
        }

        /* Get new current directory (resolved path) */
        char *new_cwd = getcwd(NULL, 0);
        if (!new_cwd)
        {
            fprintf(stderr, "cd: warning: cannot determine new directory: %s\n", strerror(errno));
            /* Continue anyway - the chdir succeeded */
            new_cwd = xstrdup(target_dir);
        }

        /* Update OLDPWD and PWD using frame API */
        frame_set_variable_cstr(frame, "OLDPWD", old_cwd);
        frame_set_variable_cstr(frame, "PWD", new_cwd);

        free(old_cwd);
        free(new_cwd);
        if (home && !string_empty(home))
            string_destroy(&home);

        return 0;
    }
    else
    {
        const string_t *arg = string_list_at(args, optind);
        const char *arg_cstr = string_cstr(arg);

        if (strcmp(arg_cstr, "-") == 0)
        {
            /* cd - : go to OLDPWD */
            string_t *oldpwd = frame_get_variable_cstr(frame, "OLDPWD");
            if (!oldpwd || string_empty(oldpwd))
            {
                if (oldpwd)
                    string_destroy(&oldpwd);
                fprintf(stderr, "cd: OLDPWD not set\n");
                return 1;
            }
            target_dir = string_cstr(oldpwd);
            /* Print the directory when using cd - */
            printf("%s\n", target_dir);
            
            /* Get current directory before changing (for OLDPWD) */
            char *old_cwd = getcwd(NULL, 0);
            if (!old_cwd)
            {
                string_destroy(&oldpwd);
                fprintf(stderr, "cd: cannot determine current directory: %s\n", strerror(errno));
                return 1;
            }

            /* Attempt to change directory */
            if (chdir(target_dir) != 0)
            {
                int saved_errno = errno;
                free(old_cwd);
                string_destroy(&oldpwd);

                switch (saved_errno)
                {
                case ENOENT:
                    fprintf(stderr, "cd: %s: No such file or directory\n", target_dir);
                    break;
                case EACCES:
                    fprintf(stderr, "cd: %s: Permission denied\n", target_dir);
                    break;
                case ENOTDIR:
                    fprintf(stderr, "cd: %s: Not a directory\n", target_dir);
                    break;
                default:
                    fprintf(stderr, "cd: %s: %s\n", target_dir, strerror(saved_errno));
                    break;
                }
                return 1;
            }

            /* Get new current directory (resolved path) */
            char *new_cwd = getcwd(NULL, 0);
            if (!new_cwd)
            {
                fprintf(stderr, "cd: warning: cannot determine new directory: %s\n", strerror(errno));
                /* Continue anyway - the chdir succeeded */
                new_cwd = xstrdup(target_dir);
            }

            /* Update OLDPWD and PWD using frame API */
            frame_set_variable_cstr(frame, "OLDPWD", old_cwd);
            frame_set_variable_cstr(frame, "PWD", new_cwd);

            free(old_cwd);
            free(new_cwd);
            string_destroy(&oldpwd);

            return 0;
        }
        else
        {
            target_dir = arg_cstr;
            
            /* Get current directory before changing (for OLDPWD) */
            char *old_cwd = getcwd(NULL, 0);
            if (!old_cwd)
            {
                fprintf(stderr, "cd: cannot determine current directory: %s\n", strerror(errno));
                return 1;
            }

            /* Attempt to change directory */
            if (chdir(target_dir) != 0)
            {
                int saved_errno = errno;
                free(old_cwd);

                switch (saved_errno)
                {
                case ENOENT:
                    fprintf(stderr, "cd: %s: No such file or directory\n", target_dir);
                    break;
                case EACCES:
                    fprintf(stderr, "cd: %s: Permission denied\n", target_dir);
                    break;
                case ENOTDIR:
                    fprintf(stderr, "cd: %s: Not a directory\n", target_dir);
                    break;
                default:
                    fprintf(stderr, "cd: %s: %s\n", target_dir, strerror(saved_errno));
                    break;
                }
                return 1;
            }

            /* Get new current directory (resolved path) */
            char *new_cwd = getcwd(NULL, 0);
            if (!new_cwd)
            {
                fprintf(stderr, "cd: warning: cannot determine new directory: %s\n", strerror(errno));
                /* Continue anyway - the chdir succeeded */
                new_cwd = xstrdup(target_dir);
            }

            /* Update OLDPWD and PWD using frame API */
            frame_set_variable_cstr(frame, "OLDPWD", old_cwd);
            frame_set_variable_cstr(frame, "PWD", new_cwd);

            free(old_cwd);
            free(new_cwd);

            return 0;
        }
    }
}
#elif defined(UCRT_API)
int builtin_cd(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    int flag_err = 0;
    int c;

    string_t *opts = string_create_from_cstr("LP");

    while ((c = getopt_string(args, opts)) != -1)
    {
        switch (c)
        {
        case 'L':
        case 'P':
            /* Accepted but ignored on Windows - no symlink distinction */
            break;
        case '?':
            fprintf(stderr, "cd: unrecognized option: '-%c'\n", optopt);
            flag_err++;
            break;
        }
    }
    string_destroy(&opts);

    if (flag_err)
    {
        fprintf(stderr, "usage: cd [-L|-P] [directory]\n");
        return 2;
    }

    const char *target_dir = NULL;

    int remaining = string_list_size(args) - optind;

    if (remaining > 1)
    {
        fprintf(stderr, "cd: too many arguments\n");
        return 1;
    }

    if (remaining == 0)
    {
        /* No argument: go to HOME or USERPROFILE */
        string_t *home = frame_get_variable_cstr(frame, "HOME");
        if (!home || string_empty(home))
        {
            if (home)
                string_destroy(&home);
            home = frame_get_variable_cstr(frame, "USERPROFILE");
        }
        if (!home || string_empty(home))
        {
            if (home)
                string_destroy(&home);
            /* Try environment variables as fallback */
            const char *env_home = getenv("HOME");
            if (!env_home || env_home[0] == '\0')
            {
                env_home = getenv("USERPROFILE");
            }
            if (!env_home || env_home[0] == '\0')
            {
                fprintf(stderr, "cd: HOME not set\n");
                return 1;
            }
            target_dir = env_home;
        }
        else
        {
            target_dir = string_cstr(home);
        }
        
        /* Get current directory before changing (for OLDPWD) */
        char *old_cwd = _getcwd(NULL, 0);
        if (!old_cwd)
        {
            if (home && !string_empty(home))
                string_destroy(&home);
            fprintf(stderr, "cd: cannot determine current directory: %s\n", strerror(errno));
            return 1;
        }

        /* Attempt to change directory */
        if (_chdir(target_dir) != 0)
        {
            int saved_errno = errno;
            free(old_cwd);
            if (home && !string_empty(home))
                string_destroy(&home);

            switch (saved_errno)
            {
            case ENOENT:
                fprintf(stderr, "cd: %s: No such file or directory\n", target_dir);
                break;
            case EACCES:
                fprintf(stderr, "cd: %s: Permission denied\n", target_dir);
                break;
            case ENOTDIR:
                fprintf(stderr, "cd: %s: Not a directory\n", target_dir);
                break;
            default:
                fprintf(stderr, "cd: %s: %s\n", target_dir, strerror(saved_errno));
                break;
            }
            return 1;
        }

        /* Get new current directory (resolved path) */
        char *new_cwd = _getcwd(NULL, 0);
        if (!new_cwd)
        {
            fprintf(stderr, "cd: warning: cannot determine new directory: %s\n", strerror(errno));
            /* Continue anyway - the chdir succeeded */
            new_cwd = xstrdup(target_dir);
        }

        /* Update OLDPWD and PWD using frame API */
        frame_set_variable_cstr(frame, "OLDPWD", old_cwd);
        frame_set_variable_cstr(frame, "PWD", new_cwd);

        free(old_cwd);
        free(new_cwd);
        if (home && !string_empty(home))
            string_destroy(&home);

        return 0;
    }
    else
    {
        const string_t *arg = string_list_at(args, optind);
        const char *arg_cstr = string_cstr(arg);

        if (strcmp(arg_cstr, "-") == 0)
        {
            /* cd - : go to OLDPWD */
            string_t *oldpwd = frame_get_variable_cstr(frame, "OLDPWD");
            if (!oldpwd || string_empty(oldpwd))
            {
                if (oldpwd)
                    string_destroy(&oldpwd);
                fprintf(stderr, "cd: OLDPWD not set\n");
                return 1;
            }
            target_dir = string_cstr(oldpwd);
            /* Print the directory when using cd - */
            printf("%s\n", target_dir);
            
            /* Get current directory before changing (for OLDPWD) */
            char *old_cwd = _getcwd(NULL, 0);
            if (!old_cwd)
            {
                string_destroy(&oldpwd);
                fprintf(stderr, "cd: cannot determine current directory: %s\n", strerror(errno));
                return 1;
            }

            /* Attempt to change directory */
            if (_chdir(target_dir) != 0)
            {
                int saved_errno = errno;
                free(old_cwd);
                string_destroy(&oldpwd);

                switch (saved_errno)
                {
                case ENOENT:
                    fprintf(stderr, "cd: %s: No such file or directory\n", target_dir);
                    break;
                case EACCES:
                    fprintf(stderr, "cd: %s: Permission denied\n", target_dir);
                    break;
                case ENOTDIR:
                    fprintf(stderr, "cd: %s: Not a directory\n", target_dir);
                    break;
                default:
                    fprintf(stderr, "cd: %s: %s\n", target_dir, strerror(saved_errno));
                    break;
                }
                return 1;
            }

            /* Get new current directory (resolved path) */
            char *new_cwd = _getcwd(NULL, 0);
            if (!new_cwd)
            {
                fprintf(stderr, "cd: warning: cannot determine new directory: %s\n", strerror(errno));
                /* Continue anyway - the chdir succeeded */
                new_cwd = xstrdup(target_dir);
            }

            /* Update OLDPWD and PWD using frame API */
            frame_set_variable_cstr(frame, "OLDPWD", old_cwd);
            frame_set_variable_cstr(frame, "PWD", new_cwd);

            free(old_cwd);
            free(new_cwd);
            string_destroy(&oldpwd);

            return 0;
        }
        else
        {
            target_dir = arg_cstr;
            
            /* Get current directory before changing (for OLDPWD) */
            char *old_cwd = _getcwd(NULL, 0);
            if (!old_cwd)
            {
                fprintf(stderr, "cd: cannot determine current directory: %s\n", strerror(errno));
                return 1;
            }

            /* Attempt to change directory */
            if (_chdir(target_dir) != 0)
            {
                int saved_errno = errno;
                free(old_cwd);

                switch (saved_errno)
                {
                case ENOENT:
                    fprintf(stderr, "cd: %s: No such file or directory\n", target_dir);
                    break;
                case EACCES:
                    fprintf(stderr, "cd: %s: Permission denied\n", target_dir);
                    break;
                case ENOTDIR:
                    fprintf(stderr, "cd: %s: Not a directory\n", target_dir);
                    break;
                default:
                    fprintf(stderr, "cd: %s: %s\n", target_dir, strerror(saved_errno));
                    break;
                }
                return 1;
            }

            /* Get new current directory (resolved path) */
            char *new_cwd = _getcwd(NULL, 0);
            if (!new_cwd)
            {
                fprintf(stderr, "cd: warning: cannot determine new directory: %s\n", strerror(errno));
                /* Continue anyway - the chdir succeeded */
                new_cwd = xstrdup(target_dir);
            }

            /* Update OLDPWD and PWD using frame API */
            frame_set_variable_cstr(frame, "OLDPWD", old_cwd);
            frame_set_variable_cstr(frame, "PWD", new_cwd);

            free(old_cwd);
            free(new_cwd);

            return 0;
        }
    }
}
#else
int builtin_cd(exec_frame_t *frame, const string_list_t *args)
{
    (void)frame;
    (void)args;
    fprintf(stderr, "cd: not supported on this platform\n");
    return 2;
}
#endif

/* ============================================================================
 * pwd - Print working directory
 * ============================================================================
 */
#if defined(POSIX_API)
int builtin_pwd(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    int flag_L = 0;
    int flag_P = 0;
    int flag_err = 0;
    int c;

    string_t *opts = string_create_from_cstr("LP");

    while ((c = getopt_string(args, opts)) != -1)
    {
        switch (c)
        {
        case 'L':
            flag_L = 1;
            flag_P = 0;
            break;
        case 'P':
            flag_P = 1;
            flag_L = 0;
            break;
        case '?':
            fprintf(stderr, "pwd: unrecognized option: '-%c'\n", optopt);
            flag_err++;
            break;
        }
    }
    string_destroy(&opts);

    if (flag_err)
    {
        fprintf(stderr, "usage: pwd [-L|-P]\n");
        return 2;
    }

    if (optind < string_list_size(args))
    {
        fprintf(stderr, "pwd: too many arguments\n");
        return 1;
    }

    if (flag_L)
    {
        /* Logical mode: use PWD if it's set and valid */
        string_t *pwd_var = frame_get_variable_cstr(frame, "PWD");
        if (pwd_var && !string_empty(pwd_var))
        {
            printf("%s\n", string_cstr(pwd_var));
            string_destroy(&pwd_var);
            return 0;
        }
        if (pwd_var)
            string_destroy(&pwd_var);
    }

    /* Physical mode (default) or PWD not available: use getcwd */
    char *cwd = getcwd(NULL, 0);
    if (!cwd)
    {
        fprintf(stderr, "pwd: cannot determine current directory: %s\n", strerror(errno));
        return 1;
    }

    printf("%s\n", cwd);
    free(cwd);
    return 0;
}
#elif defined(UCRT_API)
int builtin_pwd(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    int flag_L = 0;
    int flag_P = 0;
    int flag_err = 0;
    int c;

    string_t *opts = string_create_from_cstr("LP");

    while ((c = getopt_string(args, opts)) != -1)
    {
        switch (c)
        {
        case 'L':
            flag_L = 1;
            flag_P = 0;
            break;
        case 'P':
            flag_P = 1;
            flag_L = 0;
            break;
        case '?':
            fprintf(stderr, "pwd: unrecognized option: '-%c'\n", optopt);
            flag_err++;
            break;
        }
    }
    string_destroy(&opts);

    if (flag_err)
    {
        fprintf(stderr, "usage: pwd [-L|-P]\n");
        return 2;
    }

    if (optind < string_list_size(args))
    {
        fprintf(stderr, "pwd: too many arguments\n");
        return 1;
    }

    if (flag_L)
    {
        /*
         * Logical mode: use PWD if it's set and valid.
         * "Valid" means it refers to the same directory as the actual cwd.
         * On Windows without Windows API, we can't easily verify this,
         * so we just check that PWD is set and non-empty, then trust it.
         */
        string_t *pwd_var = frame_get_variable_cstr(frame, "PWD");
        if (pwd_var && !string_empty(pwd_var))
        {
            printf("%s\n", string_cstr(pwd_var));
            string_destroy(&pwd_var);
            return 0;
        }
        if (pwd_var)
            string_destroy(&pwd_var);
    }

    /* Physical mode (default) or PWD not available: use _getcwd */
    char *cwd = _getcwd(NULL, 0);
    if (!cwd)
    {
        fprintf(stderr, "pwd: cannot determine current directory: %s\n", strerror(errno));
        return 1;
    }

    printf("%s\n", cwd);
    free(cwd);
    return 0;
}
#else
int builtin_pwd(exec_frame_t *frame, const string_list_t *args)
{
    (void)frame;
    (void)args;
    fprintf(stderr, "pwd: not supported on this platform\n");
    return 2;
}
#endif

/* ============================================================================
 * jobs - Job control builtins
 * ============================================================================
 */

int builtin_jobs(exec_frame_t *frame, const string_list_t *args)
{
    getopt_reset();

    if (!frame)
        return 1;

    /* Check if there are any jobs */
    if (!frame_has_jobs(frame))
    {
        /* No jobs - nothing to show */
        return 0;
    }

    frame_jobs_format_t format = FRAME_JOBS_FORMAT_DEFAULT;
    int first_operand = 1; /* Index of first non-option argument */
    int exit_status = 0;

    /* Parse options */
    int argc = string_list_size(args);
    for (int i = 1; i < argc; i++)
    {
        const char *arg = string_cstr(string_list_at(args, i));

        if (arg[0] != '-')
        {
            first_operand = i;
            break;
        }

        if (strcmp(arg, "--") == 0)
        {
            first_operand = i + 1;
            break;
        }

        /* Parse option characters */
        for (const char *p = arg + 1; *p; p++)
        {
            switch (*p)
            {
            case 'l':
                format = FRAME_JOBS_FORMAT_LONG;
                break;
            case 'p':
                format = FRAME_JOBS_FORMAT_PID_ONLY;
                break;
            default:
                fprintf(stderr, "jobs: -%c: invalid option\n", *p);
                fprintf(stderr, "jobs: usage: jobs [-lp] [job_id ...]\n");
                return 2;
            }
        }

        first_operand = i + 1;
    }

    /* If specific job_ids are given, show only those */
    if (first_operand < argc)
    {
        for (int i = first_operand; i < argc; i++)
        {
            const string_t *arg_str = string_list_at(args, i);
            int job_id = frame_parse_job_id(frame, arg_str);

            if (job_id < 0)
            {
                fprintf(stderr, "jobs: %s: no such job\n", string_cstr(arg_str));
                exit_status = 1;
                continue;
            }

            if (!frame_print_job_by_id(frame, job_id, format))
            {
                fprintf(stderr, "jobs: %s: no such job\n", string_cstr(arg_str));
                exit_status = 1;
            }
        }
    }
    else
    {
        /* No job_ids specified - show all jobs */
        frame_print_all_jobs(frame, format);
    }

    return exit_status;
}

/* ============================================================================
 * kill - Send a signal to a job or process
 * 
 * POSIX Synopsis:
 *   kill -s signal_name pid ...
 *   kill -l [exit_status]
 *   kill [-signal_name] pid ...
 *   kill [-signal_number] pid ...
 * 
 * Options:
 *   -l           List signal names
 *   -s signame   Specify signal by name (e.g., TERM, KILL)
 *   -signal      Specify signal by name or number (e.g., -TERM, -9)
 * 
 * Arguments:
 *   pid          Process ID or job specification (%n, %%, etc.)
 * 
 * Default signal is SIGTERM.
 * ============================================================================
 */

/* Signal name to number mapping */
typedef struct kill_signal_map_t
{
    const char *name;
    int number;
} kill_signal_map_t;

/* Standard POSIX signals */
static const kill_signal_map_t kill_signal_table[] = {
#ifdef SIGHUP
    {"HUP", SIGHUP},
    {"SIGHUP", SIGHUP},
#endif
#ifdef SIGINT
    {"INT", SIGINT},
    {"SIGINT", SIGINT},
#endif
#ifdef SIGQUIT
    {"QUIT", SIGQUIT},
    {"SIGQUIT", SIGQUIT},
#endif
#ifdef SIGILL
    {"ILL", SIGILL},
    {"SIGILL", SIGILL},
#endif
#ifdef SIGTRAP
    {"TRAP", SIGTRAP},
    {"SIGTRAP", SIGTRAP},
#endif
#ifdef SIGABRT
    {"ABRT", SIGABRT},
    {"SIGABRT", SIGABRT},
#endif
#ifdef SIGFPE
    {"FPE", SIGFPE},
    {"SIGFPE", SIGFPE},
#endif
#ifdef SIGKILL
    {"KILL", SIGKILL},
    {"SIGKILL", SIGKILL},
#endif
#ifdef SIGBUS
    {"BUS", SIGBUS},
    {"SIGBUS", SIGBUS},
#endif
#ifdef SIGSEGV
    {"SEGV", SIGSEGV},
    {"SIGSEGV", SIGSEGV},
#endif
#ifdef SIGSYS
    {"SYS", SIGSYS},
    {"SIGSYS", SIGSYS},
#endif
#ifdef SIGPIPE
    {"PIPE", SIGPIPE},
    {"SIGPIPE", SIGPIPE},
#endif
#ifdef SIGALRM
    {"ALRM", SIGALRM},
    {"SIGALRM", SIGALRM},
#endif
#ifdef SIGTERM
    {"TERM", SIGTERM},
    {"SIGTERM", SIGTERM},
#endif
#ifdef SIGUSR1
    {"USR1", SIGUSR1},
    {"SIGUSR1", SIGUSR1},
#endif
#ifdef SIGUSR2
    {"USR2", SIGUSR2},
    {"SIGUSR2", SIGUSR2},
#endif
#ifdef SIGCHLD
    {"CHLD", SIGCHLD},
    {"SIGCHLD", SIGCHLD},
#endif
#ifdef SIGCONT
    {"CONT", SIGCONT},
    {"SIGCONT", SIGCONT},
#endif
#ifdef SIGSTOP
    {"STOP", SIGSTOP},
    {"SIGSTOP", SIGSTOP},
#endif
#ifdef SIGTSTP
    {"TSTP", SIGTSTP},
    {"SIGTSTP", SIGTSTP},
#endif
#ifdef SIGTTIN
    {"TTIN", SIGTTIN},
    {"SIGTTIN", SIGTTIN},
#endif
#ifdef SIGTTOU
    {"TTOU", SIGTTOU},
    {"SIGTTOU", SIGTTOU},
#endif
#ifdef SIGURG
    {"URG", SIGURG},
    {"SIGURG", SIGURG},
#endif
#ifdef SIGXCPU
    {"XCPU", SIGXCPU},
    {"SIGXCPU", SIGXCPU},
#endif
#ifdef SIGXFSZ
    {"XFSZ", SIGXFSZ},
    {"SIGXFSZ", SIGXFSZ},
#endif
#ifdef SIGVTALRM
    {"VTALRM", SIGVTALRM},
    {"SIGVTALRM", SIGVTALRM},
#endif
#ifdef SIGPROF
    {"PROF", SIGPROF},
    {"SIGPROF", SIGPROF},
#endif
#ifdef SIGWINCH
    {"WINCH", SIGWINCH},
    {"SIGWINCH", SIGWINCH},
#endif
#ifdef SIGIO
    {"IO", SIGIO},
    {"SIGIO", SIGIO},
#endif
    {NULL, 0}
};

/**
 * Convert signal name to number.
 * Returns -1 if not found.
 */
static int kill_signal_name_to_number(const char *name)
{
    /* Check for numeric signal */
    char *endptr;
    long val = strtol(name, &endptr, 10);
    if (*endptr == '\0' && val >= 0 && val < NSIG)
    {
        return (int)val;
    }

    /* Look up by name */
    for (const kill_signal_map_t *p = kill_signal_table; p->name != NULL; p++)
    {
        if (ascii_strcasecmp(name, p->name) == 0)
        {
            return p->number;
        }
    }

    return -1;
}

/**
 * Convert signal number to name (without SIG prefix).
 * Returns NULL if not found.
 */
static const char *kill_signal_number_to_name(int signum)
{
    for (const kill_signal_map_t *p = kill_signal_table; p->name != NULL; p++)
    {
        if (p->number == signum && strncmp(p->name, "SIG", 3) != 0)
        {
            return p->name;
        }
    }
    return NULL;
}

/**
 * Print list of signal names.
 */
static void kill_list_signals(void)
{
    int col = 0;
    for (const kill_signal_map_t *p = kill_signal_table; p->name != NULL; p++)
    {
        /* Only print short names (without SIG prefix) */
        if (strncmp(p->name, "SIG", 3) != 0)
        {
            printf("%2d) %-10s", p->number, p->name);
            col++;
            if (col >= 5)
            {
                printf("\n");
                col = 0;
            }
        }
    }
    if (col > 0)
    {
        printf("\n");
    }
}

/**
 * Convert exit status to signal name (for -l option with argument).
 */
static void kill_list_signal_for_status(int status)
{
    /* If status > 128, it's 128 + signal_number */
    int signum = (status > 128) ? (status - 128) : status;
    const char *name = kill_signal_number_to_name(signum);
    if (name)
    {
        printf("%s\n", name);
    }
    else
    {
        printf("%d\n", signum);
    }
}

/**
 * Send a signal to a process.
 * Returns 0 on success, non-zero on failure.
 */
static int kill_send_signal(exec_frame_t *frame, int signum, intptr_t pid, const char *target_str)
{
#ifdef POSIX_API
    (void)frame;
    if (kill((pid_t)pid, signum) != 0)
    {
        fprintf(stderr, "kill: (%ld) - %s\n", (long)pid, strerror(errno));
        return 1;
    }
    return 0;
#elifdef UCRT_API
    /* On Windows, we can only terminate processes, not send arbitrary signals */
    if (signum == SIGTERM
#ifdef SIGKILL
        || signum == SIGKILL
#endif
#ifdef SIGINT
        || signum == SIGINT
#endif
    )
    {
        /* Search for process handle in job store using iterator */
        if (frame && frame->executor && frame->executor->jobs)
        {
            job_process_iterator_t iter = job_store_active_processes_begin(frame->executor->jobs);
            while (job_store_active_processes_next(&iter))
            {
                if (job_store_iter_get_pid(&iter) == pid)
                {
                    uintptr_t handle = (uintptr_t)job_store_iter_get_handle(&iter);
                    if (handle != 0)
                    {
                        if (TerminateProcess((HANDLE)handle, 1))
                        {
                            /* Update job state */
                            job_store_iter_set_state(&iter, JOB_TERMINATED, 128 + signum);
                            return 0;
                        }
                        else
                        {
                            fprintf(stderr, "kill: (%ld) - failed to terminate process\n", (long)pid);
                            return 1;
                        }
                    }
                }
            }
        }

        /* Process not found in job store - try to open it directly */
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
        if (hProcess != NULL)
        {
            BOOL result = TerminateProcess(hProcess, 1);
            CloseHandle(hProcess);
            if (result)
            {
                return 0;
            }
        }

        fprintf(stderr, "kill: (%ld) - No such process or access denied\n", (long)pid);
        return 1;
    }
    else
    {
        fprintf(stderr, "kill: signal %d not supported on this platform\n", signum);
        return 1;
    }
#else
    (void)frame;
    (void)signum;
    (void)pid;
    (void)target_str;
    fprintf(stderr, "kill: not supported on this platform\n");
    return 1;
#endif
}

/**
 * Send a signal to all processes in a job.
 * Returns 0 on success, non-zero on failure.
 */
static int kill_send_to_job(exec_frame_t *frame, int signum, int job_id, const char *target_str)
{
    if (!frame || !frame->executor || !frame->executor->jobs)
    {
        fprintf(stderr, "kill: %s: no such job\n", target_str);
        return 1;
    }

    job_t *job = job_store_find(frame->executor->jobs, job_id);
    if (!job)
    {
        fprintf(stderr, "kill: %s: no such job\n", target_str);
        return 1;
    }

#ifdef POSIX_API
    /* Send signal to the process group */
    if (kill(-job->pgid, signum) != 0)
    {
        fprintf(stderr, "kill: %s - %s\n", target_str, strerror(errno));
        return 1;
    }
    return 0;
#elifdef UCRT_API
    /* On Windows, iterate through processes in the job using the iterator */
    if (signum != SIGTERM
#ifdef SIGKILL
        && signum != SIGKILL
#endif
#ifdef SIGINT
        && signum != SIGINT
#endif
    )
    {
        fprintf(stderr, "kill: signal %d not supported on this platform\n", signum);
        return 1;
    }

    int errors = 0;
    int terminated = 0;

    /* Use iterator to find processes belonging to this job */
    job_process_iterator_t iter = job_store_active_processes_begin(frame->executor->jobs);
    while (job_store_active_processes_next(&iter))
    {
        if (job_store_iter_get_job_id(&iter) == job_id)
        {
            uintptr_t handle = (uintptr_t)job_store_iter_get_handle(&iter);
            intptr_t pid = job_store_iter_get_pid(&iter);

            if (handle != 0)
            {
                if (TerminateProcess((HANDLE)handle, 1))
                {
                    job_store_iter_set_state(&iter, JOB_TERMINATED, 128 + signum);
                    terminated++;
                }
                else
                {
                    fprintf(stderr, "kill: %s (pid %ld) - failed to terminate process\n", 
                            target_str, (long)pid);
                    errors++;
                }
            }
            else
            {
                fprintf(stderr, "kill: %s (pid %ld) - no handle available\n", 
                        target_str, (long)pid);
                errors++;
            }
        }
    }

    if (terminated == 0 && errors == 0)
    {
        fprintf(stderr, "kill: %s: no active processes in job\n", target_str);
        return 1;
    }

    return errors > 0 ? 1 : 0;
#else
    /* Send to each process in the job */
    int errors = 0;
    size_t proc_count = job_process_count(job);
    for (size_t i = 0; i < proc_count; i++)
    {
        intptr_t pid = job_get_process_pid(job, i);
        if (pid > 0)
        {
            errors += kill_send_signal(frame, signum, pid, target_str);
        }
    }
    return errors > 0 ? 1 : 0;
#endif
}

int builtin_kill(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    int argc = string_list_size(args);

    if (argc < 2)
    {
        fprintf(stderr, "kill: usage: kill [-s sigspec | -sigspec] pid | jobspec ...\n");
        fprintf(stderr, "       kill -l [sigspec]\n");
        return 2;
    }

    int signal_num = SIGTERM;  /* Default signal */
    int first_operand = 1;
    bool list_signals = false;
    int exit_status = 0;

    /* Parse options */
    for (int i = 1; i < argc; i++)
    {
        const char *arg = string_cstr(string_list_at(args, i));

        /* Check for -l (list signals) */
        if (strcmp(arg, "-l") == 0 || strcmp(arg, "-L") == 0)
        {
            list_signals = true;
            first_operand = i + 1;
            break;
        }

        /* Check for -s signame */
        if (strcmp(arg, "-s") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "kill: -s requires an argument\n");
                return 2;
            }
            const char *signame = string_cstr(string_list_at(args, i + 1));
            signal_num = kill_signal_name_to_number(signame);
            if (signal_num < 0)
            {
                fprintf(stderr, "kill: %s: invalid signal specification\n", signame);
                return 2;
            }
            i++;  /* Skip the signal name */
            first_operand = i + 1;
            continue;
        }

        /* Check for -- (end of options) */
        if (strcmp(arg, "--") == 0)
        {
            first_operand = i + 1;
            break;
        }

        /* Check for -SIGNAL or -NUMBER */
        if (arg[0] == '-' && arg[1] != '\0')
        {
            /* Could be -9, -TERM, -SIGTERM, etc. */
            const char *sigspec = arg + 1;
            int sig = kill_signal_name_to_number(sigspec);
            if (sig >= 0)
            {
                signal_num = sig;
                first_operand = i + 1;
                continue;
            }
            else
            {
                /* Not a valid signal, treat as start of operands */
                first_operand = i;
                break;
            }
        }

        /* Not an option, start of operands */
        first_operand = i;
        break;
    }

    /* Handle -l option */
    if (list_signals)
    {
        if (first_operand < argc)
        {
            /* -l with argument: convert exit status to signal name */
            for (int i = first_operand; i < argc; i++)
            {
                const string_t *arg_str = string_list_at(args, i);
                int endpos = 0;
                long val = string_atol_at(arg_str, 0, &endpos);
                if (endpos != string_length(arg_str))
                {
                    /* Try as signal name */
                    int sig = kill_signal_name_to_number(string_cstr(arg_str));
                    if (sig >= 0)
                    {
                        printf("%d\n", sig);
                    }
                    else
                    {
                        fprintf(stderr, "kill: %s: invalid signal specification\n", 
                                string_cstr(arg_str));
                        exit_status = 1;
                    }
                }
                else
                {
                    kill_list_signal_for_status((int)val);
                }
            }
        }
        else
        {
            /* -l without argument: list all signals */
            kill_list_signals();
        }
        return exit_status;
    }

    /* Need at least one pid/jobspec */
    if (first_operand >= argc)
    {
        fprintf(stderr, "kill: usage: kill [-s sigspec | -sigspec] pid | jobspec ...\n");
        return 2;
    }

    /* Process each target */
    for (int i = first_operand; i < argc; i++)
    {
        const string_t *arg_str = string_list_at(args, i);
        const char *target = string_cstr(arg_str);

        /* Check if it's a job specification */
        if (target[0] == '%')
        {
            int job_id = frame_parse_job_id(frame, arg_str);
            if (job_id < 0)
            {
                fprintf(stderr, "kill: %s: no such job\n", target);
                exit_status = 1;
                continue;
            }
            exit_status |= kill_send_to_job(frame, signal_num, job_id, target);
        }
        else
        {
            /* Parse as PID */
            int endpos = 0;
            long pid = string_atol_at(arg_str, 0, &endpos);
            if (endpos != string_length(arg_str))
            {
                fprintf(stderr, "kill: %s: arguments must be process or job IDs\n", target);
                exit_status = 1;
                continue;
            }
            exit_status |= kill_send_signal(frame, signal_num, (intptr_t)pid, target);
        }
    }

    return exit_status;
}

/* ============================================================================
 * wait - Wait for background jobs to complete
 * 
 * POSIX Synopsis:
 *   wait [job_id...]
 *   wait [pid...]
 * 
 * If no operands are given, waits for all currently active child processes.
 * If one or more job_id or pid operands are given, waits for those specific
 * jobs/processes.
 * 
 * Returns:
 *   - Exit status of the last process waited for
 *   - 0 if no children to wait for
 *   - 127 if a specified job/pid doesn't exist
 * ============================================================================
 */

/**
 * Wait for a specific job to complete.
 * Returns the exit status of the job, or -1 on error.
 */
static int wait_for_job(exec_frame_t *frame, int job_id, const char *target_str)
{
    if (!frame || !frame->executor || !frame->executor->jobs)
    {
        fprintf(stderr, "wait: %s: no such job\n", target_str);
        return 127;
    }

    job_t *job = job_store_find(frame->executor->jobs, job_id);
    if (!job)
    {
        fprintf(stderr, "wait: %s: no such job\n", target_str);
        return 127;
    }

    /* If job is already completed, return its exit status */
    if (job_is_completed(job))
    {
        /* Get exit status from first process (or last, depending on convention) */
        if (job->processes)
        {
            return job->processes->exit_status;
        }
        return 0;
    }

#ifdef POSIX_API
    /* Wait for the process group */
    int status;
    pid_t result;

    /* Wait for any process in the job's process group */
    while ((result = waitpid(-job->pgid, &status, 0)) > 0 || 
           (result == -1 && errno == EINTR))
    {
        if (result > 0)
        {
            /* Update the process state */
            if (WIFEXITED(status))
            {
                job_store_set_process_state(frame->executor->jobs, result, 
                                           JOB_DONE, WEXITSTATUS(status));
            }
            else if (WIFSIGNALED(status))
            {
                job_store_set_process_state(frame->executor->jobs, result,
                                           JOB_TERMINATED, WTERMSIG(status));
            }
        }

        /* Check if job is now complete */
        if (job_is_completed(job))
        {
            break;
        }
    }

    /* Return the exit status of the first process */
    if (job->processes)
    {
        return job->processes->exit_status;
    }
    return 0;

#elifdef UCRT_API
    /* On Windows, wait for all processes in the job using their handles */
    int last_exit_status = 0;

    /* Use iterator to find and wait for processes belonging to this job */
    job_process_iterator_t iter = job_store_active_processes_begin(frame->executor->jobs);
    while (job_store_active_processes_next(&iter))
    {
        if (job_store_iter_get_job_id(&iter) == job_id)
        {
            uintptr_t handle = (uintptr_t)job_store_iter_get_handle(&iter);

            if (handle != 0)
            {
                /* Wait for this process */
                DWORD wait_result = WaitForSingleObject((HANDLE)handle, INFINITE);

                if (wait_result == WAIT_OBJECT_0)
                {
                    /* Process completed, get exit code */
                    DWORD exit_code = 0;
                    if (GetExitCodeProcess((HANDLE)handle, &exit_code))
                    {
                        last_exit_status = (int)exit_code;
                        job_store_iter_set_state(&iter, JOB_DONE, (int)exit_code);
                    }
                    else
                    {
                        job_store_iter_set_state(&iter, JOB_DONE, 0);
                    }
                }
                else
                {
                    /* Wait failed */
                    job_store_iter_set_state(&iter, JOB_TERMINATED, 1);
                    last_exit_status = 1;
                }
            }
        }
    }

    return last_exit_status;

#else
    (void)target_str;
    fprintf(stderr, "wait: not supported on this platform\n");
    return 127;
#endif
}

/**
 * Wait for a specific PID.
 * Returns the exit status, or 127 if PID not found.
 */
static int wait_for_pid(exec_frame_t *frame, intptr_t pid, const char *target_str)
{
#ifdef POSIX_API
    int status;
    pid_t result = waitpid((pid_t)pid, &status, 0);

    if (result == -1)
    {
        if (errno == ECHILD)
        {
            fprintf(stderr, "wait: pid %ld is not a child of this shell\n", (long)pid);
        }
        else
        {
            fprintf(stderr, "wait: %s: %s\n", target_str, strerror(errno));
        }
        return 127;
    }

    /* Update job store if this process is tracked */
    if (frame && frame->executor && frame->executor->jobs)
    {
        if (WIFEXITED(status))
        {
            job_store_set_process_state(frame->executor->jobs, (pid_t)pid,
                                       JOB_DONE, WEXITSTATUS(status));
            return WEXITSTATUS(status);
        }
        else if (WIFSIGNALED(status))
        {
            job_store_set_process_state(frame->executor->jobs, (pid_t)pid,
                                       JOB_TERMINATED, WTERMSIG(status));
            return 128 + WTERMSIG(status);
        }
    }

    return 0;

#elifdef UCRT_API
    /* Search for the PID in the job store to get its handle */
    if (frame && frame->executor && frame->executor->jobs)
    {
        job_process_iterator_t iter = job_store_active_processes_begin(frame->executor->jobs);
        while (job_store_active_processes_next(&iter))
        {
            if (job_store_iter_get_pid(&iter) == pid)
            {
                uintptr_t handle = (uintptr_t)job_store_iter_get_handle(&iter);

                if (handle != 0)
                {
                    DWORD wait_result = WaitForSingleObject((HANDLE)handle, INFINITE);

                    if (wait_result == WAIT_OBJECT_0)
                    {
                        DWORD exit_code = 0;
                        GetExitCodeProcess((HANDLE)handle, &exit_code);
                        job_store_iter_set_state(&iter, JOB_DONE, (int)exit_code);
                        return (int)exit_code;
                    }
                    else
                    {
                        job_store_iter_set_state(&iter, JOB_TERMINATED, 1);
                        return 1;
                    }
                }
            }
        }
    }

    fprintf(stderr, "wait: pid %ld is not a child of this shell\n", (long)pid);
    return 127;

#else
    (void)frame;
    (void)pid;
    (void)target_str;
    fprintf(stderr, "wait: not supported on this platform\n");
    return 127;
#endif
}

/**
 * Wait for all background jobs.
 * Returns the exit status of the last job waited for, or 0 if none.
 */
static int wait_for_all(exec_frame_t *frame)
{
    if (!frame || !frame->executor || !frame->executor->jobs)
    {
        return 0;
    }

    int last_exit_status = 0;

#ifdef POSIX_API
    int status;
    pid_t result;

    /* Wait for all child processes */
    while ((result = waitpid(-1, &status, 0)) > 0 || 
           (result == -1 && errno == EINTR))
    {
        if (result > 0)
        {
            if (WIFEXITED(status))
            {
                job_store_set_process_state(frame->executor->jobs, result,
                                           JOB_DONE, WEXITSTATUS(status));
                last_exit_status = WEXITSTATUS(status);
            }
            else if (WIFSIGNALED(status))
            {
                job_store_set_process_state(frame->executor->jobs, result,
                                           JOB_TERMINATED, WTERMSIG(status));
                last_exit_status = 128 + WTERMSIG(status);
            }
        }
    }

#elifdef UCRT_API
    /* Collect all active process handles and wait for them */
    #define MAX_WAIT_HANDLES 64
    HANDLE handles[MAX_WAIT_HANDLES];
    job_process_iterator_t iters[MAX_WAIT_HANDLES];
    int handle_count = 0;

    /* First pass: collect handles */
    job_process_iterator_t iter = job_store_active_processes_begin(frame->executor->jobs);
    while (job_store_active_processes_next(&iter) && handle_count < MAX_WAIT_HANDLES)
    {
        uintptr_t handle = (uintptr_t)job_store_iter_get_handle(&iter);
        if (handle != 0)
        {
            handles[handle_count] = (HANDLE)handle;
            iters[handle_count] = iter;
            handle_count++;
        }
    }

    /* Wait for each process */
    for (int i = 0; i < handle_count; i++)
    {
        DWORD wait_result = WaitForSingleObject(handles[i], INFINITE);

        if (wait_result == WAIT_OBJECT_0)
        {
            DWORD exit_code = 0;
            GetExitCodeProcess(handles[i], &exit_code);
            job_store_iter_set_state(&iters[i], JOB_DONE, (int)exit_code);
            last_exit_status = (int)exit_code;
        }
        else
        {
            job_store_iter_set_state(&iters[i], JOB_TERMINATED, 1);
            last_exit_status = 1;
        }
    }
    #undef MAX_WAIT_HANDLES
#endif

    return last_exit_status;
}

int builtin_wait(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    int argc = string_list_size(args);
    int exit_status = 0;

    /* No arguments: wait for all background jobs */
    if (argc == 1)
    {
        return wait_for_all(frame);
    }

    /* Parse options (wait has no standard options, but handle -- for consistency) */
    int first_operand = 1;
    for (int i = 1; i < argc; i++)
    {
        const char *arg = string_cstr(string_list_at(args, i));

        if (strcmp(arg, "--") == 0)
        {
            first_operand = i + 1;
            break;
        }

        /* If it starts with - but isn't --, it might be an invalid option or a negative number */
        if (arg[0] == '-' && arg[1] != '\0' && arg[1] != '-')
        {
            /* Check if it's a valid negative number (unlikely for PIDs, but possible) */
            char *endptr;
            strtol(arg, &endptr, 10);
            if (*endptr != '\0')
            {
                fprintf(stderr, "wait: %s: invalid option\n", arg);
                return 2;
            }
        }

        first_operand = i;
        break;
    }

    /* Process each job_id or pid */
    for (int i = first_operand; i < argc; i++)
    {
        const string_t *arg_str = string_list_at(args, i);
        const char *target = string_cstr(arg_str);

        /* Check if it's a job specification */
        if (target[0] == '%')
        {
            int job_id = frame_parse_job_id(frame, arg_str);
            if (job_id < 0)
            {
                fprintf(stderr, "wait: %s: no such job\n", target);
                exit_status = 127;
                continue;
            }
            int result = wait_for_job(frame, job_id, target);
            exit_status = result;
        }
        else
        {
            /* Parse as PID */
            int endpos = 0;
            long pid = string_atol_at(arg_str, 0, &endpos);
            if (endpos != string_length(arg_str))
            {
                fprintf(stderr, "wait: %s: not a valid pid or job specification\n", target);
                exit_status = 127;
                continue;
            }
            int result = wait_for_pid(frame, (intptr_t)pid, target);
            exit_status = result;
        }
    }

    return exit_status;
}

/* ============================================================================
 * fg - Bring job to foreground
 * 
 * POSIX Synopsis:
 *   fg [job_id]
 * 
 * Moves the specified job (or current job if none specified) to the foreground,
 * making it the current job. The job is continued if it was stopped.
 * 
 * Returns:
 *   - Exit status of the foreground job
 *   - 1 if job_id does not exist
 * ============================================================================
 */

int builtin_fg(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    if (!frame->executor || !frame->executor->jobs)
    {
        fprintf(stderr, "fg: no job control\n");
        return 1;
    }

    /* Check if there are any jobs */
    if (!frame_has_jobs(frame))
    {
        fprintf(stderr, "fg: no current job\n");
        return 1;
    }

    int argc = string_list_size(args);
    int job_id = -1;

    /* Parse optional job_id argument */
    if (argc > 2)
    {
        fprintf(stderr, "fg: too many arguments\n");
        return 2;
    }

    if (argc == 2)
    {
        const string_t *arg_str = string_list_at(args, 1);
        job_id = frame_parse_job_id(frame, arg_str);
        if (job_id < 0)
        {
            fprintf(stderr, "fg: %s: no such job\n", string_cstr(arg_str));
            return 1;
        }
    }
    else
    {
        /* No argument: use current job */
        job_t *current = job_store_get_current(frame->executor->jobs);
        if (!current)
        {
            fprintf(stderr, "fg: no current job\n");
            return 1;
        }
        job_id = current->job_id;
    }

    job_t *job = job_store_find(frame->executor->jobs, job_id);
    if (!job)
    {
        fprintf(stderr, "fg: job %d not found\n", job_id);
        return 1;
    }

    /* Print the job's command line */
    if (job->command_line)
    {
        printf("%s\n", string_cstr(job->command_line));
    }

#ifdef POSIX_API
    /* Give the job's process group control of the terminal */
    if (isatty(STDIN_FILENO))
    {
        tcsetpgrp(STDIN_FILENO, job->pgid);
    }

    /* Send SIGCONT if the job was stopped */
    if (job->state == JOB_STOPPED)
    {
        if (kill(-job->pgid, SIGCONT) < 0)
        {
            fprintf(stderr, "fg: failed to continue job: %s\n", strerror(errno));
            return 1;
        }
        job_store_set_state(frame->executor->jobs, job_id, JOB_RUNNING);
    }

    /* Wait for the job to complete or stop */
    int status;
    pid_t result;
    int exit_status = 0;

    while ((result = waitpid(-job->pgid, &status, WUNTRACED)) > 0 ||
           (result == -1 && errno == EINTR))
    {
        if (result > 0)
        {
            if (WIFEXITED(status))
            {
                job_store_set_process_state(frame->executor->jobs, result,
                                           JOB_DONE, WEXITSTATUS(status));
                exit_status = WEXITSTATUS(status);
            }
            else if (WIFSIGNALED(status))
            {
                job_store_set_process_state(frame->executor->jobs, result,
                                           JOB_TERMINATED, WTERMSIG(status));
                exit_status = 128 + WTERMSIG(status);
            }
            else if (WIFSTOPPED(status))
            {
                job_store_set_process_state(frame->executor->jobs, result,
                                           JOB_STOPPED, WSTOPSIG(status));
                /* Job was stopped, print notification */
                fprintf(stderr, "\n[%d]+  Stopped                 %s\n", 
                        job_id, job->command_line ? string_cstr(job->command_line) : "");
                exit_status = 128 + WSTOPSIG(status);
                break;
            }
        }

        /* Check if job is complete */
        if (job_is_completed(job))
        {
            break;
        }
    }

    /* Return terminal control to the shell */
    if (isatty(STDIN_FILENO) && frame->executor->pgid_valid)
    {
        tcsetpgrp(STDIN_FILENO, frame->executor->pgid);
    }

    return exit_status;

#elifdef UCRT_API
    /* Windows doesn't have true foreground/background job control */
    /* We can wait for the job, but can't give it terminal control */

    if (job->state == JOB_STOPPED)
    {
        fprintf(stderr, "fg: resuming stopped jobs not supported on this platform\n");
        return 1;
    }

    /* Wait for the job to complete */
    int last_exit_status = 0;
    job_process_iterator_t iter = job_store_active_processes_begin(frame->executor->jobs);
    while (job_store_active_processes_next(&iter))
    {
        if (job_store_iter_get_job_id(&iter) == job_id)
        {
            uintptr_t handle = (uintptr_t)job_store_iter_get_handle(&iter);
            if (handle != 0)
            {
                DWORD wait_result = WaitForSingleObject((HANDLE)handle, INFINITE);
                if (wait_result == WAIT_OBJECT_0)
                {
                    DWORD exit_code = 0;
                    GetExitCodeProcess((HANDLE)handle, &exit_code);
                    job_store_iter_set_state(&iter, JOB_DONE, (int)exit_code);
                    last_exit_status = (int)exit_code;
                }
                else
                {
                    job_store_iter_set_state(&iter, JOB_TERMINATED, 1);
                    last_exit_status = 1;
                }
            }
        }
    }

    return last_exit_status;

#else
    fprintf(stderr, "fg: job control not supported on this platform\n");
    return 1;
#endif
}

/* ============================================================================
 * bg - Resume job in background
 * 
 * POSIX Synopsis:
 *   bg [job_id ...]
 * 
 * Resumes the specified stopped job(s) in the background. If no job is 
 * specified, the current job is resumed.
 * 
 * Returns:
 *   - 0 on success
 *   - 1 if job_id does not exist or job is not stopped
 * ============================================================================
 */

int builtin_bg(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    if (!frame->executor || !frame->executor->jobs)
    {
        fprintf(stderr, "bg: no job control\n");
        return 1;
    }

    /* Check if there are any jobs */
    if (!frame_has_jobs(frame))
    {
        fprintf(stderr, "bg: no current job\n");
        return 1;
    }

    int argc = string_list_size(args);
    int exit_status = 0;

    if (argc == 1)
    {
        /* No arguments: use current job */
        job_t *current = job_store_get_current(frame->executor->jobs);
        if (!current)
        {
            fprintf(stderr, "bg: no current job\n");
            return 1;
        }

        if (current->state != JOB_STOPPED)
        {
            fprintf(stderr, "bg: job %d is not stopped\n", current->job_id);
            return 1;
        }

#ifdef POSIX_API
        /* Send SIGCONT to resume the job */
        if (kill(-current->pgid, SIGCONT) < 0)
        {
            fprintf(stderr, "bg: failed to continue job: %s\n", strerror(errno));
            return 1;
        }
        job_store_set_state(frame->executor->jobs, current->job_id, JOB_RUNNING);

        /* Print the job info */
        printf("[%d]+ %s &\n", current->job_id,
               current->command_line ? string_cstr(current->command_line) : "");
#elifdef UCRT_API
        fprintf(stderr, "bg: resuming stopped jobs not supported on this platform\n");
        return 1;
#else
        fprintf(stderr, "bg: job control not supported on this platform\n");
        return 1;
#endif
    }
    else
    {
        /* Process each job_id argument */
        for (int i = 1; i < argc; i++)
        {
            const string_t *arg_str = string_list_at(args, i);
            int job_id = frame_parse_job_id(frame, arg_str);

            if (job_id < 0)
            {
                fprintf(stderr, "bg: %s: no such job\n", string_cstr(arg_str));
                exit_status = 1;
                continue;
            }

            job_t *job = job_store_find(frame->executor->jobs, job_id);
            if (!job)
            {
                fprintf(stderr, "bg: %s: no such job\n", string_cstr(arg_str));
                exit_status = 1;
                continue;
            }

            if (job->state != JOB_STOPPED)
            {
                fprintf(stderr, "bg: job %d is not stopped\n", job_id);
                exit_status = 1;
                continue;
            }

#ifdef POSIX_API
            /* Send SIGCONT to resume the job */
            if (kill(-job->pgid, SIGCONT) < 0)
            {
                fprintf(stderr, "bg: %s: failed to continue job: %s\n", 
                        string_cstr(arg_str), strerror(errno));
                exit_status = 1;
                continue;
            }
            job_store_set_state(frame->executor->jobs, job_id, JOB_RUNNING);

            /* Print the job info */
            printf("[%d]%c %s &\n", job_id,
                   (job == job_store_get_current(frame->executor->jobs)) ? '+' : '-',
                   job->command_line ? string_cstr(job->command_line) : "");
#elifdef UCRT_API
            fprintf(stderr, "bg: resuming stopped jobs not supported on this platform\n");
            exit_status = 1;
#else
            fprintf(stderr, "bg: job control not supported on this platform\n");
            exit_status = 1;
#endif
        }
    }

    return exit_status;
}


/* ===========================================================================
 * ls - list files
 * ===========================================================================
 */
#ifdef UCRT_API
/**
 * builtin_ls - List directory contents (Windows UCRT implementation)
 *
 * Implements a basic 'ls' command for Windows since there's no standard
 * external 'ls' command available. Uses only UCRT functions, no Windows API.
 *
 * Options:
 *   -a    Include hidden files and directories (those starting with '.')
 *   -A    Like -a, but exclude '.' and '..'
 *   -l    Long listing format (size and name; limited metadata available)
 *   -1    One entry per line (default if output is not a terminal)
 *   -F    Append indicator (/ for directories, * for executables)
 *   -h    Human-readable sizes (with -l)
 *
 * @param ex   Execution context
 * @param args Command arguments (including "ls" as args[0])
 * @return 0 on success, 1 on minor errors, 2 on usage errors
 */

/**
 * Entry structure for collecting and sorting
 */
typedef struct ls_entry_t
{
    char *name;
    unsigned attrib;
    uint64_t size;
    time_t mtime;
} ls_entry_t;

/**
 * Format a file size in human-readable form (K, M, G, T)
 */
static void ls_format_size_human(uint64_t size, char *buf, size_t buf_size)
{
    const char *units[] = {"", "K", "M", "G", "T", "P"};
    int unit_index = 0;
    double display_size = (double)size;

    while (display_size >= 1024.0 && unit_index < 5)
    {
        display_size /= 1024.0;
        unit_index++;
    }

    if (unit_index == 0)
    {
        snprintf(buf, buf_size, "%7llu", (unsigned long long)size);
    }
    else
    {
        snprintf(buf, buf_size, "%6.1f%s", display_size, units[unit_index]);
    }
}

/**
 * Format time_t to a readable date string
 */
static void ls_format_time(time_t t, char *buf, size_t buf_size)
{
    static const char *month_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    struct tm *tm_info = localtime(&t);
    if (!tm_info)
    {
        snprintf(buf, buf_size, "            ");
        return;
    }

    time_t now = time(NULL);
    struct tm *now_info = localtime(&now);

    /* Show time if within the last 6 months, otherwise show year */
    int months_diff = 12; /* Default to showing year */
    if (now_info)
    {
        months_diff =
            (now_info->tm_year - tm_info->tm_year) * 12 + (now_info->tm_mon - tm_info->tm_mon);
    }

    if (months_diff < 6 && months_diff >= 0)
    {
        snprintf(buf, buf_size, "%s %2d %02d:%02d", month_names[tm_info->tm_mon], tm_info->tm_mday,
                 tm_info->tm_hour, tm_info->tm_min);
    }
    else
    {
        snprintf(buf, buf_size, "%s %2d  %4d", month_names[tm_info->tm_mon], tm_info->tm_mday,
                 tm_info->tm_year + 1900);
    }
}

/**
 * Check if a file should be shown based on flags
 */
static bool ls_should_show_entry(const char *name, unsigned attrib, int flag_a, int flag_A)
{
    bool is_dot = (strcmp(name, ".") == 0 || strcmp(name, "..") == 0);
    bool starts_with_dot = (name[0] == '.');
    bool is_hidden = (attrib & _A_HIDDEN) != 0;

    if (flag_a)
    {
        return true;
    }

    if (flag_A)
    {
        return !is_dot;
    }

    return !starts_with_dot && !is_hidden;
}

/**
 * Get type indicator character for -F option
 */
static char ls_get_type_indicator(unsigned attrib, const char *name)
{
    if (attrib & _A_SUBDIR)
    {
        return '/';
    }

    /* Check for executable extensions */
    const char *ext = strrchr(name, '.');
    if (ext)
    {
        if (_stricmp(ext, ".exe") == 0 || _stricmp(ext, ".cmd") == 0 ||
            _stricmp(ext, ".bat") == 0 || _stricmp(ext, ".com") == 0)
        {
            return '*';
        }
    }

    return '\0';
}

/**
 * Comparison function for sorting entries by name
 */
static int ls_compare_entries(const void *a, const void *b)
{
    const ls_entry_t *ea = (const ls_entry_t *)a;
    const ls_entry_t *eb = (const ls_entry_t *)b;
    return lib_strcoll(ea->name, eb->name);
}

/**
 * List a single directory's contents
 */
static int ls_list_directory(const string_t *dir_path, int flag_a, int flag_A, int flag_l,
                             int flag_1, int flag_F, int flag_h)
{
    struct _finddata_t find_data; // _finddata64i32_t
    intptr_t find_handle;

    /* Build search pattern: "dir_path/*" or "dir_path\*" */
    string_t *pattern = string_create_from(dir_path);
    int len = string_length(pattern);
    if (len > 0)
    {
        char last = string_cstr(pattern)[len - 1];
        if (last != '/' && last != '\\')
        {
            string_append_cstr(pattern, "/");
        }
    }
    string_append_cstr(pattern, "*");

    find_handle = _findfirst(string_cstr(pattern), &find_data);
    string_destroy(&pattern);

    if (find_handle == -1)
    {
        if (errno == ENOENT)
        {
            fprintf(stderr, "ls: cannot access '%s': No such file or directory\n",
                    string_cstr(dir_path));
        }
        else if (errno == EACCES)
        {
            fprintf(stderr, "ls: cannot open '%s': Permission denied\n", string_cstr(dir_path));
        }
        else
        {
            fprintf(stderr, "ls: cannot access '%s': %s\n", string_cstr(dir_path), strerror(errno));
        }
        return 1;
    }

    /* Collect entries */
    int capacity = 64;
    int count = 0;
    ls_entry_t *entries = xmalloc(capacity * sizeof(ls_entry_t));

    do
    {
        if (!ls_should_show_entry(find_data.name, find_data.attrib, flag_a, flag_A))
        {
            continue;
        }

        if (count >= capacity)
        {
            capacity *= 2;
            entries = xrealloc(entries, capacity * sizeof(ls_entry_t));
        }

        entries[count].name = xstrdup(find_data.name);
        entries[count].attrib = find_data.attrib;
        entries[count].size = (uint64_t)find_data.size;
        entries[count].mtime = find_data.time_write;
        count++;

    } while (_findnext(find_handle, &find_data) == 0);

    _findclose(find_handle);

    /* Sort entries by name */
    if (count > 1)
    {
        qsort(entries, count, sizeof(ls_entry_t), ls_compare_entries);
    }

    /* Calculate column width for non-long format */
    int max_name_len = 0;
    if (!flag_1 && !flag_l)
    {
        for (int i = 0; i < count; i++)
        {
            int name_len = (int)strlen(entries[i].name);
            if (flag_F && ls_get_type_indicator(entries[i].attrib, entries[i].name))
            {
                name_len++;
            }
            if (name_len > max_name_len)
            {
                max_name_len = name_len;
            }
        }
    }

    /* Output entries */
    int col = 0;
    int term_width = 80; /* Default; no portable way to detect without Windows API */
    int col_width = max_name_len + 2;
    int cols_per_row = (col_width > 0) ? (term_width / col_width) : 1;
    if (cols_per_row < 1)
    {
        cols_per_row = 1;
    }

    for (int i = 0; i < count; i++)
    {
        ls_entry_t *entry = &entries[i];
        bool is_dir = (entry->attrib & _A_SUBDIR) != 0;
        char indicator = flag_F ? ls_get_type_indicator(entry->attrib, entry->name) : '\0';

        if (flag_l)
        {
            char size_buf[16];
            char date_buf[16];

            ls_format_time(entry->mtime, date_buf, sizeof(date_buf));

            if (is_dir)
            {
                snprintf(size_buf, sizeof(size_buf), "      -");
            }
            else if (flag_h)
            {
                ls_format_size_human(entry->size, size_buf, sizeof(size_buf));
            }
            else
            {
                snprintf(size_buf, sizeof(size_buf), "%7llu", (unsigned long long)entry->size);
            }

            printf("%s %s %s", size_buf, date_buf, entry->name);
            if (indicator)
            {
                putchar(indicator);
            }
            putchar('\n');
        }
        else if (flag_1)
        {
            printf("%s", entry->name);
            if (indicator)
            {
                putchar(indicator);
            }
            putchar('\n');
        }
        else
        {
            /* Columnar output */
            int printed;
            if (indicator)
            {
                printed = printf("%s%c", entry->name, indicator);
            }
            else
            {
                printed = printf("%s", entry->name);
            }
            /* Pad to column width */
            for (int p = printed; p < col_width; p++)
            {
                putchar(' ');
            }

            col++;
            if (col >= cols_per_row)
            {
                putchar('\n');
                col = 0;
            }
        }
    }

    /* Final newline for columnar output if needed */
    if (!flag_1 && !flag_l && col > 0)
    {
        putchar('\n');
    }

    /* Cleanup */
    for (int i = 0; i < count; i++)
    {
        xfree(entries[i].name);
    }
    xfree(entries);

    return 0;
}

int builtin_ls(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    (void)frame; /* Unused, but kept for consistent builtin signature */

    getopt_reset();

    int flag_a = 0;
    int flag_A = 0;
    int flag_l = 0;
    int flag_1 = 0;
    int flag_F = 0;
    int flag_h = 0;
    int flag_err = 0;
    int err_count = 0;
    int c;

    string_t *opts = string_create_from_cstr("aAlFh1");

    while ((c = getopt_string(args, opts)) != -1)
    {
        switch (c)
        {
        case 'a':
            flag_a = 1;
            break;
        case 'A':
            flag_A = 1;
            break;
        case 'l':
            flag_l = 1;
            break;
        case '1':
            flag_1 = 1;
            break;
        case 'F':
            flag_F = 1;
            break;
        case 'h':
            flag_h = 1;
            break;
        case '?':
            fprintf(stderr, "ls: unrecognized option: '-%c'\n", optopt);
            flag_err++;
            break;
        }
    }
    string_destroy(&opts);

    if (flag_err)
    {
        fprintf(stderr, "usage: ls [-aAlFh1] [directory...]\n");
        return 2;
    }

    /* Default to one-per-line if not a terminal or if -l is set */
    int is_tty = _isatty(_fileno(stdout));
    if (!is_tty || flag_l)
    {
        flag_1 = 1;
    }

    /* Collect directories to list */
    int dir_count = string_list_size(args) - optind;
    int start_index = optind;

    /* Default to current directory if none specified */
    string_t *default_dir = NULL;
    if (dir_count == 0)
    {
        default_dir = string_create_from_cstr(".");
        dir_count = 1;
    }

    /* Process each directory */
    for (int i = 0; i < dir_count; i++)
    {
        const string_t *dir_path;
        if (default_dir)
        {
            dir_path = default_dir;
        }
        else
        {
            dir_path = string_list_at(args, start_index + i);
        }

        /* Print directory name if listing multiple directories */
        if (dir_count > 1)
        {
            if (i > 0)
            {
                printf("\n");
            }
            printf("%s:\n", string_cstr(dir_path));
        }

        int result = ls_list_directory(dir_path, flag_a, flag_A, flag_l, flag_1, flag_F, flag_h);
        if (result != 0)
        {
            err_count++;
        }
    }

    if (default_dir)
    {
        string_destroy(&default_dir);
    }

    return err_count > 0 ? 1 : 0;
}
#else
int builtin_ls(exec_frame_t *frame, const string_list_t *args)
{
    (void)frame;
    (void)args;
    fprintf(stderr, "ls: not supported on this platform\n");
    return -2;
}
#endif

/* ============================================================================
 * echo - Display a line of text
 * ============================================================================
 */

int builtin_echo(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    int flag_n = 0;  /* suppress newline */
    int flag_e = 0;  /* interpret escapes */
    int flag_err = 0;
    int c;

    string_t *opts = string_create_from_cstr("neE");

    while ((c = getopt_string(args, opts)) != -1)
    {
        switch (c)
        {
        case 'n':
            flag_n = 1;
            break;
        case 'e':
            flag_e = 1;
            flag_err = 0;  /* -e overrides -E */
            break;
        case 'E':
            if (!flag_e)
                flag_e = 0;
            break;
        case '?':
            fprintf(stderr, "echo: unrecognized option: '-%c'\n", optopt);
            flag_err++;
            break;
        }
    }
    string_destroy(&opts);

    if (flag_err)
    {
        fprintf(stderr, "usage: echo [-neE] [string ...]\n");
        return 2;
    }

    /* Print arguments starting from optind, separated by spaces */
    int argc = string_list_size(args);
    for (int i = optind; i < argc; i++)
    {
        if (i > optind)
        {
            putchar(' ');
        }

        const char *arg = string_cstr(string_list_at(args, i));

        if (flag_e)
        {
            /* Interpret escape sequences */
            for (const char *p = arg; *p; p++)
            {
                if (*p == '\\' && *(p + 1))
                {
                    p++;
                    switch (*p)
                    {
                    case 'a': putchar('\a'); break;
                    case 'b': putchar('\b'); break;
                    case 'c': return 0; /* Stop printing */
                    case 'e': putchar('\033'); break; /* ESC */
                    case 'f': putchar('\f'); break;
                    case 'n': putchar('\n'); break;
                    case 'r': putchar('\r'); break;
                    case 't': putchar('\t'); break;
                    case 'v': putchar('\v'); break;
                    case '\\': putchar('\\'); break;
                    case '0': /* Octal */
                    {
                        int val = 0;
                        for (int j = 0; j < 3 && p[1] >= '0' && p[1] <= '7'; j++)
                        {
                            p++;
                            val = val * 8 + (*p - '0');
                        }
                        putchar(val);
                        break;
                    }
                    default:
                        /* Unknown escape - print literally */
                        putchar('\\');
                        putchar(*p);
                        break;
                    }
                }
                else
                {
                    putchar(*p);
                }
            }
        }
        else
        {
            /* No escape interpretation - print as-is */
            fputs(arg, stdout);
        }
    }

    if (!flag_n)
    {
        putchar('\n');
    }

    fflush(stdout);
    return 0;
}

/* ============================================================================
 * printf - Format and print data
 * 
 * POSIX printf utility with format specifiers:
 *   %b    - Print with backslash escapes interpreted
 *   %c    - Print as character
 *   %d, %i - Print as signed decimal integer
 *   %u    - Print as unsigned decimal integer
 *   %o    - Print as unsigned octal integer
 *   %x    - Print as unsigned hexadecimal (lowercase)
 *   %X    - Print as unsigned hexadecimal (uppercase)
 *   %s    - Print as string
 *   %%    - Print literal %
 *
 * Width and precision modifiers supported.
 * If more arguments than format specifiers, format is reused.
 * ============================================================================
 */

/* Helper: Parse and process escape sequences for %b format 
 * Returns a string_t that must be freed by the caller.
 */
static string_t *printf_process_escapes(const char *str, int *stop_output)
{
    string_t *result = string_create();
    const char *p = str;
    
    *stop_output = 0;
    
    while (*p)
    {
        if (*p == '\\' && *(p + 1))
        {
            p++;
            switch (*p)
            {
            case 'a': string_push_back(result, '\a'); break;
            case 'b': string_push_back(result, '\b'); break;
            case 'c': *stop_output = 1; return result; /* Stop */
            case 'e': /* Extension: ESC */
            case 'E': string_push_back(result, '\033'); break;
            case 'f': string_push_back(result, '\f'); break;
            case 'n': string_push_back(result, '\n'); break;
            case 'r': string_push_back(result, '\r'); break;
            case 't': string_push_back(result, '\t'); break;
            case 'v': string_push_back(result, '\v'); break;
            case '\\': string_push_back(result, '\\'); break;
            case '0': /* Octal */
            case '1': case '2': case '3':
            case '4': case '5': case '6': case '7':
            {
                int val = 0;
                int count = 0;
                while (count < 3 && *p >= '0' && *p <= '7')
                {
                    val = val * 8 + (*p - '0');
                    p++;
                    count++;
                }
                p--; /* Back up for loop increment */
                string_push_back(result, (char)val);
                break;
            }
            default:
                /* Unknown escape - print literally */
                string_push_back(result, '\\');
                string_push_back(result, *p);
                break;
            }
            p++;
        }
        else
        {
            string_push_back(result, *p++);
        }
    }
    return result;
}

/* Helper: Parse width or precision number */
static int printf_parse_number(const char **fmt)
{
    int num = 0;
    while (**fmt >= '0' && **fmt <= '9')
    {
        num = num * 10 + (**fmt - '0');
        (*fmt)++;
    }
    return num;
}

/* Helper: Parse and print a single format specifier */
static int printf_process_format(const char **fmt, const char *arg, int *stop_output)
{
    const char *f = *fmt;
    int width = 0;
    int precision = -1;
    int left_justify = 0;
    int zero_pad = 0;
    int has_precision = 0;
    
    *stop_output = 0;
    
    /* Skip % */
    f++;
    
    /* Parse flags */
    while (*f == '-' || *f == '0' || *f == ' ' || *f == '+' || *f == '#')
    {
        if (*f == '-') left_justify = 1;
        if (*f == '0') zero_pad = 1;
        f++;
    }
    
    /* Parse width */
    if (*f >= '0' && *f <= '9')
    {
        width = printf_parse_number(&f);
    }
    
    /* Parse precision */
    if (*f == '.')
    {
        f++;
        has_precision = 1;
        precision = printf_parse_number(&f);
    }
    
    /* Get conversion specifier */
    char spec = *f;
    if (spec) f++;
    
    *fmt = f;
    
    /* Process conversion */
    switch (spec)
    {
    case 'b': /* String with backslash escapes */
    {
        string_t *processed = printf_process_escapes(arg, stop_output);
        const char *processed_str = string_cstr(processed);
        if (width > 0)
        {
            int len = (int)strlen(processed_str);
            int pad = width - len;
            if (pad > 0)
            {
                if (left_justify)
                {
                    printf("%s%*s", processed_str, pad, "");
                }
                else
                {
                    printf("%*s%s", pad, "", processed_str);
                }
            }
            else
            {
                printf("%s", processed_str);
            }
        }
        else
        {
            printf("%s", processed_str);
        }
        string_destroy(&processed);
        break;
    }
    
    case 'c': /* Character */
    {
        int c = arg && *arg ? (unsigned char)*arg : 0;
        if (width > 0)
        {
            if (left_justify)
                printf("%-*c", width, c);
            else
                printf("%*c", width, c);
        }
        else
        {
            putchar(c);
        }
        break;
    }
    
    case 'd':
    case 'i': /* Signed decimal */
    {
        long val = arg && *arg ? strtol(arg, NULL, 0) : 0;
        if (has_precision)
        {
            if (left_justify)
                printf("%-*.*ld", width, precision, val);
            else
                printf("%*.*ld", width, precision, val);
        }
        else if (zero_pad && !left_justify && width > 0)
        {
            printf("%0*ld", width, val);
        }
        else if (width > 0)
        {
            if (left_justify)
                printf("%-*ld", width, val);
            else
                printf("%*ld", width, val);
        }
        else
        {
            printf("%ld", val);
        }
        break;
    }
    
    case 'u': /* Unsigned decimal */
    {
        unsigned long val = arg && *arg ? strtoul(arg, NULL, 0) : 0;
        if (has_precision)
        {
            if (left_justify)
                printf("%-*.*lu", width, precision, val);
            else
                printf("%*.*lu", width, precision, val);
        }
        else if (zero_pad && !left_justify && width > 0)
        {
            printf("%0*lu", width, val);
        }
        else if (width > 0)
        {
            if (left_justify)
                printf("%-*lu", width, val);
            else
                printf("%*lu", width, val);
        }
        else
        {
            printf("%lu", val);
        }
        break;
    }
    
    case 'o': /* Octal */
    {
        unsigned long val = arg && *arg ? strtoul(arg, NULL, 0) : 0;
        if (has_precision)
        {
            if (left_justify)
                printf("%-*.*lo", width, precision, val);
            else
                printf("%*.*lo", width, precision, val);
        }
        else if (zero_pad && !left_justify && width > 0)
        {
            printf("%0*lo", width, val);
        }
        else if (width > 0)
        {
            if (left_justify)
                printf("%-*lo", width, val);
            else
                printf("%*lo", width, val);
        }
        else
        {
            printf("%lo", val);
        }
        break;
    }
    
    case 'x': /* Hexadecimal lowercase */
    {
        unsigned long val = arg && *arg ? strtoul(arg, NULL, 0) : 0;
        if (has_precision)
        {
            if (left_justify)
                printf("%-*.*lx", width, precision, val);
            else
                printf("%*.*lx", width, precision, val);
        }
        else if (zero_pad && !left_justify && width > 0)
        {
            printf("%0*lx", width, val);
        }
        else if (width > 0)
        {
            if (left_justify)
                printf("%-*lx", width, val);
            else
                printf("%*lx", width, val);
        }
        else
        {
            printf("%lx", val);
        }
        break;
    }
    
    case 'X': /* Hexadecimal uppercase */
    {
        unsigned long val = arg && *arg ? strtoul(arg, NULL, 0) : 0;
        if (has_precision)
        {
            if (left_justify)
                printf("%-*.*lX", width, precision, val);
            else
                printf("%*.*lX", width, precision, val);
        }
        else if (zero_pad && !left_justify && width > 0)
        {
            printf("%0*lX", width, val);
        }
        else if (width > 0)
        {
            if (left_justify)
                printf("%-*lX", width, val);
            else
                printf("%*lX", width, val);
        }
        else
        {
            printf("%lX", val);
        }
        break;
    }
    
    case 's': /* String */
    {
        const char *str = arg ? arg : "";
        int len = (int)strlen(str);
        
        /* Apply precision (max chars) */
        if (has_precision && precision < len)
        {
            len = precision;
        }
        
        if (width > 0)
        {
            int pad = width - len;
            if (left_justify)
            {
                printf("%.*s%*s", len, str, pad > 0 ? pad : 0, "");
            }
            else
            {
                printf("%*s%.*s", pad > 0 ? pad : 0, "", len, str);
            }
        }
        else
        {
            printf("%.*s", len, str);
        }
        break;
    }
    
    case '%': /* Literal % */
        putchar('%');
        break;
    
    default:
        /* Unknown specifier - print as-is */
        putchar('%');
        if (spec) putchar(spec);
        return 1; /* Error */
    }
    
    return 0;
}

int builtin_printf(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    int argc = string_list_size(args);
    
    // for (int i = 0; i < string_list_size(args); i++)
    //     log_debug(string_cstr(string_list_at(args, i)));
    
    /* Need at least format string */
    if (argc < 2)
    {
        frame_set_error_printf(frame, "printf: usage: printf format [arguments...]");
        return 2;
    }
    
    const char *format = string_cstr(string_list_at(args, 1));
    int arg_index = 2; /* Start of actual arguments */
    int stop_output = 0;
    
    /* Process format string */
    while (!stop_output)
    {
        const char *f = format;
        int format_used = 0;
        
        while (*f && !stop_output)
        {
            if (*f == '%' && *(f + 1))
            {
                /* Get next argument (or empty string if no more args) */
                const char *arg = "";
                if (arg_index < argc)
                {
                    arg = string_cstr(string_list_at(args, arg_index));
                    arg_index++;
                    format_used = 1;
                }
                else if (format_used)
                {
                    /* No more arguments, use empty/zero defaults */
                    arg = "";
                }
                else
                {
                    /* First pass through format, no arguments yet */
                    arg = "";
                }
                
                int err = printf_process_format(&f, arg, &stop_output);
                if (err)
                {
                    frame_set_error_printf(frame, "printf: invalid format");
                    return 1;
                }
            }
            else if (*f == '\\' && *(f + 1))
            {
                /* Process escape sequences in format string */
                f++;
                switch (*f)
                {
                case 'a': putchar('\a'); break;
                case 'b': putchar('\b'); break;
                case 'c': stop_output = 1; break;
                case 'e': putchar('\033'); break;
                case 'f': putchar('\f'); break;
                case 'n': putchar('\n'); break;
                case 'r': putchar('\r'); break;
                case 't': putchar('\t'); break;
                case 'v': putchar('\v'); break;
                case '\\': putchar('\\'); break;
                case '0': /* Octal */
                {
                    int val = 0;
                    int count = 0;
                    f++;
                    while (count < 3 && *f >= '0' && *f <= '7')
                    {
                        val = val * 8 + (*f - '0');
                        f++;
                        count++;
                    }
                    f--;
                    putchar(val);
                    break;
                }
                default:
                    putchar(*f);
                    break;
                }
                f++;
            }
            else
            {
                putchar(*f);
                f++;
            }
        }
        
        /* If we used arguments, check if there are more to process */
        if (format_used && arg_index < argc)
        {
            /* Reuse format string with remaining arguments */
            continue;
        }
        else
        {
            /* Done */
            break;
        }
    }
    
    fflush(stdout);
    return 0;
}

/* ============================================================================
 * bracket (test) - Conditional expression evaluation
 * Implements: [ expression ]
 * Focuses on ISO C compatible tests
 * ============================================================================
 */

int builtin_bracket(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    int argc = string_list_size(args);
    
    /* Last argument must be "]" */
    if (argc < 2)
    {
        frame_set_error_printf(frame, "[: missing ']'");
        return 2;
    }
    
    const string_t *last_str = string_list_at(args, argc - 1);
    if (string_length(last_str) != 1 || string_cstr(last_str)[0] != ']')
    {
        frame_set_error_printf(frame, "[: missing ']'");
        return 2;
    }

    /* [ ] is false */
    if (argc == 2)
    {
        return 1;
    }

    /* Single argument - test if non-empty string */
    if (argc == 3)
    {
        const string_t *arg = string_list_at(args, 1);
        return (string_length(arg) > 0) ? 0 : 1;
    }

    /* Unary operators or field splitting artifacts */
    if (argc == 4)
    {
        const string_t *op_str = string_list_at(args, 1);
        const string_t *arg_str = string_list_at(args, 2);
        const char *op = string_cstr(op_str);

        /* First check for unary operators */

        /* File descriptor tests */
        if (strcmp(op, "-t") == 0)
        {
            int endpos = 0;
            long fd = string_atol_at(arg_str, 0, &endpos);

            /* Check if valid file descriptor number */
            if (endpos != (int)string_length(arg_str) || fd < 0)
            {
                frame_set_error_printf(frame, "[: -t: invalid file descriptor");
                return 2;
            }

#ifdef POSIX_API
            return isatty((int)fd) ? 0 : 1;
#elif defined UCRT_API
            return _isatty((int)fd) ? 0 : 1;
#endif
        /* else, fallthrough to failure */
        }

        /* String tests */
        if (strcmp(op, "-z") == 0)
            return (string_length(arg_str) == 0) ? 0 : 1;

        if (strcmp(op, "-n") == 0)
            return (string_length(arg_str) > 0) ? 0 : 1;

        /* Check for field splitting artifact: binary operator as first argument.
         * 
         * This happens when field splitting removes the entire first argument
         * (producing zero words), leaving the binary operator as the first token.
         * Per POSIX, this is expected behavior when an unquoted expansion produces
         * only IFS whitespace.
         * 
         * Example: [ $nl = "x" ] where nl contains only whitespace
         * Correctly becomes: [ = "x" ] after field splitting (argc=4)
         */

        /* Common binary operators */
        if (strcmp(op, "=") == 0 || strcmp(op, "!=") == 0 ||
            strcmp(op, "-eq") == 0 || strcmp(op, "-ne") == 0 ||
            strcmp(op, "-lt") == 0 || strcmp(op, "-le") == 0 ||
            strcmp(op, "-gt") == 0 || strcmp(op, "-ge") == 0)
        {
            fprintf(stderr, "[: %s: unary operator expected\n", op);
            return 2;
        }

        /* Unknown operator or too many arguments */
        fprintf(stderr, "[: too many arguments\n");
        return 2;
    }

    /* Binary operators */
    if (argc == 5)
    {
        const string_t *arg1_str = string_list_at(args, 1);
        const string_t *op_str = string_list_at(args, 2);
        const string_t *arg2_str = string_list_at(args, 3);
        const char *op = string_cstr(op_str);

        /* String comparisons */
        if (strcmp(op, "=") == 0)
            return (string_eq(arg1_str, arg2_str)) ? 0 : 1;
        
        if (strcmp(op, "!=") == 0)
            return (string_ne(arg1_str, arg2_str)) ? 0 : 1;

        /* Integer comparisons */
        int endpos1 = 0, endpos2 = 0;
        long val1 = string_atol_at(arg1_str, 0, &endpos1);
        long val2 = string_atol_at(arg2_str, 0, &endpos2);
        
        /* Check if both arguments are valid integers */
        int arg1_is_int = (endpos1 == (int)string_length(arg1_str));
        int arg2_is_int = (endpos2 == (int)string_length(arg2_str));

        if (arg1_is_int && arg2_is_int)
        {
            if (strcmp(op, "-eq") == 0)
                return (val1 == val2) ? 0 : 1;
            
            if (strcmp(op, "-ne") == 0)
                return (val1 != val2) ? 0 : 1;
            
            if (strcmp(op, "-lt") == 0)
                return (val1 < val2) ? 0 : 1;
            
            if (strcmp(op, "-le") == 0)
                return (val1 <= val2) ? 0 : 1;
            
            if (strcmp(op, "-gt") == 0)
                return (val1 > val2) ? 0 : 1;
            
            if (strcmp(op, "-ge") == 0)
                return (val1 >= val2) ? 0 : 1;
        }

        frame_set_error_printf(frame, "[: unknown operator '%s'", op);
        return 2;
    }

    frame_set_error_printf(frame, "[: too many arguments");
    return 2;
}

/* ============================================================================
 * return - Return from a function or dot script
 * ============================================================================
 */

int builtin_return(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    /* Check if return is valid (must be in a function or dot script) */
    exec_frame_t *return_target = frame_find_return_target(frame);
    if (!return_target)
    {
        frame_set_error_printf(frame, "return: can only be used in a function or sourced script");
        return 2;
    }

    /* Parse optional exit status argument */
    int exit_status = frame_get_last_exit_status(frame);

    if (string_list_size(args) > 1)
    {
        const string_t *arg_str = string_list_at(args, 1);
        int endpos = 0;
        long val = string_atol_at(arg_str, 0, &endpos);

        if (endpos != string_length(arg_str))
        {
            frame_set_error_printf(frame, "return: numeric argument required");
            return 2;
        }

        exit_status = (int)(val & 0xFF);
    }

    if (string_list_size(args) > 2)
    {
        frame_set_error_printf(frame, "return: too many arguments");
        return 1;
    }

    frame_set_pending_control_flow(frame, FRAME_FLOW_RETURN, 0);

    return exit_status;
}

/* ============================================================================
 * Builtin function classification and lookup
 * ============================================================================
 */
builtin_class_t builtin_classify_cstr(const char *name)
{
    if (name == NULL)
        return BUILTIN_NONE;

    for (builtin_implemented_function_map_t *p = builtin_implemented_functions; p->name != NULL;
         p++)
    {
        if (strcmp(name, p->name) == 0)
            return p->class;
    }

    return BUILTIN_NONE;
}

builtin_class_t builtin_classify(const string_t *name)
{
    if (name == NULL)
        return BUILTIN_NONE;

    return builtin_classify_cstr(string_cstr(name));
}

bool builtin_is_special_cstr(const char *name)
{
    return builtin_classify_cstr(name) == BUILTIN_SPECIAL;
}

bool builtin_is_special(const string_t *name)
{
    return builtin_classify(name) == BUILTIN_SPECIAL;
}

bool builtin_is_defined_cstr(const char *name)
{
    if (name == NULL)
        return false;
    for (builtin_implemented_function_map_t *p = builtin_implemented_functions; p->name != NULL;
         p++)
    {
        if (strcmp(name, p->name) == 0)
            return true;
    }
    return false;
}

bool builtin_is_defined(const string_t *name)
{
    if (name == NULL)
        return false;
    return builtin_is_defined_cstr(string_cstr(name));
}

builtin_func_t builtin_get_function_cstr(const char *name)
{
    if (name == NULL)
        return NULL;
    for (builtin_implemented_function_map_t *p = builtin_implemented_functions; p->name != NULL;
         p++)
    {
        if (strcmp(name, p->name) == 0)
            return p->func;
    }
    return NULL;
}

builtin_func_t builtin_get_function(const string_t *name)
{
    if (name == NULL)
        return NULL;
    return builtin_get_function_cstr(string_cstr(name));
}

/* ============================================================================
 * basename - Return the filename portion of a pathname
 * 
 * Usage: basename string [suffix]
 * 
 * The basename utility shall remove any prefix from string that ends with
 * the last '/' and an optional suffix.
 * 
 * Examples:
 *   basename /usr/bin/sort          -> "sort"
 *   basename /usr/bin/              -> "bin"
 *   basename stdio.h .h             -> "stdio"
 *   basename /home/user/file.txt .txt -> "file"
 * ============================================================================
 */

int builtin_basename(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    int argc = string_list_size(args);

    /* Usage check */
    if (argc < 2 || argc > 3)
    {
        frame_set_error_printf(frame, "basename: usage: basename string [suffix]");
        return 2;
    }

    const string_t *path_arg = string_list_at(args, 1);
    const string_t *suffix = (argc == 3) ? string_list_at(args, 2) : NULL;

    /* Handle empty string */
    if (!path_arg || string_empty(path_arg))
    {
        printf(".\n");
        return 0;
    }

    /* Make a working copy */
    string_t *work = string_create_from(path_arg);
    int len = string_length(work);

    /* Remove trailing slashes (except if the whole string is slashes) */
    while (len > 1)
    {
        char last = string_at(work, len - 1);
        if (last == '/' || last == '\\')
        {
            string_pop_back(work);
            len--;
        }
        else
        {
            break;
        }
    }

    /* If only slashes remain, return "/" */
    if (len == 1)
    {
        char first = string_at(work, 0);
        if (first == '/' || first == '\\')
        {
            printf("/\n");
            string_destroy(&work);
            return 0;
        }
    }

    /* Find last slash */
    int last_slash_pos = string_rfind_cstr(work, "/");
    int last_backslash_pos = string_rfind_cstr(work, "\\");
    
    /* Use whichever is later */
    int last_sep_pos = -1;
    if (last_slash_pos > last_backslash_pos)
        last_sep_pos = last_slash_pos;
    else if (last_backslash_pos > last_slash_pos)
        last_sep_pos = last_backslash_pos;
    else
        last_sep_pos = last_slash_pos; /* Both are -1 or equal */

    /* Extract basename */
    string_t *base;
    if (last_sep_pos >= 0 && last_sep_pos < len - 1)
    {
        /* There's a separator, take everything after it */
        base = string_create_from_range(work, last_sep_pos + 1, -1);
    }
    else
    {
        /* No separator or separator at end, use whole string */
        base = string_create_from(work);
    }

    /* Remove suffix if specified and matches */
    if (suffix && !string_empty(suffix))
    {
        int base_len = string_length(base);
        int suffix_len = string_length(suffix);

        /* Only remove if suffix is shorter and matches end of basename */
        if (suffix_len < base_len)
        {
            /* Check if the end of base matches suffix */
            string_t *base_end = string_create_from_range(base, base_len - suffix_len, -1);
            if (string_eq(base_end, suffix))
            {
                /* Remove the suffix */
                string_resize(base, base_len - suffix_len);
            }
            string_destroy(&base_end);
        }
    }

    /* Print the basename */
    printf("%s\n", string_cstr(base));
    
    string_destroy(&base);
    string_destroy(&work);
    return 0;
}

/* ============================================================================
 * dirname - Return the directory portion of a pathname
 * 
 * Usage: dirname string
 * 
 * The dirname utility shall remove the last component from string,
 * leaving the directory path.
 * 
 * Examples:
 *   dirname /usr/bin/sort           -> "/usr/bin"
 *   dirname /usr/bin/               -> "/usr"
 *   dirname stdio.h                 -> "."
 *   dirname /                       -> "/"
 *   dirname //                      -> "/"
 *   dirname ///                     -> "/"
 * ============================================================================
 */

int builtin_dirname(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    int argc = string_list_size(args);

    /* Usage check */
    if (argc != 2)
    {
        // frame_set_error_printf(frame, "dirname: usage: dirname string");
        fprintf(stderr, "dirname: usage: dirname string\n");
        return 2;
    }

    const string_t *path_arg = string_list_at(args, 1);

    log_debug("dirname: path_arg %s", string_cstr(path_arg));

    /* Handle empty string */
    if (!path_arg || string_empty(path_arg))
    {
        printf(".\n");
        return 0;
    }

    /* Make a working copy */
    string_t *work = string_create_from(path_arg);
    int len = string_length(work);

    /* Remove trailing slashes */
    while (len > 1)
    {
        char last = string_at(work, len - 1);
        if (last == '/' || last == '\\')
        {
            string_pop_back(work);
            len--;
        }
        else
        {
            break;
        }
    }

    /* If only slashes remain, return "/" */
    if (len == 1)
    {
        char first = string_at(work, 0);
        if (first == '/' || first == '\\')
        {
            printf("/\n");
            string_destroy(&work);
            return 0;
        }
    }

    /* If no slashes, return "." */
    int has_slash_pos = string_find_first_of_cstr(work, "/\\");
    if (has_slash_pos < 0)
    {
        printf(".\n");
        string_destroy(&work);
        return 0;
    }

    /* Find last slash */
    int last_slash_pos = string_rfind_cstr(work, "/");
    int last_backslash_pos = string_rfind_cstr(work, "\\");

    /* Use whichever is later */
    int last_sep_pos = -1;
    if (last_slash_pos > last_backslash_pos)
        last_sep_pos = last_slash_pos;
    else if (last_backslash_pos > last_slash_pos)
        last_sep_pos = last_backslash_pos;
    else
        last_sep_pos = last_slash_pos; /* Both are -1 or equal */

    /* Remove everything after the last slash */
    if (last_sep_pos >= 0)
    {
        string_resize(work, last_sep_pos);
        len = last_sep_pos;
    }

    /* Remove trailing slashes again */
    while (len > 1)
    {
        char last = string_at(work, len - 1);
        if (last == '/' || last == '\\')
        {
            string_pop_back(work);
            len--;
        }
        else
        {
            break;
        }
    }

    /* If we ended up with empty string or only slashes, return "/" */
    if (len == 0 || (len == 1 && (string_at(work, 0) == '/' || string_at(work, 0) == '\\')))
    {
        printf("/\n");
    }
    else
    {
        printf("%s\n", string_cstr(work));
    }

    string_destroy(&work);
    return 0;
}

/* ============================================================================
 * mgsh_dirnamevar - Compute dirname and assign to a variable
 * 
 * Usage: mgsh_dirnamevar varname string
 * 
 * This is a workaround for platforms where command substitution doesn't work
 * reliably (like UCRT). Instead of:
 *   SCRIPT_DIR=$(dirname "$0")
 * Use:
 *   mgsh_dirnamevar SCRIPT_DIR "$0"
 * 
 * Examples:
 *   mgsh_dirnamevar mydir /usr/bin/sort    -> sets mydir="/usr/bin"
 *   mgsh_dirnamevar mydir stdio.h          -> sets mydir="."
 *   mgsh_dirnamevar mydir /                -> sets mydir="/"
 * ============================================================================
 */

int builtin_mgsh_dirnamevar(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    int argc = string_list_size(args);

    /* Usage check */
    if (argc != 3)
    {
        fprintf(stderr, "mgsh_dirnamevar: usage: mgsh_dirnamevar varname string\n");
        return 2;
    }

    const string_t *varname_arg = string_list_at(args, 1);
    const string_t *path_arg = string_list_at(args, 2);
    log_debug("mgsh_dirnamevar: (%s, %s)", string_cstr(varname_arg), string_cstr(path_arg));

    /* Validate variable name is not empty */
    if (!varname_arg || string_empty(varname_arg))
    {
        fprintf(stderr, "mgsh_dirnamevar: variable name cannot be empty\n");
        return 2;
    }

    /* Handle empty path string - dirname of empty is "." */
    if (!path_arg || string_empty(path_arg))
    {
        frame_set_persistent_variable_cstr(frame, string_cstr(varname_arg), ".");
        return 0;
    }

    /* Make a working copy */
    string_t *work = string_create_from(path_arg);
    int len = string_length(work);

    /* Remove trailing slashes */
    while (len > 1)
    {
        char last = string_at(work, len - 1);
        if (last == '/' || last == '\\')
        {
            string_pop_back(work);
            len--;
        }
        else
        {
            break;
        }
    }

    /* If only slashes remain, result is "/" */
    if (len == 1)
    {
        char first = string_at(work, 0);
        if (first == '/' || first == '\\')
        {
            frame_set_persistent_variable_cstr(frame, string_cstr(varname_arg), "/");
            string_destroy(&work);
            return 0;
        }
    }

    /* If no slashes, result is "." */
    int has_slash_pos = string_find_first_of_cstr(work, "/\\");
    if (has_slash_pos < 0)
    {
        frame_set_persistent_variable_cstr(frame, string_cstr(varname_arg), ".");
        string_destroy(&work);
        return 0;
    }

    /* Find last slash */
    int last_slash_pos = string_rfind_cstr(work, "/");
    int last_backslash_pos = string_rfind_cstr(work, "\\");

    /* Use whichever is later */
    int last_sep_pos = -1;
    if (last_slash_pos > last_backslash_pos)
        last_sep_pos = last_slash_pos;
    else if (last_backslash_pos > last_slash_pos)
        last_sep_pos = last_backslash_pos;
    else
        last_sep_pos = last_slash_pos; /* Both are -1 or equal */

    /* Remove everything after the last slash */
    if (last_sep_pos >= 0)
    {
        string_resize(work, last_sep_pos);
        len = last_sep_pos;
    }

    /* Remove trailing slashes again */
    while (len > 1)
    {
        char last = string_at(work, len - 1);
        if (last == '/' || last == '\\')
        {
            string_pop_back(work);
            len--;
        }
        else
        {
            break;
        }
    }

    /* If we ended up with empty string or only slashes, result is "/" */
    if (len == 0 || (len == 1 && (string_at(work, 0) == '/' || string_at(work, 0) == '\\')))
    {
        frame_set_persistent_variable_cstr(frame, string_cstr(varname_arg), "/");
    }
    else
    {
        frame_set_persistent_variable_cstr(frame, string_cstr(varname_arg), string_cstr(work));
    }

    string_destroy(&work);
    return 0;
}

/* ============================================================================
 * mgsh_printfvar - Format data and assign to a variable
 * 
 * Usage: mgsh_printfvar varname format [arguments...]
 * 
 * This is a workaround for platforms where command substitution doesn't work
 * reliably (like UCRT). Instead of:
 *   RESULT=$(printf "%s-%d" "$name" "$count")
 * Use:
 *   mgsh_printfvar RESULT "%s-%d" "$name" "$count"
 * 
 * Supports the same format specifiers as printf:
 *   %b    - String with backslash escapes interpreted
 *   %c    - Character
 *   %d, %i - Signed decimal integer
 *   %u    - Unsigned decimal integer
 *   %o    - Unsigned octal integer
 *   %x    - Unsigned hexadecimal (lowercase)
 *   %X    - Unsigned hexadecimal (uppercase)
 *   %s    - String
 *   %%    - Literal %
 * ============================================================================
 */

/* Helper: Process a single format specifier and append to output string */
static int printfvar_process_format(string_t *output, const char **fmt, const char *arg, int *stop_output)
{
    const char *f = *fmt;
    int width = 0;
    int precision = -1;
    int left_justify = 0;
    int zero_pad = 0;
    int has_precision = 0;
    char buf[256];

    *stop_output = 0;

    /* Skip % */
    f++;

    /* Parse flags */
    while (*f == '-' || *f == '0' || *f == ' ' || *f == '+' || *f == '#')
    {
        if (*f == '-') left_justify = 1;
        if (*f == '0') zero_pad = 1;
        f++;
    }

    /* Parse width */
    if (*f >= '0' && *f <= '9')
    {
        width = printf_parse_number(&f);
    }

    /* Parse precision */
    if (*f == '.')
    {
        f++;
        has_precision = 1;
        precision = printf_parse_number(&f);
    }

    /* Get conversion specifier */
    char spec = *f;
    if (spec) f++;

    *fmt = f;

    /* Process conversion */
    switch (spec)
    {
    case 'b': /* String with backslash escapes */
    {
        string_t *processed = printf_process_escapes(arg, stop_output);
        const char *processed_str = string_cstr(processed);
        if (width > 0)
        {
            int len = (int)strlen(processed_str);
            int pad = width - len;
            if (pad > 0)
            {
                if (left_justify)
                {
                    string_append_cstr(output, processed_str);
                    for (int i = 0; i < pad; i++)
                        string_append_char(output, ' ');
                }
                else
                {
                    for (int i = 0; i < pad; i++)
                        string_append_char(output, ' ');
                    string_append_cstr(output, processed_str);
                }
            }
            else
            {
                string_append_cstr(output, processed_str);
            }
        }
        else
        {
            string_append_cstr(output, processed_str);
        }
        string_destroy(&processed);
        break;
    }

    case 'c': /* Character */
    {
        int c = arg && *arg ? (unsigned char)*arg : 0;
        if (width > 0)
        {
            if (left_justify)
                snprintf(buf, sizeof(buf), "%-*c", width, c);
            else
                snprintf(buf, sizeof(buf), "%*c", width, c);
            string_append_cstr(output, buf);
        }
        else
        {
            string_append_char(output, (char)c);
        }
        break;
    }

    case 'd':
    case 'i': /* Signed decimal */
    {
        long val = arg && *arg ? strtol(arg, NULL, 0) : 0;
        if (has_precision)
        {
            if (left_justify)
                snprintf(buf, sizeof(buf), "%-*.*ld", width, precision, val);
            else
                snprintf(buf, sizeof(buf), "%*.*ld", width, precision, val);
        }
        else if (zero_pad && !left_justify && width > 0)
        {
            snprintf(buf, sizeof(buf), "%0*ld", width, val);
        }
        else if (width > 0)
        {
            if (left_justify)
                snprintf(buf, sizeof(buf), "%-*ld", width, val);
            else
                snprintf(buf, sizeof(buf), "%*ld", width, val);
        }
        else
        {
            snprintf(buf, sizeof(buf), "%ld", val);
        }
        string_append_cstr(output, buf);
        break;
    }

    case 'u': /* Unsigned decimal */
    {
        unsigned long val = arg && *arg ? strtoul(arg, NULL, 0) : 0;
        if (has_precision)
        {
            if (left_justify)
                snprintf(buf, sizeof(buf), "%-*.*lu", width, precision, val);
            else
                snprintf(buf, sizeof(buf), "%*.*lu", width, precision, val);
        }
        else if (zero_pad && !left_justify && width > 0)
        {
            snprintf(buf, sizeof(buf), "%0*lu", width, val);
        }
        else if (width > 0)
        {
            if (left_justify)
                snprintf(buf, sizeof(buf), "%-*lu", width, val);
            else
                snprintf(buf, sizeof(buf), "%*lu", width, val);
        }
        else
        {
            snprintf(buf, sizeof(buf), "%lu", val);
        }
        string_append_cstr(output, buf);
        break;
    }

    case 'o': /* Octal */
    {
        unsigned long val = arg && *arg ? strtoul(arg, NULL, 0) : 0;
        if (has_precision)
        {
            if (left_justify)
                snprintf(buf, sizeof(buf), "%-*.*lo", width, precision, val);
            else
                snprintf(buf, sizeof(buf), "%*.*lo", width, precision, val);
        }
        else if (zero_pad && !left_justify && width > 0)
        {
            snprintf(buf, sizeof(buf), "%0*lo", width, val);
        }
        else if (width > 0)
        {
            if (left_justify)
                snprintf(buf, sizeof(buf), "%-*lo", width, val);
            else
                snprintf(buf, sizeof(buf), "%*lo", width, val);
        }
        else
        {
            snprintf(buf, sizeof(buf), "%lo", val);
        }
        string_append_cstr(output, buf);
        break;
    }

    case 'x': /* Hexadecimal lowercase */
    {
        unsigned long val = arg && *arg ? strtoul(arg, NULL, 0) : 0;
        if (has_precision)
        {
            if (left_justify)
                snprintf(buf, sizeof(buf), "%-*.*lx", width, precision, val);
            else
                snprintf(buf, sizeof(buf), "%*.*lx", width, precision, val);
        }
        else if (zero_pad && !left_justify && width > 0)
        {
            snprintf(buf, sizeof(buf), "%0*lx", width, val);
        }
        else if (width > 0)
        {
            if (left_justify)
                snprintf(buf, sizeof(buf), "%-*lx", width, val);
            else
                snprintf(buf, sizeof(buf), "%*lx", width, val);
        }
        else
        {
            snprintf(buf, sizeof(buf), "%lx", val);
        }
        string_append_cstr(output, buf);
        break;
    }

    case 'X': /* Hexadecimal uppercase */
    {
        unsigned long val = arg && *arg ? strtoul(arg, NULL, 0) : 0;
        if (has_precision)
        {
            if (left_justify)
                snprintf(buf, sizeof(buf), "%-*.*lX", width, precision, val);
            else
                snprintf(buf, sizeof(buf), "%*.*lX", width, precision, val);
        }
        else if (zero_pad && !left_justify && width > 0)
        {
            snprintf(buf, sizeof(buf), "%0*lX", width, val);
        }
        else if (width > 0)
        {
            if (left_justify)
                snprintf(buf, sizeof(buf), "%-*lX", width, val);
            else
                snprintf(buf, sizeof(buf), "%*lX", width, val);
        }
        else
        {
            snprintf(buf, sizeof(buf), "%lX", val);
        }
        string_append_cstr(output, buf);
        break;
    }

    case 's': /* String */
    {
        const char *str = arg ? arg : "";
        int len = (int)strlen(str);

        /* Apply precision (max chars) */
        if (has_precision && precision < len)
        {
            len = precision;
        }

        if (width > 0)
        {
            int pad = width - len;
            if (left_justify)
            {
                for (int i = 0; i < len; i++)
                    string_append_char(output, str[i]);
                for (int i = 0; i < pad; i++)
                    string_append_char(output, ' ');
            }
            else
            {
                for (int i = 0; i < pad; i++)
                    string_append_char(output, ' ');
                for (int i = 0; i < len; i++)
                    string_append_char(output, str[i]);
            }
        }
        else
        {
            for (int i = 0; i < len; i++)
                string_append_char(output, str[i]);
        }
        break;
    }

    case '%': /* Literal % */
        string_append_char(output, '%');
        break;

    default:
        /* Unknown specifier - append as-is */
        string_append_char(output, '%');
        if (spec) string_append_char(output, spec);
        return 1; /* Error */
    }

    return 0;
}

int builtin_mgsh_printfvar(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    int argc = string_list_size(args);

    /* Need at least variable name and format string */
    if (argc < 3)
    {
        fprintf(stderr, "mgsh_printfvar: usage: mgsh_printfvar varname format [arguments...]\n");
        return 2;
    }

    const string_t *varname_arg = string_list_at(args, 1);

    /* Validate variable name is not empty */
    if (!varname_arg || string_empty(varname_arg))
    {
        fprintf(stderr, "mgsh_printfvar: variable name cannot be empty\n");
        return 2;
    }

    const char *format = string_cstr(string_list_at(args, 2));
    int arg_index = 3; /* Start of actual arguments */
    int stop_output = 0;

    /* Create output buffer */
    string_t *output = string_create();

    /* Process format string */
    while (!stop_output)
    {
        const char *f = format;
        int format_used = 0;

        while (*f && !stop_output)
        {
            if (*f == '%' && *(f + 1))
            {
                /* Get next argument (or empty string if no more args) */
                const char *arg = "";
                if (arg_index < argc)
                {
                    arg = string_cstr(string_list_at(args, arg_index));
                    arg_index++;
                    format_used = 1;
                }
                else if (format_used)
                {
                    /* No more arguments, use empty/zero defaults */
                    arg = "";
                }
                else
                {
                    /* First pass through format, no arguments yet */
                    arg = "";
                }

                int err = printfvar_process_format(output, &f, arg, &stop_output);
                if (err)
                {
                    frame_set_error_printf(frame, "mgsh_printfvar: invalid format");
                    string_destroy(&output);
                    return 1;
                }
            }
            else if (*f == '\\' && *(f + 1))
            {
                /* Process escape sequences in format string */
                f++;
                switch (*f)
                {
                case 'a': string_append_char(output, '\a'); break;
                case 'b': string_append_char(output, '\b'); break;
                case 'c': stop_output = 1; break;
                case 'e': string_append_char(output, '\033'); break;
                case 'f': string_append_char(output, '\f'); break;
                case 'n': string_append_char(output, '\n'); break;
                case 'r': string_append_char(output, '\r'); break;
                case 't': string_append_char(output, '\t'); break;
                case 'v': string_append_char(output, '\v'); break;
                case '\\': string_append_char(output, '\\'); break;
                case '0': /* Octal */
                {
                    int val = 0;
                    int count = 0;
                    f++;
                    while (count < 3 && *f >= '0' && *f <= '7')
                    {
                        val = val * 8 + (*f - '0');
                        f++;
                        count++;
                    }
                    f--;
                    string_append_char(output, (char)val);
                    break;
                }
                default:
                    string_append_char(output, *f);
                    break;
                }
                f++;
            }
            else
            {
                string_append_char(output, *f);
                f++;
            }
        }

        /* If we used arguments, check if there are more to process */
        if (format_used && arg_index < argc)
        {
            /* Reuse format string with remaining arguments */
            continue;
        }
        else
        {
            /* Done */
            break;
        }
    }

    /* Assign result to variable */
    frame_set_persistent_variable(frame, varname_arg, output);

    string_destroy(&output);
    return 0;
}

/* ============================================================================
 * true / false - Return success or failure
 * ============================================================================
 */

int builtin_true(exec_frame_t *frame, const string_list_t *args)
{
    (void)frame;
    (void)args;
    return 0;
}

int builtin_false(exec_frame_t *frame, const string_list_t *args)
{
    (void)frame;
    (void)args;
    return 1;
}

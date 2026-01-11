#include "builtins.h"
#include "string_t.h"
#include "logging.h"
#include "getopt.h"
#include "xalloc.h"
#include "exec.h"
#include "variable_store.h"
#include "variable_map.h"
#include "lib.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

typedef struct builtin_implemented_function_map_t
{
    const char *name;
    builtin_class_t class;
    builtin_func_t func;
} builtin_implemented_function_map_t;

builtin_implemented_function_map_t builtin_implemented_functions[] = {
    /* { "break", BUILTIN_SPECIAL, builtin_break}, */
    { ":", BUILTIN_SPECIAL, builtin_colon},
    /* { "continue", BUILTIN_SPECIAL, builtin_continue}, */
    { ".", BUILTIN_SPECIAL, builtin_dot},
    /* { "eval", BUILTIN_SPECIAL, builtin_eval}, */
    /* { "exec", BUILTIN_SPECIAL, builtin_exec}, */
    /* { "exit", BUILTIN_SPECIAL, builtin_exit}, */
    { "export", BUILTIN_SPECIAL, builtin_export},
    /* { "readonly", BUILTIN_SPECIAL, builtin_readonly}, */
    /* { "return", BUILTIN_SPECIAL, builtin_return}, */
    {"set", BUILTIN_SPECIAL, builtin_set},
    /* { "shift", BUILTIN_SPECIAL, builtin_shift}, */
    /* { "times", BUILTIN_SPECIAL, builtin_times}, */
    /* { "trap", BUILTIN_SPECIAL, builtin_trap}, */
    { "unset", BUILTIN_SPECIAL, builtin_unset},

    /* { "cd", BUILTIN_REGULAR, builtin_cd}, */
    /* { "pwd", BUILTIN_REGULAR, builtin_pwd}, */
    /* { "echo", BUILTIN_REGULAR, builtin_echo}, */
    /* { "printf", BUILTIN_REGULAR, builtin_printf}, */
    /* { "test", BUILTIN_REGULAR, builtin_test}, */
    /* { "[", BUILTIN_REGULAR, builtin_bracket}, */
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
    /* { "fg", BUILTIN_REGULAR, builtin_fg}, */
    /* { "bg", BUILTIN_REGULAR, builtin_bg}, */
    /* { "wait", BUILTIN_REGULAR, builtin_wait}, */
    /* { "kill", BUILTIN_REGULAR, builtin_kill}, */
    /* { "true", BUILTIN_REGULAR, builtin_true}, */
    /* { "false", BUILTIN_REGULAR, builtin_false}, */
    {NULL, BUILTIN_NONE, NULL} // Sentinel
};

/* ============================================================================
 * colon - do nothing builtin
 * ============================================================================
 */

int builtin_colon(exec_t *ex, const string_list_t *args)
{
    /* Suppress unused parameter warnings */
    (void)ex;
    (void)args;

    /* Do nothing, return success */
    return 0;
}

/* ============================================================================
 * dot - run file contents in current environment
 * ============================================================================
 */

int builtin_dot(exec_t *ex, const string_list_t *args)
{
    Expects_not_null(ex);
    Expects_not_null(args);

    if (string_list_size(args) != 1)
    {
        exec_set_error(ex, "dot: filename argument required");
        return 2; /* misuse of shell builtin */
    }
    FILE *fp = fopen(string_cstr(string_list_at(args, 1)), "r");
    if (!fp)
    {
        exec_set_error(ex, "dot: cannot open file: %s", string_cstr(string_list_at(args, 1)));
        return 1; /* general error */
    }
    exec_status_t status = exec_execute_stream(ex, fp);
    fclose(fp);
    return (status == EXEC_OK) ? 0 : 1;
}

/* ============================================================================
 * export - export variables to environment
 * ============================================================================
 */

static void builtin_export_print_usage(FILE *stream)
{
    fprintf(stream, "Usage: export [VAR[=VALUE] ...]\n");
    fprintf(stream, "Export shell variables to the environment.\n");
    fprintf(stream, "With no arguments, prints all exported variables.\n");
}

void builtin_export_variable_store_print(const string_t *name, const string_t *val, bool exported,
                                         bool read_only, void *user_data)
{
    Expects_not_null(name);
    Expects_not_null(val);
    FILE *stream = (FILE *)user_data;
    if (stream == NULL)
    {
        stream = stdout;
    }
    if (exported)
    {
        /* Quote according to POSIX shell rules so output is reinput-safe */
        string_t *quoted = lib_quote(name, val);
        fprintf(stream, "export %s\n", string_cstr(quoted));
        string_destroy(&quoted);
    }
}

static void builtin_export_variable_store_print_exported(const variable_store_t *var_store)
{
    Expects_not_null(var_store);
    if (!var_store->map)
    {
        return; /* No variables to print */
    }
    variable_store_for_each(var_store, builtin_export_variable_store_print, stdout);
}

int builtin_export(exec_t *ex, const string_list_t *args)
{
    Expects_not_null(ex);
    Expects_not_null(args);

    variable_store_t *var_store = ex->variables;
    if (!var_store)
    {
        exec_set_error(ex, "export: no variable store available");
        return 1;
    }

    /* No arguments → print exported variables */
    if (string_list_size(args) == 1)
    {
        builtin_export_variable_store_print_exported(var_store);
        return 0;
    }

    int exit_status = 0;

    for (int i = 1; i < string_list_size(args); i++)
    {
        const string_t *arg = string_list_at(args, i);
        if (!arg || string_empty(arg))
        {
            exec_set_error(ex, "export: invalid variable name");
            exit_status = 2;
            continue;
        }

#if defined(POSIX_API) || defined(UCRT_API)
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

        var_store_error_t res = variable_store_add(var_store, name, value, false, false);

        if (res == VAR_STORE_ERROR_READ_ONLY)
        {
            exec_set_error(ex, "export: variable '%s' is read-only", string_cstr(name));
            exit_status = 1;
        }
        else if (res == VAR_STORE_ERROR_EMPTY_NAME || res == VAR_STORE_ERROR_NAME_TOO_LONG ||
                 res == VAR_STORE_ERROR_NAME_STARTS_WITH_DIGIT ||
                 res == VAR_STORE_ERROR_NAME_INVALID_CHARACTER)
        {
            exec_set_error(ex, "export: invalid variable name '%s'", string_cstr(name));
            exit_status = 2;
        }
        else if (res != VAR_STORE_ERROR_NONE)
        {
            exec_set_error(ex, "export: failed to set variable");
            exit_status = 1;
        }
        else
        {
            /* Successful: update environment + store */
#ifdef POSIX_API
            if (eq_pos)
                putenv(string_cstr(arg));
#elifdef UCRT_API
            if (eq_pos)
            {
                int ret = _putenv_s(string_cstr(name), string_cstr(value));
                if (ret != 0)
                {
                    exec_set_error(ex, "export: failed to set environment variable");
                    exit_status = 1;
                }
            }
#endif
            variable_store_set_exported(var_store, name, true);
        }

        string_destroy(&name);
        string_destroy(&value);

#else
        exec_set_error(ex, "export: not supported on this platform");
        exit_status = 1;
#endif
    }

    return exit_status;
}



/* ============================================================================
 * set - Set or unset shell options and positional parameters
 * ============================================================================
 */

static void builtin_set_print_options(exec_t *ex, bool reusable_format);


/* Helper structure for sorting variables */
typedef struct
{
    const string_t *key;
    const string_t *value;
} builtin_set_var_entry_t;

/* Comparison function for sorting variables by name using locale collation */
static int builtin_set_compare_vars(const void *a, const void *b)
{
    const builtin_set_var_entry_t *va = (const builtin_set_var_entry_t *)a;
    const builtin_set_var_entry_t *vb = (const builtin_set_var_entry_t *)b;
    return lib_strcoll(string_cstr(va->key), string_cstr(vb->key));
}

/* Print all variables in collation sequence (set with no arguments) */
static int builtin_set_print_variables(exec_t *ex)
{
    Expects_not_null(ex);

    if (!ex->variables || !ex->variables->map)
    {
        return 0; /* No variables to print */
    }

    variable_map_t *map = ex->variables->map;

    /* Count occupied entries */
    int count = 0;
    for (int32_t i = 0; i < map->capacity; i++)
    {
        if (map->entries[i].occupied)
            count++;
    }

    if (count == 0)
        return 0; /* No variables to print */

    /* Collect all variables into an array */
    builtin_set_var_entry_t *vars = xmalloc(count * sizeof(builtin_set_var_entry_t));
    int idx = 0;
    for (int32_t i = 0; i < map->capacity; i++)
    {
        if (map->entries[i].occupied)
        {
            vars[idx].key = map->entries[i].key;
            vars[idx].value = map->entries[i].mapped.value;
            idx++;
        }
    }

    /* Sort by name using locale collation */
    qsort(vars, count, sizeof(builtin_set_var_entry_t), builtin_set_compare_vars);

    /* Print each variable in name=value format */
    for (int i = 0; i < count; i++)
    {
        /* Quote according to POSIX shell rules so output is reinput-safe */
        string_t *quoted = lib_quote(vars[i].key, vars[i].value);
        fprintf(stdout, "%s\n", string_cstr(quoted));
        string_destroy(&quoted);
    }

    xfree(vars);
    return 0;
}

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

/* Set or unset a named option via -o/+o */
static int builtin_set_set_named_option(exec_t *ex, const char *name, bool unset)
{
    Expects_not_null(ex);
    Expects_not_null(name);

    bool value = !unset;
    bool handled = true;

    if (strcmp(name, "allexport") == 0)
        ex->opt.allexport = value;
    else if (strcmp(name, "errexit") == 0)
        ex->opt.errexit = value;
    else if (strcmp(name, "ignoreeof") == 0)
        ex->opt.ignoreeof = value;
    else if (strcmp(name, "noclobber") == 0)
        ex->opt.noclobber = value;
    else if (strcmp(name, "noglob") == 0)
        ex->opt.noglob = value;
    else if (strcmp(name, "noexec") == 0)
        ex->opt.noexec = value;
    else if (strcmp(name, "nounset") == 0)
        ex->opt.nounset = value;
    else if (strcmp(name, "pipefail") == 0)
        ex->opt.pipefail = value;
    else if (strcmp(name, "verbose") == 0)
        ex->opt.verbose = value;
    else if (strcmp(name, "vi") == 0)
        ex->opt.vi = value;
    else if (strcmp(name, "xtrace") == 0)
        ex->opt.xtrace = value;
    else if (strcmp(name, "monitor") == 0 || strcmp(name, "notify") == 0)
    {
        // Not implemented yet
        handled = false;
    }
    else
    {
        handled = false;
    }

    if (handled)
    {
        ex->opt_flags_set = true;
        return 0;
    }

    fprintf(stderr, "set: option '%s' not supported yet\n", name);
    return 1;
}

/* Print all shell options (set -o) */
static void builtin_set_print_options(exec_t *ex, bool reusable_format)
{
    Expects_not_null(ex);

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

int builtin_set(exec_t *ex, const string_list_t *args)
{
    Expects_not_null(ex);
    Expects_not_null(args);

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
            builtin_set_set_named_option(ex, state.optarg, state.opt_plus_prefix);
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
        builtin_set_print_options(ex, reusable_format);
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
            int ret = builtin_set_print_variables(ex);
            xfree(argv);
            return ret;
        }
    }

    /* Apply collected short options to executor flags */
    if (flag_a != -1) { ex->opt.allexport = (flag_a != 0); options_changed = true; }
    if (flag_C != -1) { ex->opt.noclobber = (flag_C != 0); options_changed = true; }
    if (flag_e != -1) { ex->opt.errexit = (flag_e != 0); options_changed = true; }
    if (flag_f != -1) { ex->opt.noglob = (flag_f != 0); options_changed = true; }
    if (flag_n != -1) { ex->opt.noexec = (flag_n != 0); options_changed = true; }
    if (flag_u != -1) { ex->opt.nounset = (flag_u != 0); options_changed = true; }
    if (flag_v != -1) { ex->opt.verbose = (flag_v != 0); options_changed = true; }
    if (flag_x != -1) { ex->opt.xtrace = (flag_x != 0); options_changed = true; }

    /* Flags not yet implemented: b (notify), h (hashall), m (monitor) */

    if (options_changed)
        ex->opt_flags_set = true;

    /* Replace positional parameters if requested (includes explicit "set --") */
    if (have_positional_request)
    {
        if (!ex->positional_params)
        {
            ex->positional_params = positional_params_create();
            if (!ex->positional_params)
            {
                fprintf(stderr, "set: failed to allocate positional parameters\n");
                xfree(argv);
                return 1;
            }
        }

        int max_params = positional_params_get_max(ex->positional_params);
        if (new_param_count > max_params)
        {
            fprintf(stderr, "set: too many positional parameters (max %d)\n", max_params);
            xfree(argv);
            return 1;
        }

        string_t **new_params = NULL;
        if (new_param_count > 0)
        {
            new_params = xcalloc((size_t)new_param_count, sizeof(string_t *));
            for (int i = 0; i < new_param_count; i++)
            {
                new_params[i] = string_create_from_cstr(argv[state.optind + i]);
            }
        }

        if (!positional_params_replace(ex->positional_params, new_params, new_param_count))
        {
            if (new_params)
            {
                for (int i = 0; i < new_param_count; i++)
                {
                    if (new_params[i])
                        string_destroy(&new_params[i]);
                }
                xfree(new_params);
            }
            fprintf(stderr, "set: failed to replace positional parameters\n");
            xfree(argv);
            return 1;
        }
    }

    xfree(argv);
    return 0;
}

/* ============================================================================
 * unset - unset values and attributes of variables and functions
 * ============================================================================
 */
int builtin_unset(exec_t *ex, const string_list_t *args)
{
    Expects_not_null(ex);
    Expects_not_null(args);
    int flag_f = 0;
    int flag_v = 0;
    int flag_err = 0;
    int err_count = 0;
    int c;
    variable_store_t *var_store = ex->variables;
    func_store_t *func_store = ex->functions;
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
            func_store_error_t err;
            err = func_store_remove(func_store, string_list_at(args, optind));
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
            /* Either flag_v or no specific flag */
            if (!variable_store_has_name(var_store, string_list_at(args, optind)))
            {
                fprintf(stderr, "unset: variable '%s' not found\n",
                        string_cstr(string_list_at(args, optind)));
                err_count++;
            }
            else if (variable_store_is_read_only(var_store, string_list_at(args, optind)))
            {
                fprintf(stderr, "unset: variable '%s' is read-only\n",
                        string_cstr(string_list_at(args, optind)));
                err_count++;
            }
            else
                variable_store_remove(var_store, string_list_at(args, optind));
        }
    }
    if (err_count > 0)
        return 1;
    return 0;
}


/* ============================================================================
 * jobs - Job control builtins
 * ============================================================================
 */

/* ============================================================================
 * builtin_jobs.c
 *
 * Implementation of the POSIX 'jobs' builtin command.
 *
 * Synopsis:
 *   jobs [-l | -p] [job_id...]
 *
 * Options:
 *   -l    Long format: include process IDs
 *   -p    PID only: display only the process group leader's PID
 *
 * If job_id arguments are given, only those jobs are displayed.
 * Otherwise, all jobs are displayed.
 *
 * Exit status:
 *   0     Successful completion
 *   >0    An error occurred (e.g., invalid job_id)
 *
 * ============================================================================ */

typedef enum
{
    JOBS_FORMAT_DEFAULT,
    JOBS_FORMAT_LONG,    /* -l: include PIDs */
    JOBS_FORMAT_PID_ONLY /* -p: only PIDs */
} jobs_format_t;

static const char *builtin_jobs_job_state_to_string(job_state_t state)
{
    switch (state)
    {
    case JOB_RUNNING:
        return "Running";
    case JOB_STOPPED:
        return "Stopped";
    case JOB_DONE:
        return "Done";
    case JOB_TERMINATED:
        return "Terminated";
    default:
        return "Unknown";
    }
}

static char builtin_jobs_job_indicator(const job_store_t *store, const job_t *job)
{
    if (job == store->current_job)
        return '+';
    if (job == store->previous_job)
        return '-';
    return ' ';
}

/* ----------------------------------------------------------------------------
 * Helper: Parse job_id from string
 *
 * Accepts:
 *   %n    - job number n
 *   %+    - current job
 *   %%    - current job
 *   %-    - previous job
 *   %?str - job whose command contains str (not implemented)
 *   %str  - job whose command starts with str (not implemented)
 *   n     - job number n (without %)
 *
 * Returns job_id on success, -1 on error
 * ---------------------------------------------------------------------------- */

static int builtin_jobs_parse_job_id(const job_store_t *store, const char *arg)
{
    if (!arg || !*arg)
        return -1;

    if (arg[0] == '%')
    {
        arg++; /* Skip % */

        if (*arg == '\0' || *arg == '+' || *arg == '%')
        {
            /* %%, %+, or just % -> current job */
            job_t *current = job_store_get_current(store);
            return current ? current->job_id : -1;
        }

        if (*arg == '-')
        {
            /* %- -> previous job */
            job_t *previous = job_store_get_previous(store);
            return previous ? previous->job_id : -1;
        }

        /* %n -> job number n */
        char *endptr;
        long val = strtol(arg, &endptr, 10);
        if (*endptr == '\0' && val > 0)
        {
            return (int)val;
        }

        /* %?str or %str not implemented */
        return -1;
    }

    /* Plain number */
    char *endptr;
    long val = strtol(arg, &endptr, 10);
    if (*endptr == '\0' && val > 0)
    {
        return (int)val;
    }

    return -1;
}

static void builtin_jobs_print_job(const job_store_t *store, const job_t *job, jobs_format_t format)
{
    if (!job)
        return;

    char indicator = builtin_jobs_job_indicator(store, job);
    const char *state_str = builtin_jobs_job_state_to_string(job->state);
    const char *cmd = job->command_line ? string_cstr(job->command_line) : "";

    switch (format)
    {
    case JOBS_FORMAT_PID_ONLY:
        /* Print only the process group leader PID */
        if (job->processes)
        {
#ifdef POSIX_API
            printf("%d\n", (int)job->pgid);
#else
            printf("%d\n", job->pgid);
#endif
        }
        break;

    case JOBS_FORMAT_LONG:
        /* Long format: [job_id]± PID state command */
        printf("[%d]%c ", job->job_id, indicator);

        /* Print each process in the pipeline */
        for (process_t *proc = job->processes; proc; proc = proc->next)
        {
#ifdef POSIX_API
            printf("%d ", (int)proc->pid);
#else
            printf("%d ", proc->pid);
#endif
        }

        printf(" %s\t%s\n", state_str, cmd);
        break;

    case JOBS_FORMAT_DEFAULT:
    default:
        /* Default format: [job_id]± state command */
        printf("[%d]%c  %s\t\t%s\n", job->job_id, indicator, state_str, cmd);
        break;
    }
}

int builtin_jobs(exec_t *ex, const string_list_t *args)
{
    if (!ex)
        return 1;

    job_store_t *store = ex->jobs;
    if (!store)
    {
        /* No job store - nothing to show */
        return 0;
    }

    jobs_format_t format = JOBS_FORMAT_DEFAULT;
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
                format = JOBS_FORMAT_LONG;
                break;
            case 'p':
                format = JOBS_FORMAT_PID_ONLY;
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
            const char *arg = string_cstr(string_list_at(args, i));
            int job_id = builtin_jobs_parse_job_id(store, arg);

            if (job_id < 0)
            {
                fprintf(stderr, "jobs: %s: no such job\n", arg);
                exit_status = 1;
                continue;
            }

            job_t *job = job_store_find(store, job_id);
            if (!job)
            {
                fprintf(stderr, "jobs: %s: no such job\n", arg);
                exit_status = 1;
                continue;
            }

            builtin_jobs_print_job(store, job, format);
        }
    }
    else
    {
        /* No job_ids specified - show all jobs */
        for (job_t *job = store->jobs; job; job = job->next)
        {
            builtin_jobs_print_job(store, job, format);
        }
    }

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

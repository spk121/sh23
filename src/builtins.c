#define _CRT_SECURE_NO_WARNINGS

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtins.h"

#include "exec.h"
#include "exec_frame.h"
#include "func_store.h"
#include "getopt.h"
#include "getopt_string.h"
#include "job_store.h"
#include "lib.h"
#include "logging.h"
#include "positional_params.h"
#include "string_list.h"
#include "string_t.h"
#include "variable_map.h"
#include "variable_store.h"
#include "xalloc.h"
#ifdef UCRT_API
#include <direct.h>
#include <io.h>
#include <time.h>
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
    /* { "continue", BUILTIN_SPECIAL, builtin_continue}, */
    { ".", BUILTIN_SPECIAL, builtin_dot},
    /* { "eval", BUILTIN_SPECIAL, builtin_eval}, */
    /* { "exec", BUILTIN_SPECIAL, builtin_exec}, */
    /* { "exit", BUILTIN_SPECIAL, builtin_exit}, */
    { "export", BUILTIN_SPECIAL, builtin_export},
    /* { "readonly", BUILTIN_SPECIAL, builtin_readonly}, */
    { "return", BUILTIN_SPECIAL, builtin_return},
    {"set", BUILTIN_SPECIAL, builtin_set},
    /* { "shift", BUILTIN_SPECIAL, builtin_shift}, */
    /* { "times", BUILTIN_SPECIAL, builtin_times}, */
    /* { "trap", BUILTIN_SPECIAL, builtin_trap}, */
    { "unset", BUILTIN_SPECIAL, builtin_unset},

#ifdef UCRT_API
    {"cd", BUILTIN_REGULAR, builtin_cd },
        {"pwd", BUILTIN_REGULAR, builtin_pwd},
#endif
    /* { "cd", BUILTIN_REGULAR, builtin_cd}, */
    /* { "pwd", BUILTIN_REGULAR, builtin_pwd}, */
    { "echo", BUILTIN_REGULAR, builtin_echo},
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
#ifdef UCRT_API
    {"ls", BUILTIN_REGULAR, builtin_ls},
#endif
    /* { "fg", BUILTIN_REGULAR, builtin_fg}, */
    /* { "bg", BUILTIN_REGULAR, builtin_bg}, */
    /* { "wait", BUILTIN_REGULAR, builtin_wait}, */
    /* { "kill", BUILTIN_REGULAR, builtin_kill}, */
    { "true", BUILTIN_REGULAR, builtin_true},
    { "false", BUILTIN_REGULAR, builtin_false},
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

    exec_t *ex = frame->executor;
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
            exec_set_error(ex, "break: numeric argument required");
            return 2;
        }

        loop_count = (int)val;
    }

    if (string_list_size(args) > 2)
    {
        exec_set_error(ex, "break: too many arguments");
        return 1;
    }

    frame->pending_control_flow = EXEC_FLOW_BREAK;
    frame->pending_flow_depth = loop_count - 1;

    return 0;
}

/* ============================================================================
 * dot - run file contents in current environment
 * ============================================================================
 */

int builtin_dot(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    exec_t *ex = frame->executor;
    getopt_reset();

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

static void builtin_export_variable_store_print(const string_t *name, const string_t *val, bool exported,
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

int builtin_export(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    exec_t *ex = frame->executor;
    getopt_reset();

    variable_store_t *var_store = frame->variables;
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

static void builtin_set_print_options(exec_frame_t *frame, bool reusable_format);


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
static int builtin_set_print_variables(exec_frame_t *frame)
{
    Expects_not_null(frame);

    if (!frame->variables || !frame->variables->map)
    {
        return 0; /* No variables to print */
    }

    variable_map_t *map = frame->variables->map;

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
static int builtin_set_set_named_option(exec_frame_t *frame, const char *name, bool unset)
{
    Expects_not_null(frame);
    Expects_not_null(name);

    exec_opt_flags_t *opt = frame->opt_flags;
    bool value = !unset;
    bool handled = true;

    if (strcmp(name, "allexport") == 0)
        opt->allexport = value;
    else if (strcmp(name, "errexit") == 0)
        opt->errexit = value;
    else if (strcmp(name, "ignoreeof") == 0)
        opt->ignoreeof = value;
    else if (strcmp(name, "noclobber") == 0)
        opt->noclobber = value;
    else if (strcmp(name, "noglob") == 0)
        opt->noglob = value;
    else if (strcmp(name, "noexec") == 0)
        opt->noexec = value;
    else if (strcmp(name, "nounset") == 0)
        opt->nounset = value;
    else if (strcmp(name, "pipefail") == 0)
        opt->pipefail = value;
    else if (strcmp(name, "verbose") == 0)
        opt->verbose = value;
    else if (strcmp(name, "vi") == 0)
        opt->vi = value;
    else if (strcmp(name, "xtrace") == 0)
        opt->xtrace = value;
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
        // ex->opt_flags_set = true;
        return 0;
    }

    fprintf(stderr, "set: option '%s' not supported yet\n", name);
    return 1;
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

    exec_t *ex = frame->executor;
    exec_opt_flags_t *opt = frame->opt_flags;
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
            builtin_set_set_named_option(frame, state.optarg, state.opt_plus_prefix);
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
            int ret = builtin_set_print_variables(frame);
            xfree(argv);
            return ret;
        }
    }

    /* Apply collected short options to executor flags */
    if (flag_a != -1) { opt->allexport = (flag_a != 0); options_changed = true; }
    if (flag_C != -1) { opt->noclobber = (flag_C != 0); options_changed = true; }
    if (flag_e != -1) { opt->errexit = (flag_e != 0); options_changed = true; }
    if (flag_f != -1) { opt->noglob = (flag_f != 0); options_changed = true; }
    if (flag_n != -1) { opt->noexec = (flag_n != 0); options_changed = true; }
    if (flag_u != -1) { opt->nounset = (flag_u != 0); options_changed = true; }
    if (flag_v != -1) { opt->verbose = (flag_v != 0); options_changed = true; }
    if (flag_x != -1) { opt->xtrace = (flag_x != 0); options_changed = true; }

    /* Flags not yet implemented: b (notify), h (hashall), m (monitor) */

    //if (options_changed)
    //    ex->opt_flags_set = true;

    /* Replace positional parameters if requested (includes explicit "set --") */
    if (have_positional_request)
    {
        if (!frame->positional_params)
        {
            frame->positional_params = positional_params_create();
            if (!frame->positional_params)
            {
                fprintf(stderr, "set: failed to allocate positional parameters\n");
                xfree(argv);
                return 1;
            }
        }

        int max_params = positional_params_get_max(frame->positional_params);
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

        if (!positional_params_replace(frame->positional_params, new_params, new_param_count))
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
    variable_store_t *var_store = frame->variables;
    func_store_t *func_store = frame->functions;
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
 * cd - Change the shell working directory
 * ============================================================================
 */
#ifdef UCRT_API
int builtin_cd(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    int flag_err = 0;
    int c;
    variable_store_t *var_store = frame->variables;

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
    string_t *allocated_target = NULL;

    int remaining = string_list_size(args) - optind;

    if (remaining > 1)
    {
        fprintf(stderr, "cd: too many arguments\n");
        return 1;
    }

    if (remaining == 0)
    {
        /* No argument: go to HOME or USERPROFILE */
        const char *home = variable_store_get_value_cstr(var_store, "HOME");
        if (!home || home[0] == '\0')
        {
            home = variable_store_get_value_cstr(var_store, "USERPROFILE");
        }
        if (!home || home[0] == '\0')
        {
            home = getenv("HOME");
        }
        if (!home || home[0] == '\0')
        {
            home = getenv("USERPROFILE");
        }
        if (!home || home[0] == '\0')
        {
            fprintf(stderr, "cd: HOME not set\n");
            return 1;
        }
        target_dir = home;
    }
    else
    {
        const string_t *arg = string_list_at(args, optind);
        const char *arg_cstr = string_cstr(arg);

        if (strcmp(arg_cstr, "-") == 0)
        {
            /* cd - : go to OLDPWD */
            const char *oldpwd = variable_store_get_value_cstr(var_store, "OLDPWD");
            if (!oldpwd || oldpwd[0] == '\0')
            {
                fprintf(stderr, "cd: OLDPWD not set\n");
                return 1;
            }
            target_dir = oldpwd;
            /* Print the directory when using cd - */
            printf("%s\n", target_dir);
        }
        else
        {
            target_dir = arg_cstr;
        }
    }

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

        free(old_cwd);
        if (allocated_target)
        {
            string_destroy(&allocated_target);
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

    /* Update OLDPWD and PWD */
    variable_store_add_cstr(var_store, "OLDPWD", old_cwd, true, false);
    variable_store_add_cstr(var_store, "PWD", new_cwd, true, false);

    free(old_cwd);
    free(new_cwd);

    if (allocated_target)
    {
        string_destroy(&allocated_target);
    }

    return 0;
}
#endif

/* ============================================================================
 * pwd - Print working directory
 * ============================================================================
 */
#ifdef UCRT_API
/**
 * builtin_pwd - Print working directory (Windows UCRT implementation)
 *
 * Implements the 'pwd' command for Windows using only UCRT functions.
 *
 * Options:
 *   -P    Print the physical directory (resolve symlinks) - default on Windows
 *   -L    Print the logical directory (from PWD variable if valid)
 *
 * @param ex   Execution context
 * @param args Command arguments (including "pwd" as args[0])
 * @return 0 on success, 1 on failure
 */
int builtin_pwd(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    getopt_reset();

    int flag_L = 0;
    int flag_P = 0;
    int flag_err = 0;
    int c;
    variable_store_t *var_store = frame->variables;

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

    const char *pwd_to_print = NULL;

    if (flag_L)
    {
        /*
         * Logical mode: use PWD if it's set and valid.
         * "Valid" means it refers to the same directory as the actual cwd.
         * On Windows without Windows API, we can't easily verify this,
         * so we just check that PWD is set and non-empty, then trust it.
         */
        const char *pwd_var = variable_store_get_value_cstr(var_store, "PWD");
        if (pwd_var && pwd_var[0] != '\0')
        {
            pwd_to_print = pwd_var;
        }
    }

    if (!pwd_to_print)
    {
        /* Physical mode (default) or PWD not available: use _getcwd */
        char *cwd = _getcwd(NULL, 0);
        if (!cwd)
        {
            fprintf(stderr, "pwd: cannot determine current directory: %s\n", strerror(errno));
            return 1;
        }

        printf("%s\n", cwd);
        free(cwd);
    }
    else
    {
        printf("%s\n", pwd_to_print);
    }

    return 0;
}
#endif

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

static int builtin_jobs_parse_job_id(const job_store_t *store, const string_t *arg_str)
{
    if (!arg_str || string_length(arg_str) == 0)
        return -1;

    const char *arg = string_cstr(arg_str);

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
        int endpos = 0;
        long val = strtol(arg, (char **)&arg, 10);
        if (*arg == '\0' && val > 0)
        {
            return (int)val;
        }

        /* %?str or %str not implemented */
        return -1;
    }

    /* Plain number */
    int endpos = 0;
    long val = string_atol_at(arg_str, 0, &endpos);
    if (endpos == string_length(arg_str) && val > 0)
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

int builtin_jobs(exec_frame_t *frame, const string_list_t *args)
{
    getopt_reset();

    if (!frame)
        return 1;

    exec_t *ex = frame->executor;
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
            const string_t *arg_str = string_list_at(args, i);
            int job_id = builtin_jobs_parse_job_id(store, arg_str);

            if (job_id < 0)
            {
                fprintf(stderr, "jobs: %s: no such job\n", string_cstr(arg_str));
                exit_status = 1;
                continue;
            }

            job_t *job = job_store_find(store, job_id);
            if (!job)
            {
                fprintf(stderr, "jobs: %s: no such job\n", string_cstr(arg_str));
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

    int argc = string_list_size(args);
    bool suppress_newline = false;
    bool interpret_escapes = false;
    int first_arg = 1;

    /* Parse options (non-standard but common: -n and -e) */
    for (int i = 1; i < argc; i++)
    {
        const char *arg = string_cstr(string_list_at(args, i));
        
        if (strcmp(arg, "-n") == 0)
        {
            suppress_newline = true;
            first_arg = i + 1;
        }
        else if (strcmp(arg, "-e") == 0)
        {
            interpret_escapes = true;
            first_arg = i + 1;
        }
        else if (strcmp(arg, "-E") == 0)
        {
            interpret_escapes = false;
            first_arg = i + 1;
        }
        else
        {
            /* Stop processing options at first non-option */
            break;
        }
    }

    /* Print arguments separated by spaces */
    for (int i = first_arg; i < argc; i++)
    {
        if (i > first_arg)
        {
            putchar(' ');
        }

        const char *arg = string_cstr(string_list_at(args, i));

        if (interpret_escapes)
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

    if (!suppress_newline)
    {
        putchar('\n');
    }

    fflush(stdout);
    return 0;
}

/* ============================================================================
 * return - Return from a function or dot script
 * ============================================================================
 */

int builtin_return(exec_frame_t *frame, const string_list_t *args)
{
    Expects_not_null(frame);
    Expects_not_null(args);

    exec_t *ex = frame->executor;
    getopt_reset();

    /* Check if return is valid (must be in a function or dot script) */
    exec_frame_t *return_target = exec_frame_find_return_target(frame);
    if (!return_target)
    {
        exec_set_error(ex, "return: can only be used in a function or sourced script");
        return 2;
    }

    /* Parse optional exit status argument */
    int exit_status = frame->last_exit_status;

    if (string_list_size(args) > 1)
    {
        const string_t *arg_str = string_list_at(args, 1);
        int endpos = 0;
        long val = string_atol_at(arg_str, 0, &endpos);

        if (endpos != string_length(arg_str))
        {
            exec_set_error(ex, "return: numeric argument required");
            return 2;
        }

        exit_status = (int)(val & 0xFF);
    }

    if (string_list_size(args) > 2)
    {
        exec_set_error(ex, "return: too many arguments");
        return 1;
    }

    frame->pending_control_flow = EXEC_FLOW_RETURN;
    frame->pending_flow_depth = 0;

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

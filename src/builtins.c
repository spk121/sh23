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
    /* { ":", BUILTIN_SPECIAL, builtin_colon}, */
    /* { "continue", BUILTIN_SPECIAL, builtin_continue}, */
    /* { ".", BUILTIN_SPECIAL, builtin_dot}, */
    /* { "eval", BUILTIN_SPECIAL, builtin_eval}, */
    /* { "exec", BUILTIN_SPECIAL, builtin_exec}, */
    /* { "exit", BUILTIN_SPECIAL, builtin_exit}, */
    /* { "export", BUILTIN_SPECIAL, builtin_export}, */
    /* { "readonly", BUILTIN_SPECIAL, builtin_readonly}, */
    /* { "return", BUILTIN_SPECIAL, builtin_return}, */
    {"set", BUILTIN_SPECIAL, builtin_set},
    /* { "shift", BUILTIN_SPECIAL, builtin_shift}, */
    /* { "times", BUILTIN_SPECIAL, builtin_times}, */
    /* { "trap", BUILTIN_SPECIAL, builtin_trap}, */
    /* { "unset", BUILTIN_SPECIAL, builtin_unset}, */

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
    /* { "jobs", BUILTIN_REGULAR, builtin_jobs}, */
    /* { "fg", BUILTIN_REGULAR, builtin_fg}, */
    /* { "bg", BUILTIN_REGULAR, builtin_bg}, */
    /* { "wait", BUILTIN_REGULAR, builtin_wait}, */
    /* { "kill", BUILTIN_REGULAR, builtin_kill}, */
    /* { "true", BUILTIN_REGULAR, builtin_true}, */
    /* { "false", BUILTIN_REGULAR, builtin_false}, */
    {NULL, BUILTIN_NONE, NULL} // Sentinel
};

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
    for (builtin_implemented_function_map_t *p = builtin_implemented_functions; p->name != NULL; p++)
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

builtin_func_t builtin_get_function_cstr(const char* name)
{
    if (name == NULL)
        return NULL;
    for (builtin_implemented_function_map_t *p = builtin_implemented_functions; p->name != NULL; p++)
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

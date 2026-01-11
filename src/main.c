#include <stdio.h>
#include <string.h>
#include <stdbool.h>
//#include "lexer.h"
#include "xalloc.h"
//#include "tokenizer.h"
#include "shell.h"
#include "logging.h"
#include "getopt.h"
#include "lib.h"
#ifdef POSIX_API
#include <unistd.h>
#include <sys/types.h>
#endif
#ifdef UCRT_API
#include <io.h>
#include <process.h>
#endif

typedef enum
{
    SH_EXIT_SUCCESS = 0,
    // A non-interactive shell can return 1 to 125 on all errors
    // except command_file not found, command_file not executable,
    // or unrecoverable read error while reading commands.
    // 1 to 126 explicitly includes non-interactive syntax errors,
    // non-interactive redirection errors, and non-interactive
    // variable assignment errors.
    SH_EXIT_GENERAL_ERROR = 1,
    SH_EXIT_COMMAND_FILE_ENOEXEC = 126,
    SH_EXIT_COMMAND_FILE_NOT_FOUND = 127,
    SH_EXIT_COMMAND_FILE_READ_ERROR = 128
} sh_exit_code_t;

/* Valid -o/+o option arguments */
static const char *valid_o_args[] = {"allexport", "errexit", "ignoreeof", "monitor", "noclobber",
                                     "noglob",    "noexec",  "notify",    "nounset", "pipefail",
                                     "verbose",   "vi",      "xtrace",    NULL};

/* Shell option flags - these will be set/unset by option_ex.flag mechanism */
static int flag_a = 0; /* allexport */
static int flag_b = 0;
static int flag_C = 0; /* noclobber */
static int flag_e = 0; /* errexit */
static int flag_f = 0; /* noglob */
static int flag_i = 0; /* interactive */
static int flag_m = 0; /* monitor */
static int flag_n = 0; /* noexec */
static int flag_u = 0; /* nounset */
static int flag_v = 0; /* verbose */
static int flag_x = 0; /* xtrace */
static int flag_c = 0; /* command string mode */
static int flag_s = 0; /* stdin mode */

/* -o options list */
typedef struct
{
    char **o_options;
    int o_count;
    int o_capacity;
} o_options_list;

static void init_o_options(o_options_list *opts)
{
    opts->o_capacity = 8;
    opts->o_options = malloc(opts->o_capacity * sizeof(char *));
    opts->o_count = 0;
}

static void free_o_options(o_options_list *opts)
{
    if (opts->o_options)
    {
        for (int i = 0; i < opts->o_count; i++)
        {
            free(opts->o_options[i]);
        }
        free(opts->o_options);
    }
}

static int is_valid_o_arg(const char *arg)
{
    for (int i = 0; valid_o_args[i]; i++)
    {
        if (strcmp(arg, valid_o_args[i]) == 0)
            return 1;
    }
    return 0;
}

static void add_o_option(o_options_list *opts, const char *value, int is_plus)
{
    if (opts->o_count >= opts->o_capacity)
    {
        opts->o_capacity *= 2;
        opts->o_options = realloc(opts->o_options, opts->o_capacity * sizeof(char *));
    }

    size_t len = strlen(value) + 2;
    opts->o_options[opts->o_count] = malloc(len);
    snprintf(opts->o_options[opts->o_count], len, "%c%s", is_plus ? '+' : '-', value);
    opts->o_count++;
}

static int check_command_file_readable(const char *command_file)
{
#ifdef POSIX_API
    // Check if file exists
    if (access(command_file, F_OK) != 0)
    {
        return SH_EXIT_COMMAND_FILE_NOT_FOUND;
    }
    // Check if file is readable
    if (access(command_file, R_OK) != 0)
    {
        return SH_EXIT_COMMAND_FILE_READ_ERROR;
    }
#elifdef UCRT_API
    // Check if file exists
    if (_access(command_file, 0) != 0)
    {
        return SH_EXIT_COMMAND_FILE_NOT_FOUND;
    }
    // Check if file is readable
    if ((_access(command_file, 4) != 0))
    {
        return SH_EXIT_COMMAND_FILE_READ_ERROR;
    }
#else
    // Check readability of command file
    FILE *cf = fopen(command_file, "r");
    if (cf == NULL)
    {
        return SH_EXIT_COMMAND_FILE_NOT_FOUND;
    }
    fclose(cf);
    return SH_EXIT_SUCCESS;
#endif
    return SH_EXIT_SUCCESS;
}

static shell_mode_t compute_shell_mode(int c_flag, int s_flag, int i_flag, const char *command_file)
{
    shell_mode_t mode = SHELL_MODE_UNKNOWN;

    if (c_flag)
        mode = SHELL_MODE_COMMAND_STRING;
    else if (i_flag)
    {
#ifdef POSIX_API
        if (getuid() != geteuid() || getgid() != getegid())
            return SHELL_MODE_INVALID_UID_GID;
#endif
        mode = SHELL_MODE_INTERACTIVE;
    }
    else if (s_flag || !command_file)
        mode = SHELL_MODE_STDIN;
    else
        mode = SHELL_MODE_SCRIPT_FILE;

    /* Upgrade non-interactive stdin to interactive if connected to a terminal */
    if (mode == SHELL_MODE_STDIN)
    {
#ifdef POSIX_API
        if (isatty(STDIN_FILENO))
            mode = SHELL_MODE_INTERACTIVE;
#elif defined(UCRT_API)
        if (_isatty(_fileno(stdin)))
            mode = SHELL_MODE_INTERACTIVE;
#endif
        /* Else: in pure ISO C, remain SHELL_MODE_STDIN (conservative) */
    }

    return mode;
}

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [options] [command_file [argument ...]]\n", prog);
    fprintf(stderr, "       %s [options] -c command_string [command_name [argument ...]]\n", prog);
    fprintf(stderr, "       %s [options] -s [argument ...]\n", prog);
    fprintf(stderr, "\nOptions (can use - or + prefix):\n");
    fprintf(stderr, "  -a/+a           allexport\n");
    fprintf(stderr, "  -b/+b           (b flag)\n");
    fprintf(stderr, "  -C/+C           noclobber\n");
    fprintf(stderr, "  -e/+e           errexit\n");
    fprintf(stderr, "  -f/+f           noglob\n");
    fprintf(stderr, "  -i/+i           interactive\n");
    fprintf(stderr, "  -m/+m           monitor\n");
    fprintf(stderr, "  -n/+n           noexec\n");
    fprintf(stderr, "  -u/+u           nounset\n");
    fprintf(stderr, "  -v/+v           verbose\n");
    fprintf(stderr, "  -x/+x           xtrace\n");
    fprintf(stderr, "  -o/+o option    set/unset named option\n");
    fprintf(stderr, "\nOptions (- prefix only):\n");
    fprintf(stderr, "  -c command      command string mode\n");
    fprintf(stderr, "  -s              read from stdin\n");
}

// Note: envp is not part of ISO C, but it is a standard extension
// in both POSIX and UCRT environments
int main(int argc, char **argv, char **envp)
{
    log_init();
    lib_setlocale();

    o_options_list o_opts;
    init_o_options(&o_opts);

    char *command_string = NULL;
    char *command_file = NULL;

    opterr = 1;

    /* Define option_ex array with allow_plus settings.
       Options a, b, C, e, f, i, m, n, u, v, x allow both - and +
       Options c and s only allow - (not +)

       For short-only options: name = NULL, val = short character
       This allows the loop in getopt.c to match short options by val field.
    */
    struct option_ex long_options[] = {
        /* Short options that support both - and + with automatic flag handling */
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_a, .val = 'a'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_b, .val = 'b'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_C, .val = 'C'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_e, .val = 'e'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_f, .val = 'f'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_i, .val = 'i'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_m, .val = 'm'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_n, .val = 'n'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_u, .val = 'u'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_v, .val = 'v'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 1, .flag = &flag_x, .val = 'x'},

        /* Options that DON'T allow + prefix and need manual handling */
        {.name = NULL, .has_arg = no_argument, .allow_plus = 0, .flag = NULL, .val = 'c'},
        {.name = NULL, .has_arg = no_argument, .allow_plus = 0, .flag = NULL, .val = 's'},

        /* 'o' needs special handling - it takes an argument */
        {.name = NULL, .has_arg = required_argument, .allow_plus = 1, .flag = NULL, .val = 'o'},

        /* Terminator: both name and val are NULL/0 */
        {0}};

    int c;
    struct getopt_state state = {0};
    state.optind = 1;
    state.opterr = 1;
    state.posix_hyphen = 1; /* Enable POSIX shell lone '-' handling */

    /* Use getopt_long_plus_r for re-entrant version with explicit state
       Note: -c and -s are flags, not options with arguments.
       The command_string comes from positional args after option processing. */
    while ((c = getopt_long_plus_r(argc, argv, "abCefimno:uvxcs", long_options, NULL, &state)) !=
           -1)
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
        case 'i':
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
            if (!is_valid_o_arg(state.optarg))
            {
                fprintf(stderr, "%s: invalid -o option: %s\n", argv[0], state.optarg);
                print_usage(argv[0]);
                free_o_options(&o_opts);
                return 2;
            }
            /* Check if + prefix was used */
            add_o_option(&o_opts, state.optarg, state.opt_plus_prefix);
            break;

        case 'c':
            /* -c flag indicates command string mode */
            /* getopt already validated that + prefix wasn't used */
            /* command_string will be captured from first positional arg */
            flag_c = 1;
            break;

        case 's':
            /* -s stdin mode */
            /* getopt already validated that + prefix wasn't used */
            flag_s = 1;
            break;

        case '?':
            /* getopt_long_plus already printed an error message */
            print_usage(argv[0]);
            free_o_options(&o_opts);
            return 2;

        default:
            fprintf(stderr, "%s: unexpected getopt return: %c\n", argv[0], c);
            free_o_options(&o_opts);
            return 2;
        }
    }

    optind = state.optind; /* sync global optind */

    /* Validate -c and -s mutual exclusion */
    if (flag_c && flag_s)
    {
        fprintf(stderr, "%s: cannot specify both -c and -s\n", argv[0]);
        print_usage(argv[0]);
        free_o_options(&o_opts);
        return 2;
    }

    /* Parse positional arguments based on -c and -s */
    char *command_name = NULL;
    char **arguments = NULL;
    int arg_count = 0;

    if (flag_c)
    {
        /* -c mode: first positional arg is command_string (required)
           Remaining args are: [command_name [argument ...]] */
        if (optind >= argc)
        {
            fprintf(stderr, "%s: -c requires a command string\n", argv[0]);
            print_usage(argv[0]);
            free_o_options(&o_opts);
            return 2;
        }
        command_string = argv[optind++];

        if (optind < argc)
        {
            command_name = argv[optind++];
        }

        if (optind < argc)
        {
            arg_count = argc - optind;
            arguments = &argv[optind];
        }
    }
    else if (flag_s)
    {
        /* -s mode: [argument ...] */
        if (optind < argc)
        {
            arg_count = argc - optind;
            arguments = &argv[optind];
        }
    }
    else
    {
        /* Normal mode: [command_file [argument ...]] */
        if (optind < argc)
        {
            command_file = argv[optind++];

            if (optind < argc)
            {
                arg_count = argc - optind;
                arguments = &argv[optind];
            }
        }
    }

    /* Print parsed results */
    log_debug("=== Parsed Shell Options ===\n");
    log_debug("Flags:\n");
    log_debug("  -a (allexport):  %s\n", flag_a ? "set" : "unset");
    log_debug("  -b:              %s\n", flag_b ? "set" : "unset");
    log_debug("  -C (noclobber):  %s\n", flag_C ? "set" : "unset");
    log_debug("  -e (errexit):    %s\n", flag_e ? "set" : "unset");
    log_debug("  -f (noglob):     %s\n", flag_f ? "set" : "unset");
    log_debug("  -i (interactive):%s\n", flag_i ? "set" : "unset");
    log_debug("  -m (monitor):    %s\n", flag_m ? "set" : "unset");
    log_debug("  -n (noexec):     %s\n", flag_n ? "set" : "unset");
    log_debug("  -u (nounset):    %s\n", flag_u ? "set" : "unset");
    log_debug("  -v (verbose):    %s\n", flag_v ? "set" : "unset");
    log_debug("  -x (xtrace):     %s\n", flag_x ? "set" : "unset");

    if (o_opts.o_count > 0)
    {
        log_debug("\n-o options:\n");
        for (int j = 0; j < o_opts.o_count; j++)
        {
            log_debug("  %s\n", o_opts.o_options[j]);
        }
    }

    log_debug("\nMode:\n");
    if (flag_c)
    {
        log_debug("  -c mode (command string)\n");
        log_debug("  Command string: %s\n", command_string);
        if (command_name)
            log_debug("  Command name: %s\n", command_name);
    }
    else if (flag_s)
    {
        log_debug("  -s mode (stdin)\n");
    }
    else
    {
        log_debug("  Normal mode\n");
        if (command_file)
            log_debug("  Command file: %s\n", command_file);
    }

    if (arg_count > 0)
    {
        log_debug("\nArguments:\n");
        for (int j = 0; j < arg_count; j++)
        {
            log_debug("  [%d]: %s\n", j, arguments[j]);
        }
    }

    free_o_options(&o_opts);

    if (command_file)
    {
        int ret = check_command_file_readable(command_file);
        if (ret == SH_EXIT_COMMAND_FILE_NOT_FOUND)
        {
            fprintf(stderr, "%s: command file '%s' not found\n", argv[0], command_file);
            return SH_EXIT_COMMAND_FILE_NOT_FOUND;
        }
        else if (ret == SH_EXIT_COMMAND_FILE_READ_ERROR)
        {
            fprintf(stderr, "%s: cannot read command file '%s'\n", argv[0], command_file);
            return SH_EXIT_COMMAND_FILE_READ_ERROR;
        }
    }

    shell_mode_t mode = compute_shell_mode(flag_c, flag_s, flag_i, command_file);
    if (mode == SHELL_MODE_INVALID_UID_GID)
    {
        fprintf(stderr, "%s: cannot run interactive shell with differing real and effective UID/GID\n", argv[0]);
        return SH_EXIT_GENERAL_ERROR;
    }

    char *name =
        command_name ? command_name : ((argc > 0 && argv[0][0]) ? argv[0] : "shell");

    shell_cfg_t cfg = {.mode = mode,
                       .command_name = name,
                       .command_string = command_string,
                       .command_file = command_file,
                       .arguments = arguments,
                       .envp = envp,
                       .flags = {.allexport = flag_a,
                                 .noclobber = flag_C,
                                 .errexit = flag_e,
                                 .noglob = flag_f,
                                 .monitor = flag_m,
                                 .noexec = flag_n,
                                 .nounset = flag_u,
                                 .verbose = flag_v,
                                 .vi = false,
                                 .xtrace = flag_x}};

    if (flag_c && (command_string == NULL || command_string[0] == '\0'))
    {
        free_o_options(&o_opts);
        return SH_EXIT_SUCCESS;
    }

    // On memory allocation failures, the shell
    // will longjmp out to here.
    arena_start();
    shell_t *sh = shell_create(&cfg);
    arena_set_cleanup(shell_cleanup, (void *)sh);
    sh_status_t status;
    status = shell_execute(sh);

    // Print any error message before exiting
    if (status != SH_OK)
    {
        const char *error = shell_last_error(sh);
        if (error && error[0])
        {
            fprintf(stderr, "%s: %s\n", argv[0], error);
        }
    }

    // Don't call shell_destroy here - arena_end will call shell_cleanup
    arena_end();
    return status;
}


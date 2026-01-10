/* test_getopt.c -- Comprehensive test suite for the enhanced getopt_long_plus implementation */

#include "getopt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ctest.h"

static int flag_a = 0;
static int flag_b = 0;
static int flag_C = 0;
static int flag_e = 0;
static int flag_f = 0;
static int flag_i = 0;
static int flag_m = 0;
static int flag_n = 0;
static int flag_u = 0;
static int flag_v = 0;
static int flag_x = 0;

static struct option_ex shell_options[] = {
    /* Toggle options that support both - and + */
    {NULL, no_argument,       1, &flag_a, 'a', NULL},  /* allexport */
    {NULL, no_argument,       1, &flag_b, 'b', NULL},
    {NULL, no_argument,       1, &flag_C, 'C', NULL},  /* noclobber */
    {NULL, no_argument,       1, &flag_e, 'e', NULL},  /* errexit */
    {NULL, no_argument,       1, &flag_f, 'f', NULL},  /* noglob */
    {NULL, no_argument,       1, &flag_m, 'm', NULL},  /* monitor */
    {NULL, no_argument,       1, &flag_n, 'n', NULL},  /* noexec */
    {NULL, no_argument,       1, &flag_u, 'u', NULL},  /* nounset */
    {NULL, no_argument,       1, &flag_v, 'v', NULL},  /* verbose */
    {NULL, no_argument,       1, &flag_x, 'x', NULL},  /* xtrace */

    /* Mode options: only - prefix allowed */
    {NULL, no_argument,       0, &flag_i, 'i', NULL},  /* interactive */
    {NULL, no_argument,       0, NULL,   's', NULL}, /* stdin - no flag, just return 's' */
    {NULL, required_argument, 0, NULL,   'c', NULL}, /* command - no flag, return 'c' */

    /* Terminator */
    {NULL, 0, 0, NULL, 0, NULL}
};

CTEST(test_basic_toggle_set)
{
    char* argv[] = { "prog", "-v", NULL };
    int argc = 2;
    struct getopt_state state = { 0 };
    state.opterr = 0;

    int c = getopt_long_plus_r(argc, argv, "v", shell_options, NULL, &state);
    CTEST_ASSERT_EQ(ctest, c, 'v', "returns 'v'");
    CTEST_ASSERT_EQ(ctest, flag_v, 1, "-v sets flag_v to 1");
    CTEST_ASSERT_EQ(ctest, state.optind, 2, "optind advanced");
}

CTEST(test_basic_toggle_unset)
{
    char* argv[] = { "prog", "+v", NULL };
    int argc = 2;
    struct getopt_state state = { 0 };
    state.opterr = 0;

    flag_v = 1; /* precondition */

    int c = getopt_long_plus_r(argc, argv, "v", shell_options, NULL, &state);
    CTEST_ASSERT_EQ(ctest, c, 0, "+v returns 0 (flag handled)");
    CTEST_ASSERT_EQ(ctest, flag_v, 0, "+v clears flag_v");
    CTEST_ASSERT_EQ(ctest, state.optind, 2, "optind advanced");
}

CTEST(test_invalid_plus_prefix)
{
    char* argv[] = { "prog", "+c", NULL };
    int argc = 2;
    struct getopt_state state = { 0 };
    state.opterr = 0;

    int c = getopt_long_plus_r(argc, argv, "c", shell_options, NULL, &state);
    CTEST_ASSERT_EQ(ctest, c, '?', "+c returns '?'");
    CTEST_ASSERT_EQ(ctest, state.optopt, 'c', "optopt set to 'c'");
}

CTEST(test_c_mode_command_string)
{
    char* argv[] = { "prog", "-c", "echo hello", "myscript", "arg1", NULL };
    int argc = 5;
    struct getopt_state state = { 0 };
    state.opterr = 0;

    int c_seen = 0;
    int c;
    while ((c = getopt_long_plus_r(argc, argv, "c", shell_options, NULL, &state)) != -1) {
        if (c == 'c') c_seen = 1;
    }

    CTEST_ASSERT_EQ(ctest, c_seen, 1, "-c flag seen");
    CTEST_ASSERT_EQ(ctest, state.optind, 2, "optind after -c");
    CTEST_ASSERT_STR_EQ(ctest, argv[state.optind], "echo hello", "first non-option is command string");
    CTEST_ASSERT_STR_EQ(ctest, argv[state.optind + 1], "myscript", "command name follows");
}

CTEST(test_permutation_intermixed)
{
    char* argv[] = { "prog", "file1", "-v", "file2", "+x", "file3", NULL };
    int argc = 6;
    struct getopt_state state = { 0 };
    state.opterr = 0;

    flag_v = 0;
    flag_x = 1;

    int seen_v = 0, seen_x = 0;
    int c;
    while ((c = getopt_long_plus_r(argc, argv, "vx", shell_options, NULL, &state)) != -1) {
        if (c == 'v') seen_v = 1;
        if (c == 0 && state.opt_plus_prefix) seen_x = 1; /* +x clears */
    }

    CTEST_ASSERT_EQ(ctest, flag_v, 1, "-v set");
    CTEST_ASSERT_EQ(ctest, flag_x, 0, "+x unset");
    CTEST_ASSERT_EQ(ctest, state.optind, 3, "optind points to first non-option after permutation");
    /* Non-options remain in order due to permutation */
}

CTEST(test_unknown_option)
{
    char* argv[] = { "prog", "-z", NULL };
    int argc = 2;
    struct getopt_state state = { 0 };
    state.opterr = 0;

    int c = getopt_long_plus_r(argc, argv, "", shell_options, NULL, &state);
    CTEST_ASSERT_EQ(ctest, c, '?', "unknown option returns '?'");
}

CTEST(test_optarg_required)
{
    char* argv[] = { "prog", "-o", "noclobber", NULL };
    int argc = 3;
    struct getopt_state state = { 0 };
    state.opterr = 0;

    int c = getopt_long_plus_r(argc, argv, "o:", shell_options, NULL, &state);
    CTEST_ASSERT_EQ(ctest, c, 'o', "returns 'o'");
    CTEST_ASSERT_STR_EQ(ctest, state.optarg, "noclobber", "optarg set correctly");
}

CTEST(test_optarg_missing)
{
    char* argv[] = { "prog", "-o", NULL };
    int argc = 2;
    struct getopt_state state = { 0 };
    state.opterr = 0;

    int c = getopt_long_plus_r(argc, argv, "o:", shell_options, NULL, &state);
    CTEST_ASSERT_EQ(ctest, c, '?', "missing argument returns '?'");
    CTEST_ASSERT_EQ(ctest, state.optopt, 'o', "optopt set to 'o'");
}

CTEST(test_long_option_basic)
{
    static struct option_ex long_opts[] = {
        {"verbose", no_argument, 1, &flag_v, 'v', NULL},
        {"xtrace", no_argument, 1, &flag_x, 'x', NULL},
        {NULL, 0, 0, NULL, 0, NULL}
    };

    char* argv[] = { "prog", "--verbose", NULL };
    int argc = 2;
    struct getopt_state state = { 0 };
    state.opterr = 0;
    flag_v = 0;

    int c = getopt_long_plus_r(argc, argv, "vx", long_opts, NULL, &state);
    CTEST_ASSERT_EQ(ctest, c, 'v', "returns 'v'");
    CTEST_ASSERT_EQ(ctest, flag_v, 1, "--verbose sets flag_v to 1");
    CTEST_ASSERT_EQ(ctest, state.optind, 2, "optind advanced");
}

CTEST(test_long_option_with_plus)
{
    static struct option_ex long_opts[] = {
        {"verbose", no_argument, 1, &flag_v, 'v', NULL},
        {NULL, 0, 0, NULL, 0, NULL}
    };

    char* argv[] = { "prog", "++verbose", NULL };
    int argc = 2;
    struct getopt_state state = { 0 };
    state.opterr = 0;
    flag_v = 1; /* precondition: set to 1 */

    int c = getopt_long_plus_r(argc, argv, "v", long_opts, NULL, &state);
    CTEST_ASSERT_EQ(ctest, c, 0, "++verbose returns 0 (flag handled)");
    CTEST_ASSERT_EQ(ctest, flag_v, 0, "++verbose clears flag_v");
    CTEST_ASSERT_EQ(ctest, state.optind, 2, "optind advanced");
}

CTEST(test_optional_argument_present)
{
    char* argv[] = { "prog", "-ovalue", NULL };
    int argc = 2;
    struct getopt_state state = { 0 };
    state.opterr = 0;

    int c = getopt_long_plus_r(argc, argv, "o::", shell_options, NULL, &state);
    CTEST_ASSERT_EQ(ctest, c, 'o', "returns 'o'");
    CTEST_ASSERT_STR_EQ(ctest, state.optarg, "value", "optarg set to 'value'");
}

CTEST(test_optional_argument_missing)
{
    char* argv[] = { "prog", "-o", "next_arg", NULL };
    int argc = 3;
    struct getopt_state state = { 0 };
    state.opterr = 0;

    int c = getopt_long_plus_r(argc, argv, "o::", shell_options, NULL, &state);
    CTEST_ASSERT_EQ(ctest, c, 'o', "returns 'o'");
    CTEST_ASSERT_NULL(ctest, state.optarg, "optarg is NULL when optional arg missing");
    CTEST_ASSERT_EQ(ctest, state.optind, 2, "optind doesn't consume next_arg");
}

CTEST(test_colon_prefix_missing_arg)
{
    char* argv[] = { "prog", "-o", NULL };
    int argc = 2;
    struct getopt_state state = { 0 };
    state.opterr = 0;

    /* Leading ':' means return ':' instead of '?' for missing arg */
    int c = getopt_long_plus_r(argc, argv, ":o:", shell_options, NULL, &state);
    CTEST_ASSERT_EQ(ctest, c, ':', "missing arg with ':' prefix returns ':'");
    CTEST_ASSERT_EQ(ctest, state.optopt, 'o', "optopt set to 'o'");
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
    CTestEntry* suite[] = {
        CTEST_ENTRY(test_basic_toggle_set),
        CTEST_ENTRY(test_basic_toggle_unset),
        CTEST_ENTRY(test_invalid_plus_prefix),
        CTEST_ENTRY(test_c_mode_command_string),
        CTEST_ENTRY(test_permutation_intermixed),
        CTEST_ENTRY(test_unknown_option),
        CTEST_ENTRY(test_optarg_required),
        CTEST_ENTRY(test_optarg_missing),
        CTEST_ENTRY(test_long_option_basic),
        CTEST_ENTRY(test_long_option_with_plus),
        CTEST_ENTRY(test_optional_argument_present),
        CTEST_ENTRY(test_optional_argument_missing),
        CTEST_ENTRY(test_colon_prefix_missing_arg),
        NULL
    };

    return ctest_run_suite(suite);
}

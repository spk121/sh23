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

/* ============================================================================
 * POSIX single-hyphen handling tests
 * ============================================================================ */

CTEST(test_posix_hyphen_terminates_options)
{
    /* sh -a - script.sh arg1
     * The '-' should terminate option processing and be skipped.
     * optind should point to 'script.sh'
     */
    char* argv[] = { "sh", "-a", "-", "script.sh", "arg1", NULL };
    int argc = 5;
    struct getopt_state state = { 0 };
    state.opterr = 0;
    state.posix_hyphen = 1;  /* Enable POSIX hyphen handling */
    flag_a = 0;

    int c;
    int count = 0;
    while ((c = getopt_long_plus_r(argc, argv, "aev", shell_options, NULL, &state)) != -1) {
        count++;
        if (c == 'a')
            flag_a = 1;
    }

    CTEST_ASSERT_EQ(ctest, count, 1, "only one option parsed (-a)");
    CTEST_ASSERT_EQ(ctest, flag_a, 1, "-a was set");
    CTEST_ASSERT_EQ(ctest, state.optind, 3, "optind points past the hyphen to script.sh");
    CTEST_ASSERT_STR_EQ(ctest, argv[state.optind], "script.sh", "first operand is script.sh");
}

CTEST(test_posix_hyphen_skips_following_options)
{
    /* sh -a - -e script.sh
     * The '-' terminates options, so '-e' is NOT parsed as an option.
     * It becomes an operand.
     */
    char* argv[] = { "sh", "-a", "-", "-e", "script.sh", NULL };
    int argc = 5;
    struct getopt_state state = { 0 };
    state.opterr = 0;
    state.posix_hyphen = 1;
    flag_a = 0;
    flag_e = 0;

    int c;
    while ((c = getopt_long_plus_r(argc, argv, "ae", shell_options, NULL, &state)) != -1) {
        if (c == 'a') flag_a = 1;
        if (c == 'e') flag_e = 1;
    }

    CTEST_ASSERT_EQ(ctest, flag_a, 1, "-a was set");
    CTEST_ASSERT_EQ(ctest, flag_e, 0, "-e was NOT set (came after hyphen)");
    CTEST_ASSERT_EQ(ctest, state.optind, 3, "optind points to -e (now an operand)");
    CTEST_ASSERT_STR_EQ(ctest, argv[state.optind], "-e", "first operand is -e");
}

CTEST(test_posix_hyphen_alone)
{
    /* sh - script.sh
     * The '-' as first argument terminates options immediately.
     */
    char* argv[] = { "sh", "-", "script.sh", NULL };
    int argc = 3;
    struct getopt_state state = { 0 };
    state.opterr = 0;
    state.posix_hyphen = 1;

    int c;
    int count = 0;
    while ((c = getopt_long_plus_r(argc, argv, "aev", shell_options, NULL, &state)) != -1) {
        count++;
    }

    CTEST_ASSERT_EQ(ctest, count, 0, "no options parsed");
    CTEST_ASSERT_EQ(ctest, state.optind, 2, "optind points to script.sh");
    CTEST_ASSERT_STR_EQ(ctest, argv[state.optind], "script.sh", "first operand is script.sh");
}

CTEST(test_posix_hyphen_at_end)
{
    /* sh -a -
     * The '-' terminates options, no operands follow.
     */
    char* argv[] = { "sh", "-a", "-", NULL };
    int argc = 3;
    struct getopt_state state = { 0 };
    state.opterr = 0;
    state.posix_hyphen = 1;
    flag_a = 0;

    int c;
    while ((c = getopt_long_plus_r(argc, argv, "a", shell_options, NULL, &state)) != -1) {
        if (c == 'a') flag_a = 1;
    }

    CTEST_ASSERT_EQ(ctest, flag_a, 1, "-a was set");
    CTEST_ASSERT_EQ(ctest, state.optind, 3, "optind is argc (no operands)");
}

CTEST(test_posix_hyphen_disabled)
{
    /* With posix_hyphen = 0, lone '-' should be treated as non-option argument
     * (standard getopt behavior with PERMUTE ordering)
     */
    char* argv[] = { "sh", "-a", "-", "-e", NULL };
    int argc = 4;
    struct getopt_state state = { 0 };
    state.opterr = 0;
    state.posix_hyphen = 0;  /* Disabled */
    flag_a = 0;
    flag_e = 0;

    int c;
    while ((c = getopt_long_plus_r(argc, argv, "ae", shell_options, NULL, &state)) != -1) {
        if (c == 'a') flag_a = 1;
        if (c == 'e') flag_e = 1;
    }

    /* With PERMUTE (default), options are extracted from anywhere */
    CTEST_ASSERT_EQ(ctest, flag_a, 1, "-a was set");
    CTEST_ASSERT_EQ(ctest, flag_e, 1, "-e was also set (hyphen handling disabled)");
}

CTEST(test_posix_hyphen_vs_double_dash)
{
    /* Verify '-' and '--' behave similarly but '-' is skipped
     * sh -a -- -e   -> optind points to -e, which is an operand
     * sh -a - -e    -> optind points to -e, which is an operand
     * The difference: '--' is consumed but could be preserved in some modes,
     * while '-' per POSIX is always ignored.
     */
    char* argv1[] = { "sh", "-a", "--", "-e", NULL };
    char* argv2[] = { "sh", "-a", "-", "-e", NULL };
    int argc = 4;

    /* Test with -- */
    struct getopt_state state1 = { 0 };
    state1.opterr = 0;
    state1.posix_hyphen = 1;
    flag_a = 0;
    flag_e = 0;

    int c;
    while ((c = getopt_long_plus_r(argc, argv1, "ae", shell_options, NULL, &state1)) != -1) {
        if (c == 'a') flag_a = 1;
        if (c == 'e') flag_e = 1;
    }

    int optind_double_dash = state1.optind;

    /* Test with - */
    struct getopt_state state2 = { 0 };
    state2.opterr = 0;
    state2.posix_hyphen = 1;
    flag_a = 0;
    flag_e = 0;

    while ((c = getopt_long_plus_r(argc, argv2, "ae", shell_options, NULL, &state2)) != -1) {
        if (c == 'a') flag_a = 1;
        if (c == 'e') flag_e = 1;
    }

    int optind_single_dash = state2.optind;

    /* Both should leave -e as the first operand */
    CTEST_ASSERT_EQ(ctest, optind_double_dash, 3, "-- leaves optind at -e");
    CTEST_ASSERT_EQ(ctest, optind_single_dash, 3, "- leaves optind at -e");
    CTEST_ASSERT_STR_EQ(ctest, argv1[optind_double_dash], "-e", "-- case: first operand is -e");
    CTEST_ASSERT_STR_EQ(ctest, argv2[optind_single_dash], "-e", "- case: first operand is -e");
}

CTEST(test_posix_hyphen_with_plus_options)
{
    /* sh +a - script.sh
     * Plus-prefix options should work before the hyphen terminator.
     */
    char* argv[] = { "sh", "+a", "-", "script.sh", NULL };
    int argc = 4;
    struct getopt_state state = { 0 };
    state.opterr = 0;
    state.posix_hyphen = 1;
    flag_a = 1;  /* Start with flag set */

    int c;
    while ((c = getopt_long_plus_r(argc, argv, "a", shell_options, NULL, &state)) != -1) {
        /* +a returns 0 when flag is handled */
    }

    CTEST_ASSERT_EQ(ctest, flag_a, 0, "+a cleared the flag");
    CTEST_ASSERT_EQ(ctest, state.optind, 3, "optind points to script.sh");
}

int main(int argc, char** argv)
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
        /* POSIX single-hyphen tests */
        CTEST_ENTRY(test_posix_hyphen_terminates_options),
        CTEST_ENTRY(test_posix_hyphen_skips_following_options),
        CTEST_ENTRY(test_posix_hyphen_alone),
        CTEST_ENTRY(test_posix_hyphen_at_end),
        CTEST_ENTRY(test_posix_hyphen_disabled),
        CTEST_ENTRY(test_posix_hyphen_vs_double_dash),
        CTEST_ENTRY(test_posix_hyphen_with_plus_options),
        NULL
    };

    return ctest_run_suite(suite);
}

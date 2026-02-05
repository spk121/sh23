#!/bin/sh
# Test helper functions for POSIX shell quoting tests
# Source this file in test scripts: . ../test_helpers.sh

# Track test results within a test file
_test_count=0
_test_failures=0

# Assert that two values are equal
# Usage: assert_eq "expected" "actual" "description"
assert_eq() {
    expected="$1"
    actual="$2"
    description="$3"
    _test_count=$((_test_count + 1))

    if [ "$expected" = "$actual" ]; then
        printf "    ✓ %s\n" "$description"
        return 0
    else
        printf "    ✗ %s\n" "$description"
        printf "      Expected: '%s'\n" "$expected"
        printf "      Actual:   '%s'\n" "$actual"
        _test_failures=$((_test_failures + 1))
        return 1
    fi
}

# Assert that a command outputs the expected string
# Usage: assert_output "expected" "description" command [args...]
assert_output() {
    expected="$1"
    description="$2"
    shift 2
    actual="$("$@")"
    assert_eq "$expected" "$actual" "$description"
}

# Assert that the number of arguments equals expected
# Usage: assert_argc expected_count description "$@"
assert_argc() {
    expected="$1"
    description="$2"
    shift 2
    actual="$#"
    assert_eq "$expected" "$actual" "$description"
}

# Print all arguments, one per line (useful for debugging word splitting)
print_args() {
    for arg in "$@"; do
        printf '%s\n' "$arg"
    done
}

# Print all arguments separated by |
join_args() {
    first=1
    for arg in "$@"; do
        if [ "$first" = 1 ]; then
            printf '%s' "$arg"
            first=0
        else
            printf '|%s' "$arg"
        fi
    done
}

# Finish a test file - returns non-zero if any failures
finish_tests() {
    if [ "$_test_failures" -gt 0 ]; then
        printf "  %d/%d assertions failed\n" "$_test_failures" "$_test_count"
        return 1
    fi
    printf "  %d assertions passed\n" "$_test_count"
    return 0
}

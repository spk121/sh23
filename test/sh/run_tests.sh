#!/bin/sh
# POSIX Shell Quoting Test Suite
# Tests compliance with POSIX.1-2024 Section 2.2 (Quoting)
#
# Usage: ./run_tests.sh [shell_to_test]
# Example: ./run_tests.sh /bin/dash
#          ./run_tests.sh /bin/bash --posix

set -e

# Colors for output (if terminal supports it)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    BLUE='\033[0;34m'
    NC='\033[0m' # No Color
else
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    NC=''
fi

# Default shell to test
TEST_SHELL="${1:-/bin/sh}"
shift 2>/dev/null || true
SHELL_ARGS="$*"

TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Get the directory where this script lives
#SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SCRIPT_DIR='.'

printf "${BLUE}POSIX Shell Quoting Test Suite${NC}\n"
printf "Testing: %s %s\n" "$TEST_SHELL" "$SHELL_ARGS"
printf "========================================\n\n"

# Run a single test file
run_test_file() {
    test_file="$1"
    test_name="$(basename "$test_file" .sh)"

    printf "${YELLOW}Running: %s${NC}\n" "$test_name"

    if "$TEST_SHELL" $SHELL_ARGS "$test_file"; then
        printf "${GREEN}  PASSED${NC}\n\n"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        printf "${RED}  FAILED${NC}\n\n"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    TESTS_RUN=$((TESTS_RUN + 1))
}

# Run all test files in the tests directory
for test_file in "$SCRIPT_DIR"/[0-9]*.sh; do
    if [ -f "$test_file" ]; then
        run_test_file "$test_file"
    fi
done

# Summary
printf "========================================\n"
printf "Results: %d/%d tests passed\n" "$TESTS_PASSED" "$TESTS_RUN"

if [ "$TESTS_FAILED" -gt 0 ]; then
    printf "${RED}%d tests FAILED${NC}\n" "$TESTS_FAILED"
    exit 1
else
    printf "${GREEN}All tests PASSED${NC}\n"
    exit 0
fi

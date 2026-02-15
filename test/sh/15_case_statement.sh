#!/bin/sh
# Test: case statement (POSIX 2.9.4)
# Covers simple patterns, multiple patterns per branch, quoting,
# pattern matching rules, nested cases, fall-through (;; vs ;& / ;& not POSIX),
# and special/edge cases.

. "$(dirname "$0")/../test_helpers.sh"

printf "  Testing case statement behavior...\n"

# -----------------------------
# 1. Basic / simple case
# -----------------------------
fruit="apple"

case "$fruit" in
    apple)
        result="red round fruit"
        ;;
    banana)
        result="yellow curved"
        ;;
    *)
        result="unknown"
        ;;
esac
printf "result:\"$result\"\n"

assert_eq "red round fruit" "$result" "basic single-pattern match (apple)"

# -----------------------------
# 2. Multiple patterns per branch (pattern list)
# -----------------------------
day="saturday"

case "$day" in
    saturday|Sunday)    result="weekend" ;;
    monday|wednesday|friday) result="weekday-odd" ;;
    tuesday|thursday)   result="weekday-even" ;;
    *)                  result="invalid" ;;
esac

assert_eq "weekend" "$result" "multiple patterns in one branch (saturday|Sunday)"

# -----------------------------
# 3. Pattern matching with * ? [ ]
# -----------------------------
file="photo-2025-vacation.jpg"

case "$file" in
    *.jpg|*.jpeg)   result="jpeg image" ;;
    *.png)          result="png image"  ;;
    photo-????-*.jpg) result="photo with 4-digit year" ;;
    *)              result="other" ;;
esac

assert_eq "jpeg image" "$result" "glob-style pattern *.jpg"

# -----------------------------
# 4. Quoted patterns (literal matching)
# -----------------------------
pattern="*.txt"

case "$pattern" in
    "*.txt")    result="literal *.txt" ;;
    *.txt)      result="expanded glob (should not match)" ;;
    *)          result="no match" ;;
esac

assert_eq "literal *.txt" "$result" "quoted pattern matches literally"

# -----------------------------
# 5. Empty pattern list (deliberate no-op branch)
# -----------------------------
status=0

case $status in
    0)  ;;                    # empty branch is allowed
    1)  result="error" ;;
    *)  result="other" ;;
esac

assert_eq "" "$result" "empty branch (no-op) is valid POSIX"

# -----------------------------
# 6. Nested case statements
# -----------------------------
os="Linux"
arch="x86_64"

case "$os" in
    Linux)
        case "$arch" in
            x86_64)     result="linux-amd64" ;;
            aarch64)    result="linux-arm64" ;;
            *)          result="linux-unknown" ;;
        esac
        ;;
    Darwin)
        result="macOS"
        ;;
    *)
        result="other"
        ;;
esac

assert_eq "linux-amd64" "$result" "nested case statement"

# -----------------------------
# 7. Pattern with character class and negation
# -----------------------------
code="v7.4.2-beta"

case "$code" in
    [0-9]*.[0-9]*.[0-9]*)           result="semver stable" ;;
    [0-9]*.[0-9]*.[0-9]*-*beta*)    result="semver beta"   ;;
    [!0-9]*)                        result="invalid prefix" ;;
    *)                              result="unknown" ;;
esac

assert_eq "semver beta" "$result" "character class + negation + suffix match"

# -----------------------------
# 8. $@ / $* inside case (quoting matters)
# -----------------------------
set -- "file with spaces" "backup.tar.gz"

case "$2" in
    *.tar.gz)   result="tar archive" ;;
    *)          result="other" ;;
esac

assert_eq "tar archive" "$result" "case with positional parameter (quoted)"

# -----------------------------
# 9. Special parameter $# in case pattern
# -----------------------------
set -- one two three

case $# in
    0) result="no args" ;;
    1) result="one arg" ;;
    2) result="two args" ;;
    3) result="three args" ;;
    *) result="many" ;;
esac

assert_eq "three args" "$result" "case matching special parameter $#"

# -----------------------------
# 10. Fall-through NOT supported in POSIX (just verify ;; terminates)
# -----------------------------
color="yellow"

case "$color" in
    red)    result="stop" ;;
    yellow) result="wait" ;;
            # no ;& or ;;& — next branch should NOT execute
    green)  result="go" ;;
    *)      result="invalid" ;;
esac

assert_eq "wait" "$result" "no fall-through (POSIX requires ;; to terminate)"

# -----------------------------
# 11. Unquoted variable in case word (field splitting)
# -----------------------------
input="  banana  "

case $input in          # unquoted → field splitting + empty removal
    banana) result="matched after splitting" ;;
    *)      result="no match" ;;
esac

assert_eq "matched after splitting" "$result" "case word undergoes field splitting (unquoted)"

finish_tests
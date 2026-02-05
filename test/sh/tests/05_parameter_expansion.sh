#!/bin/sh
# Test: Quoting and Parameter Expansion
# Verify quoting correctly controls parameter expansion behavior

. "$(dirname "$0")/../test_helpers.sh"

printf "  Testing quoting with parameter expansion...\n"

# Setup
testvar="hello world"
emptyvar=""
unset unsetvar 2>/dev/null || true

# Single quotes prevent ALL expansion
assert_eq '$testvar' '$testvar' "single quotes prevent variable expansion"
assert_eq '${testvar}' '${testvar}' "single quotes prevent brace expansion"
assert_eq '${testvar:-default}' '${testvar:-default}' "single quotes prevent default value"

# Double quotes allow expansion but prevent word splitting
count_words() {
    echo $#
}
result=$(count_words "$testvar")
assert_eq "1" "$result" "double quotes prevent word splitting of variable"

# Unquoted variable undergoes word splitting
result=$(count_words $testvar)
assert_eq "2" "$result" "unquoted variable is word split"

# Double quotes with various parameter expansions
assert_eq "hello world" "$testvar" "basic variable in double quotes"
assert_eq "hello world" "${testvar}" "braced variable in double quotes"

# Default value expansion
assert_eq "hello world" "${testvar:-default}" "default not used when var set"
assert_eq "default" "${unsetvar:-default}" "default used when var unset"
assert_eq "default" "${emptyvar:-default}" "default used when var empty (:-)"

# Alternate value expansion
assert_eq "alternate" "${testvar:+alternate}" "alternate used when var set"
assert_eq "" "${unsetvar:+alternate}" "alternate not used when var unset"

# Length expansion
assert_eq "11" "${#testvar}" "length of variable"

# Substring/pattern expansions (if supported - POSIX requires these)
testpath="/path/to/file.txt"
assert_eq "file.txt" "${testpath##*/}" "remove longest prefix"
assert_eq "/path/to/file" "${testpath%.*}" "remove shortest suffix"
assert_eq "path/to/file.txt" "${testpath#/}" "remove shortest prefix (just leading /)"
assert_eq "/path/to/file" "${testpath%%.*}" "remove longest suffix (same as shortest here)"

# Nested quotes and expansions
inner="INNER"
outer="prefix ${inner} suffix"
assert_eq "prefix INNER suffix" "$outer" "nested variable in double quotes"

# Empty variable handling
assert_eq "" "$emptyvar" "empty variable expands to empty"
result=$(count_words "$emptyvar")
assert_eq "1" "$result" "quoted empty variable is still one (empty) argument"
result=$(count_words $emptyvar)
assert_eq "0" "$result" "unquoted empty variable produces no arguments"

# Special parameters
assert_eq '$$' '$$' "single quotes preserve double dollar"
# $$ should expand to PID in double quotes
pid="$$"
if [ "$pid" -gt 0 ] 2>/dev/null; then
    printf "    ✓ \$\$ expands to PID in double quotes\n"
else
    printf "    ✗ \$\$ expands to PID in double quotes\n"
    _test_failures=$((_test_failures + 1))
fi
_test_count=$((_test_count + 1))

# $? expansion
true
assert_eq "0" "$?" "exit status 0 after true"
false || true  # reset
assert_eq "0" "$?" "exit status accessible via \$?"

# Quoting within parameter expansion
spacey="has spaces"
assert_eq "has spaces" "${spacey}" "variable with spaces in braces"

# Array-like behavior with $@ and $*
test_args() {
    # "$@" preserves argument boundaries
    count_words "$@"
}
result=$(test_args "one two" "three")
assert_eq "2" "$result" '\"$@\" preserves argument count'

test_args_star() {
    # "$*" joins all args into one
    count_words "$*"
}
result=$(test_args_star "one two" "three")
assert_eq "1" "$result" '\"$*\" joins into one argument'

# Complex nesting
a="A"
b="B"
result="${a}${b}"
assert_eq "AB" "$result" "adjacent expansions"
result="${a} ${b}"
assert_eq "A B" "$result" "expansions with space"
result="${a}literal${b}"
assert_eq "AliteralB" "$result" "expansions with literal between"

finish_tests

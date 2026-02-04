#!/bin/sh
# Test: Quoting and Word Splitting (Field Splitting)
# Verify quoting correctly prevents word splitting (POSIX 2.6.5)

. "$(dirname "$0")/../test_helpers.sh"

printf "  Testing quoting with word splitting...\n"

count_words() {
    echo $#
}

# Default IFS is space, tab, newline
# Space splitting
var="one two three"
result=$(count_words $var)
assert_eq "3" "$result" "unquoted spaces cause word split"

result=$(count_words "$var")
assert_eq "1" "$result" "quoted spaces no word split"

# Tab splitting
var="one	two	three"
result=$(count_words $var)
assert_eq "3" "$result" "unquoted tabs cause word split"

result=$(count_words "$var")
assert_eq "1" "$result" "quoted tabs no word split"

# Newline splitting
var="one
two
three"
result=$(count_words $var)
assert_eq "3" "$result" "unquoted newlines cause word split"

result=$(count_words "$var")
assert_eq "1" "$result" "quoted newlines no word split"

# Multiple spaces treated as single separator when unquoted
var="one    two"
result=$(count_words $var)
assert_eq "2" "$result" "multiple spaces = single separator"

# Multiple spaces preserved when quoted
join_words() {
    printf '%s' "$1"
}
result=$(join_words "$var")
assert_eq "one    two" "$result" "quoted multiple spaces preserved"

# Empty fields from adjacent separators (not produced by unquoted)
var="one  two"  # two spaces
result=$(count_words $var)
assert_eq "2" "$result" "adjacent separators don't create empty fields"

# Custom IFS
oldIFS="$IFS"
IFS=":"
var="one:two:three"
result=$(count_words $var)
assert_eq "3" "$result" "custom IFS splits on colon"

result=$(count_words "$var")
assert_eq "1" "$result" "quoted ignores custom IFS"
IFS="$oldIFS"

# IFS with multiple characters
oldIFS="$IFS"
IFS=":,"
var="one:two,three"
result=$(count_words $var)
assert_eq "3" "$result" "IFS with multiple chars"
IFS="$oldIFS"

# Empty IFS prevents all splitting
oldIFS="$IFS"
IFS=""
var="one two three"
result=$(count_words $var)
assert_eq "1" "$result" "empty IFS prevents splitting"
IFS="$oldIFS"

# Null IFS same as empty
oldIFS="$IFS"
unset IFS
var="one two three"
# With unset IFS, default space/tab/newline used
result=$(count_words $var)
assert_eq "3" "$result" "unset IFS uses default splitting"
IFS="$oldIFS"

# Word splitting with command substitution
result=$(count_words $(echo "one two three"))
assert_eq "3" "$result" "unquoted command sub is split"

result=$(count_words "$(echo "one two three")")
assert_eq "1" "$result" "quoted command sub not split"

# Word splitting with parameter expansion results
var="a b c"
result=$(count_words ${var})
assert_eq "3" "$result" "unquoted parameter expansion is split"

result=$(count_words "${var}")
assert_eq "1" "$result" "quoted parameter expansion not split"

# Leading/trailing whitespace
var="  padded  "
result=$(count_words $var)
assert_eq "1" "$result" "leading/trailing space trimmed when unquoted"

# "$@" preserves original field boundaries
test_at() {
    count_words "$@"
}
result=$(test_at "one two" "three four")
assert_eq "2" "$result" '"$@" preserves argument boundaries'

# $@ without quotes loses boundaries
test_at_unquoted() {
    count_words $@
}
result=$(test_at_unquoted "one two" "three four")
assert_eq "4" "$result" '$@ without quotes loses boundaries'

# "$*" joins into single field
test_star() {
    count_words "$*"
}
result=$(test_star "one" "two" "three")
assert_eq "1" "$result" '"$*" joins into single field'

# $* without quotes - joins then splits
test_star_unquoted() {
    count_words $*
}
result=$(test_star_unquoted "one" "two" "three")
assert_eq "3" "$result" '$* joins then splits'

# IFS affects "$*" joining
oldIFS="$IFS"
IFS=":"
test_star_ifs() {
    printf '%s' "$*"
}
result=$(test_star_ifs a b c)
assert_eq "a:b:c" "$result" '"$*" joins with first char of IFS'
IFS="$oldIFS"

# Glob characters and word splitting combined
mkdir -p /tmp/ws_test_$$
touch /tmp/ws_test_$$/file1 /tmp/ws_test_$$/file2
cd /tmp/ws_test_$$
var="file*"
# Unquoted: word split then glob
result=$(count_words $var)
assert_eq "2" "$result" "unquoted glob in var expands to matching files"
cd - >/dev/null
rm -rf /tmp/ws_test_$$

finish_tests

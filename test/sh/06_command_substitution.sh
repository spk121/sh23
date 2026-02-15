#!/bin/sh
# Test: Quoting and Command Substitution
# Verify quoting correctly controls command substitution behavior

. "$(dirname "$0")/../test_helpers.sh"

# Set TMPDIR if not already set (for cross-platform support)
if [ -z "$TMPDIR" ]; then
    if [ -n "$TEMP" ]; then
        TMPDIR="$TEMP"
    elif [ -n "$TMP" ]; then
        TMPDIR="$TMP"
    else
        TMPDIR="/tmp"
    fi
fi

printf "  Testing quoting with command substitution...\n"

# Single quotes prevent command substitution
assert_eq '$(echo hello)' '$(echo hello)' "single quotes prevent \$() substitution"
assert_eq '`echo hello`' '`echo hello`' "single quotes prevent backtick substitution"

# Double quotes allow command substitution
assert_eq "hello" "$(echo hello)" "double quotes allow \$() substitution"
assert_eq "hello" "`echo hello`" "double quotes allow backtick substitution"

# Unquoted command substitution
result=$(echo hello)
assert_eq "hello" "$result" "unquoted \$() works"

# Command substitution output undergoes word splitting when unquoted
count_words() {
    echo $#
}
result=$(count_words $(echo "one two three"))
assert_eq "3" "$result" "unquoted command sub is word split"

result=$(count_words "$(echo "one two three")")
assert_eq "1" "$result" "quoted command sub prevents word splitting"

# Nested command substitution
result=$(echo $(echo nested))
assert_eq "nested" "$result" "nested command substitution"

result=$(echo "$(echo "double nested")")
assert_eq "double nested" "$result" "nested with quotes"

# Command substitution with special characters in output
result="$(echo 'hello$world')"
assert_eq 'hello$world' "$result" "special chars in command output preserved"

result="$(echo "with spaces   multiple")"
assert_eq "with spaces   multiple" "$result" "spaces preserved in quoted command sub"

# Backticks vs $() - behavior should be identical
result1=$(echo test)
result2=`echo test`
assert_eq "$result1" "$result2" "backticks and \$() equivalent"

# Escaping within backticks (different rules than $())
# In backticks, backslash only escapes $ ` \
result=`echo '\$HOME'`
assert_eq '$HOME' "$result" "literal dollar in backticks via single quotes"

# Nested backticks require escaping
result=`echo \`echo inner\``
assert_eq "inner" "$result" "nested backticks with escaping"

# $() nesting is cleaner
result=$(echo $(echo inner))
assert_eq "inner" "$result" "\$() nests without escaping"

# Quoting inside command substitution
var="test value"
result=$(echo "$var")
assert_eq "test value" "$result" "quoting preserved inside command sub"

# Multiple command substitutions
a=$(echo A)
b=$(echo B)
result="$a$b"
assert_eq "AB" "$result" "multiple command subs adjacent"

result="$a $b"
assert_eq "A B" "$result" "multiple command subs with space"

# Command substitution preserves exit status context
result=$(true; echo $?)
assert_eq "0" "$result" "command sub captures exit status"

result=$(false; echo $?)
assert_eq "1" "$result" "command sub captures non-zero exit status"

# Trailing newlines are stripped from command substitution
result=$(printf "hello\n\n\n")
assert_eq "hello" "$result" "trailing newlines stripped"

# But internal newlines are preserved
result=$(printf "line1\nline2")
expected="line1
line2"
assert_eq "$expected" "$result" "internal newlines preserved"

# Command substitution in different quoting contexts
assert_eq "HELLO" "$(echo HELLO)" "command sub in double quotes"
assert_eq 'prefix HELLO suffix' "prefix $(echo HELLO) suffix" "command sub mid-string"

# Empty command substitution
result="$(true)"
assert_eq "" "$result" "empty command sub result"

# Command substitution with pipes
result=$(echo "hello world" | tr ' ' '_')
assert_eq "hello_world" "$result" "command sub with pipe"

# Command substitution with redirections
echo "file content" > "$TMPDIR/cmdsub_test_$$"
result=$(cat < "$TMPDIR/cmdsub_test_$$")
assert_eq "file content" "$result" "command sub with redirection"
rm -f "$TMPDIR/cmdsub_test_$$"

finish_tests

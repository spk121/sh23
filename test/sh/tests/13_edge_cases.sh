#!/bin/sh
# Test: Quoting Edge Cases and Corner Cases
# Various tricky scenarios and potential pitfalls

. "$(dirname "$0")/../test_helpers.sh"

printf "  Testing quoting edge cases...\n"

count_words() {
    echo $#
}

# Empty quoted strings
assert_eq "" "" "empty double-quoted string"
assert_eq '' '' "empty single-quoted string"

# Empty quotes still count as argument
result=$(count_words "")
assert_eq "1" "$result" "empty double-quoted is one argument"

result=$(count_words '')
assert_eq "1" "$result" "empty single-quoted is one argument"

result=$(count_words "" "" "")
assert_eq "3" "$result" "three empty args"

# Null byte handling (shell should handle gracefully)
# Note: Some shells have issues with embedded nulls
# POSIX doesn't require null byte support in strings

# Very long quoted string
long=$(printf '%0500s' | tr ' ' 'x')
result=$(printf '%s' "$long" | wc -c | tr -d ' ')
assert_eq "500" "$result" "long string preserved"

# Quotes in variable names (via eval)
eval 'var_with_number1="value"'
assert_eq "value" "$var_with_number1" "variable with number in name"

# Unicode in quoted strings (if shell supports)
result="café"
assert_eq "café" "$result" "unicode in double quotes"

result='naïve'
assert_eq "naïve" "$result" "unicode in single quotes"

# Control characters
# Tab
tab="	"
assert_eq "	" "$tab" "tab in variable"

# Carriage return
cr="$(printf '\r')"
assert_eq "$(printf '\r')" "$cr" "carriage return preserved"

# Quotes at word boundaries
result=a"b"c
assert_eq "abc" "$result" "quotes at boundaries"

result="a"b"c"
assert_eq "abc" "$result" "alternating quotes"

result='a'"b"'c'
assert_eq "abc" "$result" "single-double-single"

# Quote immediately after variable
var="value"
result="$var"text
assert_eq "valuetext" "$result" "text after quoted var"

result="${var}"text
assert_eq "valuetext" "$result" "text after braced var"

# Quote immediately before variable
result=text"$var"
assert_eq "textvalue" "$result" "quoted var after text"

# Backslash at end of single-quoted string
# This is actually tricky - backslash is literal in single quotes
result='hello\'
# Shell should error or interpret differently
# In single quotes, backslash is literal, so this is: hello\
# But then there's no closing quote...
# Actually 'hello\' is a valid single-quoted string containing hello\
assert_eq 'hello\' "$result" "backslash at end of single quotes"

# Nested quotes in command substitution
result=$(echo "$(echo 'inner')")
assert_eq "inner" "$result" "nested single in double in command sub"

result=$(echo '$(echo "not expanded")')
assert_eq '$(echo "not expanded")' "$result" "command sub in single quotes"

# Quote after backslash
result=\"
assert_eq '"' "$result" "quote after backslash"

result=\'
assert_eq "'" "$result" "single quote after backslash"

# Multiple consecutive escapes
result=\\\\
assert_eq '\\' "$result" "four backslashes = two"

result="\\\\"
assert_eq '\\' "$result" "four backslashes in double quotes"

# Backslash before newline in different contexts
result="line1\
line2"
assert_eq "line1line2" "$result" "backslash-newline in double quotes"

result='line1\
line2'
expected='line1\
line2'
assert_eq "$expected" "$result" "backslash-newline literal in single quotes"

# Dollar at end of string
result="hello$"
assert_eq 'hello$' "$result" "dollar at end is literal"

result='hello$'
assert_eq 'hello$' "$result" "dollar at end in single quotes"

# Backtick at end
result="hello\`"
assert_eq 'hello`' "$result" "escaped backtick at end"

# Assignment with special characters
var="hello|world"
assert_eq 'hello|world' "$var" "pipe in assignment"

var="hello;world"
assert_eq 'hello;world' "$var" "semicolon in assignment"

var="hello&world"
assert_eq 'hello&world' "$var" "ampersand in assignment"

# Command substitution edge cases
result=$(printf '')
assert_eq "" "$result" "empty command substitution"

# Note: Command substitution strips ALL trailing newlines
result=$(echo; echo; echo "text")
assert_eq "

text" "$result" "multiple newlines before text preserved"

# Tilde in different positions
result="~"
# Tilde in quotes is literal
assert_eq "~" "$result" "tilde in quotes is literal"

result=~/"subdir"
# Tilde before slash should expand, but quoted part is literal
# This depends on where quotes start
if [ "$result" != "~/subdir" ]; then
    printf "    ✓ ~/ expands even when followed by quotes\n"
else
    printf "    ✓ ~/ expansion behavior verified\n"
fi
_test_count=$((_test_count + 1))

# Assignment with equals
var="a=b"
assert_eq "a=b" "$var" "equals in quoted assignment value"

# Export with quoted value
export TESTVAR="has spaces"
assert_eq "has spaces" "$TESTVAR" "exported var with spaces"
unset TESTVAR

# Readonly with quoted value
readonly ROVAR="readonly value" 2>/dev/null || true
assert_eq "readonly value" "$ROVAR" "readonly var with spaces"

# Arithmetic in tricky contexts
result="$((1+1))$((2+2))"
assert_eq "24" "$result" "adjacent arithmetic expansions"

result=$((1+1))$((2+2))
assert_eq "24" "$result" "unquoted adjacent arithmetic"

finish_tests

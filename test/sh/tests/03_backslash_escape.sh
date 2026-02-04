#!/bin/sh
# Test: Escape Character (Backslash) (POSIX 2.2.1)
# "A backslash that is not quoted shall preserve the literal value of the
# following character, with the exception of a newline."

. "$(dirname "$0")/../test_helpers.sh"

printf "  Testing backslash escape behavior...\n"

# Escaping characters that must be quoted
assert_eq '|' \| "escaped pipe"
assert_eq '&' \& "escaped ampersand"
assert_eq ';' \; "escaped semicolon"
assert_eq '<' \< "escaped less-than"
assert_eq '>' \> "escaped greater-than"
assert_eq '(' \( "escaped open paren"
assert_eq ')' \) "escaped close paren"
assert_eq '$' \$ "escaped dollar sign"
assert_eq '`' \` "escaped backtick"
assert_eq '\' \\ "escaped backslash"
assert_eq '"' \" "escaped double quote"
assert_eq "'" \' "escaped single quote"

# Escaping space prevents word splitting
count_words() {
    echo $#
}
result=$(count_words hello\ world)
assert_eq "1" "$result" "escaped space makes one word"

# Escaping tab
result=$(count_words hello\	world)
assert_eq "1" "$result" "escaped tab makes one word"

# The conditionally-special characters
assert_eq '*' \* "escaped asterisk"
assert_eq '?' \? "escaped question mark"
assert_eq '[' \[ "escaped open bracket"
assert_eq ']' \] "escaped close bracket"
assert_eq '^' \^ "escaped caret"
assert_eq '-' \- "escaped hyphen"
assert_eq '!' \! "escaped exclamation"
assert_eq '#' \# "escaped hash"
assert_eq '~' \~ "escaped tilde"
assert_eq '=' \= "escaped equals"
assert_eq '%' \% "escaped percent"
assert_eq '{' \{ "escaped open brace"
assert_eq '}' \} "escaped close brace"
assert_eq ',' \, "escaped comma"

# Backslash-newline is line continuation (character is removed)
result=hello\
world
assert_eq "helloworld" "$result" "backslash-newline continues line"

# Multiple escapes in sequence
result=\$\$\$
assert_eq '$$$' "$result" "multiple escaped dollars"

# Escape prevents glob expansion
mkdir -p /tmp/quoting_test_$$
touch /tmp/quoting_test_$$/file1.txt /tmp/quoting_test_$$/file2.txt
cd /tmp/quoting_test_$$
result=\*.txt
assert_eq '*.txt' "$result" "escaped glob not expanded"
cd - >/dev/null
rm -rf /tmp/quoting_test_$$

# Escape prevents variable expansion
var="REPLACED"
result=\$var
assert_eq '$var' "$result" "escaped dollar prevents variable expansion"

# Escape prevents command substitution
result=\`echo\ hello\`
assert_eq '`echo hello`' "$result" "escaped backticks prevent command sub"

result=\$\(echo\ hello\)
assert_eq '$(echo hello)' "$result" "escaped dollar-paren prevents command sub"

# Escaping ordinary characters (backslash is removed, char preserved)
assert_eq 'a' \a "escaped ordinary char a"
assert_eq 'z' \z "escaped ordinary char z"
assert_eq '5' \5 "escaped digit"

# Backslash at end of line in command (continuation)
result=$(echo hello \
world)
assert_eq "hello world" "$result" "backslash continues command"

finish_tests

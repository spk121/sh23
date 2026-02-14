#!/bin/sh
# Test: Quoting Prevents Reserved Word Recognition
# POSIX 2.2: "prevent reserved words from being recognized as such"
# Tests that reserved words work as reserved words when unquoted,
# and demonstrates various quoting contexts.

. "$(dirname "$0")/../test_helpers.sh"

printf "  Testing reserved word recognition...\n"

# -----------------------------
# 1. Test unquoted reserved words work as expected
# -----------------------------

# if/then/else/fi
if true; then
    result="if_works"
else
    result="if_fails"
fi
assert_eq "if_works" "$result" "unquoted 'if' works as reserved word"

# for/do/done
result=""
for item in a b c; do
    result="${result}${item}"
done
assert_eq "abc" "$result" "unquoted 'for/do/done' works as reserved words"

# while/do/done
count=0
result=""
while [ "$count" -lt 3 ]; do
    result="${result}${count}"
    count=$((count + 1))
done
assert_eq "012" "$result" "unquoted 'while' works as reserved word"

# until/do/done
count=0
result=""
until [ "$count" -ge 3 ]; do
    result="${result}${count}"
    count=$((count + 1))
done
assert_eq "012" "$result" "unquoted 'until' works as reserved word"

# case/in/esac
value="match"
case "$value" in
    match)
        result="case_matched"
        ;;
    *)
        result="no_match"
        ;;
esac
assert_eq "case_matched" "$result" "unquoted 'case/in/esac' works as reserved words"

# elif
if false; then
    result="wrong"
elif true; then
    result="elif_works"
else
    result="else_ran"
fi
assert_eq "elif_works" "$result" "unquoted 'elif' works as reserved word"

# -----------------------------
# 2. Test quoted reserved words as variable values
# -----------------------------

# Double quotes
result="if"
assert_eq "if" "$result" "double-quoted 'if' is literal string"

result="then"
assert_eq "then" "$result" "double-quoted 'then' is literal string"

result="else"
assert_eq "else" "$result" "double-quoted 'else' is literal string"

result="fi"
assert_eq "fi" "$result" "double-quoted 'fi' is literal string"

result="for"
assert_eq "for" "$result" "double-quoted 'for' is literal string"

result="while"
assert_eq "while" "$result" "double-quoted 'while' is literal string"

result="case"
assert_eq "case" "$result" "double-quoted 'case' is literal string"

result="in"
assert_eq "in" "$result" "double-quoted 'in' is literal string"

# Single quotes
result='if'
assert_eq "if" "$result" "single-quoted 'if' is literal string"

result='for'
assert_eq "for" "$result" "single-quoted 'for' is literal string"

result='while'
assert_eq "while" "$result" "single-quoted 'while' is literal string"

# -----------------------------
# 3. Test case patterns can match reserved words
# -----------------------------

word="if"
case "$word" in
    if)
        result="matched_if"
        ;;
    *)
        result="no_match"
        ;;
esac
assert_eq "matched_if" "$result" "quoted 'if' matches in case pattern"

word="for"
case "$word" in
    for)
        result="matched_for"
        ;;
    *)
        result="no_match"
        ;;
esac
assert_eq "matched_for" "$result" "quoted 'for' matches in case pattern"

word="while"
case "$word" in
    while)
        result="matched_while"
        ;;
    *)
        result="no_match"
        ;;
esac
assert_eq "matched_while" "$result" "quoted 'while' matches in case pattern"

# -----------------------------
# 4. Test reserved words in variable expansion
# -----------------------------

if_var="if_value"
for_var="for_value"
while_var="while_value"

assert_eq "if_value" "$if_var" "variable named 'if_var' with reserved word prefix"
assert_eq "for_value" "$for_var" "variable named 'for_var' with reserved word prefix"
assert_eq "while_value" "$while_var" "variable named 'while_var' with reserved word prefix"

# -----------------------------
# 5. Test parameter expansion with reserved word values
# -----------------------------

word="if"
assert_eq "if" "$word" "variable containing reserved word 'if'"

word="for"
assert_eq "for" "$word" "variable containing reserved word 'for'"

word="case"
assert_eq "case" "$word" "variable containing reserved word 'case'"

# Default value containing reserved word
unset notset
result="${notset:-if}"
assert_eq "if" "$result" "parameter expansion default value 'if'"

result="${notset:-for}"
assert_eq "for" "$result" "parameter expansion default value 'for'"

# -----------------------------
# 6. Test loop variable named after reserved word (should work)
# -----------------------------

result=""
for if in 1 2 3; do
    result="${result}${if}"
done
assert_eq "123" "$result" "loop variable named 'if' works"

result=""
for case in a b c; do
    result="${result}${case}"
done
assert_eq "abc" "$result" "loop variable named 'case' works"

# -----------------------------
# 7. Test that reserved words work in string contexts
# -----------------------------

# Reserved words as part of longer strings
result="prefix_if_suffix"
assert_eq "prefix_if_suffix" "$result" "reserved word 'if' within string"

result="start_for_end"
assert_eq "start_for_end" "$result" "reserved word 'for' within string"

# Reserved words after assignments
result="assigned"
var=if
assert_eq "if" "$var" "reserved word 'if' as assignment value"

var=for
assert_eq "for" "$var" "reserved word 'for' as assignment value"

var=while
assert_eq "while" "$var" "reserved word 'while' as assignment value"

finish_tests

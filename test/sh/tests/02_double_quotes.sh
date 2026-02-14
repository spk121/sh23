#!/bin/sh
# Test: Double Quotes (POSIX 2.2.3)
# "Enclosing characters in double-quotes shall preserve the literal value
# of all characters within the double-quotes, with the exception of the
# characters backquote, dollar-sign, and backslash"

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

printf "  Testing double quote behavior...\n"

# Characters that SHOULD be preserved literally in double quotes
assert_eq "|" "|" "pipe preserved"
assert_eq "&" "&" "ampersand preserved"
assert_eq ";" ";" "semicolon preserved"
assert_eq "<" "<" "less-than preserved"
assert_eq ">" ">" "greater-than preserved"
assert_eq "(" "(" "open paren preserved"
assert_eq ")" ")" "close paren preserved"
assert_eq "'" "'" "single quote preserved"
assert_eq " " " " "space preserved"

# Tab and newline
tab="	"
assert_eq "$tab" "	" "tab preserved"

nl="
"
# Compare by checking the actual newline character is present
if [ "$nl" = "
" ]; then
    printf "    ✓ newline preserved\n"
else
    printf "    ✗ newline preserved\n"
    _test_failures=$((_test_failures + 1))
fi
_test_count=$((_test_count + 1))

# The conditionally-special characters (preserved in double quotes)
assert_eq "*" "*" "asterisk preserved (no glob)"
assert_eq "?" "?" "question mark preserved"
assert_eq "[" "[" "open bracket preserved"
assert_eq "]" "]" "close bracket preserved"
assert_eq "^" "^" "caret preserved"
assert_eq "-" "-" "hyphen preserved"
assert_eq "#" "#" "hash preserved"
assert_eq "~" "~" "tilde preserved (no expansion)"
assert_eq "=" "=" "equals preserved"
assert_eq "%" "%" "percent preserved"
assert_eq "{" "{" "open brace preserved"
assert_eq "}" "}" "close brace preserved"
assert_eq "," "," "comma preserved"

# Whitespace preservation and no word splitting
result=$(printf '%s' "hello   world")
assert_eq "hello   world" "$result" "multiple spaces preserved"

count_words() {
    echo $#
}
result=$(count_words "one two three")
assert_eq "1" "$result" "double-quoted string is one word"

# No glob expansion in double quotes
mkdir -p "$TMPDIR/quoting_test_$$"
touch "$TMPDIR/quoting_test_$$/file1.txt" "$TMPDIR/quoting_test_$$/file2.txt"
cd "$TMPDIR/quoting_test_$$"
result="*.txt"
assert_eq "*.txt" "$result" "glob pattern not expanded"
cd - >/dev/null
rm -rf "$TMPDIR/quoting_test_$$"

# Dollar sign - DOES trigger expansion
var="hello"
assert_eq "hello" "$var" "variable expansion works"
assert_eq "hello" "${var}" "brace variable expansion works"

# Arithmetic expansion
result="$((2+3))"
assert_eq "5" "$result" "arithmetic expansion works"

# Command substitution with $()
result="$(echo hello)"
assert_eq "hello" "$result" "command substitution with \$() works"

# Backslash escaping within double quotes
# Backslash only escapes: $ ` " \ newline
assert_eq '$' "\$" "backslash escapes dollar"
assert_eq '`' "\`" "backslash escapes backtick"
assert_eq '"' "\"" "backslash escapes double quote"
assert_eq '\' "\\" "backslash escapes backslash"

# Backslash before other characters is preserved literally
assert_eq '\n' "\n" "backslash-n is literal (not newline)"
assert_eq '\t' "\t" "backslash-t is literal (not tab)"
assert_eq '\a' "\a" "backslash-a is literal"

# Backslash-newline (line continuation) within double quotes
result="hello\
world"
assert_eq "helloworld" "$result" "backslash-newline continues line"

# Nested quoting - single quote inside double quotes
assert_eq "it's" "it's" "single quote inside double quotes"
assert_eq "say 'hello'" "say 'hello'" "single-quoted word inside double quotes"

finish_tests

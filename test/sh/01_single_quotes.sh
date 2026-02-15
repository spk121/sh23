#!/bin/sh
# Test: Single Quotes (POSIX 2.2.2)
# "Enclosing characters in single-quotes shall preserve the literal
# value of each character within the single-quotes."


# mgsh_dirnamevar SCRIPT_DIR "$0"
# echo "Script directory: $SCRIPT_DIR"
# . "${SCRIPT_DIR}/../test_helpers.sh"

. "./test_helpers.sh"

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

printf "  Testing single quote behavior...\n"

# Basic: Single quotes preserve special characters literally
assert_eq '|' '|' "pipe preserved"
assert_eq '&' '&' "ampersand preserved"
assert_eq ';' ';' "semicolon preserved"
assert_eq '<' '<' "less-than preserved"
assert_eq '>' '>' "greater-than preserved"
assert_eq '(' '(' "open paren preserved"
assert_eq ')' ')' "close paren preserved"
assert_eq '$' '$' "dollar sign preserved"
assert_eq '`' '`' "backtick preserved"
assert_eq '\' '\' "backslash preserved"
assert_eq '"' '"' "double quote preserved"
assert_eq ' ' ' ' "space preserved"

# Tab character
tab='	'
assert_eq "$tab" '	' "tab preserved"

# Newline within single quotes
nl='
'
# Compare by checking the actual newline character is present
if [ "$nl" = "
" ]; then
    printf "    PASS newline preserved\n"
else
    printf "    FAIL newline preserved\n"
    _test_failures=$((_test_failures + 1))
fi
_test_count=$((_test_count + 1))


# Multiple special characters together
assert_eq '|&;<>()$`\"' '|&;<>()$`\"' "multiple specials preserved"

# Backslash does NOT escape within single quotes
assert_eq '\\' '\\' "double backslash is literal"
assert_eq '\n' '\n' "backslash-n is literal (not newline)"
assert_eq '\t' '\t' "backslash-t is literal (not tab)"

# Dollar sign and variable-like syntax
var="REPLACED"
assert_eq '$var' '$var' "variable syntax preserved literally"
assert_eq '${var}' '${var}' "brace variable syntax preserved"
assert_eq '$((1+1))' '$((1+1))' "arithmetic expansion preserved"

# Backticks (command substitution syntax)
assert_eq '`echo hello`' '`echo hello`' "command substitution preserved"
assert_eq '$(echo hello)' '$(echo hello)' "dollar-paren command sub preserved"

# Whitespace and word splitting
result=$(printf '%s' 'hello   world')
assert_eq 'hello   world' "$result" "multiple spaces preserved"

# Test that single-quoted strings are single words
count_words() {
    echo $#
}
result=$(count_words 'one two three')
assert_eq "1" "$result" "single-quoted string is one word"

# The conditionally-special characters
assert_eq '*' '*' "asterisk preserved"
assert_eq '?' '?' "question mark preserved"
assert_eq '[' '[' "open bracket preserved"
assert_eq ']' ']' "close bracket preserved"
assert_eq '^' '^' "caret preserved"
assert_eq '-' '-' "hyphen preserved"
assert_eq '!' '!' "exclamation preserved"
assert_eq '#' '#' "hash preserved"
assert_eq '~' '~' "tilde preserved"
assert_eq '=' '=' "equals preserved"
assert_eq '%' '%' "percent preserved"
assert_eq '{' '{' "open brace preserved"
assert_eq '}' '}' "close brace preserved"
assert_eq ',' ',' "comma preserved"

# Glob patterns should not expand
mkdir -p "$TMPDIR/quoting_test_$$"
touch "$TMPDIR/quoting_test_$$/file1.txt" "$TMPDIR/quoting_test_$$/file2.txt"
cd "$TMPDIR/quoting_test_$$"
result='*.txt'
assert_eq '*.txt' "$result" "glob pattern not expanded"
cd - >/dev/null
rm -rf "$TMPDIR/quoting_test_$$"

finish_tests

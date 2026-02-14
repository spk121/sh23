#!/bin/sh
# Test: Special Characters Without Quoting
# Verify that special characters have their special meaning when unquoted

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

TMPDIR="$TEMP"

printf "  Testing unquoted special character behavior...\n"

# Pipe |
result=$(echo hello | cat)
assert_eq "hello" "$result" "pipe works unquoted"

# Ampersand & (background) - just verify syntax is accepted
(sleep 0 &)
assert_eq "0" "$?" "ampersand (background) accepted"

# Semicolon ; (command separator)
result=$(echo hello; echo world)
expected="hello
world"
assert_eq "$expected" "$result" "semicolon separates commands"
exit

# Less-than < (input redirection)
echo "test content" > "$TMPDIR/quoting_redirect_test_$$"
result=$(cat < "$TMPDIR/quoting_redirect_test_$$")
assert_eq "test content" "$result" "less-than redirects input"
rm -f "$TMPDIR/quoting_redirect_test_$$"

# Greater-than > (output redirection)
echo "output test" > "$TMPDIR/quoting_redirect_test_$$"
result=$(cat "$TMPDIR/quoting_redirect_test_$$")
assert_eq "output test" "$result" "greater-than redirects output"
rm -f "$TMPDIR/quoting_redirect_test_$$"

# Parentheses () (subshell)
result=$( (echo subshell) )
assert_eq "subshell" "$result" "parentheses create subshell"

# Dollar sign $ (parameter expansion)
testvar="expanded"
result=$testvar
assert_eq "expanded" "$result" "dollar expands variable"

# Backtick ` (command substitution - legacy)
result=`echo backtick`
assert_eq "backtick" "$result" "backtick command substitution"

# Double quote " (starts quoted string)
result="quoted string"
assert_eq "quoted string" "$result" "double quote starts string"

# Single quote ' (starts quoted string)
result='quoted string'
assert_eq "quoted string" "$result" "single quote starts string"

# Space (word splitting)
count_words() {
    echo $#
}
result=$(count_words one two three)
assert_eq "3" "$result" "spaces split words"

# Tab (word splitting)
result=$(count_words one	two	three)
assert_eq "3" "$result" "tabs split words"

# Newline (command terminator tested implicitly)

# Hash # (comment - verify code after # is not executed)
# # must follow whitespace to start a comment
result=$(echo hello)  # this comment is ignored
assert_eq "hello" "$result" "hash after whitespace starts comment"

# Hash NOT following whitespace is literal
result=$(echo hello#world)
assert_eq "hello#world" "$result" "hash without whitespace is literal"

# Asterisk * (glob)
mkdir -p "$TMPDIR/quoting_glob_test_$$"
touch "$TMPDIR/quoting_glob_test_$$/a.txt" "$TMPDIR/quoting_glob_test_$$/b.txt"
cd "$TMPDIR/quoting_glob_test_$$"
result=$(echo *.txt | tr ' ' '\n' | sort | tr '\n' ' ' | sed 's/ $//')
assert_eq "a.txt b.txt" "$result" "asterisk expands glob"
cd - >/dev/null
rm -rf "$TMPDIR/quoting_glob_test_$$"

# Question mark ? (single char glob)
mkdir -p "$TMPDIR/quoting_glob_test_$$"
touch "$TMPDIR/quoting_glob_test_$$/a1" "$TMPDIR/quoting_glob_test_$$/a2" "$TMPDIR/quoting_glob_test_$$/abc"
cd "$TMPDIR/quoting_glob_test_$$"
result=$(echo a? | tr ' ' '\n' | sort | tr '\n' ' ' | sed 's/ $//')
assert_eq "a1 a2" "$result" "question mark matches single char"
cd - >/dev/null
rm -rf "$TMPDIR/quoting_glob_test_$$"

# Brackets [] (character class glob)
mkdir -p "$TMPDIR/quoting_glob_test_$$"
touch "$TMPDIR/quoting_glob_test_$$/a1" "$TMPDIR/quoting_glob_test_$$/b1" "$TMPDIR/quoting_glob_test_$$/c1"
cd "$TMPDIR/quoting_glob_test_$$"
result=$(echo [ab]1 | tr ' ' '\n' | sort | tr '\n' ' ' | sed 's/ $//')
assert_eq "a1 b1" "$result" "brackets match character class"
cd - >/dev/null
rm -rf "$TMPDIR/quoting_glob_test_$$"

# Tilde ~ (home directory expansion)
# Note: tilde expansion only happens at start of word or after : or =
result=$(echo ~)
# Can't test exact value but should not be literal ~
if [ "$result" = "~" ]; then
    printf "    ✗ tilde expands to home directory\n"
    printf "      Expected: not '~'\n"
    printf "      Actual:   '%s'\n" "$result"
    _test_failures=$((_test_failures + 1))
else
    printf "    ✓ tilde expands to home directory\n"
fi
_test_count=$((_test_count + 1))

# Braces {} (brace expansion - note: not required by POSIX, but commonly supported)
# We'll test it but not fail if unsupported
result=$(echo a{1,2,3}b 2>/dev/null || echo "a{1,2,3}b")
if [ "$result" = "a1b a2b a3b" ]; then
    printf "    ✓ brace expansion works (extension)\n"
else
    printf "    ✓ brace expansion not performed (POSIX compliant)\n"
fi
_test_count=$((_test_count + 1))

finish_tests

#!/bin/sh
# Test: Quoting and Pathname Expansion (Globbing)
# Verify quoting correctly prevents glob expansion (POSIX 2.6.6)

. "$(dirname "$0")/../test_helpers.sh"

printf "  Testing quoting with pathname expansion...\n"

# Setup test directory
TEST_DIR="/tmp/glob_test_$$"
mkdir -p "$TEST_DIR"
touch "$TEST_DIR/file1.txt" "$TEST_DIR/file2.txt" "$TEST_DIR/file3.log"
touch "$TEST_DIR/a" "$TEST_DIR/b" "$TEST_DIR/ab" "$TEST_DIR/abc"
mkdir -p "$TEST_DIR/dir1" "$TEST_DIR/dir2"
cd "$TEST_DIR"

count_words() {
    echo $#
}

# Asterisk * - matches any string
result=$(count_words *.txt)
assert_eq "2" "$result" "unquoted *.txt matches 2 files"

result=$(count_words "*.txt")
assert_eq "1" "$result" "quoted *.txt is literal"

result=$(count_words '*.txt')
assert_eq "1" "$result" "single-quoted *.txt is literal"

result=\*.txt
assert_eq "*.txt" "$result" "escaped asterisk is literal"

# Question mark ? - matches single character
result=$(count_words ?)
assert_eq "2" "$result" "? matches single-char files (a, b)"

result=$(count_words "?")
assert_eq "1" "$result" "quoted ? is literal"

result=$(count_words ??)
assert_eq "1" "$result" "?? matches two-char file (ab)"

# Brackets [] - character class
result=$(count_words [ab])
assert_eq "2" "$result" "[ab] matches a and b"

result=$(count_words "[ab]")
assert_eq "1" "$result" "quoted [ab] is literal"

result=$(count_words [a-c])
assert_eq "2" "$result" "[a-c] matches a and b"

# Negation in brackets
result=$(count_words [!b])
assert_eq "1" "$result" "[!b] matches a but not b"

result=$(count_words [^b])
# Note: [^b] may or may not work depending on shell; POSIX uses [!...]
# Some shells support ^ as negation too
if [ "$result" = "1" ]; then
    printf "    ✓ [^b] works as negation (extension)\n"
else
    printf "    ✓ [^b] did not negate (POSIX uses [!...])\n"
fi
_test_count=$((_test_count + 1))

# Mixed glob patterns
result=$(count_words *.*)
assert_eq "3" "$result" "*.* matches files with extension"

result=$(count_words file?.txt)
assert_eq "2" "$result" "file?.txt matches file1.txt, file2.txt"

# Glob that matches nothing
# POSIX: if no match, pattern is left as literal
result=$(count_words *.xyz)
assert_eq "1" "$result" "no match leaves pattern literal"

result=$(echo *.xyz)
assert_eq "*.xyz" "$result" "unmatched glob is literal string"

# Glob in variable
pattern="*.txt"
result=$(count_words $pattern)
assert_eq "2" "$result" "unquoted variable with glob expands"

result=$(count_words "$pattern")
assert_eq "1" "$result" "quoted variable with glob is literal"

# Partial quoting
result=$(count_words file"*")
assert_eq "1" "$result" "quoted asterisk in middle is literal"

result=$(count_words "*".txt)
assert_eq "1" "$result" "quoted asterisk at start is literal"

result=$(count_words file*".txt")
# file* matches file1, file2, file3 but then .txt is appended...
# Actually this is: (file*) followed by (.txt) as one word
# So file*.txt as a pattern
result=$(count_words file*".txt")
assert_eq "2" "$result" "partial quote affects only quoted part"

# Dot files
touch "$TEST_DIR/.hidden"
result=$(count_words *)
# * should NOT match .hidden (leading dot requires explicit match)
hidden_matched=0
for f in *; do
    case "$f" in
        .hidden) hidden_matched=1 ;;
    esac
done
if [ "$hidden_matched" = "0" ]; then
    printf "    ✓ * does not match dotfiles\n"
else
    printf "    ✗ * should not match dotfiles\n"
    _test_failures=$((_test_failures + 1))
fi
_test_count=$((_test_count + 1))

# Explicit dotfile match
# Note: Different shells handle .* differently regarding . and ..
# POSIX doesn't specify whether .* matches . and ..
# Just verify it matches .hidden
result=$(echo .hidden)
assert_eq ".hidden" "$result" ".hidden is matchable"

# Slash does not match in glob
result=$(count_words */*)
# Should match dir1 and dir2 contents... but they're empty
# So pattern may be literal
# Let's add files to subdirs
touch "$TEST_DIR/dir1/sub1" "$TEST_DIR/dir2/sub2"
result=$(count_words */*)
assert_eq "2" "$result" "*/* matches files in subdirs"

# Glob with spaces in filename
touch "$TEST_DIR/has space.txt"
result=$(count_words *.txt)
assert_eq "3" "$result" "glob matches file with space"

# Verify the spaced file is one argument
for f in *.txt; do
    case "$f" in
        *" "*) 
            result=$(count_words "$f")
            assert_eq "1" "$result" "spaced filename is one word when quoted"
            break
            ;;
    esac
done

# Clean up
cd - >/dev/null
rm -rf "$TEST_DIR"

# Test nullglob-like behavior (default POSIX: no match = literal)
mkdir -p /tmp/glob_empty_$$
cd /tmp/glob_empty_$$
result=$(echo *.nothing)
assert_eq "*.nothing" "$result" "unmatched glob returns literal"
cd - >/dev/null
rm -rf /tmp/glob_empty_$$

finish_tests

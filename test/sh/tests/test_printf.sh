#!/bin/sh
# Test printf builtin

set -e

# Test basic string printing
test "$(printf "hello")" = "hello" || exit 1
test "$(printf "hello world")" = "hello world" || exit 1

# Test %s format
test "$(printf "%s" "test")" = "test" || exit 1
test "$(printf "%s %s" "hello" "world")" = "hello world" || exit 1

# Test %d format
test "$(printf "%d" 42)" = "42" || exit 1
test "$(printf "%d" -42)" = "-42" || exit 1

# Test %x format
test "$(printf "%x" 255)" = "ff" || exit 1
test "$(printf "%X" 255)" = "FF" || exit 1

# Test %o format
test "$(printf "%o" 8)" = "10" || exit 1

# Test %% (literal percent)
test "$(printf "%%")" = "%" || exit 1
test "$(printf "100%%")" = "100%" || exit 1

# Test width specifier
test "$(printf "%5s" "hi")" = "   hi" || exit 1
test "$(printf "%5d" 42)" = "   42" || exit 1

# Test left justification
test "$(printf "%-5s" "hi")" = "hi   " || exit 1

# Test precision
test "$(printf "%.3s" "hello")" = "hel" || exit 1

# Test %b with escape sequences
test "$(printf "%b" "hello\nworld")" = "hello
world" || exit 1

# Test escape sequences in format string
test "$(printf "hello\nworld")" = "hello
world" || exit 1

# Test multiple arguments with format reuse
test "$(printf "%s\n" "a" "b" "c")" = "a
b
c" || exit 1

# Test %c format
test "$(printf "%c" "A")" = "A" || exit 1

echo "All printf tests passed!"
exit 0

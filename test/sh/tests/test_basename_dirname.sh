#!/bin/sh
# Test basename and dirname builtins

set -e

# Test basename
test "$(basename /usr/bin/sort)" = "sort" || exit 1
test "$(basename /usr/bin/)" = "bin" || exit 1
test "$(basename stdio.h .h)" = "stdio" || exit 1
test "$(basename /home/user/file.txt .txt)" = "file" || exit 1
test "$(basename file.txt)" = "file.txt" || exit 1
test "$(basename /)" = "/" || exit 1
test "$(basename //)" = "/" || exit 1
test "$(basename "")" = "." || exit 1

# Test dirname
test "$(dirname /usr/bin/sort)" = "/usr/bin" || exit 1
test "$(dirname /usr/bin/)" = "/usr" || exit 1
test "$(dirname stdio.h)" = "." || exit 1
test "$(dirname /)" = "/" || exit 1
test "$(dirname //)" = "/" || exit 1
test "$(dirname ///)" = "/" || exit 1
test "$(dirname "")" = "." || exit 1
test "$(dirname a/b/c)" = "a/b" || exit 1
test "$(dirname a/b)" = "a" || exit 1
test "$(dirname a)" = "." || exit 1

echo "All basename and dirname tests passed!"
exit 0

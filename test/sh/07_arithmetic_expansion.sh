#!/bin/sh
# Test: Quoting and Arithmetic Expansion
# Verify quoting correctly controls arithmetic expansion behavior

. "$(dirname "$0")/../test_helpers.sh"

printf "  Testing quoting with arithmetic expansion...\n"

# Single quotes prevent arithmetic expansion
assert_eq '$((1+1))' '$((1+1))' "single quotes prevent arithmetic expansion"
assert_eq '$((2*3))' '$((2*3))' "single quotes prevent multiplication"

# Double quotes allow arithmetic expansion
assert_eq "2" "$((1+1))" "double quotes allow addition"
assert_eq "6" "$((2*3))" "double quotes allow multiplication"
assert_eq "5" "$((10/2))" "double quotes allow division"
assert_eq "1" "$((10%3))" "double quotes allow modulo"
assert_eq "3" "$((10-7))" "double quotes allow subtraction"

# Unquoted arithmetic expansion
result=$((5+5))
assert_eq "10" "$result" "unquoted arithmetic works"

# Arithmetic with variables
a=5
b=3
assert_eq "8" "$((a+b))" "arithmetic with variables (no dollar)"
assert_eq "8" "$(($a+$b))" "arithmetic with dollar variables"
assert_eq "15" "$((a*b))" "multiplication with variables"

# Nested arithmetic
assert_eq "10" "$((2 * (3 + 2)))" "nested parentheses in arithmetic"

# Comparison operators (return 1 for true, 0 for false)
assert_eq "1" "$((5 > 3))" "greater than comparison"
assert_eq "0" "$((3 > 5))" "greater than false"
assert_eq "1" "$((5 >= 5))" "greater or equal"
assert_eq "1" "$((3 < 5))" "less than"
assert_eq "1" "$((5 == 5))" "equality"
assert_eq "1" "$((5 != 3))" "inequality"

# Logical operators
assert_eq "1" "$((1 && 1))" "logical and true"
assert_eq "0" "$((1 && 0))" "logical and false"
assert_eq "1" "$((1 || 0))" "logical or true"
assert_eq "0" "$((0 || 0))" "logical or false"
assert_eq "0" "$((!1))" "logical not"
assert_eq "1" "$((!0))" "logical not of zero"

# Bitwise operators
assert_eq "2" "$((3 & 6))" "bitwise and"
assert_eq "7" "$((3 | 4))" "bitwise or"
assert_eq "5" "$((3 ^ 6))" "bitwise xor"
assert_eq "8" "$((2 << 2))" "left shift"
assert_eq "2" "$((8 >> 2))" "right shift"

# Negative numbers
assert_eq "-5" "$((-5))" "negative number"
assert_eq "-2" "$((3 + -5))" "addition with negative"
assert_eq "8" "$((-3 - -11))" "subtraction of negative"

# Octal and hex (if supported)
# POSIX requires these
assert_eq "8" "$((010))" "octal literal"
assert_eq "255" "$((0xff))" "hex literal lowercase"
assert_eq "255" "$((0xFF))" "hex literal uppercase"

# Ternary operator (if supported by shell)
result=$((5 > 3 ? 10 : 20))
assert_eq "10" "$result" "ternary operator true case"
result=$((3 > 5 ? 10 : 20))
assert_eq "20" "$result" "ternary operator false case"

# Assignment within arithmetic
x=0
result=$((x = 5))
assert_eq "5" "$result" "assignment returns value"
assert_eq "5" "$x" "assignment modifies variable"

# Compound assignment
x=10
result=$((x += 5))
assert_eq "15" "$result" "compound addition"
assert_eq "15" "$x" "compound modifies variable"

x=10
result=$((x -= 3))
assert_eq "7" "$result" "compound subtraction"

x=4
result=$((x *= 3))
assert_eq "12" "$result" "compound multiplication"

# Pre/post increment/decrement (common extension, may not be in all POSIX shells)
# dash doesn't support ++x, but does support x=x+1
x=5
result=$((x=x+1))
assert_eq "6" "$result" "increment via assignment"
assert_eq "6" "$x" "increment modifies var"

# Whitespace in arithmetic
assert_eq "10" "$((  5  +  5  ))" "whitespace allowed in arithmetic"
assert_eq "15" "$((5+5+5))" "no whitespace also works"

# Arithmetic in different contexts
result="The sum is $((2+2))"
assert_eq "The sum is 4" "$result" "arithmetic in string"

result="prefix$((1+1))suffix"
assert_eq "prefix2suffix" "$result" "arithmetic adjacent to text"

finish_tests

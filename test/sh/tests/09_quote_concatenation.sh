#!/bin/sh
# Test: Quote Concatenation and Mixing
# Verify that adjacent quoted strings combine correctly

. "$(dirname "$0")/../test_helpers.sh"

printf "  Testing quote concatenation and mixing...\n"

# Adjacent single-quoted strings
result='hello'"world"
assert_eq "helloworld" "$result" "single+double quoted adjacent"

result='hello''world'
assert_eq "helloworld" "$result" "two single-quoted adjacent"

result="hello""world"
assert_eq "helloworld" "$result" "two double-quoted adjacent"

# Adjacent quoted and unquoted
result='hello'world
assert_eq "helloworld" "$result" "single-quoted + unquoted"

result=hello'world'
assert_eq "helloworld" "$result" "unquoted + single-quoted"

result="hello"world
assert_eq "helloworld" "$result" "double-quoted + unquoted"

result=hello"world"
assert_eq "helloworld" "$result" "unquoted + double-quoted"

# Including single quote in string (must leave single quotes)
result="it's"
assert_eq "it's" "$result" "single quote inside double quotes"

result='it'"'"'s'
assert_eq "it's" "$result" "single quote via concatenation"

result=it\'s
assert_eq "it's" "$result" "single quote via backslash"

# Including double quote in string
result='say "hello"'
assert_eq 'say "hello"' "$result" "double quote inside single quotes"

result="say \"hello\""
assert_eq 'say "hello"' "$result" "double quote escaped in double quotes"

result=say\ \"hello\"
assert_eq 'say "hello"' "$result" "double quote via backslash"

# Mixing quoting styles in one value
var="EXPANDED"
result='literal $var '"$var"' more literal'
assert_eq 'literal $var EXPANDED more literal' "$result" "mixed quoting with expansion"

# Complex concatenation
a='part1'
b="part2"
c=part3
result="$a""$b""$c"
assert_eq "part1part2part3" "$result" "concatenated variables"

result=${a}${b}${c}
assert_eq "part1part2part3" "$result" "braced variable concatenation"

# Quotes forming a single argument
count_words() {
    echo $#
}
result=$(count_words 'hello '"world"' there')
assert_eq "1" "$result" "concatenated quotes form single word"

result=$(count_words "hello "'world'" there")
assert_eq "1" "$result" "alternating quotes form single word"

# Empty strings in concatenation
result='hello'""'world'
assert_eq "helloworld" "$result" "empty string in middle"

result=""'hello'""
assert_eq "hello" "$result" "empty strings at ends"

# Newline in concatenated string
result='line1
'"line2"
expected="line1
line2"
assert_eq "$expected" "$result" "newline across quote boundary"

# Tab in concatenated string
result='col1	'"col2"
assert_eq "col1	col2" "$result" "tab across quote boundary"

# Escapes across boundaries
result='\'"n"
assert_eq '\n' "$result" "backslash-n across boundary (literal)"

# Dollar sign across boundaries
var="test"
result='$'"var"
assert_eq '$var' "$result" "dollar literal + unquoted text"

result='$'var
assert_eq '$var' "$result" "single-quoted dollar + unquoted var name"

# But this expands:
result="\$"var
assert_eq '$var' "$result" "escaped dollar + unquoted var name"

# Complex path-like strings
result='/path/to/'"$HOME"'/subdir'
expected="/path/to/$HOME/subdir"
assert_eq "$expected" "$result" "path with embedded variable"

# Arguments with mixed quoting
test_mixed() {
    printf '%s\n' "$1"
}
result=$(test_mixed 'say "hello, world"')
assert_eq 'say "hello, world"' "$result" "complex mixed argument"

# Three-part single quote inclusion
# To include ' in single quotes: end quote, escaped quote, start quote
result='can'"'"'t'
assert_eq "can't" "$result" "three-part single quote technique"

# Empty quoted strings are still arguments
result=$(count_words '' "" hello)
assert_eq "3" "$result" "empty quotes are still arguments"

# But unquoted nothing is nothing
result=$(count_words hello)
assert_eq "1" "$result" "baseline single arg"

finish_tests

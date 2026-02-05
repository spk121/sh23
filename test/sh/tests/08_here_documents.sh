#!/bin/sh
# Test: Quoting and Here-Documents (POSIX 2.7.4)
# Verify quoting correctly controls here-document behavior

. "$(dirname "$0")/../test_helpers.sh"

printf "  Testing quoting with here-documents...\n"

# Basic here-document (with expansion)
var="EXPANDED"
result=$(cat <<EOF
hello $var world
EOF
)
assert_eq "hello EXPANDED world" "$result" "here-doc expands variables"

# Command substitution in here-doc
result=$(cat <<EOF
value: $(echo computed)
EOF
)
assert_eq "value: computed" "$result" "here-doc expands command substitution"

# Arithmetic in here-doc
result=$(cat <<EOF
sum: $((2+2))
EOF
)
assert_eq "sum: 4" "$result" "here-doc expands arithmetic"

# Quoted delimiter prevents ALL expansion
var="SHOULD_NOT_EXPAND"
result=$(cat <<'EOF'
hello $var world
$(echo cmd)
$((1+1))
EOF
)
expected='hello $var world
$(echo cmd)
$((1+1))'
assert_eq "$expected" "$result" "single-quoted delimiter prevents expansion"

# Double-quoted delimiter also prevents expansion
result=$(cat <<"EOF"
hello $var world
EOF
)
assert_eq 'hello $var world' "$result" "double-quoted delimiter prevents expansion"

# Partially quoted delimiter prevents expansion
result=$(cat <<EO"F"
hello $var world
EOF
)
assert_eq 'hello $var world' "$result" "partially quoted delimiter prevents expansion"

# Backslash-quoted delimiter prevents expansion
result=$(cat <<\EOF
hello $var world
EOF
)
assert_eq 'hello $var world' "$result" "backslash-quoted delimiter prevents expansion"

# Tab stripping with <<- (tabs before delimiter and content)
result=$(cat <<-EOF
    indented line
    EOF
)
assert_eq "indented line" "$result" "<<- strips leading tabs"

# <<- with quoted delimiter
result=$(cat <<-'EOF'
    $var not expanded
    EOF
)
assert_eq '$var not expanded' "$result" "<<- with quoted delimiter"

# Special characters preserved in here-doc
result=$(cat <<EOF
pipe: |
amp: &
semi: ;
lt: <
gt: >
EOF
)
expected='pipe: |
amp: &
semi: ;
lt: <
gt: >'
assert_eq "$expected" "$result" "special chars preserved in here-doc"

# Backslash in here-doc (unquoted delimiter)
# Backslash escapes: $ ` \ newline
result=$(cat <<EOF
dollar: \$var
backtick: \`echo\`
backslash: \\
newline-escape: hello\
world
EOF
)
expected='dollar: $var
backtick: `echo`
backslash: \
newline-escape: helloworld'
assert_eq "$expected" "$result" "backslash escapes in unquoted here-doc"

# Backslash in here-doc (quoted delimiter) - literal
result=$(cat <<'EOF'
dollar: \$var
backslash: \\
EOF
)
expected='dollar: \$var
backslash: \\'
assert_eq "$expected" "$result" "backslash literal in quoted here-doc"

# Empty here-document
result=$(cat <<EOF
EOF
)
assert_eq "" "$result" "empty here-doc"

# Multiple here-documents in sequence
result=$(cat <<EOF1; cat <<EOF2
first
EOF1
second
EOF2
)
expected="first
second"
assert_eq "$expected" "$result" "multiple here-docs in sequence"

# Here-doc as input to command with arguments
result=$(tr 'a-z' 'A-Z' <<EOF
hello world
EOF
)
assert_eq "HELLO WORLD" "$result" "here-doc as command input"

# Nested quotes inside here-doc content
result=$(cat <<EOF
He said "hello"
It's working
EOF
)
expected="He said \"hello\"
It's working"
assert_eq "$expected" "$result" "quotes inside here-doc content"

# Variable assignment from here-doc
var=$(cat <<EOF
assigned value
EOF
)
assert_eq "assigned value" "$var" "variable from here-doc"

finish_tests

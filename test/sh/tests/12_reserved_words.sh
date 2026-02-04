#!/bin/sh
# Test: Quoting Prevents Reserved Word Recognition
# POSIX 2.2: "prevent reserved words from being recognized as such"

. "$(dirname "$0")/../test_helpers.sh"

printf "  Testing quoting prevents reserved word recognition...\n"

# When a reserved word is quoted, it's treated as a regular word (command name)
# We can test this by creating commands/scripts with these names and calling them quoted

# Create test bin directory
TEST_BIN="/tmp/reserved_word_test_$$"
mkdir -p "$TEST_BIN"
export PATH="$TEST_BIN:$PATH"

# Helper to create a test command
make_cmd() {
    name="$1"
    output="$2"
    printf '#!/bin/sh\necho "%s"\n' "$output" > "$TEST_BIN/$name"
    chmod +x "$TEST_BIN/$name"
}

# Create test commands named after reserved words
make_cmd "if" "if_command_ran"
make_cmd "then" "then_command_ran"
make_cmd "else" "else_command_ran"
make_cmd "elif" "elif_command_ran"
make_cmd "fi" "fi_command_ran"
make_cmd "for" "for_command_ran"
make_cmd "do" "do_command_ran"
make_cmd "done" "done_command_ran"
make_cmd "while" "while_command_ran"
make_cmd "until" "until_command_ran"
make_cmd "case" "case_command_ran"
make_cmd "esac" "esac_command_ran"
make_cmd "in" "in_command_ran"

# Testing quoted reserved words as command names
# Double quotes
result=$("if")
assert_eq "if_command_ran" "$result" "double-quoted 'if' runs as command"

result=$("then")
assert_eq "then_command_ran" "$result" "double-quoted 'then' runs as command"

result=$("else")
assert_eq "else_command_ran" "$result" "double-quoted 'else' runs as command"

result=$("fi")
assert_eq "fi_command_ran" "$result" "double-quoted 'fi' runs as command"

result=$("for")
assert_eq "for_command_ran" "$result" "double-quoted 'for' runs as command"

result=$("while")
assert_eq "while_command_ran" "$result" "double-quoted 'while' runs as command"

result=$("case")
assert_eq "case_command_ran" "$result" "double-quoted 'case' runs as command"

result=$("in")
assert_eq "in_command_ran" "$result" "double-quoted 'in' runs as command"

# Single quotes
result=$('if')
assert_eq "if_command_ran" "$result" "single-quoted 'if' runs as command"

result=$('for')
assert_eq "for_command_ran" "$result" "single-quoted 'for' runs as command"

# Backslash-escaped
result=$(\if)
assert_eq "if_command_ran" "$result" "escaped 'if' runs as command"

result=$(\for)
assert_eq "for_command_ran" "$result" "escaped 'for' runs as command"

# Partial quoting
result=$(i"f")
assert_eq "if_command_ran" "$result" "partially quoted 'if' runs as command"

result=$(fo"r")
assert_eq "for_command_ran" "$result" "partially quoted 'for' runs as command"

# Variable containing reserved word
word="if"
result=$("$word")
assert_eq "if_command_ran" "$result" "variable expanding to 'if' runs as command"

word="for"
result=$("$word")
assert_eq "for_command_ran" "$result" "variable expanding to 'for' runs as command"

# Test that unquoted reserved words still work as reserved words
result=$(
    if true; then
        echo "if_is_reserved"
    fi
)
assert_eq "if_is_reserved" "$result" "unquoted 'if' works as reserved word"

result=$(
    for i in a b c; do
        printf '%s' "$i"
    done
)
assert_eq "abc" "$result" "unquoted 'for' works as reserved word"

result=$(
    x=0
    while [ "$x" -lt 3 ]; do
        printf '%s' "$x"
        x=$((x + 1))
    done
)
assert_eq "012" "$result" "unquoted 'while' works as reserved word"

result=$(
    case "test" in
        test) echo "case_matched" ;;
        *) echo "no_match" ;;
    esac
)
assert_eq "case_matched" "$result" "unquoted 'case' works as reserved word"

# Clean up
rm -rf "$TEST_BIN"

finish_tests

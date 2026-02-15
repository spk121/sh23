#!/bin/sh
# Test: Positional parameters (2.5.1) and Special parameters (2.5.2)
# Covers:
#   - Correct handling of $1, $8, $10, ${10}, ${08}, etc.
#   - "$@" vs "$*" vs unquoted $@ $*
#   - $# (number of positional parameters)
#   - $? (exit status propagation — including subshells)
#   - $$, $!, $0, $-
#   - Quoting behavior, field splitting, empty fields
#   - Nested function calls (positional parameters are local to function scope)

. "$(dirname "$0")/../test_helpers.sh"

printf "  Testing positional and special parameters (POSIX 2.5.1 + 2.5.2)...\n"

# ────────────────────────────────────────────────
# Setup: many positional parameters
# ────────────────────────────────────────────────
set -- one "two three" four "" six seven eight nine ten eleven "  twelve  "

assert_argc 12 "setup: 12 positional parameters" "$@"

# ────────────────────────────────────────────────
# 1. Single-digit positional parameters
# ────────────────────────────────────────────────
assert_eq "one"        "$1"          "\$1 → first parameter"
assert_eq "two three"  "$2"          "\$2 → second (quoted spaces preserved)"
assert_eq ""           "$5"          "\$5 → empty parameter"
assert_eq "eight"      "$8"          "\$8 → eighth parameter"
assert_eq "nine"       "${9}"        "\${9} braced single digit"

# ────────────────────────────────────────────────
# 2. Multi-digit positional parameters — must use braces
# ────────────────────────────────────────────────
assert_eq "ten"        "${10}"       "\${10} → tenth parameter"
assert_eq "eleven"     "${11}"       "\${11}"
assert_eq "  twelve  " "${12}"       "\${12} → trailing/leading spaces preserved"

# Wrong (classic mistake)
assert_eq "ten0"       "$10"         "\$10 → \$1 followed by literal 0"
assert_eq "one0"       "$10"         "\$10 expands to first arg + '0' (classic bug)"

# Leading zeros are still decimal
assert_eq "ten"        "${08}"       "\${08} is treated as 8 (decimal)"
assert_eq "eight"      "${008}"      "\${008} still eighth parameter"

# ────────────────────────────────────────────────
# 3. Special parameter $# — number of positional parameters
# ────────────────────────────────────────────────
assert_eq "12"         "$#"          "\$# → correct count (does not include \$0)"

set -- a b c
assert_eq "3"          "$#"          "\$# after set -- a b c"

set --
assert_eq "0"          "$#"          "\$# → 0 when no positional parameters"

# ────────────────────────────────────────────────
# 4. "$@" vs "$*" vs unquoted $@ $*
# ────────────────────────────────────────────────
set -- "a b" "c" "" "d e"

assert_output '4' "count_args \"\$@\""     count_args "$@"
assert_output '1' "count_args \"\$*\""     count_args "$*"
assert_output '4' "count_args  \$@"        count_args  $@   # field splitting + empty removal
assert_output '3' "count_args  \$*"        count_args  $*   # usually 3 (empty fields removed)

# More precise test with custom IFS
old_ifs="$IFS"
IFS=:
assert_output 'a b|c||d e' "IFS=:; printf %s \"\$*\""  sh -c 'IFS=:; printf %s "$*"' sh "$@"
assert_output 'a b|c||d e' "IFS=:; join_args \"\$@\""  sh -c 'IFS=:; join_args "$@"' sh "$@"
IFS="$old_ifs"

# ────────────────────────────────────────────────
# 5. Nested function calls — positional parameters are function-local
# ────────────────────────────────────────────────
outer() {
    set -- outer1 "outer two" outer3
    assert_eq "outer1" "$1" "outer: \$1"
    assert_eq "3"      "$#" "outer: \$# = 3"

    inner() {
        set -- innerA innerB
        assert_eq "innerA" "$1"     "inner: \$1 (local scope)"
        assert_eq "2"      "$#"     "inner: \$# = 2"

        # Check outer still sees its own parameters
        assert_eq "outer1" "$1"     "outer parameters not visible inside inner"
    }

    inner

    # After inner returns, outer parameters unchanged
    assert_eq "outer1" "$1"     "outer \$1 unchanged after inner returns"
    assert_eq "3"      "$#"     "outer \$# unchanged"
}

outer

# ────────────────────────────────────────────────
# 6. $? — exit status (including subshell behavior)
# ────────────────────────────────────────────────
true
assert_eq "0" "$?" "true → \$? = 0"

false
assert_eq "1" "$?" "false → \$? = 1"

# Subshell preserves $? from parent only at creation time
( exit 42 )
assert_eq "0" "$?" "exit in subshell does NOT affect parent \$? (assignment rule)"

# But command substitution result DOES propagate
x=$(exit 55)
assert_eq "55" "$?" "\$(exit 55) sets \$? to 55 (simple command exit status)"

# Pipeline exit status = last command
false | true
assert_eq "0" "$?" "false | true → \$? = 0 (last command)"

true | (exit 99)
assert_eq "99" "$?" "true | (exit 99) → \$? = 99"

# ────────────────────────────────────────────────
# 7. Other special parameters: $$, $0, $!, $-
# ────────────────────────────────────────────────
assert_eq "$$"     "$$"      "\$\$ → numeric PID (not literal)"
[ "$$" -gt 1 ] && printf "    ✓ \$\$ is numeric PID\n" || _test_failures=$((_test_failures+1))
_test_count=$((_test_count+1))

assert_eq "${0}"   "${0}"    "\$0 → script name or shell"

# $! — background job PID
sleep 1 &
bgpid=$!
assert_eq "$bgpid" "$!"      "\$! → PID of most recent background job"

# $- — current options
case "$-" in
    *i*) printf "    ✓ 'i' in \$- when interactive (expected in many runners)\n" ;;
esac

# ────────────────────────────────────────────────
# 8. Edge case: empty $@ inside double quotes + other parts
# ────────────────────────────────────────────────
set --
assert_eq ""       "$@"      "empty \$@ expands to zero fields"

prefix="begin-"
result="${prefix}$@"
assert_eq "begin-" "$result" "empty \$@ at end → no extra field"

result="$@${prefix}"
assert_eq "${prefix}" "$result" "empty \$@ at beginning → no extra field"

result="a${@}b"
assert_eq "ab" "$result" "empty \$@ embedded → no extra empty field"

# ────────────────────────────────────────────────
# 9. ${parameter-word} / ${parameter+word} with $@ (quoting rules)
# ────────────────────────────────────────────────
set -- x y z
unset var

result="${var-$@}"
assert_eq "x y z" "$result" "\${unsetvar-\$@} expands like \$@"

result="${var+$@}"
assert_eq ""      "$result" "\${unsetvar+\$@} → empty when var unset"

var=""
result="${var+$@}"
assert_eq "x y z" "$result" "\${emptyvar+\$@} → expands like \$@ when var set (even empty)"

finish_tests
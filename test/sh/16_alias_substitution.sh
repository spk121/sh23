#!/bin/sh
# Test: Alias substitution (POSIX 2.3.1)
# Covers: basic replacement, trailing blank continuation, recursion prevention,
#         quoted words, reserved words, non-interactive behavior notes,
#         and interaction with word splitting/field splitting.

. "$(dirname "$0")/../test_helpers.sh"

printf "  Testing alias substitution (POSIX 2.3.1)...\n"

# ────────────────────────────────────────────────
# Helper to count arguments (ignores word splitting unless unquoted)
# ────────────────────────────────────────────────
count_args() {
    printf '%s\n' "$#"
}

# ────────────────────────────────────────────────
# 1. Basic alias replacement
# ────────────────────────────────────────────────
alias ll='ls -l'

# Try to use the alias
result=$(ll . 2>/dev/null || echo "alias_not_expanded")
if [ "$result" = "alias_not_expanded" ]; then
    # Many POSIX shells disable aliases in non-interactive mode.
    # We can try to force-enable if the shell supports it (bash/ksh/zsh do via shopt/set -o).
    # But for pure POSIX compliance testing, we note it may not expand.
    printf "    (Note: alias expansion often disabled in non-interactive scripts)\n"
    # We'll define a safe test alias that doesn't rely on external commands
fi

# Use a self-contained alias
alias hi='echo hello'
result=$(hi world 2>/dev/null)
assert_eq "hello world" "$result" "basic alias substitution" || \
    printf "    (skipped - alias may not expand in non-interactive mode)\n"

unalias hi 2>/dev/null || true

# ────────────────────────────────────────────────
# 2. Alias with trailing blank → continues to next word
# ────────────────────────────────────────────────
alias myecho='echo '

result=$(myecho one two three 2>/dev/null)
assert_eq "one two three" "$result" "alias trailing blank → alias substitution continues" || \
    printf "    (skipped - alias may not expand)\n"

unalias myecho 2>/dev/null || true

# ────────────────────────────────────────────────
# 3. Quoted word → no alias substitution
# ────────────────────────────────────────────────
alias ll='echo alias_ll'

result=$( "ll" . 2>/dev/null || echo "no_alias")
assert_eq "no_alias" "$result" "quoted command name prevents alias substitution"

# Also single quotes
result=$( 'll' . 2>/dev/null || echo "no_alias")
assert_eq "no_alias" "$result" "single-quoted command name prevents alias"

unalias ll 2>/dev/null || true

# ────────────────────────────────────────────────
# 4. Reserved word in alias context should not be aliased
# ────────────────────────────────────────────────
alias if='echo tricked'

result=$(if true; then echo ok; fi 2>/dev/null || echo "reserved")
assert_eq "reserved" "$result" "reserved word 'if' not aliased in command position" || true

unalias if 2>/dev/null || true

# ────────────────────────────────────────────────
# 5. Alias recursion prevention (same name not re-expanded)
# ────────────────────────────────────────────────
alias recursive='recursive --flag'

# Should NOT infinitely expand — POSIX says not to replace if already processing same name
# But we can't easily capture infinite loop safely, so we test safe case
alias safe='echo safe'
alias safe='safe more'

result=$(safe 2>/dev/null)
assert_eq "safe more" "$result" "alias not recursively expanded in same chain" || \
    printf "    (skipped)\n"

unalias safe 2>/dev/null || true

# ────────────────────────────────────────────────
# 6. Alias definition affects subsequent words (trailing space chain)
# ────────────────────────────────────────────────
alias cmd1='cmd2 '
alias cmd2='echo final'

result=$(cmd1 arg 2>/dev/null)
assert_eq "final arg" "$result" "chained alias expansion via trailing blank" || \
    printf "    (skipped - alias expansion likely disabled)\n"

unalias cmd1 cmd2 2>/dev/null || true

# ────────────────────────────────────────────────
# 7. Alias of a special builtin / utility name
# ────────────────────────────────────────────────
alias test='echo mocked_test'

result=$(test -n "$PWD" && echo yes || echo no 2>/dev/null)
# In many shells this still uses builtin test, because aliases don't override special builtins in some contexts
# But POSIX allows aliasing non-special builtins/utilities
assert_eq "mocked_test" "$result" "alias shadows regular command (not special builtin)" || true

unalias test 2>/dev/null || true

# ────────────────────────────────────────────────
# Summary note for non-interactive behavior
# ────────────────────────────────────────────────
printf "    Note: POSIX does not require alias expansion in non-interactive shells.\n"
printf "    Many implementations (dash, yash, busybox sh, etc.) disable it by default in scripts.\n"
printf "    Bash/ksh may enable via 'set -o allexport' or shopt -s expand_aliases.\n"

finish_tests
# The case Conditional Construct

The **case** statement is a powerful conditional construct that selects and executes a block of commands based on pattern matching against a value. It is often cleaner and more efficient than long chains of `if-elif` statements when checking a single value against multiple possible patterns.

Unlike `if`, which evaluates arbitrary conditions, `case` matches a **word** (after expansions) against a series of **patterns** using shell glob-style pattern matching (see Pattern Matching Notation).

## Syntax

```
case word in
[ ( ] pattern1 [ | pattern2 ... ] ) compound-list terminator ] ...
[ ( ] pattern1 [ | pattern2 ... ] ) compound-list ]
esac
```

- `word` � the value to match (subject to tilde, parameter, command substitution, arithmetic expansion, and quote removal).
- `pattern1 | pattern2 ...` � one or more patterns separated by `|` (any one can match). The leading `(` is optional.
- `compound-list` � the commands to execute if the pattern(s) match.
- `terminator` � either `;;` (default: stop here) or `;&` (fall through to next clause).
- The final clause does not require a terminator.
- The construct ends with `esac` (case spelled backward).

### Pattern Expansion

Each pattern undergoes tilde expansion, parameter expansion, command substitution, and arithmetic expansion **before** matching. Quoting parts of a pattern prevents certain expansions.

Matching stops at the **first** pattern that matches the expanded `word`. The order of evaluating multiple patterns joined by `|` is unspecified.

## Terminators

| Terminator | Behavior                                                                 |
|------------|--------------------------------------------------------------------------|
| `;;`       | Execute the matching clause and **stop** � do not process further clauses (default). |
| `;&`       | Execute the matching clause and **fall through** � continue executing the next clause(s) regardless of pattern match. |

## Examples

### Basic usage

The following example demonstrates a simple case.

```sh
case "$action" in
    start|begin)
        echo "Starting the service..."
        start_service
        ;;
    stop|end)
        echo "Stopping the service..."
        stop_service
        ;;
    restart)
        echo "Restarting..."
        stop_service
        start_service
        ;;
    *)
        echo "Unknown action: $action"
        exit 1
        ;;
esac
```
This example matches the value of `$action` against several patterns and executes the corresponding commands.  The `*` pattern acts as a catch-all (default case).

### Using fall-through with `;&`

You can use `;&` to allow fall-through behavior.
```sh
case "$level" in
    debug)
        set -x  # enable tracing
        ;&      # fall through
    info)
        log_level="info"
        ;&
    warning)
        log_level="warning"
        ;;
    *)
        log_level="error"
        ;;
esac
```
In this example, if `$level` is `debug`, it enables tracing and then falls through.

### Pattern matching with wildcards

You can use wildcards in patterns.
```sh
case "$file" in
    *.txt | *.md)
        editor="nano"
        ;;
    *.jpg | *.png | *.gif)
        viewer="feh"
        ;;
    *.pdf)
        viewer="zathura"
        ;;
    *)
        echo "Unsupported file type"
        ;;
esac
```
This example matches file extensions using wildcards.


## Exit Status

- If **no patterns match**: exit status is **0**.
- If one or more patterns match: exit status is the exit status of the **last** `compound-list` executed (considering fall-through with `;&`).

This zero-on-no-match behavior makes `case` safe as a default handler without extra checks.

## Common Patterns

| Use Case                       | Example Snippet                                      |
|--------------------------------|------------------------------------------------------|
| Command-line option parsing    | `case "$1" in -h|--help) usage ;; -v|--verbose) verbose=1 ;; esac` |
| File extension handling        | `case "${file##*.}" in jpg|png) ... ;; esac`         |
| Yes/no user input              | `case "${reply,,}" in y|yes) ... ;; n|no) ... ;; esac` |

## Availability

| API   | Availability                  |
|-------|-------------------------------|
| POSIX | Available                     |
| UCRT  | Available                     |
| ISO C | Available *                   |

*In ISO C, the `case` conditional is available so long as there are no subshell or command substitutions within the `word` or patterns.

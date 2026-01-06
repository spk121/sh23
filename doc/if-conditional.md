# The if Conditional Construct

The **if** statement is the primary way to make decisions in shell scripts. It executes a sequence of commands (the condition), checks their exit status, and then runs different blocks of code depending on whether the condition succeeded (exit status 0) or failed (non-zero).

It supports `elif` for additional conditions and an optional `else` for a default case.

## Syntax

```
if compound-list
then
compound-list
[elif compound-list
then
compound-list] ...
[else
compound-list]
fi
```

- `if compound-list` — the condition: one or more commands whose **combined exit status** determines the branch taken.
- `then compound-list` — executed if the preceding condition succeeds (exit status 0).
- `elif ... then ...` — zero or more additional conditions (checked only if previous ones failed).
- `else compound-list` — optional final block executed if all previous conditions failed.
- The construct ends with `fi` (if spelled backward).

The shell evaluates conditions **in order** and executes only the **first** matching `then` block (or the `else` if none match).

## How Conditions Work

Shell conditions are simply commands:
- Exit status **0** = true/success → take the `then` branch.
- Exit status **non-zero** = false/failure → continue to next `elif` or `else`.

Common condition commands:
- `test` or `[ ... ]` for file checks, string/number comparisons
- Regular commands like `grep -q`, `command`, etc.

**Note**: Double square brackets `[[ ... ]]` are a non-POSIX extension (e.g., in bash). For portability, use single square brackets `[ ... ]` or the `test` command.

## Examples

### Simple if

In the following example, we check if the string "ERROR" appears in the file `logfile.txt`.
```sh
if grep -q "ERROR" logfile.txt
then
echo "Errors found in log"
mail -s "Log errors" admin@example.com < logfile.txt
fi
```

### if-elif-else

Here, we check the value of a variable and take different actions based on its content.
```sh
status="warning"
if [ "$status" = "ok" ]
then
  echo "All systems operational"
elif [ "$status" = "warning" ]
then
  echo "Warning: Check system status"
else
  echo "Error: System failure"
fi
```

### Checking a Command's Success or Failure

A command's exit status can be directly used in an `if` statement.
```sh
if ping -c 1 example.com > /dev/null 2>&1
then
  echo "Host is reachable"
else
  echo "Host is unreachable"
fi
```
In the above example, if the `ping` command succeeds, we print that the host is reachable; otherwise, we indicate it is unreachable.

### Nested `if` Statements
You can nest `if` statements for more complex logic.
```sh
if [ -f "config.txt" ]
then
  if grep -q "ENABLE_FEATURE" config.txt
  then
    echo "Feature is enabled"
  else
    echo "Feature is disabled"
  fi
else
  echo "Configuration file not found"
fi
```
In the preceding example, we first check if `config.txt` exists. If it does, we then check if it contains the string "ENABLE_FEATURE" and print the appropriate message.

## Exit Status
The exit status of an `if` statement is the exit status of the last command executed in the taken branch. If no branch is taken (which can happen if there are no conditions), the exit status is that of the last command before the `fi`.

**Important Note**: The exit status of the `if` statement is not used as the final exit status of the script unless explicitly handled., but it **is** available via `$?` immediately after the `if` construct inside the subsequent `then`, `elif`, or `else` blocks.

```sh
if grep "pattern" file.txt
then
 echo "grep exit status was: $?"
fi
```


## Common Patterns

| Use Case                          | Example Snippet                                      |
|-----------------------------------|------------------------------------------------------|
| File existence and type           | `if [ -f "$file" ]; then ... fi`                     |
| Numeric comparison                | `if [ "$count" -gt 10 ]; then ... fi`               |
| String equality/non-empty         | `if [ -n "$var" ]; then ... fi`                      |
| Command success with fallback     | `if cmd1; then :; else cmd2; fi`                     |
| Multiple conditions (AND/OR)      | `if [ "$a" -eq 1 ] && [ "$b" = "yes" ]; then ... fi` |

## Availability

| API   | Availability                  |
|-------|-------------------------------|
| POSIX | Available                     |
| UCRT  | Available                     |
| ISO C | Available              |


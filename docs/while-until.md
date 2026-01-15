# The `while` and `until` Loop Constructs

The **while** and **until** statements provide the primary looping mechanisms in POSIX shell scripts. They repeatedly execute a block of commands (the *loop body*) as long as a condition command list evaluates to a particular exit status.
- `while` loops continue **while the condition succeeds** (exit status 0).
- `until` loops continue **until the condition succeeds** (i.e., they loop while the condition fails).
Both constructs use the same general structure and terminate with `done`.

## Syntax

```
while compound-list
do
    compound-list
done

until compound-list
do
    compound-list
done
```

- `while compound-list` — the condition: one or more commands whose combined exit status determines whether the loop continues.
- `until compound-list` — similar to `while` but the loop conditions while the condition fails.
- `do compound-list` — the loop body, executed each time the condition is met (for `while`) or not met (for `until`).
- The construct ends with `done`.

The shell evaluates the condition **before each iteration**. If the condition’s exit status matches the loop type (0 for while, non‑zero for until), the body executes; otherwise, the loop terminates.

## How Loop Conditions Work

Loop conditions are simply commands:
- Exit status **0** = true/success.
- Exit status **non-zero** = false/failure.

For a `while` loop:
- `0` → run the body again
- non‑zero → stop looping

For an `until` loop:
- `0` → stop looping
- non‑zero → run the body again

Common condition commands include:
- File tests (`[ -f file ]`)
- String or numeric comparisons
- Commands that naturally succeed/fail (`grep -q,` `command`, etc.)

## Examples

### Simple `while` Loop

This loop prints numbers from 1 to 5.
```sh
i=1
while [ "$i" -le 5 ]
do
  echo "$i"
  i=$((i + 1))
done
```

### Reading a File Line-by-Line

A common pattern is using `while` to process input streams.
```sh
while IFS= read -r line
do
  echo "Line: $line"
done < logfile.txt
```

### `until` Loop Example

This loop waits until a host becomes reachable.
```sh
until ping -c 1 example.com > /dev/null 2>&1
do
  echo "Waiting for host..."
  sleep 2
done
echo "Host is reachable"
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

### Nested Loops
Loops can be nested for more complex logic.
```sh
while read -r dir
do
  until [ -d "$dir" ]
  do
    echo "Directory $dir not found"
    break
  done
  echo "Processing $dir"
done < dirs.txt
```

## Exit Status
The exit status of a `while` or `until` loop is the **exit status of the last command executed**:
- If the loop body ran at least once, the exit status is that of the final command in the body.
- If the loop body never ran, the exit status is that of the condition command list.
This allows loops to be chained or tested in larger constructs.

## Common Patterns

| Use Case                          | Example Snippet                           |
|-----------------------------------|-------------------------------------------|
| Counting or iteration             | `while [ "$i" -lt 10 ]; do ... done`      |
| Waiting for a condition           | `until [ -f "$file" ]; do ... done`       |
| Reading input streams             | `while read line; do ... done < file`     |
| Polling a command until success   | `until cmd; do sleep 1; done`             |
| Looping while a command succeeds  | `while grep -q pattern file; do ... done` |

## Availability

| API   | Availability                  |
|-------|-------------------------------|
| POSIX | Available                     |
| UCRT  | Available                     |
| ISO C | Available                     |


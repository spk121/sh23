# Lists

A **list** is a sequence of one or more pipelines combined with control operators to control execution flow and order.

There are two main types of constructs:

- **AND-OR lists** — pipelines connected by `&&` (AND) and `||` (OR)
- **Lists** — AND-OR lists separated by `;` (sequential), `&` (asynchronous/background), or newlines

These constructs allow conditional execution, chaining commands based on success/failure, and running commands sequentially or in the background.

## AND-OR Lists

An **AND-OR list** is a sequence of one or more pipelines separated by `&&` or `||`.

The operators `&&` and `||` have **equal precedence** and are evaluated with **left associativity**.

### Examples

Both of these commands output only `bar`:

```sh
false && echo foo || echo bar
true || echo foo && echo bar
```

- In the first: `false` fails → skip `echo foo` → `echo bar` runs.
- In the second: `true` succeeds → skip `echo foo` → `echo bar` runs.

### 3.3 AND Lists (`&&`)

Format:
```
command1 [ && command2 ] ...
```

Behavior:
- Execute `command1`.
- If it succeeds (exit status 0), execute `command2`, and continue.
- Stop at the first failure (non-zero exit status).

**Exit Status**: Exit status of the last command executed (or 0 if none failed and all ran).

**Use case**: Run subsequent commands only if previous ones succeed.

Example:

```sh
mkdir build && cd build && cmake ..
```
This creates a `build` directory, changes into it, and runs `cmake ..` only if the previous commands succeeded.

### 3.4 OR Lists (`||`)

Format:
```
command1 [ || command2 ] ...
```

Behavior:
- Execute `command1`.
- If it fails (non-zero exit status), execute `command2`, and continue.
- Stop at the first success (exit status 0).

**Exit Status**: Exit status of the last command executed.

**Use case**: Provide fallbacks or run alternatives on failure.

Example:

```sh
gcc source.c || clang source.c
```
This tries to compile `source.c` with `gcc`, and if it fails, it tries with `clang`.


## Full Lists (Sequential and Asynchronous)

A **list** combines one or more AND-OR lists using:

- `;` or newline — execute sequentially (wait for completion)
- `&` — execute asynchronously (in background)

### Sequential Lists

Format:

```
aolist1 [; aolist2] ...
```

Behavior:

- Each AND-OR list runs one after another.
- The shell waits for each to finish before starting the next.
- If job control is enabled, these form part of a foreground job.

**Exit Status**: Exit status of the last pipeline executed.

Example:
```sh
clean && build ; test
```
This runs `clean` and `build` sequentially, then runs `test` after both complete.


### Asynchronous Lists (Background)

Terminate an AND-OR list with `&` to run it in the background:

```
aolist &
```

Behavior:
- The AND-OR list runs in the background.
- The shell does not wait for it to finish before accepting new commands.
- The process ID is stored (accessible via `$!`).
- If job control is enabled (`set -m`), it becomes a job-controlled background job with a job number.
- If interactive, the shell typically prints: `[%d] %d` (job number and PID).

**Exit Status of the list itself**: Always 0 (the `&` command succeeds immediately).

The actual exit status of the background command can be retrieved later with `wait`.

**Standard input**: When job control is disabled, background commands initially have stdin connected to `/dev/null` unless redirected.

## Summary of Control Operators

| Operator | Meaning                          | Execution Order                     | Wait?     |
|----------|----------------------------------|-------------------------------------|-----------|
| `&&`     | AND — run next only if success   | Left to right, short-circuit on failure | Yes      |
| `||`     | OR — run next only if failure    | Left to right, short-circuit on success | Yes      |
| `;`      | Sequential                       | Run next regardless                 | Yes      |
| `&`      | Background (asynchronous)        | Start in background                 | No       |
| newline  | Same as `;`                      | Run next                            | Yes      |

## Availability

| API   | Availability                  |
|-------|-------------------------------|
| POSIX | Available                     |
| UCRT  | Available                     |
| ISO C | Partial *                     |

*In ISO C, AND, OR and Sequential lists are available. Asynchronous execution is not supported. The success or failure of a command is based on the value returned by the `system` command used to execute the command.



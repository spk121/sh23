# Functions

Functions in POSIX shell provide a way to group commands under a user‑defined name. Calling the function name executes its associated compound command with a fresh set of positional parameters, allowing scripts to encapsulate reusable logic cleanly and predictably.

A function behaves much like a simple command: it can receive arguments, it has an exit status, and it participates in the shell’s normal command search and execution rules.

## Syntax

```
name() compound-command [io-redirect ...]
```

- `name()` — the function’s identifier. It must be a valid shell name and must not conflict with any special built‑in utility.
- `compound-command` — the body of the function, typically a group of commands enclosed in `{ ... }` or another compound form.
- `io-redirect` — optional redirections applied each time the function is invoked.
When the function is *defined*, no expansions are performed on the body or redirections. All expansions occur at *call time*, just as if the commands had been written inline.

## How Function Calls Work

Invoking a function is as simple as writing its name:
```sh
myfunc arg1 arg2
```

When a function is called:
- The function’s arguments become the **positional parameters** `$1`, `$2`, …
- The special parameter `#` reflects the number of arguments.
- `$0` remains unchanged (it continues to refer to the script or shell).
- After the function finishes, all positional parameters are restored to their previous values.

A function ends when:
- The last command in its body finishes, or
- A `return` built‑in is executed, which immediately exits the function and resumes execution after the call site.

## Examples

### A Simple Function

```sh
greet() {
  echo "Hello, $1"
}

greet "Alice"
```

### Functions with Multiple Commands

```sh
backup() {
  cp "$1" "$1.bak"
  echo "Backup created: $1.bak"
}
```

### Using Positional Parameters

```sh
sum() {
  total=0
  for n in "$@"
  do
    total=$((total + n))
  done
  echo "$total"
}

sum 5 7 3

```

### Returning Early

```sh
check_file() {
  [ -f "$1" ] || return 1
  echo "File exists"
}

if check_file config.txt
then
  echo "Ready to proceed"
else
  echo "Missing configuration"
fi
```

###  Functions with Redirections

Redirections attached to the function definition apply each time the function runs:
```sh
logmsg() {
  echo "$(date): $*"
} >> logfile.txt
```

### Exit Status

Functions have two relevant exit statuses:
- **Function definition**: \
  `0` if the function was successfully declared; non‑zero otherwise.
- **Function invocation**: \
  The exit status of the **last command executed** inside the function (or the status passed to return).

This allows functions to be used naturally in conditionals:
```sh
if do_something
then
  echo "Success"
else
  echo "Failure"
fi
```

## Common Patterns

| Use Case                          | Example Snippet                          |
|-----------------------------------|------------------------------------------|
| Encapsulating reusable logic      | `cleanup() { rm -f "$tmp"; }`            |
| Argument processing               | `myfunc() { echo "First: $1"; }`         |
| Returning success/failure         | `return 0` / `return 1`                  |
| Using all arguments               | `myfunc() { printf "%s\n" "$@"; }`       |
| Functions as predicates           | `if is_ready; then ... fi`               |

## Availability

| API   | Availability                  |
|-------|-------------------------------|
| POSIX | Available                     |
| UCRT  | Available                     |
| ISO C | Available                     |
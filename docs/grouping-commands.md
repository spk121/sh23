# Grouping Commands

**Grouping commands** allow you to combine multiple commands into a single unit. This is useful for:

- Running a sequence of commands in a subshell (isolated environment)
- Applying redirections or control operators to an entire group
- Treating several commands as one for pipelines, lists, or conditionals

There are two forms of grouping: **subshell grouping** with `()` and **brace grouping** with `{}`.

## Subshell Grouping: `( ...)`

Format:
```
( compound-list )
```


### Behavior

- The commands inside `( ... )` execute in a **subshell environment** (a separate child process).
- Changes to variables, built-ins that affect the shell environment (e.g., `cd`, `set -o`, trap changes), or functions do **not** persist after the group finishes.
- Redirections applied to the group affect all commands inside.

### Examples

Run multiple commands with combined output redirected:

```sh
( echo "Starting..."; cd build; make build; echo "Done." ) > build.log 2>&1
```

This runs the three commands in a subshell, redirecting both standard output and standard error to `build.log`. Note that the `cd build` does not affect the current shell's directory.

Running a pipeline inside a conditional:

```sh
if ( grep "error" logfile ); then
  echo "Errors found."
fi
```
This checks if "error" appears in `logfile` using a subshell for the `grep` command.

## Important Note on Arithmetic Evaluation

Because of the possible confustion between subshell and arithmetic evaluation, if you intend grouping and your command starts with `((`, **always separate the two opening parentheses with whitespace**:

```sh
( ( cmd1 && cmd2 ) )
```

This ensures the shell interprets it as a subshell grouping rather than an arithmetic evaluation.

## Brace Grouping: `{ ... ; }`

Format:

```
{ compound-list ; }
```

### Behavior
- The commands inside `{ ... ; }` execute in the **current shell environment** (no subshell).
- Changes to variables, built-ins, or functions persist after the group finishes.
- A semicolon `;` or newline is required before the closing brace `}`.

### Examples

Group commands without creating a subshell:

```sh
{ cd /path/to/dir; ls -l; }
```
This changes the directory and lists files in the current shell.

Apply redirection to multiple commands:

```sh
{ echo "Log Start"; date; echo "Log End"; } >> logfile.txt
```

This appends the output of all three commands to `logfile.txt`.

## Summary
- Use `( ... )` for subshell grouping when you want isolation.
- Use `{ ... ; }` for brace grouping when you want to affect the current shell.
- Always separate `((` from `(` with whitespace when using subshells to avoid confusion with arithmetic evaluation.

## Availability

| API   | Availability                  |
|-------|-------------------------------|
| POSIX | Available                     |
| UCRT  | Available                     |
| ISO C | Limited *               |

*In ISO C mode, subshell grouping is not supported due to the lack of process control capabilities. Brace grouping is available as it does not require a separate process, though redirection behavior is not available.
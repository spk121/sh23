# Pipelines

A **pipeline** is a sequence of one or more commands separated by the control operator `|`.

In a pipeline, the shell connects the standard output of each command (except the last) to the standard input of the next command. It does this by creating a pipe: the write end of the pipe becomes the standard output of the earlier command, and the read end becomes the standard input of the next command.

Pipelines allow you to chain commands together, where the output of one command becomes the input to the next. This is useful for processing data step by step, such as filtering output or combining tools.

## Syntax

The format of a pipeline is:

```
[!] command1 [ | command2 ... ]
```

- The pipeline can optionally start with the reserved word `!`, which inverts the exit status of the pipeline (success becomes failure and vice versa).
- If the pipeline begins with `!` and *command1* is a subshell (starting with `(`), you must separate the `!` from the `(` with one or more blank characters (spaces or tabs). \[TBR\]The behavior if they are not separated is unspecified.

**Note**: The standard input, standard output, or both of a command are assigned by the pipeline *before* any redirections (using `<` or `>`) that are part of the individual commands. See [Redirection](redirection.md) for more information.

## Examples

A simple pipeline to list files and search for a pattern:

```shell
ls -l | grep ".txt"
```

Here, the output of `ls -l` is piped to the input of `grep`, which filters for lines containing ".txt".

To invert the exit status (e.g., succeed only if the pipeline fails):

```shell
! ls -l | grep "nonexistent"
```

## Behavior

If the pipeline is not running in the background (see [Asynchronous Lists](asynchronous-lists.md) and [Job Control](job-control.md)), the shell waits for the last command in the pipeline to complete. It may also wait for all commands to complete.

## Exit Status

The exit status of a pipeline depends on:

- Whether the `pipefail` option is enabled (set using the `set` builtin: `set -o pipefail` to enable, `set +o pipefail` to disable).
- Whether the pipeline begins with the `!` reserved word.

The shell determines the `pipefail` setting at the start of pipeline execution.

The following table summarizes the exit status:

| pipefail Enabled | Begins with ! | Exit Status |
|------------------|---------------|-------------|
| no               | no            | The exit status of the last (rightmost) command in the pipeline. |
| no               | yes           | 0 if the last command returned non-zero; otherwise 1. (Logical NOT of the last command's status.) |
| yes              | no            | 0 if **all** commands returned 0; otherwise, the exit status of the last command that returned non-zero. |
| yes              | yes           | 0 if **any** command returned non-zero; otherwise 1. (Logical NOT of whether all succeeded.) |

**Important**: When `pipefail` is enabled, a failure in any command (not just the last) can affect the pipeline's exit status. This is useful in scripts to detect errors early in a chain.

## Availability

| API   | Availability                  |
|-------|-------------------------------|
| POSIX | Available                     |
| UCRT  | Available                     |
| ISO C | Limited *                     |

*In ISO C, pipelines will error unless they consist of a single command with no redirections. You may still use the `!` reserved word with this single command.


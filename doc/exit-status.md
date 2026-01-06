# Exit Status and Errors

Every command executed by the shell produces an **exit status**—a small integer that indicates whether it succeeded or failed. Shell scripts rely heavily on these values to make decisions, control flow, and detect errors. Understanding how exit statuses work, and how the shell reacts to different kinds of errors, is essential for writing reliable scripts.

This section explains:
- How the shell behaves when errors occur
- How exit statuses are assigned
- What special cases (like signals or missing commands) look like

Not all errors are treated the same way. Depending on the type of error and whether the shell is interactive or non‑interactive, the shell may:
- Exit immediately
- Continue running
- Print a diagnostic message

The table below summarizes the required behavior:

| Error | Non-Interactive Shell | Interactive Shell | Shell Diagnostic Message Required |
|-------|-----------------------|-------------------|-----------------------------------|
| Shell language syntax error | shall exit |  shall not exit | yes |
| Special built-in utility error | shall exit | shall not exit | no |
| Other utility (not a specialbuilt-in) error | shall not exit | shall not exit | no |
| Redirection error with special built-in utilities | shall exit | shall not exit |  yes |
| Redirection error with compound commands | shall not exit | shall not exit | yes |
| Redirection error with function execution |  shall not exit |  shall not exit |  yes |
| Redirection error with other utilities (not special built-ins) | shall not exit |  shall not exit | yes |
| Variable assignment error |  shall exit | shall not exit | yes |
| Expansion error |  shall exit | shall not exit |  yes |
| Command not found | may exit | shall not exit | yes |
| Unrecoverable read error when reading commands | shall exit | shall exit | yes |

Notes and Clarifications
- **Special built‑ins behave differently.** \
  If a special built‑in (like export, readonly, eval, etc.) fails directly, a non‑interactive shell must exit. If the same utility is invoked through command, the shell does not exit.
- **Diagnostic messages.** \
  A “shell diagnostic message” is distinct from messages printed by utilities themselves. Special built‑ins may print their own messages, but these are not considered shell diagnostics.
- **Unrecoverable read errors.** \
  If the shell cannot continue reading commands (for example, due to an I/O failure), it must stop executing further commands.
  The only exception is when reading from a file via the . (dot) built‑in; in that case, the error is treated like a special built‑in failure.
- **Expansion errors.** \
  Errors during word expansion (e.g., invalid parameter expansion syntax) cause non‑interactive shells to exit.
  Some implementations may detect these earlier and treat them as syntax errors.
- **Subshell behavior.** \
  If an error that “shall exit” occurs inside a subshell, only the subshell exits; the parent shell continues.
- **Interactive shells do not continue the faulty command.** \
  Even when they are required not to exit, they must stop processing the command in which the error occurred.

## Exit Status for Commands

Every command returns an exit status that the shell uses to determine success or failure. This value is available in the special parameter `$?`.

The rules for determining a command’s exit status are:
- **Command Not Found** \
  If the shell cannot locate the command:
  Exit status is 127.
- **Command Found but Not Executable** \
  If the command exists but cannot be executed (e.g., missing execute permission):
  Exit status is 126.
- **Command Terminated by a Signal** \
  If a command is killed by a signal the shell assigns an exit status greater than 128.
  The exact value is implementation‑defined and may exceed 255.
- **Normal Command Completion** \
  If the command runs and exits normally the exit status is the value returned by the command (modulo 256 for C programs).
  This corresponds to the `WEXITSTATUS` macro applied to the `wait()` result.

Examples:
```
$ nonexistent_command
sh: 1: nonexistent_command: not found
$ echo $?
127

$ ./not_executable_script.sh
sh: 1: ./not_executable_script.sh: Permission denied
$ echo $?
126

$ kill -9 $$
$ echo $?
137  # Example exit status when terminated by SIGKILL (9)

$ false
$ echo $?
1

$ true
$ echo $?
0
```

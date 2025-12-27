# Notes on the shell execution environment

| Field                     | Top-Level Initialization       | Subshell Creation                                                          |
|---------------------------|--------------------------------|----------------------------------------------------------------------------|
| **Subshell Tracking**     |                                |                                                                            |
| `parent`                  | `NULL`                         | Pointer to parent executor                                                 |
| `is_subshell`             | `false`                        | `true`                                                                     |
| `is_interactive`          | If stdin is a tty              | Copy from parent                                                           |
| `is_login_shell`          | if argv[0] == `-` prefix       | `false` (subshells are never login shells)                                 |
| **Working Directory**     |                                |                                                                            |
| `working_directory`       | `getcwd()` / `_getcwd()`       | Deep copy from parent (string_dup)                                         |
| **File Permissions**      |                                |                                                                            |
| `file_creation_mask`      | `umask(0);`                    | Copy from parent                                                           |
| `file_size_limit`         | `getrlimit(RLIMIT_FSIZE)`      | Copy from parent                                                           |
| **Signal Handling**       |                                |                                                                            |
| `traps`                   | Empty trap_store_t             | Deep copy from parent                                                      |
| `original_signals`        | Capture signal dispositions    | Copy from parent (or re-capture in child)                                  |
| **Vars & Params**         |                                |                                                                            |
| `variables`               | Import `environ`, set defaults | Deep copy from parent                                                      |
| `positional_params`       | Command line args or empty     | Deep copy from parent                                                      |
| `argument_count`          | Count of positional params     | Copy from parent                                                           |
| **Special Params**        |                                |                                                                            |
| `last_exit_status_set`    | `true`                         | Copy from parent                                                           |
| `last_exit_status`        | `0`                            | Copy from parent                                                           |
| `last_background_pid_set` | `false`                        | Copy from parent                                                           |
| `last_background_pid`     | `0` or undefined               | Copy from parent                                                           |
| `shell_pid_set`           | `true`                         | `true`                                                                     |
| `shell_pid`               | `getpid()`                     | `getpid()` (NEW - different from parent!)                                  |
| `last_argument_set`       | `false`                        | Copy from parent                                                           |
| `last_argument`           | `NULL` or empty                | Deep copy from parent if set                                               |
| `shell_name`              | `argv[0]`                      | Deep copy from parent                                                      |
| **Functions**             |                                |                                                                            |
| `functions`               | Empty func_store_t             | Deep copy from parent                                                      |
| **Shell Options**         |                                |                                                                            |
| `shell_flags_set`         | `true`                         | `true`                                                                     |
| `flag_allexport`          | `false` (unless `-a`)          | Copy from parent                                                           |
| `flag_errexit`            | `false` (unless `-e`)          | Copy from parent                                                           |
| `flag_ignoreeof`          | `false`                        | Copy from parent                                                           |
| `flag_noclobber`          | `false` (unless `-C`)          | Copy from parent                                                           |
| `flag_noglob`             | `false` (unless `-f`)          | Copy from parent                                                           |
| `flag_noexec`             | `false` (unless `-n`)          | Copy from parent                                                           |
| `flag_nounset`            | `false` (unless `-u`)          | Copy from parent                                                           |
| `flag_pipefail`           | `false`                        | Copy from parent                                                           |
| `flag_verbose`            | `false` (unless `-v`)          | Copy from parent                                                           |
| `flag_vi`                 | `false`                        | Copy from parent                                                           |
| `flag_xtrace`             | `false` (unless `-x`)          | Copy from parent                                                           |
| **Job Control**           |                                |                                                                            |
| `jobs`                    | Empty `job_table_t`            | Empty `job_table_t` (subshells start with no jobs)                         |
| `job_control_enabled`     | `true` if interactive          | `false` (POSIX: job control disabled in subshells)                         |
| `pgid`                    | `getpgrp()` or set new pgid    | `getpid()` (subshell gets own process group)                               |
| **File Descriptors**      |                                |                                                                            |
| `open_fds`                | stdin(0), stdout(1), stderr(2) | Inherit parent's FDs, but create new tracker                               |
| `next_fd`                 | `3` (first available)          | Copy from parent or reset to `3`                                           |
| **Aliases**               |                                |                                                                            |
| `aliases`                 | Empty or init files            | **Shallow copy** (aliases are shared) or deep copy (implementation choice) |
| **Error Reporting**       |                                |                                                                            |
| `error_msg`               | empty string                   | `NULL` or empty string (fresh error state)                                 |

## Key Notes:

1. **`shell_pid` is special**: It's the ONLY field that must be
   re-queried in the subshell rather than copied, because the subshell
   has a new PID.

2. **Job control in subshells**: POSIX requires job control to be
   disabled in subshells, so `job_control_enabled` becomes `false` and
   `jobs` starts empty.

3. **Deep copy vs. shallow copy**:
   - **Deep copy** (must be separate): `variables`, `functions`,
     `traps`, `positional_params`, all strings
   - **Shallow copy** (can share): `aliases` is debatable - some
     shells share aliases, others don't
   - **Fresh/empty**: `jobs`, `error_msg`

4. **Process groups**: In a subshell, you typically want to create a
   new process group for job control isolation.

5. **File descriptors**: Inherit the open FDs from parent (they're
   process-level), but track them separately so the subshell can
   manage its own redirections.


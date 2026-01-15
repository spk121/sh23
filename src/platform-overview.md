# Platform-Specific Features

This whole shell project originated with my silly project to create a userland that is strictly compliant to ISO C. So depending on its compile-time configuration, it may be either built presuming a POSIX system, a UCRT system, or an ISO C system. POSIX is the most powerful and should operate in accordance with the POSIX standard. The UCRT C library is far more limited, and several features having to do with redirection, running in the background, and signals will not function. The ISO C build is even more severely limited, with no redirection, no pipelines, no running in the background, and no directory support of any kind. In this section, those limitations will be described.

## POSIX Build Deviations from the Standard

The goal of the POSIX build is to be as compliant as possible with the POSIX standard. The POSIX build uses standard POSIX APIs including `fork()`, `execve()`, `pipe()`, `dup2()`, and signal handling. This provides full shell functionality including:

- **Process Control**: Full fork/exec support for command execution
- **Pipelines**: Multi-command pipelines with proper exit status handling
- **Redirections**: All standard redirections including `<`, `>`, `>>`, `<>`, `>&`, heredocs (`<<`), and here-strings
- **Background Jobs**: True background execution with job control
- **Subshells**: Isolated execution environments via `fork()`
- **Signal Handling**: Trap command and signal disposition control
- **File Descriptor Management**: Arbitrary file descriptor manipulation

However, there are some deviations from the standard that are necessary due to implementation limitations or design decisions. Here are some of the known deviations:

- **Arithmetic Expansion**: The `$((...))` syntax is not yet fully implemented
- **Parameter Expansion**: Some advanced parameter expansion forms (e.g., `${var:offset:length}`) are not yet implemented
- **Job Control**: While background jobs (`&`) work, interactive job control commands (`fg`, `bg`, `jobs`) are not yet fully implemented
- **History**: Command history and the `fc` builtin are not implemented
- **Aliases**: Alias expansion is partially implemented but may have edge cases
- **For Loops**: Newly implemented, may have edge cases not yet discovered
- **Case Statements**: Newly implemented, may have edge cases not yet discovered

## UCRT Build Deviations from the Standard

The UCRT (Universal C Runtime) build targets Windows systems and uses the Microsoft C runtime library. This build has significant limitations compared to POSIX due to the lack of `fork()` and limited process control capabilities.

### Supported Features

- **Simple Command Execution**: External commands execute via `_spawnvpe()`
- **Simple Redirections**: Single-command redirections work for basic cases (e.g., `ls >tmp.txt`)
- **Variable Expansion**: Full parameter and command substitution
- **Control Structures**: `if`, `while`, `until`, `for`, and `case` statements work
- **Functions**: Shell function definitions and invocation
- **Builtins**: All shell built-in commands function normally

### Limitations and Non-Working Features

#### Pipelines
Multi-command pipelines (`|`) are **not supported**. The shell will return an error when attempting to execute a pipeline. This is because pipelines require the ability to create multiple child processes and connect their input/output streams, which requires `fork()` and `pipe()` system calls not available in UCRT.

#### Complex Redirections
While simple single-command redirections like `command >file` work, **compound command redirections do not work**. Examples that will fail:
- `{ hostname; pwd; } >tmp.txt` - Brace group with redirection
- `(cd /tmp; ls) >tmp.txt` - Subshell with redirection
- `if true; then echo hello; fi >tmp.txt` - Control structure with redirection

This is because compound commands would require forking to isolate the redirection's effect, which is not possible in UCRT.

#### Background Execution
Background execution (`&`) has **limited support**:
- **Works**: Simple external commands can be backgrounded (e.g., `sleep 10 &`)
- **Doesn't Work**: Shell builtins, functions, pipelines, and compound commands cannot be backgrounded
- **Fallback Behavior**: When backgrounding unsupported constructs, the shell prints a warning and executes the command synchronously

Background jobs use `_spawnvpe()` with `_P_NOWAIT` mode, which provides limited job tracking. The job PID reported is actually a process handle, not a true PID.

#### Job Control
- No process groups or sessions
- Limited job status tracking via `_cwait()`
- Cannot suspend or resume jobs
- No terminal control

#### Signal Handling
Signal handling is limited to what UCRT provides:
- Only `SIGINT`, `SIGTERM`, `SIGABRT`, `SIGFPE`, `SIGILL`, and `SIGSEGV` are available
- No `SIGCHLD` notification for background job completion
- Trap command has limited signal support

#### File Descriptor Manipulation
File descriptor operations are limited:
- Uses `_dup2()` instead of `dup2()`
- File descriptor numbers above 2 may behave differently than on POSIX
- Close-on-exec behavior may differ

## ISO C Build Deviations from the Standard

The ISO C build is the most severely restricted, as it relies solely on functions defined in the ISO C standard (C99/C11). This build is primarily an academic exercise demonstrating shell implementation without any OS-specific APIs.

### Supported Features

- **Simple Command Execution**: Commands execute via `system()` call
- **Variable Expansion**: Parameter expansion and variable substitution
- **Control Structures**: `if`, `while`, `until`, `for`, and `case` statements
- **Functions**: Shell function definitions and invocation
- **Builtins**: Most shell built-in commands (except those requiring OS features)

### Non-Working Features

The following features are **completely unavailable** in ISO C mode:

#### Process Control
- **No fork/exec**: All commands run via `system()`
- **No process isolation**: Subshells and command groups run in the same process
- **No exit status distinction**: Can only detect success/failure, not specific exit codes
- **No background execution**: The `&` operator is ignored with a warning

#### Pipelines
- **No pipelines**: Multi-command pipelines (`|`) are not supported
- Attempting a pipeline will result in an error

#### Redirections
- **No file redirections**: Cannot redirect stdin, stdout, or stderr
- **No heredocs**: Cannot use `<<` or `<<<` syntax
- See workaround below for partial mitigation

#### File Descriptors
- **No file descriptor control**: Cannot manipulate file descriptors
- **No exec command**: Cannot modify current shell's I/O

#### Directory Operations
- **No cd command**: ISO C has no standard way to change directories
- **No pwd command**: ISO C has no standard way to query current directory
- Working directory is tracked internally but cannot be changed

#### Job Control
- **No background jobs**: Cannot run commands in background
- **No job control**: No job tracking or management

#### Signals
- **No signal handling**: Cannot trap signals beyond what `signal()` provides
- **No custom signal handlers**: Limited to default C signal handling

### ISO C Workarounds

The ISO C build does provide some workarounds that may make it more useful when running in that environment.

#### The Environment File

In ISO C mode, when the `MGSH_ENV_FILE` variable has been set, then, upon every call to execute a command by the system, the `mgsh` shell will generate a file with the filename provided by the `MGSH_ENV_FILE` variable. In this file, each line will have one exported variable in `<name>=<value>` format. This file is created just before the command execution and is deleted immediately after, regardless of whether the command execution succeeds or fails.

A command to be executed would have to be adapted to take advantage of this information, of course.

**Example usage:**
```sh
export MGSH_ENV_FILE=/tmp/mgsh_env.txt
export MYVAR="Hello, World!"
mgsh> ./my_command
```
In this example, before executing `./my_command`, the shell creates `/tmp/mgsh_env.txt` containing:
```
MYVAR=Hello, World!
```

#### The Redirection File

In ISO C mode, when the `MGSH_REDIR_FILE` variable has been set, then, upon every call to execute a command by the system in which redirections were given, the `mgsh` shell will generate a file with the filename provided by the `MGSH_REDIR_FILE` variable.  In this file, each line will have a description of a requested redirection, such as `>2`. For heredoc string redirection this redirection entry in the file may have multiple lines per entry. This file is created just before the command execution and is deleted immediately after, regardless of whether the command execution succeeds or fails.

A command to be executed would have to be adapted to take advantage of this information, since ISO C provides no way of doing redirections.

**Example usage:**
```sh
export MGSH_REDIR_FILE=/tmp/mgsh_redir.txt
mgsh> ls >output.txt 2>error.txt
```
In this example, before executing `ls`, the shell creates `/tmp/mgsh_redir.txt` containing:
```
>1 output.txt
>2 error.txt
```

These workarounds allow commands executed in ISO C mode to at least be aware of the shell's environment variables and requested redirections, even though the shell itself cannot implement these features directly.

## Build Configuration
The build configuration for the shell can be selected at compile time using the following options:
- `-DPLATFORM_POSIX`: Build with POSIX API support (default). This enables full shell functionality as described in the POSIX section. It should compile correctly on POSIX-compliant systems such as Linux, macOS, and BSD.
- `-DPLATFORM_UCRT`: Build with UCRT API support for Windows. This enables a limited shell functionality as described in the UCRT section. It should compile correctly on Windows systems using the Microsoft C runtime.
- `-DPLATFORM_ISO_C`: Build with ISO C standard library only. This enables the most limited shell functionality as described in the ISO C section. It should compile on any system with a standard C23 compiler.

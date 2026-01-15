# Job Control

When you're working in the shell, you're not limited to running just one command at a time and waiting for it to finish. **Job control** is a powerful feature that lets you stop (suspend) programs, run them in the background, and bring them back to the foreground when you need them. This is managed jointly by your terminal driver and the shell, and it's what makes multitasking in the shell possible.

Job control is enabled by default in interactive shells (when you're typing commands). Under the hood, this happens with `set -m`. In this section, we'll explore how job control works and how to use it effectively.

## The Basics: Foreground vs. Background

At any given time, your terminal has one **foreground job**—this is the program that's actively running and can read from your keyboard. Everything else runs in the **background**, which means it's executing but can't interact with your terminal input.

When the shell starts up with job control enabled, it makes sure it's in control of the terminal. It does this by:
1. Setting itself as the **controlling process** for the terminal session
2. Making its own process group the **foreground process group** (so it can read your input)

If the shell starts in the background (say, because another shell launched it as a background job), it has to do some extra work. It either:
- Stops itself temporarily (sends itself a SIGTTIN signal) until it can be brought to the foreground, or
- Tries to read from standard input (which triggers the stop automatically if it's not in the foreground)

Once it's properly in the foreground, the shell can then manage other jobs.

## What Counts as a Job?

The shell groups related commands into **jobs**. A job is created when you execute certain types of command lists. Here's when jobs are created:

### Background Jobs (Asynchronous Lists)

The most common way to create a background job is by ending a command with `&`:

```bash
long_running_command &
```

This creates a **background job** containing everything in that AND-OR list (commands connected by `&&` or `||`). The shell:
- Creates a new process group for this job
- Makes the first process in the pipeline the **process group leader**
- Assigns all other processes in the job to the same process group
- Gives you back a **job number** and **process ID** so you can reference it later

Example:
```bash
$ sleep 100 &
[1] 12345
```
Here, `[1]` is the job number and `12345` is the process ID.

### Foreground Jobs (Sequential Lists)

When you run commands sequentially (separated by `;` or newlines), they form a single **foreground job** until any asynchronous part:

```bash
command1 ; command2 ; command3
```

All three commands are part of the same foreground job. The job runs in the foreground until all sequential commands complete.

If you mix sequential and asynchronous commands:
```bash
command1 ; command2 ; command3 &
```

Commands 1 and 2 run as a foreground job, then command 3 becomes a separate background job once the first two finish.

### Multiple Background Jobs

If you have multiple `&` operators in a single line:
```bash
cmd1 & cmd2 & cmd3 &
```

The shell treats this as if you had typed three separate lines, creating three separate background jobs.

## Process Groups and Pipelines

Within a job, the shell keeps related processes together in **process groups**. This is crucial for signal management.

For a pipeline in a foreground job:
```bash
command1 | command2 | command3
```

All three commands are placed in the same process group (usually). This means:
- If you press Ctrl+C (SIGINT), all three processes receive the signal
- If you press Ctrl+Z (SIGTSTP), all three processes are suspended together

**Exception**: If the shell executes some commands in its own environment (built-ins) and others in subshells, the process groups might differ. This is an implementation detail you usually don't need to worry about.

## Controlling Jobs: fg, bg, and Signals

Once you have jobs running (or suspended), you can control them with utilities and signals:

### Bringing Jobs to the Foreground: `fg`

The `fg` utility brings a background or suspended job to the foreground:

```bash
$ sleep 100 &
[1] 12345
$ fg %1        # Bring job 1 to foreground
```

When this happens:
- The job number is removed from the background jobs list
- The process ID is removed from the known process IDs
- The shell sets this job's process group as the foreground process group for the terminal
- If the job was suspended, `fg` sends a SIGCONT signal to resume it
- If bringing back a suspended foreground job in an interactive shell, the terminal settings are restored to what they were when the job was suspended

### Running Jobs in the Background: `bg`

The `bg` utility continues a suspended job in the background:

```bash
$ sleep 100     # Running in foreground
^Z              # Press Ctrl+Z to suspend
[1]+  Stopped   sleep 100
$ bg %1         # Continue in background
[1]+ sleep 100 &
```

The `bg` utility sends a SIGCONT signal to the suspended job's process group, allowing it to continue execution without bringing it to the foreground.

## Suspending Jobs (Ctrl+Z and Stop Signals)

Jobs can be **suspended** (stopped) in several ways:

### Interactive Suspension: Ctrl+Z

When you press Ctrl+Z while a foreground job is running, the terminal sends a **SIGTSTP** signal to the foreground process group. This is a "catchable" stop signal that most programs respond to by suspending.

When this happens to a foreground job:
- The shell creates a **suspended job** from the stopped processes
- The job is assigned a job number (if it didn't have one already)
- An interactive shell writes a message like: `[1]+  Stopped   command`
- The shell saves the current terminal settings before returning to its input prompt
- The shell sets itself back as the foreground process group

What gets suspended depends on what was running:
- **Simple case**: If the stopped process was part of a single pipeline of simple commands, at minimum that entire pipeline becomes the suspended job.
- **Complex case**: If it's a more complex structure (like pipelines with compound commands), the exact suspended content is less predictable, but it always includes at least the pipeline containing the stopped process.

**Pro tip**: To ensure complex commands stay intact when suspended, start them in the background and immediately bring them to the foreground:
```bash
command1 && command2 | { command3 || command4; } & fg
```

### Other Stop Signals

Programs can also be stopped by:
- **SIGTTIN**: Sent when a background job tries to read from the terminal
- **SIGTTOU**: Sent when a background job tries to write to the terminal (if terminal output is restricted)
- **SIGSTOP**: An uncatchable stop signal (can't be ignored)

When a **background job** is stopped by any of these signals:
- The job becomes a suspended job
- An interactive shell writes a notification message

The timing of this message depends on the `set -b` option:
- If `set -b` is enabled: Message appears immediately (or just before the next prompt)
- If `set -b` is disabled: Message appears just before the next prompt

### Special Case: SIGSTOP and Built-ins

If a SIGSTOP signal stops the shell itself (while it's executing a built-in command), and it has stopped child processes when it receives SIGCONT, the shell creates a suspended job for those child processes.

## Job Completion and Termination

When a **background job completes** normally or is **terminated by a signal**, an interactive shell notifies you:

```bash
$ sleep 5 &
[1] 12345
$ # ... wait ...
[1]+  Done    sleep 5
```

Or if terminated:
```bash
$ sleep 100 &
[1] 12345
$ kill %1
[1]+  Terminated   sleep 100
```

Again, the timing depends on `set -b`:
- Enabled: Notification appears immediately after completion/termination
- Disabled: Notification appears just before the next prompt

Non-interactive shells may also write these messages, but the timing is less predictable:
- After the next foreground job terminates or is suspended
- Before parsing further input
- Before the shell exits

## Job Numbers and Process IDs

Each background job (whether suspended or running) has two identifiers:

1. **Job number**: A small integer like `[1]`, `[2]`, etc. Used with `%` notation in commands
2. **Process ID**: The actual PID of the process group leader

You can reference jobs in several ways:
- `%1` or `%2` – by job number
- `%%` or `%+` – the current job (most recently started or brought to foreground)
- `%-` – the previous job
- `%command` – by command name (e.g., `%sleep`)

Examples:
```bash
$ fg %1          # Bring job 1 to foreground
$ kill %2        # Send SIGTERM to job 2
$ bg %%          # Continue current job in background
```

## Practical Tips for Using Job Control

### Quick Workflow Examples

**Pause a long-running command to check something:**
```bash
$ find / -name "*.log"     # Started, but output is overwhelming
^Z                         # Suspend it
[1]+  Stopped   find / -name "*.log"
$ ls -la                   # Do something else
$ fg                       # Resume the find
```

**Run multiple commands concurrently:**
```bash
$ compile_project &        # Start compilation in background
[1] 12345
$ run_tests &              # Start tests in background
[2] 12346
$ jobs                     # Check status
[1]-  Running   compile_project &
[2]+  Running   run_tests &
```

**Accidentally started something in foreground:**
```bash
$ very_long_download      # Oops, forgot the &
^Z                        # Suspend it
[1]+  Stopped   very_long_download
$ bg                      # Continue in background
[1]+ very_long_download &
```

### Common Pitfalls

**Background jobs and terminal I/O:**
If a background job tries to read from or write to the terminal, it might be stopped:
```bash
$ read_input &            # Tries to read
[1] 12345
[1]+  Stopped (tty input)   read_input
```

Solution: Redirect input/output when backgrounding:
```bash
$ read_input < input.txt > output.txt &
```

**Lost suspended jobs:**
If you suspend a job and forget about it, the shell will remind you when you try to exit:
```bash
$ exit
There are stopped jobs.
```

Use `jobs` to see what's suspended, then either `fg` to bring them back, `bg` to continue them, or `kill` to terminate them.

### Job Control Settings

- `set -m`: Enable job control (default in interactive shells)
- `set +m`: Disable job control
- `set -b`: Enable immediate notification of background job status changes
- `set +b`: Disable immediate notification (wait until next prompt)

## Summary: The Job Control Lifecycle

Here's the typical lifecycle of a job:

1. **Creation**: Job is created as foreground or background
2. **Execution**:
   - Foreground jobs control the terminal and can read/write
   - Background jobs run without terminal access
3. **Suspension** (optional):
   - User presses Ctrl+Z, or
   - Job receives a stop signal (SIGTSTP, SIGTTIN, SIGTTOU, SIGSTOP)
   - Job becomes "suspended" with a job number
4. **Continuation** (if suspended):
   - `fg` brings to foreground (and sends SIGCONT)
   - `bg` continues in background (and sends SIGCONT)
5. **Completion**:
   - Job finishes normally, or
   - Job is terminated by a signal
   - Shell notifies you and removes job from jobs list

Job control makes the shell a powerful multitasking environment. With practice, you'll find yourself naturally suspending, backgrounding, and resuming jobs as part of your normal workflow.

## Availability: Job Control Across Different Builds

The shell can be built with different APIs, and each offers different levels of job control support. Here's what you can expect from each build:

### POSIX API Build (Full Job Control)

When built with the POSIX API, the shell provides **complete job control** as described throughout this document. All features are available:

- ✅ Background jobs with `&`
- ✅ Process groups for pipelines
- ✅ Job suspension with Ctrl+Z (SIGTSTP)
- ✅ Resuming jobs with `fg` and `bg`
- ✅ Job numbers and the `%` notation
- ✅ Signal propagation (Ctrl+C affects entire job)
- ✅ Terminal foreground/background switching
- ✅ Job status notifications
- ✅ Full pipeline support with I/O redirection
- ✅ Job control settings (`set -m`, `set -b`)

This is the standard, full-featured shell experience you'd expect on Unix-like systems.

### UCRT API Build (Limited Job Control)

When built with the Universal C Runtime (UCRT) API on Windows, the shell provides **basic job control**—you can run background jobs and track them, but interactive job management is limited. This build uses `_spawn` and `_cwait` functions instead of full POSIX process control.

**What works:**

- ✅ **Background jobs**: You can launch commands with `&` and they run asynchronously
  ```
  $ long_command &
  [1] 12345
  ```
  The shell tracks these jobs internally and gives you job numbers and process IDs.

- ✅ **Foreground jobs**: Sequential commands work normally
  ```
  $ command1 ; command2 ; command3
  ```
  The shell waits for each to complete before running the next.

- ✅ **Pipelines**: Commands can be piped together, both in foreground and background
  ```
  $ command1 | command2 &
  [1] 12345
  ```
  The shell uses `_pipe` and `_dup2` to connect processes.

- ✅ **Job listing**: The `jobs` command shows active background jobs with their status
  ```
  $ jobs
  [1]   Running   long_command &
  ```

- ✅ **Waiting for jobs**: You can wait for specific jobs to complete
  ```
  $ wait %1
  [1] Done
  ```

- ✅ **Completion notifications**: The shell tells you when background jobs finish
  ```
  [1]   Done   long_command
  ```

**What doesn't work:**

- ❌ **No job suspension**: Ctrl+Z doesn't suspend jobs—there's no SIGTSTP support
  - You can't pause a running foreground job
  - No "Stopped" job state exists

- ❌ **No `fg` or `bg` commands**: Once a job is running, you can't move it between foreground and background
  - Background jobs stay in the background
  - Foreground jobs must complete or be terminated
  - No way to resume suspended jobs (since suspension isn't supported)

- ❌ **No process groups**: Each process in a pipeline is independent
  - Ctrl+C only affects the shell itself, not the entire job
  - No coordinated signal handling across pipeline processes

- ❌ **No terminal control**: Background jobs aren't prevented from trying to read/write the terminal
  - They may interfere with your shell prompt
  - No SIGTTIN/SIGTTOU enforcement

- ❌ **Limited job termination**: You can't send signals to jobs
  - No `kill %1` support
  - Jobs must complete naturally or the entire shell must exit

**Practical implications:**

Think of this as a **batch-style job launcher** rather than an interactive job controller. You can:
- Run multiple commands in the background simultaneously
- Track what's running and when things complete
- Build pipelines that work in background or foreground

But you can't:
- Pause work and come back to it later
- Interrupt long-running background jobs
- Dynamically move jobs between foreground and background

**Example workflow in UCRT build:**
```
$ compile_project &        # Start compilation
[1] 12345
$ run_tests &              # Start tests
[2] 12346
$ jobs                     # Check what's running
[1]   Running   compile_project &
[2]   Running   run_tests &
$ wait %1                  # Wait for compilation
[1]   Done   compile_project
$ jobs                     # Tests still running
[2]   Running   run_tests &
```

This works well for launching multiple independent tasks, but if you need to stop something, you'll need to use external tools (like Task Manager on Windows) or let it complete.

### ISO C Build (No Job Control)

When built with only ISO C standard library functions, the shell has **no job control**. It uses only the `system()` function to execute commands, which is extremely limited.

**What works:**

- ✅ **Sequential foreground commands**: Commands run one at a time, waiting for each to complete
  ```
  $ command1
  $ command2
  ```

**What doesn't work:**

- ❌ No background jobs (no `&` support)
- ❌ No pipelines
- ❌ No I/O redirection
- ❌ No job tracking
- ❌ No job suspension or control
- ❌ Limited command parsing (depends on system shell)

The `system()` function essentially passes each command line to the system's command interpreter, so:
- You can only run one command at a time
- You must wait for it to finish
- No multitasking is possible
- Command features depend entirely on the underlying system shell

**Practical use:**

This build is mainly useful for:
- Maximum portability across different systems
- Simple scripting where you just need to run commands sequentially
- Environments where process control isn't available or needed

**Example workflow in ISO C build:**
```
$ command1        # Runs and waits
$ command2        # Runs after command1 completes
$ command3        # Runs after command2 completes
```

It's essentially a command-by-command interpreter with no ability to manage multiple processes.

### Choosing Your Build

- **Need full job control?** Use the POSIX build on Unix-like systems
- **On Windows but need background jobs?** Use the UCRT build—you get basic multitasking without full interactive control
- **Need maximum portability?** Use the ISO C build, but accept that it's a simple sequential command executor

The good news is that scripts written for more limited builds will generally work in more capable builds. A script that works in the ISO C build will work in UCRT, and a UCRT script will work in POSIX (though you might not be using all available features).

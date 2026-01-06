# Signals and Error Handling

**Signals** are how Unix-like systems communicate asynchronous events to programs. Think of them as urgent notifications that say things like "the user pressed Ctrl+C" (SIGINT), "this process should terminate" (SIGTERM), or "a child process just finished" (SIGCHLD). Understanding how the shell handles signals is crucial for writing robust scripts and understanding what happens when things go wrong.

The shell has specific rules about how it manages signals—both for itself and for the commands it runs. These rules affect how Ctrl+C works, what happens when background jobs receive signals, and how your `trap` commands behave.

## The Default Signal Inheritance Model

When the shell starts up, it inherits signal handling behavior from its parent process (usually your terminal or another shell). By default, **commands you run inherit the same signal actions as the shell**.

What does this mean? If the shell's parent had SIGTERM set to default behavior (terminate the process), then:
1. The shell inherits that default behavior
2. Commands the shell runs also inherit that default behavior
3. Everyone reacts the same way to SIGTERM

This consistent inheritance makes programs behave predictably. When you press Ctrl+C (which sends SIGINT), both interactive commands and the shell itself typically terminate, because they've all inherited the same "default termination" behavior for SIGINT.

### The Exception: Background Jobs and Job Control

There's one major exception to this inheritance rule: **background jobs when job control is disabled**.

If you run a command in the background with `&` and job control is turned off (`set +m`), that background command inherits **ignored** signal actions for SIGINT and SIGQUIT:

```bash
$ set +m                # Disable job control
$ long_command &        # Background job ignores SIGINT and SIGQUIT
```

Why? Because background jobs shouldn't be interrupted by Ctrl+C (SIGINT) or Ctrl+\ (SIGQUIT)—those keystrokes are meant for the foreground job. Without process groups to manage this automatically, the shell must explicitly tell background jobs to ignore these signals.

When job control **is** enabled (`set -m`, the default in interactive shells), process groups handle this automatically, so the explicit ignoring isn't needed.

## Modifying Signal Behavior with `trap`

The shell's `trap` command lets you override the default signal handling. You can:
- Run a command when a signal arrives
- Ignore a signal entirely
- Restore default behavior

Basic syntax:
```bash
trap 'command' SIGNAL
trap '' SIGNAL          # Ignore signal
trap - SIGNAL           # Restore default
```

Examples:
```bash
# Run cleanup on interrupt
trap 'echo "Interrupted! Cleaning up..."; rm -f /tmp/temp.*' SIGINT

# Ignore hangup signal
trap '' SIGHUP

# Restore default behavior for TERM
trap - SIGTERM
```

**Important**: Commands you run after setting a trap do **not** inherit your trap actions. They still inherit the shell's original signal actions from its parent. The `trap` command only affects the shell itself, not its children.

For example:
```bash
trap 'echo "Shell got SIGINT"' SIGINT
./my_program    # This program still has default SIGINT behavior
```

If you press Ctrl+C while `my_program` is running, the program terminates (default behavior), and then the shell runs your trap command.

## Signals and Foreground Commands: The Waiting Game

Here's a subtle but important behavior: **when the shell is waiting for a foreground command to finish, traps are deferred**.

Consider this scenario:
```bash
#!/bin/bash
trap 'echo "Caught signal!"' SIGINT

echo "Running slow command..."
sleep 30    # Foreground command
echo "Done"
```

If you press Ctrl+C while `sleep` is running:
1. The SIGINT signal is sent to the foreground process group (which includes `sleep`)
2. `sleep` terminates (default SIGINT behavior)
3. **Only after `sleep` finishes** does the shell execute the trap command
4. You see "Caught signal!"

The trap doesn't run until the foreground command completes. This prevents the shell from interfering with foreground commands—they get to handle (or die from) signals first.

### Why This Matters

This deferred execution means:
- You can't use traps to "protect" foreground commands from signals
- The foreground command always gets first chance to handle the signal
- Your trap cleanup code runs after the command terminates

This is usually what you want—it lets foreground programs handle signals naturally, then your shell script can respond afterward.

## Signals and Background Jobs: The `wait` Utility

The behavior changes when you're using the `wait` utility to wait for background jobs. In this case, **signals can interrupt the waiting immediately**.

Consider this script:
```bash
#!/bin/bash
trap 'echo "Interrupted wait!"' SIGINT

long_job &    # Start background job
wait          # Wait for background jobs
echo "All done"
```

If you press Ctrl+C while `wait` is executing:
1. The `wait` utility receives SIGINT
2. `wait` **immediately returns** with an exit status greater than 128
3. The trap command runs: "Interrupted wait!"
4. The script continues to "All done"

The key differences from foreground commands:
- `wait` returns immediately when a trapped signal arrives
- The exit status is >128 (typically 128 + signal number)
- The trap executes right after `wait` returns
- Background jobs continue running (they weren't interrupted)

This lets you handle signals interactively while waiting for background work. For example:
```bash
trap 'echo "Still waiting... (Ctrl+C again to abort)"; interrupt_count=$((interrupt_count+1))' SIGINT

interrupt_count=0
while [ $interrupt_count -lt 2 ]; do
    if wait; then
        echo "Job completed!"
        break
    fi
done
```

Each Ctrl+C interrupts `wait` and increments the counter, but jobs keep running until you press it twice.

## Multiple Pending Signals: Order Is Unspecified

If multiple signals arrive while the shell is busy (and has traps set for them), the order in which the trap actions execute is **unspecified**. Don't rely on signals being processed in any particular order.

For example, if SIGTERM and SIGUSR1 both arrive while a foreground command is running:
```bash
trap 'echo "Got TERM"' SIGTERM
trap 'echo "Got USR1"' SIGUSR1

sleep 30
```

You might see:
```
Got TERM
Got USR1
```

Or:
```
Got USR1
Got TERM
```

Either is valid. If the order matters to you, you'll need to design your trap handlers to coordinate (perhaps using a shared state file or flags).

## Common Signal Handling Patterns

### Cleanup on Exit

The most common use of `trap` is cleanup when your script exits (normally or abnormally):

```bash
#!/bin/bash
temp_file=$(mktemp)
trap 'rm -f "$temp_file"' EXIT

# Use temp_file...
# It will be cleaned up automatically
```

The EXIT "signal" (actually a pseudo-signal) triggers when the shell exits for any reason: normal completion, error, or signal.

### Ignoring Interrupts During Critical Sections

Sometimes you need to complete an operation without interruption:

```bash
#!/bin/bash
trap '' SIGINT SIGTERM    # Ignore during critical section

echo "Updating database..."
critical_update           # Must complete
echo "Update complete"

trap - SIGINT SIGTERM     # Restore defaults
```

Be cautious with this—users get frustrated when Ctrl+C doesn't work!

### Graceful Shutdown

For long-running scripts, allow graceful shutdown:

```bash
#!/bin/bash
keep_running=true
trap 'keep_running=false' SIGTERM SIGINT

while $keep_running; do
    process_batch
    sleep 1
done

echo "Shutting down gracefully..."
cleanup
```

### Debugging Signal Issues

To see what signals your script receives:

```bash
#!/bin/bash
trap 'echo "Received signal: $?"' SIGINT SIGTERM SIGHUP

# Your script logic...
```

## Practical Tips

1. **Test your traps**: Signal handling is hard to get right. Test with actual signals (using `kill` or keyboard shortcuts).

2. **Keep trap actions simple**: Complex trap handlers can cause problems. Consider calling a function instead:
   ```bash
   cleanup() {
       # Complex cleanup logic
   }
   trap cleanup EXIT
   ```

3. **Remember trap scope**: Traps only affect the current shell, not child processes.

4. **Background jobs need special care**: They don't receive keyboard signals (Ctrl+C) like foreground jobs do.

5. **Exit status from wait**: When `wait` is interrupted by a signal, check the exit status to distinguish between job completion and signal interruption:
   ```bash
   if wait $pid; then
       echo "Job completed"
   else
       status=$?
       if [ $status -gt 128 ]; then
           echo "Interrupted by signal"
       else
           echo "Job failed with status $status"
       fi
   fi
   ```

## Common Signals Reference

Here are the signals you'll encounter most often:

- **SIGINT** (2): Interrupt (Ctrl+C) - Default: terminate
- **SIGQUIT** (3): Quit (Ctrl+\) - Default: terminate with core dump
- **SIGTERM** (15): Termination request - Default: terminate
- **SIGHUP** (1): Hangup (terminal closed) - Default: terminate
- **SIGKILL** (9): Force kill - **Cannot be trapped or ignored**
- **SIGSTOP** (19): Stop process - **Cannot be trapped or ignored**
- **SIGTSTP** (20): Stop from terminal (Ctrl+Z) - Default: stop
- **SIGCONT** (18): Continue if stopped
- **SIGCHLD** (17): Child process status changed
- **SIGUSR1** (10): User-defined signal 1
- **SIGUSR2** (12): User-defined signal 2

Note: Signal numbers can vary by system. Use signal names (SIGINT, not 2) in scripts for portability.

## Availability: Signal Handling Across Different Builds

Signal handling capabilities vary significantly depending on which API the shell is built with. Here's what's available in each build:

### POSIX API Build (Full Signal Support)

When built with the POSIX API, the shell provides **complete signal handling** as described throughout this document. All features are available:

- ✅ Full signal inheritance from parent process
- ✅ Background jobs with SIGINT/SIGQUIT automatically ignored (when job control is disabled)
- ✅ Complete `trap` command functionality
  - Set custom handlers for any signal
  - Ignore signals with `trap ''`
  - Restore defaults with `trap -`
- ✅ Deferred trap execution during foreground commands
- ✅ Immediate trap execution during `wait`
- ✅ EXIT pseudo-signal for cleanup
- ✅ All standard signals: SIGINT, SIGTERM, SIGHUP, SIGCHLD, SIGUSR1, SIGUSR2, etc.
- ✅ Process group signal handling (Ctrl+C affects entire pipeline)
- ✅ Proper signal handling with job control

This provides the robust, predictable signal behavior you expect from a Unix shell.

### UCRT API Build (Limited Signal Support)

When built with the Universal C Runtime (UCRT) API on Windows, the shell has **severely limited signal support**. UCRT only provides a small subset of signals, and signal handling is constrained to the current process.

**What works:**

- ✅ **Basic `trap` for limited signals**: You can set traps for:
  - SIGINT (Ctrl+C) - works within the shell process
  - SIGTERM - can be used for cleanup
  - SIGBREAK (Windows-specific Ctrl+Break)
  - SIGABRT (abort signal)
  - EXIT pseudo-signal for cleanup
  
  ```bash
  trap 'echo "Cleaning up..."; rm -f temp.*' EXIT
  ```

- ✅ **Signal handling in the shell itself**: The shell can catch and respond to signals it receives directly

- ✅ **Exit status preservation**: When signals cause termination, exit codes are preserved

**What doesn't work:**

- ❌ **No signal propagation to child processes**: The shell can't send signals to running commands
  - `trap` only affects the shell itself
  - Can't interrupt foreground commands with trapped signals
  - Background jobs don't receive shell signals

- ❌ **No process group signaling**: Since there are no process groups:
  - Ctrl+C only affects what's directly reading from the console
  - Can't send signals to entire pipelines
  - Each process handles signals independently

- ❌ **Limited signal set**: Most Unix signals don't exist:
  - No SIGHUP (hangup)
  - No SIGQUIT (quit)
  - No SIGTSTP/SIGSTOP (job control signals)
  - No SIGCHLD (child process notification)
  - No SIGUSR1/SIGUSR2 (user-defined signals)
  - No SIGPIPE (broken pipe)

- ❌ **No signal-based job control**: Since job control is already limited in the UCRT build, signal-based suspension/resumption doesn't exist

- ❌ **Console Ctrl+C behavior is different**: On Windows console:
  - Ctrl+C might affect the entire console process tree
  - Behavior can be unpredictable with background jobs
  - Terminal I/O is fundamentally different from Unix

**Practical implications:**

Signal handling in the UCRT build is mainly useful for:
- **Cleanup on shell exit**: Use `trap '...' EXIT` for cleanup code
- **Responding to shell interruption**: Handle Ctrl+C in the shell itself
- **Graceful shutdown**: Respond to SIGTERM

But you cannot:
- Reliably interrupt or signal running commands
- Coordinate signal handling across pipelines
- Use most Unix signal patterns
- Depend on consistent Ctrl+C behavior with background jobs

**Example of what works:**
```bash
#!/bin/bash
# Cleanup on exit works fine
temp_file="temp_$$.txt"
trap 'rm -f "$temp_file"' EXIT

echo "Working..."
# Do stuff with temp_file
# File will be cleaned up when shell exits
```

**Example of what doesn't work:**
```bash
#!/bin/bash
# This WON'T work as expected
trap 'echo "Interrupt ignored"' SIGINT

long_running_command    # Ctrl+C here will kill the command
                        # Your trap won't prevent it
```

The command runs in a separate process that doesn't inherit your trap, and there's no way to propagate signal handling to it.

### ISO C Build (Minimal Signal Support)

When built with only ISO C standard library functions, the shell has **extremely minimal signal support**. The C standard only defines a few signals and very basic handling.

**What works:**

- ⚠️ **Very basic `trap` for standard signals only**:
  - SIGINT - interrupt
  - SIGTERM - termination  
  - SIGABRT - abort
  - EXIT pseudo-signal
  
  ```bash
  trap 'cleanup' EXIT
  ```

- ⚠️ **Shell-only handling**: Like UCRT, signals only affect the shell itself, not child processes

**What doesn't work:**

- ❌ All the limitations of the UCRT build, plus:
- ❌ No Windows-specific signals (SIGBREAK)
- ❌ Signal handling behavior is implementation-defined and unpredictable
- ❌ The `system()` function may or may not preserve signal handlers
- ❌ Signal handling during command execution is completely undefined

**Practical implications:**

Signal handling in the ISO C build is unreliable and should be avoided except for the most basic cases:

**Safe to use:**
```bash
# Cleanup on normal exit is reasonably safe
trap 'rm -f temp.*' EXIT
```

**Unsafe/unpredictable:**
```bash
# These may or may not work, behavior is undefined
trap 'echo "Interrupted"' SIGINT   # Might not be called
trap '' SIGHUP                      # SIGHUP might not exist
```

Since the ISO C build uses `system()` to run commands, and `system()` blocks the shell until the command completes, signal handling is essentially out of your control. The system's command interpreter decides how signals work.

**Recommendation**: Don't rely on signal handling in the ISO C build except for basic EXIT traps. If you need signal handling, use the UCRT or POSIX builds.

### Summary Table

| Feature | POSIX | UCRT | ISO C |
|---------|-------|------|-------|
| Trap custom handlers | ✅ All signals | ⚠️ Limited | ⚠️ Very limited |
| Signal children | ✅ Yes | ❌ No | ❌ No |
| Process group signals | ✅ Yes | ❌ No | ❌ No |
| Deferred trap execution | ✅ Yes | ⚠️ Limited | ⚠️ Undefined |
| Wait interruption | ✅ Yes | ⚠️ Partial | ❌ No |
| Job control signals | ✅ Yes | ❌ No | ❌ No |
| EXIT cleanup | ✅ Yes | ✅ Yes | ⚠️ Usually |
| Signal reliability | ✅ Reliable | ⚠️ Shell-only | ❌ Unpredictable |

### Choosing Your Build for Signal Handling

- **Need robust signal handling?** Use the POSIX build—it's the only one that provides reliable, predictable signal behavior
- **On Windows with basic needs?** Use the UCRT build—you can at least do cleanup and handle shell interrupts
- **Maximum portability?** Use the ISO C build, but avoid relying on any signal handling beyond basic EXIT traps

For scripts that need reliable signal handling, the POSIX build is the only real choice. The other builds can handle basic cleanup but shouldn't be trusted for complex signal coordination.

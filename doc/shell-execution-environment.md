# Shell Execution Environment

When you run commands in the shell, they don't all execute in the same "context." Some commands run in the current shell's environment, others in a completely separate process environment, and some in a special **subshell environment** that's a copy of the current shell. Understanding these different execution environments is crucial for predicting how variables, traps, directory changes, and other settings behave in your scripts.

In this section, we'll explore what makes up an execution environment, when new environments are created, and how changes in one environment affect (or don't affect) others.

## What Is a Shell Execution Environment?

A **shell execution environment** is the complete context in which commands execute. It's like a workspace that contains all the settings, variables, open files, and state that affect how commands behave. Think of it as the shell's "memory" of its current state.

The shell execution environment consists of:

### 1. Open Files
- Files that were open when the shell started
- Files opened or closed by the `exec` built-in
- File descriptors (like stdin=0, stdout=1, stderr=2)

Example:
```bash
exec 3< input.txt    # Open file descriptor 3
cat <&3              # Read from it
exec 3<&-            # Close it
```

### 2. Working Directory
- The current directory, as set by `cd`
- This is where relative paths are resolved from

Example:
```bash
cd /tmp
pwd          # Shows /tmp
touch file   # Creates /tmp/file
```

### 3. File Creation Mask (umask)
- Controls default permissions for new files and directories
- Set by the `umask` command

Example:
```bash
umask 022    # New files: 644, new dirs: 755
```

### 4. File Size Limit
- Maximum size for files created by the shell and its children
- Set by `ulimit`

Example:
```bash
ulimit -f 1000    # Limit files to 1000 blocks
```

### 5. Current Traps
- Signal handlers set by the `trap` command
- Determine what happens when signals arrive

Example:
```bash
trap 'echo "Interrupted!"' SIGINT
```

### 6. Shell Parameters and Variables
- Variables set by assignment or inherited from the parent process
- Includes exported environment variables
- Set by variable assignment or the `set` command

Example:
```bash
MY_VAR=value
export PATH=/usr/local/bin:$PATH
```

### 7. Shell Functions
- Functions defined in the current shell
- Available for the shell to call

Example:
```bash
greet() {
    echo "Hello, $1!"
}
```

### 8. Shell Options
- Options turned on at startup or via `set`
- Control shell behavior (like `set -e`, `set -x`)

Example:
```bash
set -e    # Exit on error
set -x    # Print commands before executing
```

### 9. Background Jobs and Process IDs
- Tracked background jobs (when job control is enabled)
- Process IDs known to this shell environment
- Includes both job-control background jobs and async commands

Example:
```bash
long_command &
[1] 12345    # Job 1, PID 12345
```

### 10. Shell Aliases
- Command aliases defined with `alias`

Example:
```bash
alias ll='ls -la'
```

All of these components together form the complete picture of the shell's current state.

## Three Types of Execution Environments

Commands can execute in three different types of environments:

### 1. Current Shell Environment
Some commands run directly in the shell's own environment. Changes they make **persist** after they complete.

**What runs in the current shell environment:**
- Shell built-ins (like `cd`, `export`, `set`)
- Variable assignments
- Function definitions
- Alias definitions

Example:
```bash
cd /tmp        # Changes current shell's directory
MY_VAR=value   # Sets variable in current shell
myfunc() { echo "hi"; }  # Defines function in current shell
```

After these commands, the shell's environment has changed permanently (for the session).

### 2. Separate Utility Environment
Regular commands (external programs) run in a **separate environment**. This is a new process with its own memory space.

**What's copied to the utility environment:**
- Open files inherited from the shell, plus any redirections
- Current working directory
- File creation mask
- **Exported variables only** (not regular shell variables)
- Trap actions are translated:
  - Trapped signals → default actions (for executables)
  - Ignored signals → remain ignored
  - Shell scripts → traps caught by shell become defaults, ignored stay ignored

**What's NOT copied:**
- Regular (non-exported) shell variables
- Shell functions
- Shell aliases
- Shell options

Example:
```bash
MY_VAR=local
export EXPORTED=visible

./my_program    # Can see EXPORTED, cannot see MY_VAR
```

Inside `my_program`, `$EXPORTED` is available, but `$MY_VAR` is not (unless you export it or prefix the command with it).

**Important**: The utility cannot change the shell's environment (with rare exceptions like `cd` in scripts that are sourced).

### 3. Subshell Environment
A **subshell** is a copy of the shell environment—it's like creating a clone of the current shell that runs independently.

**When subshells are created:**
- Command substitution: `$(command)` or `` `command` ``
- Commands grouped with parentheses: `( commands )`
- Background jobs: `command &` (usually)
- Each command in a pipeline: `cmd1 | cmd2 | cmd3` (each might be in a subshell)

**What's copied to the subshell:**
- Everything from the shell environment (all 10 components listed above)
- Includes both exported AND non-exported variables
- Includes shell functions
- Includes aliases
- Includes current directory

**What's different in the subshell:**
- Traps that aren't ignored are reset to default actions
- If the parent is interactive, the subshell acts non-interactive (mostly)
- Changes made don't affect the parent shell

Example:
```bash
MY_VAR=original

(
    MY_VAR=changed
    cd /tmp
    echo "In subshell: $MY_VAR in $(pwd)"
)

echo "In parent: $MY_VAR in $(pwd)"
```

Output:
```
In subshell: changed in /tmp
In parent: original in /home/user
```

The subshell's changes are isolated—they don't leak back to the parent.

## When Each Environment Is Used

Let's look at specific examples:

### Current Shell Environment

```bash
# Built-ins modify current shell
cd /tmp              # Shell's directory changes
export VAR=value     # Shell's environment changes
set -e               # Shell's options change
trap 'cleanup' EXIT  # Shell's traps change

# Variable assignments (without command)
MY_VAR=value         # Set in current shell

# Function definitions
myfunc() { echo "hi"; }  # Defined in current shell
```

### Separate Utility Environment

```bash
# External commands
ls -la               # Runs in separate process
./my_script.sh       # Runs in separate process
python program.py    # Runs in separate process

# These can see exported variables
export PATH=/usr/local/bin:$PATH
ls    # ls sees the new PATH

# But not regular variables
LOCAL=value
ls    # ls cannot see LOCAL
```

### Subshell Environment

```bash
# Command substitution
result=$(echo "This runs in a subshell")

# Parentheses grouping
(
    cd /tmp
    echo "Working in $PWD"
)
echo "Still in $PWD"  # Not /tmp

# Background jobs
long_command &    # Usually runs in subshell

# Pipelines
cat file | grep pattern | wc -l
# Each command might be in its own subshell
```

## Practical Implications and Common Pitfalls

### Pitfall 1: Variable Changes in Subshells Don't Persist

This is the most common confusion:

```bash
# WRONG - Won't work as expected
echo "output" | read line
echo "Line: $line"    # $line is empty!
```

Why? Because `read` runs in a subshell (it's in a pipeline), so the variable it sets doesn't exist in the parent shell.

**Solution**: Avoid pipelines when you need to capture variables:
```bash
# RIGHT - Use here-document
read line <<EOF
output
EOF
echo "Line: $line"    # Works!

# Or use command substitution
line=$(echo "output")
echo "Line: $line"    # Works!
```

### Pitfall 2: Directory Changes in Subshells Don't Persist

```bash
# WRONG
(cd /tmp && ls)    # Lists /tmp
pwd                # Still in original directory
```

The `cd` happened in the subshell, not the current shell.

**Solution**: Don't use subshells when you want persistent changes:
```bash
# RIGHT
cd /tmp && ls
pwd    # Now in /tmp
```

Or use a subshell intentionally to avoid changing directory:
```bash
# Save current directory, work in /tmp, return automatically
(cd /tmp && compile_project)
# Still in original directory
```

### Pitfall 3: Function Definitions in Subshells Are Lost

```bash
# WRONG
(
    myfunc() { echo "hi"; }
)
myfunc    # Error: myfunc not found
```

**Solution**: Define functions in the current shell:
```bash
# RIGHT
myfunc() { echo "hi"; }
myfunc    # Works
```

### Pitfall 4: Exported vs. Non-Exported Variables

```bash
LOCAL=value
export EXPORTED=value

# Script or program sees only EXPORTED
./script.sh

# But command substitution sees both
echo "$(echo $LOCAL)"      # Works - subshell has LOCAL
echo "$(./script.sh)"      # script.sh doesn't see LOCAL
```

**Rule of thumb**: If a variable should be available to external programs, `export` it. If it's only for the shell script itself, leave it unexported.

### Pitfall 5: Trap Behavior in Subshells

```bash
trap 'echo "Parent trap"' EXIT

(
    trap 'echo "Subshell trap"' EXIT
    exit
)
# Prints "Subshell trap"

exit
# Prints "Parent trap"
```

Traps are independent in each environment.

## Special Cases and Extensions

### Pipelines: Where Do They Run?

The POSIX standard says each command in a pipeline **can** run in a subshell, but shells may optimize this:

```bash
echo "test" | read var
```

- Some shells run `read` in a subshell (bash with default settings)
- Others run it in the current shell (ksh, zsh with certain options)
- Result: variable persistence is shell-dependent

**Portable approach**: Don't rely on pipeline commands setting variables.

### Special Built-ins Are Special

Special built-ins (like `export`, `set`, `trap`) always run in the current shell environment, even in contexts where other commands might create subshells.

This means:
```bash
export VAR=value | tee log    # export still affects current shell
```

### Command Substitution Variables

Variables set during command substitution affect the subshell but are visible in the substitution result:

```bash
result=$(VAR=temp; echo $VAR)
echo $result     # Shows "temp"
echo $VAR        # Empty (VAR was only in subshell)
```

## Tips for Managing Environments

### 1. Use Subshells for Isolation

When you want to experiment without affecting your current shell:

```bash
(
    cd /tmp
    rm -rf *    # Only affects /tmp, not current directory
)
# Back to original directory automatically
```

### 2. Export Variables That Cross Process Boundaries

```bash
# For scripts and external programs
export DATABASE_URL=postgres://localhost/mydb

# For shell-only use
temp_counter=0
```

### 3. Be Explicit with `export`

```bash
# Clear intent: this is for child processes
export PATH=/usr/local/bin:$PATH

# Clear intent: this is shell-local
SCRIPT_DIR=$(dirname "$0")
```

### 4. Test Where Your Code Runs

```bash
# Test if something runs in current shell or subshell
test_var=original
echo "before" | { test_var=changed; cat; }
echo $test_var    # Still "original"? Was in subshell
```

### 5. Use Here-Documents Instead of Pipes for Reading

```bash
# Instead of:
echo "$data" | while read line; do
    # Variable changes lost
done

# Use:
while read line; do
    # Variable changes preserved
done <<EOF
$data
EOF
```

## Summary: Quick Reference

| Feature | Current Shell | Separate Utility | Subshell |
|---------|--------------|------------------|----------|
| Variable changes persist | ✅ Yes | ❌ No | ❌ No |
| Sees non-exported vars | ✅ Yes | ❌ No | ✅ Yes |
| Sees exported vars | ✅ Yes | ✅ Yes | ✅ Yes |
| Sees functions | ✅ Yes | ❌ No | ✅ Yes |
| Sees aliases | ✅ Yes | ❌ No | ✅ Yes |
| Directory changes persist | ✅ Yes | ❌ No | ❌ No |
| Trap changes persist | ✅ Yes | ❌ No | ❌ No |
| Used by | Built-ins | External commands | `$()`, `()`, `&`, pipes |

## Availability: Execution Environments Across Different Builds

The concept of execution environments exists in all builds, but the level of isolation and control varies significantly with different APIs.

### POSIX API Build (Full Environment Control)

When built with the POSIX API, the shell provides **complete execution environment management** as described throughout this document.

**Current shell environment:**
- ✅ Full control over all environment components
- ✅ Built-ins modify the current environment as expected
- ✅ Variable assignments, directory changes, and traps work correctly

**Separate utility environment:**
- ✅ Complete process isolation via `fork()` and `exec()`
- ✅ Proper inheritance of exported variables
- ✅ File descriptors correctly inherited and managed
- ✅ Working directory, umask, and file limits properly set
- ✅ Signal/trap handling correctly translated for child processes
- ✅ Parent shell environment fully protected from child changes

**Subshell environment:**
- ✅ Full duplication via `fork()`
- ✅ Complete isolation—subshell changes don't affect parent
- ✅ All variables (exported and non-exported) copied
- ✅ Functions and aliases available in subshell
- ✅ Traps properly reset to defaults (unless ignored)
- ✅ Command substitution `$()` creates true subshells
- ✅ Parentheses grouping `()` creates true subshells
- ✅ Background jobs `&` run in subshells
- ✅ Pipeline commands can run in subshells

This provides the predictable, well-isolated environment behavior expected from a POSIX shell.

### UCRT API Build (Limited Environment Isolation)

When built with the Universal C Runtime (UCRT) API on Windows, the shell provides **basic environment separation** but with significant limitations due to Windows process model differences.

**Current shell environment:**
- ✅ Variables, directory, and most settings work normally
- ✅ Built-ins modify the current environment
- ⚠️ Some operations (like file descriptor management) are more limited

**Separate utility environment:**
- ✅ External commands run in separate processes via `_spawnvp()`
- ✅ Exported variables passed as environment variables
- ✅ Working directory inherited
- ✅ File redirections work via `_dup2()`
- ⚠️ File descriptor inheritance is more limited than POSIX
- ⚠️ Umask doesn't exist on Windows (file permissions work differently)
- ⚠️ File size limits via `ulimit` don't apply (Windows has different quota systems)
- ⚠️ Signal/trap translation is limited (only a few signals exist)
- ✅ Parent shell environment protected from child changes

**Subshell environment:**
- ⚠️ **No true subshells**: Windows doesn't have `fork()`, so true copy-on-write subshells don't exist
- ⚠️ Command substitution `$()` works by spawning a new shell process:
  ```bash
  result=$(echo $VAR)    # Spawns a new shell, passes command
  ```
  - Exported variables are available
  - Non-exported variables are NOT available (unlike POSIX)
  - Shell functions are NOT available
  - Aliases are NOT available
  
- ⚠️ Parentheses grouping `()` spawns a new shell:
  ```bash
  (cd /tmp && ls)    # Starts fresh shell in /tmp
  ```
  - Much more expensive than POSIX (full shell startup)
  - Non-exported variables and functions not available
  - Isolation works, but performance penalty

- ⚠️ Background jobs don't create subshell copies:
  ```bash
  command &    # Runs in separate process, but not a shell copy
  ```

- ⚠️ Pipelines create separate processes:
  ```bash
  cmd1 | cmd2    # Both in separate processes
  ```
  - Variables set in pipeline commands don't propagate back
  - Same limitation as POSIX bash, but for different reasons

**Practical implications:**

The UCRT build's environment isolation works for most purposes, but with caveats:

**What works well:**
```bash
# Separate processes for commands
./program1
./program2

# Environment variables
export VAR=value
./program    # Sees VAR

# Directory isolation with ()
(cd /tmp && work_here)
pwd    # Not in /tmp
```

**What's different:**
```bash
# Command substitution with non-exported vars
LOCAL=value
result=$(echo $LOCAL)    # $LOCAL is empty! Not exported
echo $result

# Functions in subshells
myfunc() { echo "hi"; }
result=$(myfunc)    # Error: myfunc not found in new shell

# Performance
(command)    # Much slower than POSIX (full shell startup)
```

**Workarounds:**

Export variables you need in substitutions:
```bash
export VAR=value
result=$(echo $VAR)    # Works
```

Avoid subshells for functions—call directly:
```bash
myfunc() { echo "hi"; }
result=$(echo "hi")    # Inline the command
# Or call myfunc directly without subshell
```

### ISO C Build (Minimal Environment Control)

When built with only ISO C standard library functions, the shell has **very limited environment control**. The `system()` function is the only way to execute commands, which severely restricts environment management.

**Current shell environment:**
- ✅ Variables work within the shell's own process
- ❌ No directory changes (no `chdir()` in ISO C)
- ⚠️ File descriptors, umask, and limits depend on C library implementation
- ❌ No trap management (extremely limited signals)
- ❌ No background jobs or process tracking

**Separate utility environment:**
- ⚠️ Commands executed via `system()`:
  ```bash
  system("ls -la")
  ```
- ⚠️ Environment is whatever the system's command interpreter provides
- ⚠️ No control over what gets inherited
- ⚠️ Implementation-defined behavior for:
  - Which variables are passed
  - How redirections work
  - Signal handling
  - Working directory inheritance

**Subshell environment:**
- ❌ **No subshells at all**
- ❌ No command substitution `$()` (would require process control)
- ❌ No parentheses grouping `()`
- ❌ No background jobs `&`
- ❌ No pipelines `|`

**Practical implications:**

The ISO C build is essentially a **simple command executor** with a minimal notion of environment:

**What works:**
```bash
# Sequential commands
command1
command2

# Variable tracking (in shell only)
VAR=value
echo $VAR    # Within shell parsing
```

**What doesn't work:**
```bash
# No directory changes
cd /tmp    # Not supported (no chdir() in ISO C)

# No command substitution
result=$(command)    # Not supported

# No subshell isolation
(cd /tmp && ls)    # Not supported

# No pipelines
cmd1 | cmd2    # Not supported

# Limited variable passing
VAR=value
system("program")    # May or may not see VAR
```

The shell maintains minimal state (variables, current directory) but has almost no control over the environment that external commands see. Everything depends on what the platform's `system()` function does.

**Recommendation**: The ISO C build is only suitable for the simplest scripts that just execute commands sequentially without needing environment control, process isolation, or even directory navigation.

### Summary Table

| Feature | POSIX | UCRT | ISO C |
|---------|-------|------|-------|
| Current shell env | ✅ Full | ✅ Good | ⚠️ Basic |
| Separate process env | ✅ Full control | ✅ Good | ⚠️ `system()` only |
| True subshells | ✅ Yes (`fork()`) | ❌ No (new shell) | ❌ No |
| Command substitution | ✅ Full | ⚠️ Exported only | ❌ No |
| Subshell isolation | ✅ Complete | ⚠️ Limited | ❌ No |
| Subshell performance | ✅ Fast | ⚠️ Slow | ❌ N/A |
| Functions in subshells | ✅ Yes | ❌ No | ❌ No |
| Non-exported vars in subshells | ✅ Yes | ❌ No | ❌ No |
| Pipeline isolation | ✅ Yes | ⚠️ Yes | ❌ No |
| Background job env | ✅ Subshell | ⚠️ Separate | ❌ No |

### Choosing Your Build for Environment Control

- **Need proper subshells and environment isolation?** Use the POSIX build—it's the only one with true `fork()` subshells
- **On Windows with basic needs?** Use the UCRT build—export variables you need in subshells, avoid relying on functions in command substitution
- **Maximum portability?** Use the ISO C build, but write the simplest possible scripts with no subshells or pipelines

For scripts that use command substitution, parentheses grouping, or rely on variables/functions being available in subshells, the POSIX build is essential. The UCRT build can work if you're careful about what you export and avoid function calls in subshells.
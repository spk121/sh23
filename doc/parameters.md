# Parameters: Positional and Special

The shell uses **parameters** to give you access to important information like command-line arguments, exit codes, process IDs, and more. The most common type of a parameter is a variable see [Variables](variables.md). While variables are one type of parameter (covered in their own section), this section focuses on the two other types:

- **Positional parameters**: Numbered parameters (`$1`, `$2`, etc.) that hold arguments
- **Special parameters**: Single-character parameters (`$?`, `$@`, `$$`, etc.) that provide system information

Understanding these parameters is essential for writing scripts that handle arguments correctly and respond to the system's state.

## What Is a Parameter?

A **parameter** is an entity that stores a value. Parameters can be denoted by:
- A **name** is a variable, see [Variables](variables.md)
- A **number** (positional parameters - `$1`, `$2`, etc.)
- A **special character** (special parameters - `$?`, `$@`, etc.)

**Key concepts:**

1. **Set vs. Unset**: A parameter is **set** if it has been assigned a value (even null/empty is a valid value). Once set, it can only be unset using the `unset` command.

2. **Arbitrary byte sequences**: Parameters can contain any bytes except null (byte value 0). The shell processes values as characters only when operations require it.

3. **Read-only context**: Most special parameters are read-only—you can't assign values to them. They're set by the shell itself.

## Positional Parameters

**Positional parameters** are numbered parameters that hold arguments. They're fundamental to how scripts and functions receive input.

### What Are They?

Positional parameters are denoted by positive integers:
- `$1` - first argument
- `$2` - second argument  
- `$3` - third argument
- ... and so on

They're called "positional" because each argument's value depends on its position in the argument list.

### Where They Come From

Positional parameters are set in three contexts:

1. **Shell invocation**: When you run a script or shell
   ```bash
   ./script.sh arg1 arg2 arg3
   # $1=arg1, $2=arg2, $3=arg3
   ```

2. **Function calls**: When a function is invoked (parameters are temporarily replaced)
   ```bash
   myfunc() {
       echo "Function arg 1: $1"
       echo "Function arg 2: $2"
   }
   myfunc hello world
   # Inside function: $1=hello, $2=world
   ```

3. **`set` command**: When you use `set` to manually set positional parameters
   ```bash
   set -- new1 new2 new3
   # $1=new1, $2=new2, $3=new3
   ```

### Basic Usage

```bash
#!/bin/bash
# script.sh
echo "First argument: $1"
echo "Second argument: $2"
echo "Third argument: $3"
```

Running it:
```bash
$ ./script.sh apple banana cherry
First argument: apple
Second argument: banana
Third argument: cherry
```

### Multi-Digit Positional Parameters

For positional parameters beyond `$9`, **you must use braces**:

```bash
echo "$1"          # First parameter
echo "$9"          # Ninth parameter
echo "$10"         # WRONG: $1 followed by literal "0"
echo "${10}"       # CORRECT: Tenth parameter
echo "${100}"      # One-hundredth parameter
```

**Important**: Leading zeros don't matter—they're always interpreted as decimal:

```bash
echo "${8}"        # Eighth parameter
echo "${08}"       # Also eighth parameter (leading zero ignored)
echo "${008}"      # Still eighth parameter
```

This is different from some other contexts where leading zeros mean octal!

### Checking for Positional Parameters

Check if a positional parameter is set:

```bash
if [ -n "$1" ]; then
    echo "First argument provided: $1"
else
    echo "No first argument"
fi
```

Check how many positional parameters exist using `$#` (special parameter):

```bash
if [ $# -eq 0 ]; then
    echo "No arguments provided"
    exit 1
fi
```

### Example: Processing Multiple Arguments

```bash
#!/bin/bash
# Print each argument on its own line

if [ $# -eq 0 ]; then
    echo "Usage: $0 <arg1> [arg2] [...]"
    exit 1
fi

echo "You provided $# arguments:"
echo "Argument 1: $1"
echo "Argument 2: $2"
echo "Argument 3: $3"
# ... etc
```

For processing all arguments, use the special parameters `$@` or `$*` (described below).

## Special Parameters

**Special parameters** are single-character parameters that the shell sets automatically. They provide access to important information about the shell's state, process IDs, exit codes, and arguments.

You **cannot** assign values to special parameters—they're read-only and managed by the shell.

### `$@` - All Positional Parameters (As Separate Fields)

Expands to all positional parameters, starting from `$1`. Each parameter becomes a separate field.

**The crucial behavior inside double quotes:**

```bash
set -- "arg 1" "arg 2" "arg 3"

# Unquoted $@ - word splitting happens
for arg in $@; do
    echo "Arg: [$arg]"
done
# Output:
# Arg: [arg]
# Arg: [1]
# Arg: [arg]
# Arg: [2]
# Arg: [arg]
# Arg: [3]

# Quoted "$@" - each parameter stays separate
for arg in "$@"; do
    echo "Arg: [$arg]"
done
# Output:
# Arg: [arg 1]
# Arg: [arg 2]
# Arg: [arg 3]
```

**This is the most important use of `$@`**: Inside double quotes (`"$@"`), it preserves arguments exactly as they were passed, including spaces.

**When there are no positional parameters:**
- `"$@"` expands to zero fields (nothing)
- This is perfect for functions that pass arguments through:
  ```bash
  wrapper() {
      echo "Calling real command..."
      real_command "$@"    # Pass all arguments through
  }
  ```

**Key rule**: Almost always use `"$@"` (with quotes) to preserve arguments correctly.

### `$*` - All Positional Parameters (As Single Field)

Expands to all positional parameters, but behavior differs from `$@`:

**Without quotes** - Same as `$@`:
```bash
set -- "arg 1" "arg 2"
echo $*        # Word splitting happens: arg 1 arg 2
echo $@        # Same result
```

**Inside double quotes** - Joins into single field:
```bash
set -- "arg 1" "arg 2" "arg 3"

echo "$*"
# Output: arg 1 arg 2 arg 3 (all one field)

for arg in "$*"; do
    echo "Arg: [$arg]"
done
# Output:
# Arg: [arg 1 arg 2 arg 3]    (only one iteration!)
```

**The separator character:**
- Uses the first character of `IFS` if `IFS` is set and not empty
- Uses space if `IFS` is unset
- Uses no separator if `IFS` is set to empty string

```bash
IFS=:
set -- a b c
echo "$*"        # a:b:c (joined with colon)

IFS=
echo "$*"        # abc (joined with no separator)

unset IFS
echo "$*"        # a b c (joined with space)
```

**When to use `$*`:**
- When you want to join arguments into a single string
- Logging or displaying all arguments as one message
- Building a single string from multiple pieces

**When NOT to use `$*`:**
- When passing arguments to another command (use `"$@"` instead)
- When you need to preserve individual arguments

### `$#` - Number of Positional Parameters

Expands to the count of positional parameters (not counting `$0`):

```bash
#!/bin/bash
echo "You provided $# arguments"

if [ $# -lt 2 ]; then
    echo "Error: Need at least 2 arguments"
    exit 1
fi
```

Examples:
```bash
$ ./script.sh
You provided 0 arguments

$ ./script.sh one
You provided 1 arguments

$ ./script.sh one two three
You provided 3 arguments
```

**Common uses:**
- Validating argument counts
- Looping through arguments
- Conditional logic based on number of arguments

```bash
if [ $# -eq 0 ]; then
    echo "Usage: $0 <file1> <file2> ..."
    exit 1
fi
```

### `$?` - Exit Status of Last Command

Expands to the exit status of the most recently executed pipeline:

```bash
command
if [ $? -eq 0 ]; then
    echo "Success!"
else
    echo "Failed with code $?"
fi
```

**Better pattern** - Test directly:
```bash
if command; then
    echo "Success!"
else
    echo "Failed!"
fi
```

**The value:**
- `0` means success
- Non-zero (1-255) means failure
- Specific codes have conventional meanings

**Important notes:**

1. **Initialization**: `$?` is set to 0 when the shell starts

2. **Most recent pipeline**: It's the status of the most recent command **in the current shell**, not subshells:
   ```bash
   (exit 42)        # Subshell exits with 42
   echo $?          # Prints 42 - exit status of the subshell command
   ```

3. **Command substitution**: The exit status becomes the status of the substitution:
   ```bash
   var=$(false)     # false exits with 1
   echo $?          # Prints 1 (status of the assignment command)
   ```

4. **In subshells**: The value of `$?` is preserved when creating a subshell:
   ```bash
   false
   (echo $?)        # Prints 1 (false's exit status was preserved)
   ```

**Common usage patterns:**

```bash
# Check if command succeeded
if grep pattern file > /dev/null 2>&1; then
    echo "Pattern found"
fi

# Save exit status for multiple checks
command
status=$?
if [ $status -eq 0 ]; then
    echo "Success"
elif [ $status -eq 1 ]; then
    echo "Minor error"
else
    echo "Major error: $status"
fi
```

### `$-` - Current Shell Options

Expands to the current option flags as a single string:

```bash
echo $-
# Example output: himBH
```

Each letter represents an enabled option:
- `h` - hashall (command hashing)
- `i` - interactive shell
- `m` - monitor (job control)
- `B` - brace expansion
- `H` - history expansion
- etc.

**The `-i` option** is always included in `$-` if the shell is interactive, regardless of whether it was explicitly set.

**Whether `-c` and `-s` are included is unspecified** (shell-dependent).

**Common use - checking if shell is interactive:**

```bash
case $- in
    *i*)
        # Interactive shell
        echo "Running interactively"
        ;;
    *)
        # Non-interactive (script)
        echo "Running as script"
        ;;
esac
```

**Other uses:**
- Checking if specific options are enabled
- Debugging shell configuration
- Conditional behavior based on options

### `$$` - Process ID of Current Shell

Expands to the process ID (PID) of the shell itself:

```bash
echo "My PID is $$"
# Output: My PID is 12345
```

**Important**: In a subshell, `$$` expands to the **parent shell's PID**, not the subshell's PID:

```bash
echo "Parent PID: $$"
(echo "Subshell PID: $$")    # Same value!
# Both print the same PID
```

This is different from how most people expect it to work, but it's by design—`$$` is consistent throughout a shell session.

**Common uses:**

1. **Creating unique temporary files:**
   ```bash
   tmpfile="/tmp/script.$$"
   echo "data" > "$tmpfile"
   # File like /tmp/script.12345
   ```

2. **Unique identifiers:**
   ```bash
   logfile="process.$$.log"
   lockfile="/var/lock/myapp.$$"
   ```

3. **Debugging:**
   ```bash
   echo "Debug: PID $$ at line $LINENO"
   ```

### `$!` - Process ID of Last Background Command

Expands to the process ID of the most recent background command:

```bash
long_command &
echo "Background PID: $!"
# Wait for it specifically
wait $!
```

**What it tracks:**
- The most recent asynchronous command (one ending in `&`)
- Or, if using job control with `bg`, the most recent resumed job

**Example - monitoring background job:**

```bash
# Start background job
sleep 100 &
bg_pid=$!

echo "Started background job: $bg_pid"

# Check if still running
if kill -0 $bg_pid 2>/dev/null; then
    echo "Job $bg_pid is still running"
fi

# Wait for it
wait $bg_pid
echo "Job completed with status: $?"
```

**Example - multiple background jobs:**

```bash
job1 &
pid1=$!

job2 &
pid2=$!

job3 &
pid3=$!

# Wait for all
wait $pid1
wait $pid2
wait $pid3
```

**Note**: If there are no background jobs, or if job control is disabled, `$!` might be unset or empty.

### `$0` - Shell or Script Name

Expands to the name of the shell or shell script:

```bash
echo "Script name: $0"
```

**How it's derived:**

1. **For scripts**: Usually the path used to invoke the script:
   ```bash
   $ ./myscript.sh
   # $0 = ./myscript.sh
   
   $ /full/path/to/myscript.sh
   # $0 = /full/path/to/myscript.sh
   ```

2. **For shells**: The shell's name (like `-bash` for login shells):
   ```bash
   $ bash
   $ echo $0
   # bash
   ```

**Common uses:**

1. **Usage messages:**
   ```bash
   if [ $# -eq 0 ]; then
       echo "Usage: $0 <file>"
       exit 1
   fi
   ```

2. **Getting script directory:**
   ```bash
   script_dir=$(dirname "$0")
   cd "$script_dir"
   ```

3. **Getting script name without path:**
   ```bash
   script_name=$(basename "$0")
   echo "Running: $script_name"
   ```

**Note**: `$0` is technically a special parameter, not a positional parameter, even though it looks like one. That's why `$#` doesn't count it, and why some behaviors (like `${00}`) are unspecified.

## Working with Parameters: Practical Examples

### Example 1: Argument Validation

```bash
#!/bin/bash

if [ $# -ne 2 ]; then
    echo "Usage: $0 <source> <destination>"
    exit 1
fi

source="$1"
dest="$2"

if [ ! -f "$source" ]; then
    echo "Error: $source does not exist"
    exit 1
fi

cp "$source" "$dest"
echo "Copied $source to $dest"
```

### Example 2: Processing All Arguments

```bash
#!/bin/bash

echo "Processing $# arguments..."

# Wrong way - loses spaces
for arg in $@; do
    echo "Arg: $arg"
done

# Right way - preserves spaces
for arg in "$@"; do
    echo "Arg: $arg"
done

# Or with a counter
count=1
for arg in "$@"; do
    echo "Argument $count: $arg"
    count=$((count + 1))
done
```

### Example 3: Passing Arguments Through

```bash
#!/bin/bash
# Wrapper script that adds logging

log() {
    echo "[$(date)] $*" >> /tmp/wrapper.log
}

# Log the command
log "Running command with $# arguments"

# Pass all arguments to the real command
real_command "$@"

# Log the result
log "Command exited with status $?"
```

### Example 4: Using Exit Status

```bash
#!/bin/bash

if command1; then
    echo "Command1 succeeded"
    command2
    if [ $? -ne 0 ]; then
        echo "Command2 failed!"
        exit 1
    fi
else
    echo "Command1 failed with status $?"
    exit 1
fi

echo "All commands succeeded"
```

### Example 5: Background Job Management

```bash
#!/bin/bash

# Start multiple background jobs
long_job1 &
pid1=$!

long_job2 &
pid2=$!

echo "Started jobs $pid1 and $pid2"

# Do other work...

# Wait for both
echo "Waiting for $pid1..."
wait $pid1
status1=$?

echo "Waiting for $pid2..."
wait $pid2
status2=$?

if [ $status1 -eq 0 ] && [ $status2 -eq 0 ]; then
    echo "All jobs succeeded"
else
    echo "Some jobs failed"
    exit 1
fi
```

## Common Pitfalls

### Pitfall 1: Unquoted `$@`

```bash
# WRONG - spaces in arguments are lost
copy_files() {
    cp $@ /destination/
}

# RIGHT - preserves arguments exactly
copy_files() {
    cp "$@" /destination/
}
```

### Pitfall 2: Using `$*` Instead of `$@`

```bash
# WRONG - all args become one
wrapper() {
    command "$*"    # Passes all args as single argument
}

# RIGHT - args stay separate
wrapper() {
    command "$@"    # Each arg stays separate
}
```

### Pitfall 3: Forgetting Braces for ${10}

```bash
#!/bin/bash
# WRONG
echo "Tenth arg: $10"      # Prints $1 followed by "0"

# RIGHT
echo "Tenth arg: ${10}"    # Prints tenth argument
```

### Pitfall 4: Not Saving `$?` Early Enough

```bash
# WRONG
command1
command2
if [ $? -eq 0 ]; then      # Tests command2's status, not command1's!
    echo "Success"
fi

# RIGHT
command1
status=$?                   # Save it immediately
command2
if [ $status -eq 0 ]; then # Now tests command1's status
    echo "Success"
fi
```

### Pitfall 5: Expecting `$$` to Change in Subshells

```bash
# Surprising behavior
echo "Parent: $$"
(echo "Subshell: $$")      # Same value!

# If you need the subshell's PID, use:
(echo "Subshell PID: $BASHPID")   # bash-specific
```

## Best Practices

1. **Always quote `"$@"`**: This preserves arguments exactly as passed
   ```bash
   wrapper "$@"
   ```

2. **Save `$?` immediately**: If you need to check it later
   ```bash
   command
   status=$?
   ```

3. **Use braces for clarity**: Even when not required
   ```bash
   echo "${1}"    # Clear it's a positional parameter
   ```

4. **Validate argument counts**:
   ```bash
   if [ $# -lt 1 ]; then
       echo "Usage: $0 <arg>"
       exit 1
   fi
   ```

5. **Use `$0` in usage messages**: Shows users how they invoked the script
   ```bash
   echo "Usage: $0 [options] <file>"
   ```

6. **Be careful with `$*`**: Only use when you specifically want joining behavior

7. **Save `$!` if you'll need it**: Background PIDs can get overwritten
   ```bash
   job &
   job_pid=$!
   ```

## Summary: Quick Reference

| Parameter | Expands To | Common Use |
|-----------|------------|------------|
| `$1`, `$2`, ... | Positional arguments | Script/function arguments |
| `${10}`, `${11}`, ... | Multi-digit positions | Beyond 9th argument |
| `$@` | All arguments (separate fields) | Passing args through |
| `$*` | All arguments (single field) | Joining args to string |
| `$#` | Number of arguments | Validating arg count |
| `$?` | Last command exit status | Error checking |
| `$-` | Current shell options | Checking if interactive |
| `$$` | Current shell PID | Unique file names |
| `$!` | Last background job PID | Managing background jobs |
| `$0` | Script/shell name | Usage messages |

## Availability: Parameters Across Different Builds

Parameters are fundamental to shell operation and are available in all builds, though some special parameters may have limited utility depending on the build's capabilities.

### POSIX API Build (Full Parameter Support)

When built with the POSIX API, the shell provides **complete support for all parameters**:

- ✅ All positional parameters (`$1`, `$2`, ..., `${10}`, etc.)
- ✅ All special parameters (`$@`, `$*`, `$#`, `$?`, `$-`, `$$`, `$!`, `$0`)
- ✅ Full behavior as specified (including `"$@"` preservation)
- ✅ Process IDs (`$$`, `$!`) work correctly
- ✅ Exit statuses (`$?`) accurate for all commands
- ✅ Background job tracking (`$!`) with job control

All features work exactly as described.

### UCRT API Build (Full Parameter Support)

When built with the Universal C Runtime (UCRT) API on Windows, the shell provides **full parameter support**. Parameters are a core parsing and execution feature independent of OS APIs.

- ✅ All positional parameters work identically
- ✅ All special parameters work
- ✅ `"$@"` preserves arguments correctly
- ✅ `$$` returns process ID
- ✅ `$!` returns background job PIDs
- ✅ `$?` returns exit statuses

**Platform-specific notes:**

1. **Process IDs (`$$`, `$!`)**: Windows PIDs work the same conceptually:
   ```bash
   echo "My PID: $$"
   job &
   echo "Background PID: $!"
   ```

2. **Exit codes (`$?`)**: Windows exit codes are the same range (0-255), with 0 meaning success

3. **Options (`$-`)**: Shell options work the same, though some POSIX-specific options might not exist

**Practical outcome**: Parameters work identically to POSIX. No special considerations needed.

### ISO C Build (Full Parameter Support with Limitations)

When built with only ISO C standard library functions, the shell provides **full positional and most special parameter support**, but some special parameters have limited utility.

**What works fully:**
- ✅ All positional parameters (`$1`, `$2`, etc.)
- ✅ `$@` and `$*` for accessing arguments
- ✅ `$#` for argument count
- ✅ `$?` for exit status
- ✅ `$0` for script name
- ✅ `$$` for process ID (via `getpid()` if available, or fallback)

**What has limited utility:**
- ⚠️ `$!` - Background job PID
  - No background jobs in ISO C build
  - Parameter exists but is always unset
  
- ⚠️ `$-` - Shell options
  - Fewer options available in ISO C build
  - Still works for available options

**Practical implications:**

Positional and most special parameters work normally:

```bash
#!/bin/sh
# Works fine in ISO C build
echo "Script: $0"
echo "Argument 1: $1"
echo "Argument 2: $2"
echo "Total arguments: $#"

# Process all arguments
for arg in "$@"; do
    echo "Processing: $arg"
done

# Check exit status
command
if [ $? -eq 0 ]; then
    echo "Success"
fi
```

But background-related parameters don't work:

```bash
# Doesn't work in ISO C build (no background jobs)
command &
echo "Background PID: $!"    # $! is unset/empty
```

**Why the limitations?**

- `$!` requires background job support (not in ISO C)
- Other parameters are basic shell features that work regardless

### Summary Table

| Parameter | POSIX | UCRT | ISO C |
|-----------|-------|------|-------|
| Positional (`$1`, `$2`, ...) | ✅ Full | ✅ Full | ✅ Full |
| Multi-digit (`${10}`, ...) | ✅ Yes | ✅ Yes | ✅ Yes |
| `$@` | ✅ Full | ✅ Full | ✅ Full |
| `$*` | ✅ Full | ✅ Full | ✅ Full |
| `$#` | ✅ Yes | ✅ Yes | ✅ Yes |
| `$?` | ✅ Full | ✅ Full | ✅ Full |
| `$-` | ✅ Full | ✅ Full | ⚠️ Limited options |
| `$$` | ✅ Full | ✅ Full | ✅ Full |
| `$!` | ✅ Full | ✅ Full | ❌ No background jobs |
| `$0` | ✅ Full | ✅ Full | ✅ Full |
| `"$@"` preservation | ✅ Yes | ✅ Yes | ✅ Yes |

### Choosing Your Build for Parameters

**Good news**: Parameters work consistently across all builds for the most common use cases!

**All builds fully support:**
- Positional parameters for scripts and functions
- `"$@"` for passing arguments correctly
- `$#` for validating argument counts
- `$?` for checking exit statuses
- `$0` for usage messages

**Only limitation:**
- ISO C build doesn't support `$!` (no background jobs)

For standard shell scripting with arguments, all builds are equivalent. The differences only appear with advanced features like background job management.

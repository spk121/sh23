# Simple Commands

A **simple command** is the most basic executable unit in the shell—it's what you use every time you run a program, assign a variable, or redirect I/O. Despite the name, simple commands have complex behavior involving variable assignments, redirections, expansions, and command lookup.

Understanding how simple commands work is essential for knowing when variables persist, where redirections happen, and how the shell finds and executes commands.

## What Is a Simple Command?

A simple command consists of:
1. **Optional variable assignments** - Setting variables before the command
2. **Optional redirections** - Changing input/output
3. **Optional command name and arguments** - The actual command to run

These can appear in any order (though typically you see assignments and redirections before the command name).

Examples:
```bash
# Just a command
ls

# Command with arguments
ls -la /home

# With variable assignment
VAR=value command

# With redirection
command >output.txt

# All together
VAR=value command arg1 arg2 >output.txt 2>&1
```

## Order of Processing

When the shell executes a simple command, it follows these steps:

### Step 1: Save Variable Assignments and Redirections

The shell identifies which words are variable assignments or redirections and saves them for later processing.

```bash
VAR=value OTHER=data command arg1 >output.txt
# Saves: VAR=value, OTHER=data, >output.txt
```

### Step 2: Find the Command Name

The shell expands words (in order) until it finds a command name:

```bash
# First non-assignment word becomes command name
VAR=x echo hello
# Command name: echo

# If word expands to nothing, try next word
"" ls -l
# Empty string expands away, "ls" becomes command name
```

**Special handling for declaration utilities**: If the command is recognized as a declaration utility (like `export`, `readonly`, `local` in some shells), subsequent variable-assignment-looking words get special expansion treatment.

### Step 3: Perform Redirections

All redirections are executed in the order they appear:

```bash
command >file.txt 2>&1
# First: stdout → file.txt
# Second: stderr → stdout (which goes to file.txt)
```

### Step 4: Expand Variable Assignments

Each variable assignment undergoes expansion:
- Tilde expansion
- Parameter expansion
- Command substitution
- Arithmetic expansion
- Quote removal

```bash
VAR=~/dir           # Tilde expanded
VAR=$HOME/dir       # Parameter expanded
VAR=$(pwd)          # Command substitution
VAR=$((2 + 2))      # Arithmetic expansion
```

**Note**: Steps 3 and 4 may be reversed if there's no command name or if the command is a special built-in.

### Step 5: Execute the Command

Based on what the command name is, different things happen (see "Command Search and Execution" below).

## Variable Assignment Behavior

How variable assignments behave depends on what follows them:

### No Command Name

If there's no command after the assignments, they **affect the current shell**:

```bash
VAR=value
echo $VAR           # Prints: value

VAR=hello OTHER=world
echo "$VAR $OTHER"  # Prints: hello world
```

### External Command or Non-Special Built-in

Assignments are **exported to the command** but **don't affect the current shell**:

```bash
VAR=temp env | grep VAR
# VAR is visible to 'env' command

echo $VAR
# VAR is not set in current shell (empty output)
```

These are called "environment assignments" or "command-local assignments".

### Special Built-in Utility

Assignments **affect the current shell** and persist after the command:

```bash
VAR=value set
echo $VAR           # Prints: value (persists!)
```

Special built-ins include: `:`, `.`, `break`, `continue`, `eval`, `exec`, `exit`, `export`, `readonly`, `return`, `set`, `shift`, `times`, `trap`, `unset`

### Function (Non-Standard Utility)

Behavior is **unspecified**—assignments may or may not persist after the function completes:

```bash
myfunc() {
    echo "In function: $VAR"
}

VAR=test myfunc
echo "After function: $VAR"  # May or may not print "test"
```

For portability, don't rely on persistence.

### Readonly Variables

Attempting to assign to a readonly variable is an error:

```bash
readonly VAR=original
VAR=new command     # ERROR: cannot assign to readonly variable
```

## Commands with No Command Name

If the command has no command name after expansion, special rules apply:

### Redirections Only

Redirections are performed in a **subshell**:

```bash
>file.txt           # Creates file in subshell
# Current shell unaffected
```

This prevents the current shell's file descriptors from being changed.

**Important**: This is a subshell, so variable changes don't persist:
```bash
>file.txt VAR=value
echo $VAR           # Empty - VAR was set in subshell
```

### With Command Substitution

The command completes with the exit status of the last command substitution:

```bash
$(exit 42)
echo $?             # Prints: 42
```

### Neither

If there are no redirections and no command substitutions, exit status is 0:

```bash
VAR=value
echo $?             # Prints: 0
```

## Command Search and Execution

When a command name is found, the shell searches for it in this order:

### 1. Special Built-in Utilities

If the command name matches a special built-in, it's invoked immediately:

```bash
set -x              # 'set' is a special built-in
export VAR=value    # 'export' is a special built-in
```

**Special built-ins**: `:`, `.`, `break`, `continue`, `eval`, `exec`, `exit`, `export`, `readonly`, `return`, `set`, `shift`, `times`, `trap`, `unset`

**Why they're special**:
- Variable assignments persist
- Errors in special built-ins can cause scripts to exit
- They can't be overridden by functions or executables in PATH

### 2. Shell Functions

If the name matches a defined function, the function is invoked:

```bash
greet() {
    echo "Hello, $1!"
}

greet World         # Calls the function
```

**Note**: Standard utilities implemented as functions are found later in PATH search.

### 3. Intrinsic Utilities

Some shells have "intrinsic" utilities—built-ins that are always available.

### 4. PATH Search

The shell searches directories in the `PATH` variable:

```bash
PATH=/usr/local/bin:/usr/bin:/bin
ls                  # Searches PATH for 'ls'
```

**How it works**:
1. Split PATH on `:` (colon)
2. Check each directory for the command
3. First match wins

**Command caching**: The shell may remember where it found a command and skip searching again (until PATH changes).

**Search failure**: If the command isn't found, exit status 127 and error message:
```bash
nonexistent_command
# sh: nonexistent_command: command not found
# Exit status: 127
```

### 5. Absolute or Relative Path

If the command name contains a `/` (slash), it's treated as a path:

```bash
/bin/ls             # Absolute path
./script.sh         # Relative path
../bin/command      # Relative path
```

The shell executes the file directly without searching PATH.

## How Commands Are Executed

### Built-ins

Built-in commands run in the current shell process—no new process is created:

```bash
cd /tmp             # Changes current shell's directory
VAR=value           # Sets variable in current shell
```

### External Commands

External commands run in a **separate utility environment** (basically a child process):

```bash
ls                  # Runs in child process
echo hello          # If 'echo' is external, runs in child process
```

**What's copied to the child**:
- Environment variables (exported variables)
- File descriptors (open files)
- Current directory
- umask, signal handlers, etc.

**What's NOT copied**:
- Shell variables (unless exported)
- Functions
- Aliases
- Shell options

### The `exec` Command

The `exec` special built-in **replaces** the current shell with the new command:

```bash
exec command        # Current shell is replaced
# Anything after this won't execute
```

No new process is created—the new command takes over the current process.

## Script Execution (`[ENOEXEC]` Handling)

If the shell tries to execute a file that isn't a binary executable, it assumes it's a shell script:

```bash
./myscript
# If not a binary, shell runs: sh ./myscript
```

The shell invokes a new shell instance to run the script.

**Heuristic check**: The shell may check if the file could be a script (e.g., looking for null bytes). If it decides the file can't be a script, it fails with exit status 126.

## Standard File Descriptors

If a command would run with stdin (fd 0), stdout (fd 1), or stderr (fd 2) closed, the shell may open them to an unspecified file.

**Why**: Many commands assume these are open. Closed standard FDs can cause unpredictable behavior.

## Practical Examples

### Example 1: Temporary Environment Variable

```bash
# Set variable only for one command
DEBUG=1 myprogram

# Variable not set in current shell
echo $DEBUG         # Empty
```

### Example 2: Multiple Assignments

```bash
# Multiple variable assignments
USER=admin LEVEL=5 command

# Equivalent to:
env USER=admin LEVEL=5 command
```

### Example 3: Variable Assignment with Special Built-in

```bash
# Assignment persists with special built-in
OLDPATH=$PATH export PATH=/new/path

echo $OLDPATH       # Has old PATH value
echo $PATH          # Has new path
```

### Example 4: Redirection Without Command

```bash
# Create empty file in subshell
>newfile.txt

# Redirect and assign (in subshell)
>output.txt VAR=data
echo $VAR           # Empty (VAR set in subshell)
```

### Example 5: Function vs. External Command

```bash
# Define function
ls() {
    echo "Custom ls"
    command ls "$@"     # Call external ls
}

ls                  # Calls function
/bin/ls             # Calls external ls (bypass function)
command ls          # Calls external ls (bypass function)
```

### Example 6: PATH Search

```bash
# Find where command is located
which ls            # Might print: /bin/ls

# Search order demonstration
echo() {
    builtin echo "Custom echo: $*"
}

echo test           # Uses function
/bin/echo test      # Uses external command
```

### Example 7: Script Detection

```bash
#!/bin/bash
# If this script has #!/bin/bash, shell runs it with bash

# If no shebang and executed:
./myscript
# Shell sees it's not binary, runs: sh ./myscript
```

## Common Pitfalls

### Pitfall 1: Expecting Assignment to Persist

```bash
# WRONG - expecting VAR to be set
VAR=value ls
echo $VAR           # Empty (assignment was local to 'ls')

# RIGHT - if you want to set VAR
VAR=value
ls
echo $VAR           # Prints: value
```

### Pitfall 2: Assignment Order with Special Built-ins

```bash
# These are different:
VAR=$HOME export VAR    # VAR set to $HOME at assignment time
export VAR=$HOME        # VAR set to $HOME at export time
```

### Pitfall 3: Forgetting Redirections Create Subshells

```bash
# WRONG - expecting VAR to be set
>file.txt VAR=value
echo $VAR           # Empty (VAR set in subshell)

# RIGHT
VAR=value
>file.txt
```

### Pitfall 4: Function Shadowing Commands

```bash
# Define function with same name as command
ls() {
    echo "Not really ls"
}

ls                  # Calls function, not /bin/ls

# Bypass function:
command ls          # Runs external ls
/bin/ls             # Runs external ls
\ls                 # Runs external ls
```

### Pitfall 5: Assuming PATH Order

```bash
# If PATH=/usr/local/bin:/usr/bin
# and command exists in both places
command             # Runs /usr/local/bin/command (first in PATH)

# To run specific version:
/usr/bin/command    # Use full path
```

### Pitfall 6: Readonly Variable Errors

```bash
readonly VAR=constant
VAR=new command     # ERROR: stops execution
```

## Best Practices

1. **Export variables if commands need them**:
   ```bash
   export DB_HOST=localhost
   myapp                    # Can see DB_HOST
   ```

2. **Use temporary assignments for one-off changes**:
   ```bash
   DEBUG=1 command          # DEBUG only for this command
   ```

3. **Be explicit about persistence**:
   ```bash
   # Want persistent:
   VAR=value

   # Want temporary:
   VAR=value command
   ```

4. **Use `command` to bypass functions**:
   ```bash
   # If function shadows 'ls':
   command ls               # Runs external ls
   ```

5. **Check command existence**:
   ```bash
   if command -v mycommand >/dev/null 2>&1; then
       mycommand
   else
       echo "mycommand not found" >&2
       exit 1
   fi
   ```

6. **Use full paths for critical commands**:
   ```bash
   /bin/rm -rf /tmp/data    # Explicit path
   ```

7. **Handle missing commands gracefully**:
   ```bash
   if ! mycommand; then
       echo "Error: mycommand failed" >&2
       exit 1
   fi
   ```

## Availability: Simple Commands Across Different Builds

Simple command execution varies significantly by build, particularly for external commands.

### POSIX API Build (Full Simple Command Support)

- ✅ All variable assignment behaviors work
- ✅ Full command search (special built-ins, functions, PATH)
- ✅ External command execution via `fork()` and `exec()`
- ✅ Environment export works completely
- ✅ Script detection and execution (`[ENOEXEC]` handling)
- ✅ Subshell for redirection-only commands
- ✅ All built-in utilities available

**Full functionality as specified.**

### UCRT API Build (Full Simple Command Support)

- ✅ All variable assignment behaviors work
- ✅ Full command search
- ✅ External command execution via `_spawnvp()` or `CreateProcess()`
- ✅ Environment export works

**Implementation differences but same behavior**:

1. **Process creation**: Uses `_spawnvp()` or `CreateProcess()` instead of `fork()/exec()`
2. **PATH separator**: Semicolon (`;`) on Windows instead of colon (`:`)
3. **Executable extensions**: May search for `.exe`, `.cmd`, `.bat` files
4. **Script execution**: May use Windows association for `.sh` files

**Practical outcome**: Works similarly to POSIX from user's perspective.

**Windows-specific notes**:

```bash
# PATH on Windows
PATH="C:\Windows;C:\Program Files"   # Or shell may normalize

# Executable search
command              # Might find command.exe
myscript             # Might need myscript.sh or myscript.bat
```

### ISO C Build (Limited Simple Command Support)

- ✅ Variable assignment works
- ✅ Built-in commands work
- ⚠️ External command execution is **very limited**

**What works**:

```bash
# Variable assignments
VAR=value
echo $VAR            # Works if echo is built-in

# Built-in commands
cd /tmp              # If cd is built-in
```

**What's severely limited**:

1. **External command execution**: ISO C only has `system()`
   - `system()` is very limited
   - No direct process control
   - No ability to manipulate environment for child
   - No proper exit status in some implementations

   ```bash
   ls                # Calls system("ls")
   # But: limited control, may not work as expected
   ```

2. **Environment export**:
   - `putenv()` might be available, but it's optional
   - Child processes may not see environment changes

   ```bash
   VAR=value command  # May not export properly
   ```

3. **PATH search**:
   - No standardized PATH handling
   - `system()` might search PATH, or might not

   ```bash
   command            # Whether PATH is searched depends on system()
   ```

4. **Subshells**:
   - No `fork()` to create subshells
   - Redirection-only commands may not work correctly

   ```bash
   >file.txt          # May fail (needs subshell)
   ```

5. **Script execution**:
   - No `exec()` family functions
   - Can't directly execute scripts
   - Might work if OS associates `.sh` with a shell

   ```bash
   ./script.sh        # May or may not work
   ```

**What you can do in ISO C build**:

```bash
# Basic built-ins
cd /directory        # If implemented
export VAR=value     # If export built-in exists
echo "$VAR"          # If echo is built-in

# Variable manipulation
VAR=value
OTHER=$VAR
echo "$OTHER"

# Some commands via system()
# (but behavior is unpredictable)
command              # Might work if system() succeeds
```

**What you can't reliably do**:

```bash
# External commands (uncertain)
ls                   # May work, may not

# Environment export (uncertain)
VAR=value command    # May not export

# Pipelines (needs fork/pipe)
command1 | command2  # Doesn't work

# Background jobs (needs fork)
command &            # Doesn't work

# Command substitution (needs fork/pipe)
result=$(command)    # Doesn't work
```

### Summary Table

| Feature | POSIX | UCRT | ISO C |
|---------|-------|------|-------|
| Variable assignment | ✅ Full | ✅ Full | ✅ Full |
| Built-in commands | ✅ Full | ✅ Full | ✅ Full |
| External commands | ✅ Full | ✅ Full | ⚠️ Limited (`system()`) |
| Environment export | ✅ Full | ✅ Full | ❓ Uncertain |
| PATH search | ✅ Full | ✅ Full | ❓ Uncertain |
| Script execution | ✅ Full | ✅ Full | ❌ Limited |
| Subshells | ✅ Full | ✅ Full | ❌ No |
| Special built-in behavior | ✅ Full | ✅ Full | ✅ Full |
| Function execution | ✅ Full | ✅ Full | ✅ Full |
| Command caching | ✅ Yes | ✅ Yes | ❓ N/A |

### Choosing Your Build for Simple Commands

**Need full command execution?**
- Use POSIX or UCRT builds
- ISO C build is severely limited

**Maximum portability?**
- Use only built-in commands
- Avoid external commands if ISO C support is needed
- Don't rely on environment export
- Don't use subshells

**Recommendations by build:**

**POSIX/UCRT**: Use all features
```bash
VAR=value command    # Works
./script.sh          # Works
command1 | command2  # Works
command &            # Works
```

**ISO C**: Stick to basics
```bash
# Variable assignment
VAR=value

# Built-in commands only
cd /directory
echo "$VAR"

# Avoid:
VAR=value external_command  # May not work
./script.sh                 # May not work
command1 | command2         # Won't work
```

**The reality**: Simple command execution beyond built-ins is a POSIX/UCRT feature. ISO C build can only reliably run built-in commands and do basic variable manipulation. This makes ISO C unsuitable for most real-world shell scripting.
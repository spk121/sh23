# Command Substitution

**Command substitution** lets you capture the output of commands and use it as part of another command. It's one of the most powerful shell features, allowing you to build complex operations by combining simpler commands.

Instead of running a command and manually copying its output, command substitution does it automatically—the command runs, its output replaces the substitution, and the shell continues processing.

## The Two Forms

There are two ways to write command substitution:

### Modern Form: `$(commands)`

The recommended form uses `$()`:

```bash
current_date=$(date)
echo "Today is $current_date"

file_count=$(ls | wc -l)
echo "Found $file_count files"
```

### Legacy Form: `` `commands` ``

The older form uses backticks:

```bash
current_date=`date`
echo "Today is $current_date"
```

**Recommendation**: Use `$()` instead of backticks. It's clearer, nests better, and has simpler quoting rules.

## How It Works

When the shell encounters command substitution:

1. **Executes the commands** in a subshell environment
2. **Captures the standard output** of those commands
3. **Replaces the entire substitution** (including the `$()` or backticks) with that output
4. **Strips trailing newlines** from the output
5. **Continues processing** with the substituted text

Example step-by-step:
```bash
echo "Files: $(ls)"

# Step 1: Shell sees $(ls)
# Step 2: Runs 'ls' in subshell
# Step 3: Captures output: "file1.txt\nfile2.txt\n"
# Step 4: Strips trailing newline: "file1.txt\nfile2.txt"
# Step 5: Replaces: echo "Files: file1.txt\nfile2.txt"
# Step 6: Executes the echo command
```

## Output Processing

### Trailing Newlines Are Removed

All trailing newlines at the end of the output are removed:

```bash
result=$(echo "hello")        # Output: "hello\n"
# Result: "hello" (newline removed)

result=$(echo -e "a\nb\nc")   # Output: "a\nb\nc\n"
# Result: "a\nb\nc" (trailing newline removed)

result=$(printf "text\n\n\n") # Output: "text\n\n\n"
# Result: "text" (all trailing newlines removed)
```

### Internal Newlines Are Kept

Newlines **within** the output are preserved:

```bash
result=$(echo -e "line1\nline2\nline3")
echo "$result"
# Prints:
# line1
# line2
# line3
```

**Important**: Quote the variable to preserve the newlines. Without quotes, field splitting can remove them:

```bash
result=$(echo -e "line1\nline2")

# Unquoted - field splitting happens
echo $result              # Prints: line1 line2 (one line)

# Quoted - newlines preserved
echo "$result"            # Prints: line1
                          #         line2
```

### Null Bytes Are Undefined

If the output contains null bytes (byte value 0), behavior is unspecified. Don't rely on handling binary data with command substitution.

## The Subshell Environment

Commands in a substitution run in a **subshell**—a copy of the current shell:

```bash
var="parent"
result=$(var="child"; echo $var)
echo "Result: $result"    # Prints: Result: child
echo "Var: $var"          # Prints: Var: parent
```

The child's changes don't affect the parent. This means:
- Variable assignments in the substitution don't persist
- Directory changes don't persist
- The substitution can't modify the parent shell's state

Example showing isolation:
```bash
cd /home
result=$(cd /tmp; pwd)
echo "Subshell was in: $result"  # /tmp
echo "Parent still in: $(pwd)"   # /home
```

## Using Command Substitution

### In Variable Assignments

```bash
current_user=$(whoami)
home_dir=$(echo $HOME)
file_count=$(ls -1 | wc -l)
```

### In Command Arguments

```bash
echo "Today is $(date)"
grep "pattern" $(find . -name "*.txt")
tar -czf backup-$(date +%Y%m%d).tar.gz files/
```

### In Conditionals

```bash
if [ "$(whoami)" = "root" ]; then
    echo "Running as root"
fi

if [ $(ls | wc -l) -gt 10 ]; then
    echo "More than 10 files"
fi
```

### Nested Substitution

Command substitutions can be nested:

```bash
# With $() - easy to nest
result=$(echo "Inner: $(date)")

# Deeply nested
outer=$(echo "Outer: $(echo "Middle: $(echo "Inner")")")
```

With backticks, you need to escape inner ones:
```bash
# With backticks - must escape
result=`echo "Inner: \`date\`"`
```

This is why `$()` is preferred—no escaping needed.

## Quoting and Command Substitution

### Quoting the Substitution

You can quote the entire substitution:

```bash
# Without quotes - field splitting happens
files=$(ls)
echo $files               # One line, spaces between names

# With quotes - output preserved exactly
files=$(ls)
echo "$files"             # Multiple lines preserved
```

**Best practice**: Quote command substitutions when you want to preserve their exact output (especially whitespace and newlines).

### Quoting Inside the Substitution

Quoting works normally inside the substitution:

```bash
result=$(echo "hello world")      # Quotes work inside
result=$(grep "pattern" file)     # Quotes work inside
```

### Backtick Special Case

With backticks, backslash handling is complex:

**Outside double quotes**, backslash is literal except before:
- `$` (dollar sign)
- `` ` `` (backtick)
- `\` (backslash)

**Inside double quotes**, backslash follows double-quote rules.

Example:
```bash
result=`echo \$HOME`      # Prints: $HOME (backslash escapes $)
result=`echo \\`          # Prints: \ (backslash escapes backslash)
```

With `$()`, this is simpler—quoting works normally.

## Parsing Command Substitutions

### The `$()` Form

Everything between `$(` and the matching `)` is the commands string:

```bash
result=$(echo "hello")
result=$(if [ -f file ]; then echo "exists"; fi)
result=$(
    # Can span multiple lines
    echo "line 1"
    echo "line 2"
)
```

The shell properly handles nested structures:
```bash
result=$(echo "$(nested)")        # Handles nested $()
result=$(echo $((2 + 2)))         # Handles arithmetic $(())
result=$(case $var in a) echo "a" ;; esac)  # Handles case
```

### The Backtick Form

Finding the matching backtick is more complex. Undefined results occur if the closing backtick is in:
- A shell comment
- A here-document
- An embedded `$()` substitution
- A quoted string that doesn't end

Example of undefined behavior:
```bash
# Undefined - backtick in comment
result=`echo "test" # comment with backtick: ` unclear`

# Undefined - unclosed quote
result=`echo "test`
```

**Another reason to prefer `$()`**: simpler parsing rules.

### Special Parsing Cases

**Case 1**: Substitution can contain any valid shell syntax:
```bash
result=$(
    for i in 1 2 3; do
        echo $i
    done
)
```

**Case 2**: Substitution containing only redirections has unspecified results:
```bash
result=$(> file)          # Unspecified
```

**Case 3**: Aliases might not work as expected:
```bash
alias myalias='commands'
result=$(myalias)         # May or may not work
```

The commands string might be parsed as a single block (where aliases don't take effect) or incrementally (where they might).

**Portable code**: Don't rely on aliases in command substitutions.

## The `$((` Ambiguity

There's a parsing ambiguity with `$((`:
- Could be arithmetic expansion: `$((2 + 2))`
- Could be command substitution starting with subshell: `$((commands))`

**The shell's rule**: Arithmetic expansion takes precedence. The shell tries to parse it as arithmetic first.

If you want command substitution with a subshell, **add a space**:
```bash
# Arithmetic (precedence)
echo $((2 + 2))           # Prints: 4

# Command substitution with subshell - WRONG
echo $((echo "test"))     # Tries to parse as arithmetic, fails

# Command substitution with subshell - RIGHT
echo $(( echo "test" ))   # Space after $(( makes it clear
echo $( (echo "test") )   # Alternative: space after $(
```

## No Further Expansion

The output of command substitution is **not** expanded further:

```bash
# Create file with a tilde
echo '~' > file

# Read it back
content=$(cat file)
echo "$content"           # Prints: ~ (not expanded to home)

# Create file with a variable reference
echo '$HOME' > file
content=$(cat file)
echo "$content"           # Prints: $HOME (not expanded)
```

This is important: the substitution output is treated as literal text for:
- Tilde expansion (doesn't happen)
- Parameter expansion (doesn't happen)
- Command substitution (doesn't happen)
- Arithmetic expansion (doesn't happen)

However, the output **is** subject to:
- Field splitting (if unquoted)
- Pathname expansion (if unquoted and contains wildcards)

## Practical Examples

### Example 1: Getting System Information

```bash
#!/bin/bash
hostname=$(hostname)
kernel=$(uname -r)
uptime=$(uptime)

echo "System: $hostname"
echo "Kernel: $kernel"
echo "Uptime: $uptime"
```

### Example 2: Processing File Lists

```bash
#!/bin/bash
# Count files
total=$(ls -1 | wc -l)
txt_files=$(ls -1 *.txt 2>/dev/null | wc -l)

echo "Total files: $total"
echo "Text files: $txt_files"
```

### Example 3: Building Dynamic Commands

```bash
#!/bin/bash
# Find and process files
for file in $(find . -name "*.log"); do
    echo "Processing: $file"
    # Do something with $file
done
```

**Note**: This is unsafe for filenames with spaces. Better approach:
```bash
find . -name "*.log" | while read -r file; do
    echo "Processing: $file"
done
```

### Example 4: Date-Based Filenames

```bash
#!/bin/bash
backup_file="backup-$(date +%Y%m%d-%H%M%S).tar.gz"
log_file="app-$(date +%Y-%m-%d).log"

echo "Creating: $backup_file"
tar -czf "$backup_file" files/
```

### Example 5: Conditional Logic

```bash
#!/bin/bash
if [ "$(whoami)" != "root" ]; then
    echo "Error: must run as root" >&2
    exit 1
fi

if [ $(df / | tail -1 | awk '{print $5}' | sed 's/%//') -gt 90 ]; then
    echo "Warning: disk usage above 90%"
fi
```

### Example 6: Nested Substitutions

```bash
#!/bin/bash
# Get newest file's modification time
newest=$(ls -t | head -1)
mod_time=$(stat -c %Y "$newest")
readable_time=$(date -d @$mod_time)

echo "Newest file: $newest"
echo "Modified: $readable_time"
```

## Common Pitfalls

### Pitfall 1: Forgetting to Quote

```bash
# WRONG - field splitting breaks filenames with spaces
for file in $(ls); do
    echo $file            # Breaks on spaces
done

# RIGHT
find . -type f | while read -r file; do
    echo "$file"
done
```

### Pitfall 2: Expecting Variable Changes to Persist

```bash
# WRONG - subshell can't modify parent
$(x=5)
echo $x                   # Empty (x was set in subshell)

# RIGHT
x=$(echo 5)               # Capture output
echo $x                   # Prints: 5
```

### Pitfall 3: Word Splitting on Output

```bash
# Command that outputs multiple lines
list=$(echo -e "a\nb\nc")

# WRONG - each line becomes separate argument
echo $list                # Prints: a b c (one line)

# RIGHT - preserve lines
echo "$list"              # Prints three lines
```

### Pitfall 4: Using Backticks with Nesting

```bash
# WRONG - confusing, hard to read
outer=`echo "Inner: \`date\`"`

# RIGHT - clear and easy
outer=$(echo "Inner: $(date)")
```

### Pitfall 5: Relying on Exit Status

```bash
# The substitution captures output, not exit status
result=$(false)
echo $?                   # Prints: 0 (echo's status, not false's)

# To get exit status, don't use substitution
if command; then
    echo "Success"
fi
```

### Pitfall 6: Binary Data

```bash
# WRONG - null bytes have undefined behavior
binary=$(cat binary_file)

# RIGHT - don't use command substitution for binary data
# Use redirection or other tools instead
```

## Best Practices

1. **Prefer `$()` over backticks**: Clearer syntax, better nesting

2. **Quote substitutions that might have spaces or newlines**:
   ```bash
   output="$(command)"
   echo "$output"
   ```

3. **Check if the command succeeded**:
   ```bash
   if output=$(command 2>&1); then
       echo "Success: $output"
   else
       echo "Failed: $output"
   fi
   ```

4. **Avoid command substitution in loops with many iterations**:
   ```bash
   # Slow - runs date command many times
   for i in {1..1000}; do
       echo "$(date): Processing $i"
   done

   # Fast - run date once
   timestamp=$(date)
   for i in {1..1000}; do
       echo "$timestamp: Processing $i"
   done
   ```

5. **Be careful with field splitting**:
   ```bash
   # If output might have spaces, quote it
   file=$(find_file)
   process "$file"          # Quoted
   ```

6. **Consider alternatives for large output**:
   ```bash
   # If output is huge, don't use command substitution
   # Instead, use redirection or process substitution
   command > tempfile
   # Process tempfile
   ```

## Availability: Command Substitution Across Different Builds

Command substitution depends on the shell's ability to create subshells and capture their output, which varies by build.

### POSIX API Build (Full Command Substitution)

- ✅ Both `$()` and backtick forms work completely
- ✅ Full subshell creation via `fork()`
- ✅ Output capture via pipes
- ✅ All syntax supported
- ✅ Nested substitutions work perfectly
- ✅ Proper newline handling

**Full functionality as specified.**

### UCRT API Build (Full Command Substitution)

- ✅ Both `$()` and backtick forms work
- ✅ Subshell creation via `_spawnvp()`
- ✅ Output capture via `_pipe()`
- ✅ All syntax supported

**Implementation differences but same functionality**:

1. **Process creation**: Uses `_spawnvp()` instead of `fork()`
2. **Pipe creation**: Uses `_pipe()` instead of `pipe()`
3. **Output capture**: Same mechanism, different API

**Practical outcome**: Works identically to POSIX from user's perspective.

**Platform considerations**:
- Windows path separators may appear in output
- Line endings may be CRLF, but typically handled transparently

```bash
# Works fine on Windows
current_dir=$(cd)
echo "Current: $current_dir"  # Shows Windows path

user=$(whoami)
echo "User: $user"            # Shows Windows username
```

### ISO C Build (No Command Substitution)

- ❌ Command substitution is **not supported**
- The syntax may be recognized but **cannot be executed**

**Why it doesn't work**:
- No `fork()` to create subshells
- No `pipe()` to capture output
- No process control to run and capture commands

**What happens**:
```bash
result=$(command)       # Either error or undefined behavior
result=`command`        # Either error or undefined behavior
```

The shell might:
- Report a syntax error
- Try to use `system()` but can't capture output
- Produce undefined results

**Workarounds are very limited**:

You could try using temporary files:
```bash
# Not supported directly
result=$(command)

# Possible workaround (ugly and unreliable)
command > /tmp/output.txt
result=$(cat /tmp/output.txt)    # But this uses command substitution too!
```

Even this doesn't work because reading the file would need command substitution.

**The fundamental problem**: ISO C provides no way to:
- Create subprocesses and capture their output
- Redirect output from processes

**Practical implication**: Scripts using command substitution **will not work** in ISO C build.

### Summary Table

| Feature | POSIX | UCRT | ISO C |
|---------|-------|------|-------|
| `$()` form | ✅ Yes | ✅ Yes | ❌ No |
| Backtick form | ✅ Yes | ✅ Yes | ❌ No |
| Subshell creation | ✅ `fork()` | ✅ `_spawnvp()` | ❌ None |
| Output capture | ✅ `pipe()` | ✅ `_pipe()` | ❌ None |
| Nested substitution | ✅ Yes | ✅ Yes | ❌ No |
| Newline handling | ✅ Yes | ✅ Yes | ❌ N/A |
| All shell syntax | ✅ Yes | ✅ Yes | ❌ N/A |

### Choosing Your Build for Command Substitution

**Need command substitution?**
- Use POSIX or UCRT builds
- ISO C build cannot support it

**Maximum portability?**
- Avoid command substitution if you need ISO C support
- Or accept that ISO C scripts must be very simple

**Alternative patterns for ISO C**:
There really aren't good alternatives. Command substitution is so fundamental that avoiding it severely limits what scripts can do.

Some things you might do:
- Hardcode values instead of computing them
- Use environment variables set externally
- Limit scripts to direct command execution

But these are workarounds for the lack of a core feature.

**Conclusion**: Command substitution is essential for any non-trivial shell script. It works fully in POSIX and UCRT builds, but is completely unavailable in ISO C. This is one of the major limitations that makes ISO C build unsuitable for general shell scripting.

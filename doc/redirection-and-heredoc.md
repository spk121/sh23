# Redirection and Here-Documents

**Redirection** is how you control where commands read their input from and where they send their output to. Instead of reading from the keyboard and writing to the screen, you can redirect to and from files, or even connect one command's output to another's input.

Understanding redirection is essential for shell scripting—it's how you process files, capture output, suppress errors, and build complex command pipelines.

## The Three Standard File Descriptors

Every command has three standard file descriptors:

- **0 - Standard Input (stdin)**: Where the command reads input
- **1 - Standard Output (stdout)**: Where the command writes normal output  
- **2 - Standard Error (stderr)**: Where the command writes error messages

By default:
- stdin reads from the keyboard
- stdout writes to the terminal
- stderr writes to the terminal

Redirection lets you change these.

## Basic Redirection Format

The general format is:

```
[n]operator word
```

Where:
- `n` is an optional file descriptor number (0-9, or higher if supported)
- `operator` is the redirection operator (`<`, `>`, `>>`, etc.)
- `word` is typically a filename (undergoes expansion)

**Important**: No space between `n` and the operator!

```bash
# Correct:
command 2>error.log

# Wrong:
command 2 >error.log      # "2" becomes an argument to command
```

## Input Redirection: `<`

Redirect input from a file instead of the keyboard.

**Format**: `[n]<word`

**Default**: File descriptor 0 (stdin)

```bash
# Read from file instead of keyboard
sort < unsorted.txt

# Explicit file descriptor (same as above)
sort 0<unsorted.txt

# Process a file
while read line; do
    echo "Line: $line"
done < input.txt
```

## Output Redirection: `>`

Redirect output to a file, creating or truncating it.

**Format**: `[n]>word`

**Default**: File descriptor 1 (stdout)

```bash
# Write to file (create or truncate)
echo "Hello" > output.txt

# Redirect stderr to file
command 2>error.log

# Redirect stdout to one file, stderr to another
command >output.log 2>error.log
```

### The Noclobber Option

If `set -C` (noclobber) is enabled, `>` **fails** if the file already exists:

```bash
set -C                     # Enable noclobber
echo "test" > existing.txt # ERROR if file exists

# Override noclobber with >|
echo "test" >| existing.txt # Works even with noclobber
```

Noclobber prevents accidentally overwriting files.

## Appending Output: `>>`

Redirect output to a file, appending to the end instead of truncating.

**Format**: `[n]>>word`

**Default**: File descriptor 1 (stdout)

```bash
# Append to file (create if doesn't exist)
echo "Line 1" >> log.txt
echo "Line 2" >> log.txt
echo "Line 3" >> log.txt

# Append errors to error log
command 2>>error.log

# Append both stdout and stderr
command >>output.log 2>&1
```

## Combining Redirections

You can redirect multiple file descriptors:

```bash
# Redirect stdout and stderr to different files
command >output.txt 2>error.txt

# Redirect stderr to stdout
command 2>&1

# Redirect both to the same file
command >file.txt 2>&1

# Append both to the same file
command >>file.txt 2>&1

# Redirect stdout to file, stderr to /dev/null
command >output.txt 2>/dev/null
```

**Order matters!** Redirections are processed left to right:

```bash
# WRONG - doesn't work as expected
command 2>&1 >file.txt
# stderr duplicates current stdout (terminal), then stdout redirects to file

# RIGHT - stderr goes to file too
command >file.txt 2>&1
# stdout redirects to file, then stderr duplicates stdout (file)
```

## Duplicating File Descriptors

### Duplicate Input: `<&`

**Format**: `[n]<&word`

Duplicate an input file descriptor or close it.

```bash
# Duplicate fd 3 to stdin
exec 0<&3

# Close stdin
exec 0<&-
```

If `word` is:
- **Digits**: Duplicate that file descriptor
- **`-`**: Close the file descriptor
- **Other**: Behavior is unspecified

### Duplicate Output: `>&`

**Format**: `[n]>&word`

Duplicate an output file descriptor or close it.

```bash
# Redirect stderr to stdout
command 2>&1

# Redirect stdout to stderr
command 1>&2

# Close stderr
exec 2>&-

# Duplicate fd 4 to stdout
exec 1>&4
```

**Common pattern**: Redirect stderr to stdout:

```bash
command 2>&1               # stderr goes where stdout goes

# Capture both in a variable
output=$(command 2>&1)

# Send both to a file
command >file.txt 2>&1

# Pipe both to another command
command 2>&1 | grep error
```

## Read and Write: `<>`

Open a file for both reading and writing.

**Format**: `[n]<>word`

**Default**: File descriptor 0 (stdin)

```bash
# Open file for read/write on fd 3
exec 3<>data.txt

# Read from it
read line <&3

# Write to it
echo "new data" >&3

# Close it
exec 3>&-
```

Creates the file if it doesn't exist.

## Here-Documents: `<<`

A **here-document** lets you embed multi-line input directly in the script.

**Format**:
```bash
[n]<<delimiter
lines of text
delimiter
```

The text between the delimiters becomes input to the command.

### Basic Here-Document

```bash
cat <<EOF
This is line 1
This is line 2
This is line 3
EOF
```

Output:
```
This is line 1
This is line 2
This is line 3
```

### Choosing the Delimiter

The delimiter can be any word:

```bash
cat <<END
text
END

cat <<MARKER
text
MARKER

cat <<'STOP'
text
STOP
```

**Important**: The terminating delimiter must be on a line by itself with nothing else (no spaces before or after).

### Variable Expansion in Here-Documents

By default, variables are expanded:

```bash
name="World"
cat <<EOF
Hello, $name!
Today is $(date)
EOF
```

Output:
```
Hello, World!
Today is Mon Jan 05 2026
```

### Quoted Delimiter (No Expansion)

Quote any part of the delimiter to prevent expansion:

```bash
name="World"
cat <<'EOF'
Hello, $name!
Today is $(date)
EOF
```

Output (literal):
```
Hello, $name!
Today is $(date)
```

Quoting styles that prevent expansion:
- `'EOF'` - single quotes
- `"EOF"` - double quotes (but not outside command substitution)
- `E\OF` - escaped character

### Here-Document with Indentation: `<<-`

The `<<-` operator strips **leading tabs** (not spaces!) from the here-document and delimiter:

```bash
if true; then
	cat <<-EOF
		This is indented with tabs
		The leading tabs are stripped
	EOF
fi
```

Output (tabs removed):
```
This is indented with tabs
The leading tabs are stripped
```

**Important**: Only **tabs** are stripped, not spaces. The delimiter must also use tabs.

### Multiple Here-Documents

You can have multiple here-documents on one line:

```bash
cat <<EOF1; cat <<EOF2
First document
EOF1
Second document
EOF2
```

The first here-document's content comes first, then the second's.

### Backslash in Here-Documents

Inside here-documents (when delimiter is unquoted), backslash works like inside double quotes:

```bash
cat <<EOF
Backslash escapes: \$ \` \" \\
New line: one\
two
EOF
```

Output:
```
Backslash escapes: $ ` " \
New line: onetwo
```

### Here-Documents in Scripts

Common uses:

**1. Creating multi-line files:**
```bash
cat > config.txt <<EOF
server=localhost
port=8080
timeout=30
EOF
```

**2. SQL queries:**
```bash
mysql -u user -p <<EOF
USE database;
SELECT * FROM table;
EOF
```

**3. Multi-line messages:**
```bash
cat <<EOF
Usage: $0 [options] file

Options:
  -v    Verbose mode
  -h    Show this help
EOF
```

**4. Generating scripts:**
```bash
cat > script.sh <<'EOF'
#!/bin/bash
echo "This script was generated"
exit 0
EOF
chmod +x script.sh
```

## Practical Examples

### Example 1: Logging stdout and stderr Separately

```bash
#!/bin/bash
command >output.log 2>error.log

if [ -s error.log ]; then
    echo "Errors occurred:"
    cat error.log
fi
```

### Example 2: Discarding Output

```bash
#!/bin/bash
# Discard stdout
command >/dev/null

# Discard stderr
command 2>/dev/null

# Discard both
command >/dev/null 2>&1

# Or shorter (non-standard but common):
command &>/dev/null        # bash-specific
```

### Example 3: Capturing Both stdout and stderr

```bash
#!/bin/bash
output=$(command 2>&1)
echo "Command output: $output"
```

### Example 4: Here-Document for Configuration

```bash
#!/bin/bash
cat > ~/.myapprc <<EOF
# Configuration for myapp
username=$USER
home=$HOME
timestamp=$(date)
EOF
```

### Example 5: Using File Descriptors

```bash
#!/bin/bash
# Open fd 3 for reading
exec 3<input.txt

# Open fd 4 for writing
exec 4>output.txt

# Read from fd 3
while read -u 3 line; do
    echo "Processing: $line" >&4
done

# Close file descriptors
exec 3<&-
exec 4>&-
```

### Example 6: Here-Document in a Function

```bash
usage() {
    cat <<EOF
Usage: $(basename "$0") [options] file

Options:
    -h, --help      Show this help message
    -v, --verbose   Enable verbose output
    -o, --output    Specify output file

Examples:
    $0 input.txt
    $0 -v -o output.txt input.txt
EOF
}
```

### Example 7: Swapping stdout and stderr

```bash
#!/bin/bash
# Swap stdout and stderr
command 3>&1 1>&2 2>&3 3>&-

# Explanation:
# 3>&1  - Save stdout to fd 3
# 1>&2  - Redirect stdout to stderr
# 2>&3  - Redirect stderr to saved stdout (fd 3)
# 3>&-  - Close fd 3
```

## Common Pitfalls

### Pitfall 1: Order of Redirections

```bash
# WRONG - stderr doesn't go to file
command 2>&1 >file.txt

# RIGHT - both go to file
command >file.txt 2>&1
```

### Pitfall 2: Spaces in Redirection

```bash
# WRONG - 2 becomes an argument
command 2 >file.txt

# RIGHT - no space between fd and operator
command 2>file.txt
```

### Pitfall 3: Here-Document Delimiter Not Alone

```bash
# WRONG - spaces before delimiter
cat <<EOF
text
    EOF               # Won't match (has spaces)

# RIGHT - delimiter alone on line
cat <<EOF
text
EOF
```

### Pitfall 4: Using Spaces Instead of Tabs with `<<-`

```bash
# WRONG - <<- only strips tabs, not spaces
cat <<-EOF
    text with spaces (not stripped)
EOF

# RIGHT - use tabs
cat <<-EOF
	text with tabs (stripped)
EOF
```

### Pitfall 5: Forgetting to Quote Here-Document Delimiter

```bash
# Variables are expanded
cat <<EOF
$HOME
EOF
# Prints: /home/user

# Quote delimiter to prevent expansion
cat <<'EOF'
$HOME
EOF
# Prints: $HOME
```

### Pitfall 6: Quoting the Redirect Operator

```bash
# WRONG - operator is quoted, no redirection
echo text \>file.txt      # Prints: text >file.txt

# RIGHT
echo text >file.txt       # Redirects to file
```

## Best Practices

1. **Always redirect stderr when necessary**:
   ```bash
   command 2>error.log      # Save errors
   command 2>/dev/null      # Discard errors
   ```

2. **Use here-documents for multi-line input**:
   ```bash
   # Better than multiple echo commands
   cat >file <<EOF
   line1
   line2
   line3
   EOF
   ```

3. **Quote here-document delimiters when you want literal text**:
   ```bash
   cat <<'EOF'              # No expansion
   $variable stays literal
   EOF
   ```

4. **Close file descriptors when done**:
   ```bash
   exec 3<file
   # ... use fd 3 ...
   exec 3<&-                # Close it
   ```

5. **Use meaningful delimiters**:
   ```bash
   # Good - clear purpose
   cat <<SQL_QUERY
   SELECT * FROM users;
   SQL_QUERY
   
   # Generic but fine
   cat <<EOF
   content
   EOF
   ```

6. **Redirect stderr to stdout before piping**:
   ```bash
   command 2>&1 | grep error
   ```

7. **Use `>|` to override noclobber safely**:
   ```bash
   set -C                   # Enable noclobber
   echo data >| file        # Override when needed
   ```

## Availability: Redirection Across Different Builds

Redirection depends on file descriptor manipulation, which varies by build.

### POSIX API Build (Full Redirection Support)

- ✅ All redirection operators work
- ✅ File descriptors 0-9 guaranteed (more typically supported)
- ✅ Full `dup()`, `dup2()`, `open()`, `close()` functionality
- ✅ Here-documents work completely
- ✅ All expansion in here-documents
- ✅ Pipe creation for here-documents
- ✅ Noclobber option works

**Full functionality as specified.**

### UCRT API Build (Full Redirection Support)

- ✅ All redirection operators work
- ✅ File descriptors 0-9 supported
- ✅ Uses `_dup()`, `_dup2()`, `_open()`, `_close()`
- ✅ Here-documents work

**Implementation differences but same behavior**:

1. **File operations**: Uses `_open()`, `_close()`, `_dup2()` instead of POSIX equivalents
2. **Binary vs. text mode**: Windows distinguishes binary/text mode for files
3. **Paths**: Windows path conventions (`C:\...`)

**Practical outcome**: Works the same from user's perspective.

**Windows-specific notes**:

- Line endings may differ (CRLF vs LF) but typically handled transparently
- Redirecting to `NUL` (not `/dev/null`):
  ```bash
  command >NUL 2>&1         # Windows
  command >/dev/null 2>&1   # Unix
  ```

### ISO C Build (Limited Redirection Support)

- ⚠️ **Basic redirection may work** but is limited
- ⚠️ **File descriptor duplication uncertain**
- ⚠️ **Here-documents may not work**

**What might work**:

Basic file redirection using `freopen()`:
```bash
command >output.txt        # May work
command <input.txt         # May work
```

**What's problematic**:

1. **File descriptor operations**: ISO C doesn't have `dup()` or `dup2()`
   ```bash
   command 2>&1              # Uncertain - needs dup2()
   exec 3<file               # Uncertain - needs low-level fd ops
   ```

2. **Here-documents**: Require:
   - Pipe creation (not in ISO C)
   - Forking to write to pipe (not in ISO C)
   - File descriptor manipulation
   
   ```bash
   cat <<EOF                 # May not work
   content
   EOF
   ```

3. **Advanced redirection**:
   ```bash
   command 3>&1 1>&2 2>&3    # Unlikely to work
   exec 4<>file              # Uncertain
   ```

**Workarounds**:

For simple cases, the shell might use temporary files:
```bash
# Shell might implement here-doc with temp file
cat <<EOF
content
EOF

# Equivalent to:
# echo "content" > /tmp/heredoc.$$
# cat < /tmp/heredoc.$$
# rm /tmp/heredoc.$$
```

But this is implementation-specific and not reliable.

### Summary Table

| Feature | POSIX | UCRT | ISO C |
|---------|-------|------|-------|
| `<` input redirect | ✅ Yes | ✅ Yes | ⚠️ Maybe |
| `>` output redirect | ✅ Yes | ✅ Yes | ⚠️ Maybe |
| `>>` append | ✅ Yes | ✅ Yes | ⚠️ Maybe |
| `2>` stderr redirect | ✅ Yes | ✅ Yes | ❓ Uncertain |
| `>&` fd duplication | ✅ Yes | ✅ Yes | ❌ Unlikely |
| `<&` fd duplication | ✅ Yes | ✅ Yes | ❌ Unlikely |
| `<>` read/write | ✅ Yes | ✅ Yes | ❓ Uncertain |
| `<<` here-document | ✅ Yes | ✅ Yes | ❌ Unlikely |
| `<<-` here-doc indented | ✅ Yes | ✅ Yes | ❌ Unlikely |
| Multiple fd redirects | ✅ Yes | ✅ Yes | ❌ Unlikely |
| Noclobber (`-C`) | ✅ Yes | ✅ Yes | ❓ Uncertain |
| FDs beyond 0-2 | ✅ Yes | ✅ Yes | ❌ No |

### Choosing Your Build for Redirection

**Need full redirection?**
- Use POSIX or UCRT builds
- ISO C build is very limited

**Maximum portability?**
- Stick to basic `<`, `>`, `>>` redirection
- Avoid file descriptor manipulation
- Avoid here-documents if you need ISO C support
- Use temporary files instead of complex redirections

**Recommendations by build:**

**POSIX/UCRT**: Use all features freely
```bash
command >output.txt 2>&1
cat <<EOF
content
EOF
exec 3<file
```

**ISO C**: Stick to basics
```bash
command >output.txt        # Might work
command <input.txt         # Might work

# Avoid:
command 2>&1               # Probably won't work
cat <<EOF                  # Probably won't work
exec 3<file                # Probably won't work
```

**The reality**: Redirection beyond basic input/output files is a POSIX/UCRT feature. ISO C build's limitations make it unsuitable for scripts that need complex I/O handling.

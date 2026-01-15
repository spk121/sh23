# Shell Variables

**Variables** are named parameters that store values you can use throughout your shell session or script. While we've already covered special parameters like `$1`, `$?`, and `$$` in another section, this section focuses on regular variables—the ones you create and name yourself, plus the special variables the shell maintains for you.

Variables are fundamental to shell programming. They let you store data, pass information between commands, configure shell behavior, and much more.

## What Are Variables?

A **variable** is a parameter denoted by a name. The name must follow these rules:
- Start with a letter (`a-z`, `A-Z`) or underscore (`_`)
- Contain only letters, digits, and underscores
- Case-sensitive (`var`, `Var`, and `VAR` are different variables)

Examples of valid variable names:
```sh
myvar
my_var
_private
VAR123
count
```

Invalid variable names:
```sh
123var        # Can't start with digit
my-var        # Can't contain hyphen
my.var        # Can't contain dot
my var        # Can't contain space
```

## Creating and Using Variables

### Setting Variables

The basic syntax for setting a variable:
```sh
name=value
```

**Important**: No spaces around the `=` sign!

```sh
# Correct:
var=hello
count=42
path=/usr/bin

# Wrong:
var = hello       # Error: tries to run 'var' command with arguments '=' and 'hello'
var= hello        # Sets var to empty, then runs 'hello' command
var =hello        # Tries to run 'var' with argument '=hello'
```

### Accessing Variable Values

Use `$` to get the value:
```sh
name="World"
echo "Hello, $name!"          # Output: Hello, World!
echo "Hello, ${name}!"        # Same, with braces (recommended)
```

The `${name}` form is clearer and required in some contexts:
```sh
name="file"
echo "$name.txt"              # Output: file.txt (works because '.' isn't valid in names)
echo "${name}123"             # Output: file123 (braces required)
echo "$name123"               # Output: (empty - looks for variable 'name123')
```

### Empty vs. Unset

Variables can be in three states:
1. **Set with a value**: `var="hello"`
2. **Set but empty**: `var=""`
3. **Unset**: Never assigned, or removed with `unset`

```sh
var="hello"          # Set with value
echo "${var}"        # Prints: hello

var=""               # Set but empty
echo "${var}"        # Prints: (nothing, but var is set)

unset var            # Unset
echo "${var}"        # Prints: (nothing, var is unset)
```

To test if a variable is set:
```sh
if [ -z "${var+x}" ]; then
    echo "var is unset"
else
    echo "var is set"
fi
```

Or use parameter expansion:
```sh
echo "${var:-default}"       # Use 'default' if unset or empty
echo "${var-default}"        # Use 'default' if unset (empty is OK)
```

## Where Variables Come From

Variables can be initialized in several ways:

### 1. From the Environment

When the shell starts, it initializes variables from environment variables passed by the parent process:

```sh
$ export MYVAR=value
$ sh                    # Start new shell
$ echo $MYVAR          # Prints: value
```

**Important**: Only environment variables with **valid names** are imported. Invalid names are ignored.

Variables imported from the environment are automatically marked for export (see below).

### 2. Variable Assignment

Direct assignment in the shell:
```sh
greeting="Hello"
count=42
```

### 3. The `read` Utility

Reading input into variables:
```sh
echo "Enter your name:"
read name
echo "Hello, $name"
```

### 4. The `getopts` Utility

Parsing command-line options:
```sh
while getopts "abc:" opt; do
    case $opt in
        a) flag_a=true ;;
        b) flag_b=true ;;
        c) value=$OPTARG ;;
    esac
done
```

### 5. For Loop Variable

The loop variable is automatically set:
```sh
for file in *.txt; do
    echo "Processing: $file"
done
```

### 6. Parameter Expansion with Assignment

Using `${var=value}` assigns if unset:
```sh
: ${HOME=/home/default}    # Set HOME if unset
```

## Exported Variables

Variables can be **exported** to make them available to child processes (commands and scripts run by the shell):

```sh
# Not exported - only available in current shell
local_var="value"

# Exported - available to child processes
export exported_var="value"

# Or export an existing variable
var="value"
export var
```

Example showing the difference:
```sh
#!/bin/bash
LOCAL=local_value
export EXPORTED=exported_value

# Run another script
./child_script.sh
```

In `child_script.sh`:
```sh
#!/bin/bash
echo "LOCAL: $LOCAL"           # Empty (not exported)
echo "EXPORTED: $EXPORTED"     # Prints: exported_value
```

**Environment variables** are exported variables that are passed between processes.

## Shell-Maintained Variables

The shell automatically sets and maintains certain variables. These affect shell behavior and provide information about the environment.

### `ENV` - Initialization File

**Purpose**: Path to a file that's sourced when an **interactive** shell starts.

**How it works**:
1. Only processed for interactive shells
2. Value undergoes parameter expansion
3. Resulting path is used to find a file
4. File contents are executed in the current environment
5. File doesn't need to be executable

**Security**: `ENV` is ignored if your real and effective user/group IDs differ (to prevent privilege escalation).

Example:
```sh
export ENV="$HOME/.shrc"
sh                            # Starts interactive shell, sources ~/.shrc
```

**Important**: If `ENV` expands to a non-absolute path, results are unspecified. Always use absolute paths.

### `HOME` - Home Directory

**Purpose**: Your home directory path.

**Used by**: Tilde expansion (`~`)

```sh
echo $HOME                    # /home/username
cd ~                          # Same as: cd $HOME
echo ~/documents              # Expands to: $HOME/documents
```

The shell typically sets `HOME` from the environment when it starts.

### `IFS` - Internal Field Separator

**Purpose**: Defines how the shell splits words during field splitting and how `read` splits lines.

**Default value**: Space, tab, newline (`<space><tab><newline>`)

```sh
# Default IFS
echo "$IFS" | cat -A          # Shows: $ ^I$ (space, tab, newline)

# Splitting with default IFS
list="apple banana cherry"
for item in $list; do         # Splits on spaces
    echo "Item: $item"
done
# Output:
# Item: apple
# Item: banana
# Item: cherry
```

**Changing IFS**:
```sh
# Split on colons
IFS=:
path="/usr/bin:/usr/local/bin:/bin"
for dir in $path; do
    echo "Dir: $dir"
done

# Split on commas
IFS=,
data="apple,banana,cherry"
for item in $data; do
    echo "Item: $item"
done
```

**With `read`**:
```sh
# Default: splits on whitespace
echo "John Doe 42" | while read first last age; do
    echo "First: $first, Last: $last, Age: $age"
done

# Custom: split on colons
IFS=:
echo "John:Doe:42" | while read first last age; do
    echo "First: $first, Last: $last, Age: $age"
done
```

**Important notes**:
- If `IFS` is unset, it behaves as if set to `<space><tab><newline>`
- If `IFS` contains invalid byte sequences, results are unspecified
- The shell sets `IFS` to `<space><tab><newline>` when invoked

**Best practice**: Save and restore `IFS` when changing it:
```sh
old_IFS="$IFS"
IFS=,
# ... use custom IFS ...
IFS="$old_IFS"
```

### `LANG` - Default Locale

**Purpose**: Provides default values for locale settings (language, character encoding, etc.)

```sh
LANG=en_US.UTF-8              # US English, UTF-8 encoding
LANG=fr_FR.UTF-8              # French, UTF-8 encoding
```

This affects:
- Message language
- Character interpretation
- Sorting order
- Date/time formatting

### `LC_ALL` - Locale Override

**Purpose**: Overrides all other locale settings (`LC_*` variables and `LANG`)

```sh
export LC_ALL=C               # Use "C" locale (ASCII, portable)
```

Setting `LC_ALL` forces all locale categories to use the specified locale, regardless of other `LC_*` variables.

### `LC_COLLATE` - Collation Order

**Purpose**: Determines sorting order and pattern matching behavior (ranges, equivalence classes, multi-character collating elements)

Affects:
- Sort order in `sort` command
- Range expressions like `[a-z]` in patterns
- String comparisons

```sh
export LC_COLLATE=C           # ASCII byte order
export LC_COLLATE=en_US.UTF-8 # Locale-aware ordering
```

### `LC_CTYPE` - Character Type

**Purpose**: Determines how bytes are interpreted as characters (single-byte vs. multi-byte, what counts as a letter, what's whitespace, etc.)

Affects:
- Character class matching in patterns (like `[:alpha:]`, `[:blank:]`)
- Multi-byte character handling

**Important**: Changing `LC_CTYPE` after the shell starts doesn't affect lexical processing in the current shell or subshells. Only new shell invocations (scripts, `exec sh`) see the change.

```sh
export LC_CTYPE=en_US.UTF-8   # UTF-8 character handling
```

### `LC_MESSAGES` - Message Language

**Purpose**: Determines the language for messages and diagnostics

```sh
export LC_MESSAGES=en_US      # English messages
export LC_MESSAGES=es_ES      # Spanish messages
```

### `LINENO` - Current Line Number

**Purpose**: Tracks the current line number in a script or function (starting from 1)

```sh
#!/bin/bash
echo "Line $LINENO"           # Prints line number
command
echo "Line $LINENO"           # Prints different line number
```

**Notes**:
- Set automatically by the shell before each command
- If unset or reset by user, may lose special meaning
- Value is unspecified outside scripts/functions
- Supported only if the system supports the User Portability Utilities option

Useful for debugging:
```sh
error() {
    echo "Error on line $LINENO: $1" >&2
}
```

### `PATH` - Command Search Path

**Purpose**: List of directories where the shell looks for commands

**Format**: Colon-separated list of directory paths

```sh
PATH=/usr/local/bin:/usr/bin:/bin
```

When you type a command name (without a path), the shell searches these directories in order:
```sh
$ echo $PATH
/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin

$ which ls
/usr/bin/ls           # Found in second directory
```

**Adding to PATH**:
```sh
PATH=/opt/myapp/bin:$PATH     # Prepend
PATH=$PATH:/opt/extra/bin     # Append
```

### `PPID` - Parent Process ID

**Purpose**: The process ID of the shell's parent process

```sh
echo $PPID                    # Prints parent's PID
```

**Important behavior**:
- Set during shell initialization
- In a subshell, `PPID` keeps the **same value** as the parent shell

Example:
```sh
echo $PPID                    # 1234
(echo $PPID)                  # 1234 (same value!)
```

This is different from `$$`, which also stays the same in subshells. `PPID` tells you the PID of the shell's parent process, not the current shell's PID.

### `PS1` - Primary Prompt

**Purpose**: The prompt shown before each command in an interactive shell

**Default**: `"$ "` (or implementation-defined for privileged users)

```sh
PS1="$ "                      # Simple prompt
PS1="\u@\h:\w$ "             # Common format (bash-specific)
PS1="[$(whoami)]$ "          # With command substitution
```

**Expansion**: Before displaying, `PS1` undergoes:
- Parameter expansion (always)
- Exclamation-mark expansion (history numbers)
- Possibly command substitution and arithmetic expansion (implementation-defined)

**Exclamation-mark expansion**: `!` is replaced with the next history command number, `!!` becomes `!`.

**Supported only** if the system supports the User Portability Utilities option.

### `PS2` - Secondary Prompt

**Purpose**: Shown when you need to continue a multi-line command

**Default**: `"> "`

```sh
$ echo "Start \
> continuation \
> end"
```

The `> ` is `PS2`.

Undergoes same expansion as `PS1` (parameter expansion, possibly others).

### `PS4` - Execution Trace Prompt

**Purpose**: Prefix for each command when execution tracing (`set -x`) is enabled

**Default**: `"+ "`

```sh
#!/bin/bash
set -x                        # Enable tracing
echo "Hello"
```

Output:
```
+ echo Hello
Hello
```

The `+ ` prefix is `PS4`.

Can be customized:
```sh
PS4='[$LINENO]+ '             # Show line number in trace
```

### `PWD` - Present Working Directory

**Purpose**: The current working directory path

**Initialization**: Complex rules based on environment:
1. If `PWD` from environment is absolute, no longer than `PATH_MAX`, and doesn't contain `.` or `..` components, use it
2. Otherwise, if the environment `PWD` is absolute and doesn't contain `.` or `..`, behavior is unspecified (may use it or use `pwd -P` output)
3. Otherwise, set to output of `pwd -P`

**Set by**: The shell and the `cd` command

```sh
echo $PWD                     # /home/user/documents
cd /tmp
echo $PWD                     # /tmp
```

**Important notes**:
- `PWD` can contain symlink components (when set from environment)
- Or can be the physical path (when set like `pwd -P`)
- Assignments to `PWD` may be ignored by the shell
- If you set/unset `PWD`, behavior of `cd` and `pwd` is unspecified

**Best practice**: Don't modify `PWD` manually. Let `cd` manage it.

## Variable Naming Best Practices

1. **Use descriptive names**: `count` is better than `c`
   ```sh
   # Good:
   user_count=10
   max_retries=3

   # Bad:
   uc=10
   mr=3
   ```

2. **Use lowercase for local variables**: Uppercase is traditionally for environment variables
   ```sh
   # Local variables:
   temp_file=/tmp/data.txt

   # Environment/exported variables:
   export DATABASE_URL=postgresql://localhost/mydb
   ```

3. **Use underscores for readability**: `user_count` not `usercount`

4. **Avoid reserved words**: Don't use `if`, `then`, `while`, etc. as variable names (even though you can with quoting)

5. **Don't modify special shell variables**: Changing `PWD`, `IFS`, etc. can cause unexpected behavior. If you must, save and restore:
   ```sh
   old_pwd="$PWD"
   # ... do something ...
   cd "$old_pwd"
   ```

## Common Variable Patterns

### Temporary Variables

```sh
temp=$(mktemp)
echo "data" > "$temp"
process_file "$temp"
rm "$temp"
```

### Default Values

```sh
# Use default if unset
config_file="${CONFIG_FILE:-/etc/myapp/config}"

# Use default if unset or empty
user="${USER:-nobody}"
```

### Counters

```sh
count=0
while [ $count -lt 10 ]; do
    echo "Count: $count"
    count=$((count + 1))
done
```

### Arrays (Non-POSIX, but common)

POSIX shell doesn't have arrays, but you can simulate them:
```sh
# Using IFS and word splitting
items="apple banana cherry"
for item in $items; do
    echo "$item"
done

# Using positional parameters
set -- apple banana cherry
for item in "$@"; do
    echo "$item"
done
```

## Availability: Variables Across Different Builds

Variable functionality is fundamental and works similarly across all builds, though some special variables may have limited utility depending on the build's features.

### POSIX API Build (Full Variable Support)

- ✅ All variable operations work completely
- ✅ All special shell variables function as specified
- ✅ Environment variable import/export
- ✅ All locale variables work
- ✅ `LINENO` supported (if User Portability Utilities option)
- ✅ `PS1`, `PS2`, `PS4` supported (if User Portability Utilities option)
- ✅ `ENV` file sourcing works
- ✅ `PWD` tracking with `cd`

### UCRT API Build (Full Variable Support)

- ✅ All basic variable operations identical to POSIX
- ✅ Environment variables work the same
- ✅ Most special variables work identically

**Platform-specific considerations:**

1. **Path separators**:
   - `PATH` uses semicolons (`;`) on Windows, not colons (`:`)
   - The shell may normalize this internally
   ```sh
   # Windows:
   PATH="C:\Windows;C:\Program Files"

   # Or shell may accept Unix-style:
   PATH="/c/Windows:/c/Program Files"
   ```

2. **`HOME`**:
   - Windows doesn't have a native `HOME` concept
   - Often set to `%USERPROFILE%` or similar
   - May be `C:\Users\username`

3. **`PWD`**:
   - Works but uses Windows paths
   - May use forward or back slashes depending on implementation
   ```sh
   echo $PWD     # Might be: C:/Users/username or C:\Users\username
   ```

4. **Locale variables** (`LANG`, `LC_*`):
   - May have limited effect on Windows
   - Character encoding differences between Windows and Unix
   - `LC_CTYPE` may not affect as much

5. **`ENV` file**:
   - Works if shell supports interactive mode
   - Path should use Windows conventions

**Practical outcome**: Variables work, but path and locale handling may differ.

### ISO C Build (Basic Variable Support)

- ✅ Basic variable assignment and access
- ✅ Environment variable import (via `getenv()`)
- ⚠️ Environment variable export limited (via `putenv()` if available)
- ⚠️ Some special variables have limited utility

**What works:**
- Creating and using variables
- Basic `PATH` functionality (if commands are executed)
- Variable expansion in the shell itself

**What has limited functionality:**

1. **`PWD`**:
   - May not track properly (no `cd` in pure ISO C)
   - Can be set manually but won't auto-update

2. **`PS1`, `PS2`, `PS4`**:
   - Work if interactive mode exists
   - But may be simpler/less featured

3. **`ENV`**:
   - Works if interactive shell exists
   - File sourcing depends on how shell reads files

4. **`LINENO`**:
   - May work in scripts
   - Depends on parsing implementation

5. **Locale variables** (`LANG`, `LC_*`):
   - ISO C has basic locale support via `setlocale()`
   - But much more limited than POSIX

6. **Export**:
   - Limited by `putenv()` availability
   - Child processes may not properly inherit all variables

**Practical implications**:

Basic variable usage works fine:
```sh
#!/bin/sh
name="World"
greeting="Hello, $name"
echo "$greeting"             # Works fine
```

But advanced features are limited:
```sh
# May not work well in ISO C build:
export MYVAR=value           # Export might not work
cd /tmp                      # cd might not exist
echo $PWD                    # PWD might not update
```

### Summary Table

| Feature | POSIX | UCRT | ISO C |
|---------|-------|------|-------|
| Variable assignment/access | ✅ Full | ✅ Full | ✅ Full |
| Environment import | ✅ Full | ✅ Full | ✅ Basic |
| Environment export | ✅ Full | ✅ Full | ⚠️ Limited |
| `HOME` | ✅ Full | ✅ Windows-style | ⚠️ May not exist |
| `PATH` | ✅ Full | ✅ Windows format | ✅ Basic |
| `PWD` | ✅ Full | ✅ Windows paths | ⚠️ May not update |
| `IFS` | ✅ Full | ✅ Full | ✅ Full |
| `PPID` | ✅ Full | ✅ Full | ✅ Basic |
| `LINENO` | ✅ Yes | ✅ Yes | ⚠️ May work |
| `PS1`/`PS2`/`PS4` | ✅ Full | ✅ Full | ⚠️ Limited |
| `ENV` | ✅ Full | ✅ Full | ⚠️ Limited |
| Locale variables | ✅ Full | ⚠️ Windows locales | ⚠️ Basic |

### Choosing Your Build for Variables

**For basic variable usage**: All builds work equally well
- Setting and accessing variables
- String manipulation
- Using in commands

**For environment interaction**: POSIX and UCRT are better
- Reliable `export` functionality
- Proper `PWD` tracking
- Full locale support

**For maximum portability**: Use basic variable features
- Avoid relying on specific special variables
- Don't depend on locale variables
- Keep path handling simple

Variables are one of the most portable shell features—basic usage works everywhere, though the environment interaction and special variables may vary by build.

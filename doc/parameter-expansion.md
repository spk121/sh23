# Parameter Expansion

**Parameter expansion** is one of the most powerful features of the shell. It's how you access variable values, manipulate strings, provide default values, and perform substring operations—all within the shell itself, without calling external commands.

While the simplest form is just `$var`, parameter expansion offers much more: you can strip prefixes and suffixes, provide defaults for unset variables, check if variables are set, and more.

## The Basic Format

All parameter expansions follow this format:
```bash
${expression}
```

The braces contain an expression that tells the shell what to do. The shell reads characters until it finds the matching `}`, being careful about:
- Escaped braces (`\}`) don't count as the end
- Braces inside quotes are literal
- Braces in nested expansions don't count

## Simple Parameter Expansion

The most basic form just gets the parameter's value:

```bash
name="World"
echo ${name}              # Prints: World
```

### When Braces Are Optional

For simple variables, braces are optional:
```bash
echo $name                # Same as: ${name}
```

### When Braces Are Required

Braces are **required** in these cases:

**1. Positional parameters beyond $9:**
```bash
echo $10                  # WRONG: $1 followed by "0"
echo ${10}                # RIGHT: tenth parameter
```

**2. When followed by characters that could be part of the name:**
```bash
file="report"
echo $file_name           # Looks for variable 'file_name'
echo ${file}_name         # Expands 'file', then appends '_name'
```

**3. When using parameter expansion features:**
```bash
echo ${var:-default}      # Braces required for these forms
```

### How the Shell Determines the Parameter Name

Without braces, the shell uses the **longest valid name** it can find:
```bash
ab=short
abc=long
echo $abc                 # Prints: long (uses longest valid name)
```

With special parameters (one character), no braces needed:
```bash
echo $1                   # First positional parameter
echo $?                   # Exit status
echo $$                   # Shell PID
```

## Default Values: `:-` and `-`

These forms provide fallback values when parameters are unset or empty.

### `${parameter:-word}` - Use Default If Unset or Null

If the parameter is unset **or** empty, use `word` instead:

```bash
name=""
echo ${name:-Nobody}      # Prints: Nobody (name is empty)

unset name
echo ${name:-Nobody}      # Prints: Nobody (name is unset)

name="Alice"
echo ${name:-Nobody}      # Prints: Alice (name has value)
```

The original variable is **not modified**—this just substitutes the value.

### `${parameter-word}` - Use Default If Unset Only

Without the colon, only **unset** triggers the default (empty is OK):

```bash
name=""
echo ${name-Nobody}       # Prints: (empty - name is set, though empty)

unset name
echo ${name-Nobody}       # Prints: Nobody (name is unset)

name="Alice"
echo ${name-Nobody}       # Prints: Alice
```

**The difference**: `:` tests for unset **or** null; without `:` tests for unset only.

## Assign Default Values: `:=` and `=`

These forms not only provide a default but also **assign** it to the variable.

### `${parameter:=word}` - Assign Default If Unset or Null

If unset or empty, assign `word` to the parameter and substitute it:

```bash
unset name
echo ${name:=Nobody}      # Prints: Nobody
echo $name                # Prints: Nobody (variable was set!)

name=""
echo ${name:=Someone}     # Prints: Someone
echo $name                # Prints: Someone (was empty, now set)
```

**Important**: This only works with **variables**, not positional parameters or special parameters:
```bash
echo ${1:=default}        # ERROR: can't assign to positional parameter
```

### `${parameter=word}` - Assign Default If Unset Only

Without colon, only assigns if unset (empty is OK):

```bash
name=""
echo ${name=Nobody}       # Prints: (empty - name is set)
echo $name                # Prints: (still empty)

unset name
echo ${name=Nobody}       # Prints: Nobody
echo $name                # Prints: Nobody (was assigned)
```

## Error If Unset: `:?` and `?`

These forms cause the shell to exit with an error if the parameter is unset or empty.

### `${parameter:?[word]}` - Error If Unset or Null

If unset or empty, print error message and exit:

```bash
unset required
echo ${required:?value is required}
# Output: sh: required: value is required
# Script exits with non-zero status

required=""
echo ${required:?cannot be empty}
# Output: sh: required: cannot be empty
# Script exits

required="value"
echo ${required:?}        # Prints: value (no error)
```

If `word` is omitted, a default error message is used:
```bash
echo ${required:?}
# Output: sh: required: parameter null or not set
```

**Note**: Interactive shells don't have to exit (they typically just print the error).

### `${parameter?word}` - Error If Unset Only

Without colon, only errors if unset (empty is OK):

```bash
required=""
echo ${required?}         # Prints: (empty - no error)

unset required
echo ${required?value required}
# Output: sh: required: value required
# Exits
```

## Alternative Value: `:+` and `+`

These are the opposite of defaults—they provide a value **when the parameter is set**.

### `${parameter:+word}` - Use Alternative If Set and Not Null

If set and not empty, use `word`; otherwise, use empty:

```bash
verbose=""
echo ${verbose:+"-v"}     # Prints: (empty - verbose is null)

verbose="yes"
echo ${verbose:+"-v"}     # Prints: -v

unset verbose
echo ${verbose:+"-v"}     # Prints: (empty - verbose is unset)
```

Useful for optional flags:
```bash
command ${verbose:+-v} file
# If verbose is set and not empty, becomes: command -v file
# Otherwise: command file
```

### `${parameter+word}` - Use Alternative If Set

Without colon, provides `word` if parameter is set (even if empty):

```bash
verbose=""
echo ${verbose+"-v"}      # Prints: -v (verbose is set, though empty)

unset verbose
echo ${verbose+"-v"}      # Prints: (empty - verbose is unset)
```

## Summary of Colon vs. No Colon

The **colon** (`:`) makes the expansion test for **null** (empty) in addition to unset:

| Expansion | Set & Not Null | Set But Null | Unset |
|-----------|----------------|--------------|-------|
| `${var:-word}` | substitute var | substitute word | substitute word |
| `${var-word}` | substitute var | substitute null | substitute word |
| `${var:=word}` | substitute var | assign word | assign word |
| `${var=word}` | substitute var | substitute null | assign word |
| `${var:?word}` | substitute var | error, exit | error, exit |
| `${var?word}` | substitute var | substitute null | error, exit |
| `${var:+word}` | substitute word | substitute null | substitute null |
| `${var+word}` | substitute word | substitute word | substitute null |

**Rule of thumb**: 
- **With `:` (colon)**: treats empty as if unset
- **Without `:`**: empty is different from unset

## String Length: `#`

Get the length of a parameter's value:

```bash
name="Alice"
echo ${#name}             # Prints: 5

path="/usr/local/bin"
echo ${#path}             # Prints: 14

empty=""
echo ${#empty}            # Prints: 0
```

**Special cases**:
- `${#*}` and `${#@}` are unspecified (don't use)
- If `set -u` is in effect and parameter is unset, this fails

## Pattern Matching: Prefix and Suffix Removal

These four forms let you remove parts of a string using patterns. They use **pattern matching** (like filename globbing), not regular expressions.

### `${parameter%pattern}` - Remove Smallest Suffix

Removes the **shortest** match from the **end**:

```bash
file="report.txt"
echo ${file%.*}           # Prints: report (removes .txt)

path="/usr/local/bin"
echo ${path%/*}           # Prints: /usr/local (removes /bin)

file="archive.tar.gz"
echo ${file%.*}           # Prints: archive.tar (shortest: .gz)
```

### `${parameter%%pattern}` - Remove Largest Suffix

Removes the **longest** match from the **end**:

```bash
file="archive.tar.gz"
echo ${file%%.*}          # Prints: archive (longest: .tar.gz)

path="/usr/local/bin"
echo ${path%%/*}          # Prints: (empty - matches all from first /)

path="usr/local/bin"
echo ${path%%/*}          # Prints: usr (matches from first /)
```

### `${parameter#pattern}` - Remove Smallest Prefix

Removes the **shortest** match from the **beginning**:

```bash
path="/usr/local/bin"
echo ${path#/*/}          # Prints: local/bin (removes /usr/)

file="prefix-name.txt"
echo ${file#*-}           # Prints: name.txt (removes prefix-)
```

### `${parameter##pattern}` - Remove Largest Prefix

Removes the **longest** match from the **beginning**:

```bash
path="/usr/local/bin"
echo ${path##*/}          # Prints: bin (removes /usr/local/)

file="one-two-three.txt"
echo ${file##*-}          # Prints: three.txt (removes one-two-)
```

## Practical Examples

### Example 1: Providing Defaults

```bash
#!/bin/bash
# Use defaults for optional variables
timeout=${TIMEOUT:-30}
log_file=${LOG_FILE:-/var/log/app.log}
debug=${DEBUG:-false}

echo "Timeout: $timeout"
echo "Log file: $log_file"
echo "Debug: $debug"
```

### Example 2: Required Variables

```bash
#!/bin/bash
# Ensure required variables are set
: ${DATABASE_URL:?DATABASE_URL must be set}
: ${API_KEY:?API_KEY must be set}

# Script continues only if both are set
echo "Connecting to $DATABASE_URL"
```

The `:` command is a no-op that forces the expansion to be evaluated.

### Example 3: Filename Manipulations

```bash
#!/bin/bash
file="/path/to/document.txt"

# Get directory
dir=${file%/*}               # /path/to

# Get filename
name=${file##*/}             # document.txt

# Get basename (without extension)
base=${name%.*}              # document

# Get extension
ext=${name##*.}              # txt

echo "Dir: $dir"
echo "Name: $name"
echo "Base: $base"
echo "Ext: $ext"
```

### Example 4: Optional Command Flags

```bash
#!/bin/bash
verbose=""
force=""

# Parse options
while getopts "vf" opt; do
    case $opt in
        v) verbose="yes" ;;
        f) force="yes" ;;
    esac
done

# Use flags only if set
command ${verbose:+-v} ${force:+-f} file
# If both set: command -v -f file
# If neither: command file
```

### Example 5: Safe Script Initialization

```bash
#!/bin/bash
# Initialize with defaults, allow environment override
config_dir=${CONFIG_DIR:-$HOME/.myapp}
data_dir=${DATA_DIR:-$config_dir/data}
cache_dir=${CACHE_DIR:-/tmp/myapp}

# Create if needed
mkdir -p "$config_dir" "$data_dir" "$cache_dir"
```

### Example 6: String Manipulation Without External Tools

```bash
#!/bin/bash
# Convert filename to uppercase (bash extension, not POSIX)
# But basic manipulations work everywhere:

file="MyDocument.TXT"

# Get lowercase extension (using tr, since ${var,,} is not POSIX)
ext=${file##*.}
ext_lower=$(echo "$ext" | tr '[:upper:]' '[:lower:]')

# Change extension
base=${file%.*}
newfile="${base}.pdf"

echo "New filename: $newfile"
```

## Pattern Matching and Quoting

Quoting affects pattern matching in these expansions:

```bash
var="value*123"

# * is a pattern (matches any characters)
echo "${var#*\*}"         # Removes up to and including * : 123

# * is literal (quoted)
echo "${var#"*"}"         # Nothing removed (looking for literal *)
```

**Key point**: Quote the pattern if you want to match literal special characters.

## Special Cases and Notes

### Using with `$*` or `$@`

Results are **unspecified** for most expansion forms with `$*` or `$@`. Don't use:
```bash
${*:-default}             # Unspecified
${@:=value}               # Unspecified
${#*}                     # Unspecified (use $# instead)
```

### The `#` Ambiguity

When using patterns without `:`, if parameter is `#`, you must provide `word`:
```bash
echo ${#-}                # Ambiguous: length of - or default?
echo ${#-default}         # Must specify word to avoid ambiguity
```

### Pattern Matching on Unset Variables

If `set -u` is in effect and you try pattern matching on an unset variable:
```bash
set -u
unset var
echo ${var#prefix}        # ERROR: var is unset
```

### Word Expansion in Patterns

The `word` part of expansions undergoes these expansions:
- Tilde expansion
- Parameter expansion
- Command substitution
- Arithmetic expansion
- Quote removal

But it's **not** expanded if not needed:
```bash
file="test"
echo ${file:-$(expensive_command)}  # $(expensive_command) NOT run
```

## Common Pitfalls

### Pitfall 1: Forgetting the Colon

```bash
var=""
# Want default if empty
echo ${var-default}       # WRONG: prints empty (var is set)
echo ${var:-default}      # RIGHT: prints "default"
```

### Pitfall 2: Using Pattern Matching on `$@`

```bash
# WRONG - unspecified
echo ${@#prefix}

# RIGHT - use a loop
for arg in "$@"; do
    echo "${arg#prefix}"
done
```

### Pitfall 3: Quoting the Whole Expansion

```bash
file="document.txt"

# WRONG - quotes the result, not the pattern
echo "${file%.txt}"       # Works, but result is quoted (usually OK)

# Pattern quoting happens inside
echo "${file%".txt"}"     # .txt is literal (pattern is quoted)
```

### Pitfall 4: Nested Expansions Without Quotes

```bash
default="some value"
# Want to use another variable as default
echo ${var:-$default}     # Works

# But be careful with spaces
default="value with spaces"
echo ${var:-$default}     # Works (expansion happens)
```

### Pitfall 5: Trying to Assign to Positional Parameters

```bash
# WRONG - can't assign to $1
echo ${1:=default}        # ERROR

# RIGHT - use a variable
first=${1:-default}
```

### Pitfall 6: Pattern Matching Direction Confusion

```bash
path="/usr/local/bin"

# % removes from end
echo ${path%/*}           # /usr/local (removes suffix)

# # removes from beginning  
echo ${path#/*/}          # local/bin (removes prefix)

# Remember: # is at the beginning of the line, % is at the end
```

## Best Practices

1. **Use `:-` for optional variables with defaults**:
   ```bash
   port=${PORT:-8080}
   host=${HOST:-localhost}
   ```

2. **Use `:?` for required variables**:
   ```bash
   : ${DATABASE_URL:?must be set}
   ```

3. **Prefer parameter expansion over external commands**:
   ```bash
   # Slow:
   basename=$(basename "$file")
   dirname=$(dirname "$file")
   
   # Fast (built-in to shell):
   basename=${file##*/}
   dirname=${file%/*}
   ```

4. **Always quote the expansion result**:
   ```bash
   echo "${var:-default}"
   ```

5. **Document complex patterns**:
   ```bash
   # Remove everything up to and including last slash
   filename=${path##*/}
   ```

6. **Test with empty and unset values**:
   ```bash
   # Make sure your expansions handle both
   var=""
   echo "${var:-default}"    # Test with empty
   unset var
   echo "${var:-default}"    # Test with unset
   ```

## Availability: Parameter Expansion Across Different Builds

Parameter expansion is a core shell feature that works in all builds, though some advanced features may have limitations.

### POSIX API Build (Full Parameter Expansion)

- ✅ All forms work completely as specified
- ✅ Simple expansion: `${var}`
- ✅ Default values: `${var:-word}`, `${var-word}`
- ✅ Assignment: `${var:=word}`, `${var=word}`
- ✅ Error checking: `${var:?word}`, `${var?word}`
- ✅ Alternative: `${var:+word}`, `${var+word}`
- ✅ String length: `${#var}`
- ✅ Pattern matching: `${var#pattern}`, `${var##pattern}`, `${var%pattern}`, `${var%%pattern}`
- ✅ All expansions within patterns work
- ✅ Proper quoting behavior

### UCRT API Build (Full Parameter Expansion)

- ✅ All parameter expansion forms work identically to POSIX
- ✅ This is pure string manipulation—OS-independent

**No platform-specific issues**: Parameter expansion is text processing that happens in the shell parser. It doesn't depend on OS APIs.

The only potential difference would be in path handling within the values, but the expansion mechanisms themselves work the same:
```bash
# Works the same on Windows
path="C:/Users/name/file.txt"
echo ${path##*/}              # file.txt
echo ${path%/*}               # C:/Users/name
```

### ISO C Build (Full Parameter Expansion)

- ✅ All parameter expansion forms work
- ✅ Same as POSIX and UCRT—it's pure parsing

**Why it works**: Parameter expansion is:
- String manipulation
- Pattern matching against strings
- Variable substitution

None of these require OS-specific functionality. They're implemented in the shell's parser and string processing.

**The only limitation** would be if certain variables don't exist or work properly (like `$HOME` in ISO C), but the expansion syntax itself works fine:
```bash
# Expansion works even if HOME doesn't exist
echo ${HOME:-/default}        # Works fine
echo ${file#prefix}           # Works fine
echo ${var:?required}         # Works fine (would error if unset)
```

### Summary Table

| Feature | POSIX | UCRT | ISO C |
|---------|-------|------|-------|
| `${var}` | ✅ Yes | ✅ Yes | ✅ Yes |
| `${var:-word}` | ✅ Yes | ✅ Yes | ✅ Yes |
| `${var:=word}` | ✅ Yes | ✅ Yes | ✅ Yes |
| `${var:?word}` | ✅ Yes | ✅ Yes | ✅ Yes |
| `${var:+word}` | ✅ Yes | ✅ Yes | ✅ Yes |
| `${#var}` | ✅ Yes | ✅ Yes | ✅ Yes |
| `${var#pattern}` | ✅ Yes | ✅ Yes | ✅ Yes |
| `${var##pattern}` | ✅ Yes | ✅ Yes | ✅ Yes |
| `${var%pattern}` | ✅ Yes | ✅ Yes | ✅ Yes |
| `${var%%pattern}` | ✅ Yes | ✅ Yes | ✅ Yes |
| Pattern matching | ✅ Full | ✅ Full | ✅ Full |
| Nested expansions | ✅ Yes | ✅ Yes | ✅ Yes |
| Quoting behavior | ✅ Full | ✅ Full | ✅ Full |

### Choosing Your Build for Parameter Expansion

**Good news**: Parameter expansion works identically in all three builds!

This is one of the most portable shell features because:
- It's pure text processing
- No OS interaction required
- No external commands needed
- All implemented in shell parser

**All builds fully support**:
- Providing defaults
- Checking required variables
- String manipulation
- Pattern matching
- All quoting behaviors

**Scripts using parameter expansion are fully portable** across all builds. The only consideration is whether the variables themselves exist (like `$HOME`), but the expansion mechanisms work everywhere.

This makes parameter expansion one of the safest, most portable features to use extensively in shell scripts.
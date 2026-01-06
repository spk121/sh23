# Quoting

One of the most important concepts in shell scripting is **quoting**—the art of telling the shell "treat this literally" or "don't interpret these special characters." Without proper quoting, your scripts will break when filenames have spaces, variables are empty, or patterns are interpreted when they shouldn't be.

Quoting serves three main purposes:
1. **Preserve literal meanings** of special characters
2. **Prevent reserved words** from being recognized as keywords
3. **Control expansion** in here-documents and other contexts

In this section, we'll explore all the quoting mechanisms the shell provides and when to use each one.

## Why Quoting Matters

The shell treats many characters specially. Without quoting, these characters trigger various shell behaviors:

```sh
echo hello world        # Two separate arguments
echo hello; world       # Two commands (semicolon separates)
echo $HOME              # Variable expansion
echo *.txt              # Pattern matching
```

Sometimes you want these behaviors, but often you need the literal characters:

```sh
echo "hello world"      # One argument with space
echo "hello; world"     # Semicolon is literal
echo '$HOME'            # Literal string "$HOME"
echo "*.txt"            # Literal string "*.txt"
```

## Characters That Need Quoting

### Always Special Characters

These characters **always** have special meaning and must be quoted to represent themselves:

```
|  &  ;  <  >  (  )  $  `  \  "  '  <space>  <tab>  <newline>
```

Examples:
- `|` - pipe
- `&` - background job or AND operator
- `;` - command separator
- `<` `>` - redirection
- `(` `)` - subshell grouping
- `$` - variable/expansion
- `` ` `` - command substitution
- `\` - escape character
- `"` `'` - quote characters
- spaces, tabs, newlines - word separators

### Sometimes Special Characters

These characters are special in certain contexts:

```
*  ?  [  ]  ^  -  !  #  ~  =  %  {  ,  }
```

Examples:
- `*` `?` `[` `]` - pattern matching (when unquoted)
- `!` - history expansion (in some shells), negation in brackets
- `#` - comment (at start of word)
- `~` - home directory expansion (at start of word)
- `%` - job control (as `%1`, `%2`)
- `{` `}` - brace expansion (in some shells)

**Best practice**: Quote these whenever you want them literal, even if they're not currently special. Future versions of the standard might make them special in more contexts.

**Exception**: The hyphen `-` doesn't need quoting since it's in the portable filename character set.

## The Four Quoting Mechanisms

The shell provides four ways to quote characters:

1. **Backslash** (`\`) - Escape single characters
2. **Single quotes** (`'...'`) - Preserve everything literally
3. **Double quotes** (`"..."`) - Preserve most things, allow some expansions
4. **Dollar-single-quotes** (`$'...'`) - Preserve literally with escape sequences

Let's explore each in detail.

## 1. Backslash (Escape Character)

The backslash (`\`) removes the special meaning of the **next** character.

### Basic Usage

```bash
echo hello\ world       # Space is literal, becomes one argument
echo \$HOME             # Dollar sign is literal, prints "$HOME"
echo \*.txt             # Asterisk is literal, prints "*.txt"
echo \;                 # Semicolon is literal, not command separator
```

Each backslash protects exactly one character:

```bash
echo \\                 # Prints one backslash
echo \\\$               # Prints "\$" (backslash, then dollar)
```

### Line Continuation

Backslash followed by newline is special—it's treated as **line continuation**:

```bash
echo hello \
     world
# Same as: echo hello world
```

The backslash-newline is completely removed before tokenization:
- Not replaced by a space
- Doesn't act as a word separator
- Simply continues the line

This is useful for breaking long commands:

```bash
command \
    --option1 value1 \
    --option2 value2 \
    --option3 value3
```

### What Backslash Can't Do

You can't use backslash to escape a newline to create a literal newline in a word (use `$'\n'` for that). The backslash-newline is always line continuation, never a literal.

## 2. Single Quotes

Single quotes (`'...'`) preserve the **literal value** of every character inside them. Nothing is special inside single quotes—not even backslash!

### Basic Usage

```bash
echo 'hello world'           # Space preserved
echo '$HOME'                 # Literal "$HOME", no expansion
echo '*.txt'                 # Literal "*.txt", no globbing
echo 'hello; world'          # Semicolon is literal
echo 'backslash: \'          # Backslash is literal
```

Everything inside single quotes is completely literal:

```bash
echo 'Special chars: | & ; < > ( ) $ ` \ " <tab> <newline> * ? [ ]'
# All characters printed literally
```

### The One Thing You Can't Do

**You cannot include a single quote inside single quotes.** There's no escape mechanism inside single quotes:

```bash
# WRONG - Can't escape single quote inside single quotes
echo 'can\'t'               # Error!

# WRONG - Can't use backslash
echo 'it\\'s'               # Prints "it\\'s" literally
```

**Solution**: End the quote, add an escaped quote, start a new quote:

```bash
echo 'can'\''t'             # Prints "can't"
# Breaks down as: 'can' + \' + 't'

echo 'it'"'"'s'             # Prints "it's"
# Breaks down as: 'it' + "'" + 's'
```

Or use double quotes or `$'...'`:

```bash
echo "can't"                # Works fine
echo $'can\'t'              # Works fine
```

### When to Use Single Quotes

Single quotes are best when you want **complete literal preservation**:
- Protecting strings with many special characters
- Passing literal dollar signs, backticks, or backslashes
- When you don't want any expansions at all

```bash
# Literal pattern for grep
grep 'pattern*[0-9]' file.txt

# Literal variable reference for awk
awk '{print $1}' file.txt
```

## 3. Double Quotes

Double quotes (`"..."`) preserve most characters literally, but allow certain expansions:
- `$` still triggers variable expansion, command substitution, arithmetic expansion
- `` ` `` still triggers command substitution
- `\` still escapes certain characters
- Everything else is literal

### Basic Usage

```bash
echo "hello world"          # Space preserved, one argument
echo "Current dir: $PWD"    # Variable expanded
echo "Files: $(ls)"         # Command substitution works
echo "Result: $((2 + 2))"   # Arithmetic expansion works
```

### What's Special Inside Double Quotes

Only three characters are special inside double quotes:

1. **Dollar sign** (`$`) - Expansions still happen:
   ```bash
   echo "$HOME"              # Variable expansion
   echo "$(date)"            # Command substitution
   echo "$((5 * 3))"         # Arithmetic expansion
   ```

2. **Backtick** (`` ` ``) - Command substitution:
   ```bash
   echo "Today is `date`"    # Command substitution
   ```

3. **Backslash** (`\`) - But only before certain characters:
   ```bash
   echo "Quote: \"hello\""   # \" is escaped double quote
   echo "Dollar: \$HOME"     # \$ is escaped dollar
   echo "Command: \`date\`"  # \` is escaped backtick
   echo "Backslash: \\"      # \\ is escaped backslash
   echo "Newline: \
   continues"                # \<newline> is line continuation
   ```

   Other backslashes are literal:
   ```bash
   echo "Path: C:\Users"     # Prints "C:\Users"
   echo "Regex: \d+"         # Prints "\d+"
   ```

### Special Cases: Nested Structures

**Inside `$(...)` command substitution:**
The double quotes don't affect the command inside:
```bash
echo "Result: $(echo "nested quotes")"
# Works fine - quotes inside $(...) are independent
```

**Inside `${...}` parameter expansion:**
For substring processing operations (`#`, `##`, `%`, `%%`):
```bash
var="hello world"
echo "${var#* }"            # Removes up to first space
# Double quotes outside don't affect pattern inside
```

For other parameter expansions, double quotes mostly preserve characters, but:
- Double quotes inside can cause unspecified behavior
- Use carefully or avoid nesting quotes

**The `$@` special parameter:**
Inside double quotes, `"$@"` expands to separate words:
```bash
set -- "arg 1" "arg 2" "arg 3"
echo "$@"                   # Three separate arguments preserved
echo $@                     # Splits on spaces: 6 arguments!
```

This is crucial for preserving arguments with spaces.

### When to Use Double Quotes

Double quotes are best when you want **expansions but preserved whitespace**:

```bash
# Preserve spaces in variables
name="John Doe"
echo "Hello, $name"         # "Hello, John Doe"

# Preserve command output formatting
files="$(ls -l)"
echo "$files"               # Preserves newlines

# Protect empty or unset variables
echo "$empty_var"           # Safe: prints nothing
echo $empty_var             # Dangerous: might cause errors

# Preserve arguments with spaces
rm "$file"                  # Safe with spaces in filename
rm $file                    # Breaks if filename has spaces
```

**Rule of thumb**: Almost always quote your variables with double quotes unless you specifically want word splitting and glob expansion.

## 4. Dollar-Single-Quotes

Dollar-single-quotes (`$'...'`) are like single quotes but support **backslash escape sequences**. This lets you include special characters like newlines and tabs.

### Basic Escape Sequences

```bash
echo $'hello\nworld'        # Prints on two lines
echo $'col1\tcol2\tcol3'    # Tabs between columns
echo $'it\'s working'       # Can include single quotes!
echo $'line1\nline2\nline3' # Multiple newlines
```

### Supported Escape Sequences

| Escape | Produces | Description |
|--------|----------|-------------|
| `\'` | `'` | Single quote |
| `\"` | `"` | Double quote (can also be unescaped) |
| `\\` | `\` | Backslash |
| `\a` | Alert (BEL) | Bell/alert sound |
| `\b` | Backspace | Backspace character |
| `\e` | ESC | Escape character |
| `\f` | Form feed | Page break |
| `\n` | Newline | Line feed |
| `\r` | Carriage return | CR character |
| `\t` | Tab | Horizontal tab |
| `\v` | Vertical tab | Vertical tab |
| `\cX` | Control character | Control-X (e.g., `\cA` = Ctrl-A) |
| `\xHH` | Byte value (hex) | e.g., `\x41` = 'A' |
| `\ddd` | Byte value (octal) | e.g., `\101` = 'A' |

### Examples

**Including quotes:**
```bash
echo $'can\'t stop won\'t stop'
echo $'He said "hello"'        # Can include double quotes unescaped
```

**Formatting:**
```bash
echo $'Name:\tJohn\nAge:\t30'
# Outputs:
# Name:    John
# Age:     30
```

**Control characters:**
```bash
echo $'Start\x07End'           # Includes bell character
echo $'Ctrl-A is \cA'          # Includes Ctrl-A
```

**Hex and octal:**
```bash
echo $'\x48\x65\x6c\x6c\x6f'   # "Hello" in hex
echo $'\101\102\103'           # "ABC" in octal
```

### Special Cases

**Variable-length sequences:**
- `\xHH` - Uses 1 or more hex digits (but >2 is unspecified)
- `\ddd` - Uses 1-3 octal digits (stops at first non-octal or after 3 digits)

```bash
echo $'\x48i'                  # 'H' (0x48) followed by 'i'
echo $'\101BC'                 # 'A' (octal 101) followed by "BC"
```

**Null bytes:**
If `\x00` or `\000` produces a null byte, behavior is unspecified:
- Might include the null byte
- Might stop processing there
- Avoid using null bytes in `$'...'`

**Unrecognized escapes:**
If backslash is followed by anything else, behavior is unspecified:
```bash
echo $'\z'                     # Unspecified!
```

Don't use undefined escape sequences.

### When to Use Dollar-Single-Quotes

Use `$'...'` when you need:
- Literal single quotes in your string
- Special characters like newlines or tabs
- Control characters
- Specific byte values

```bash
# Multi-line strings
message=$'Error occurred:\nPlease check the log\nExiting...'

# Tab-separated data
echo $'Name\tAge\tCity'

# Single quotes in the string
echo $'Don\'t forget to backup!'
```

## Quoting in Different Contexts

### In Command Arguments

```bash
# Without quotes - word splitting happens
echo $filename              # Breaks if filename has spaces

# With double quotes - safe
echo "$filename"            # Preserves spaces

# With single quotes - completely literal
echo '$filename'            # Prints "$filename" literally
```

### In Variable Assignments

Quotes in assignments are often optional but good practice:

```bash
var=hello                   # Works (no special chars)
var="hello world"           # Needed (space)
var='$HOME'                 # Literal dollar sign
var="$HOME"                 # Expanded
```

### In Conditionals

Always quote variables in tests:

```bash
if [ "$var" = "value" ]; then    # Safe
if [ $var = "value" ]; then      # Dangerous (breaks if var is empty)
```

### In For Loops

Quote to preserve spaces:

```bash
# Wrong - splits on spaces
for file in $files; do           # If files="a b", loops twice

# Right - treats as one item
for file in "$files"; do         # One iteration
```

Or use proper glob:
```bash
for file in *.txt; do            # Each file is one item
    echo "$file"                 # Quote when using
done
```

### In Here-Documents

Quoting the delimiter affects content:

```bash
# Unquoted delimiter - expansions happen
cat <<EOF
Today is $(date)
Home is $HOME
EOF

# Quoted delimiter - literal content
cat <<'EOF'
Today is $(date)
Home is $HOME
EOF
```

## Common Quoting Pitfalls

### Pitfall 1: Unquoted Variables

```bash
file="my file.txt"
rm $file                    # ERROR: Tries to remove "my" and "file.txt"
rm "$file"                  # CORRECT: Removes "my file.txt"
```

### Pitfall 2: Empty Variables in Tests

```bash
if [ $var = "test" ]; then  # ERROR if $var is empty: [ = "test" ]
if [ "$var" = "test" ]; then # CORRECT: [ "" = "test" ]
```

### Pitfall 3: Glob Patterns vs. Literal Strings

```bash
pattern="*.txt"
echo $pattern               # Expands to files!
echo "$pattern"             # Prints "*.txt"
```

### Pitfall 4: Mixing Quote Types

```bash
# Wrong - can't nest same quote type directly
echo "She said "hello""    # Syntax error

# Right - escape inner quotes
echo "She said \"hello\""

# Right - alternate quote types
echo 'She said "hello"'
```

### Pitfall 5: Forgetting to Quote `$@`

```bash
# Wrong - loses spaces in arguments
args="$@"
# If arguments were "a b" "c d", $args becomes "a b c d"

# Right - preserves separate arguments
for arg in "$@"; do
    echo "Argument: $arg"
done
```

## Advanced Quoting Techniques

### Concatenating Different Quote Types

You can place different quote types adjacent to each other:

```bash
echo 'Single'\''quote'      # Single, then \', then quote
echo "Value: "$var" end"    # Double quoted parts around unquoted
echo $'Line 1\n'"Line 2"    # Dollar-single then double quotes
```

### Protecting Multiline Strings

```bash
# Using $'...'
message=$'Line 1\nLine 2\nLine 3'

# Using double quotes (actual newlines)
message="Line 1
Line 2
Line 3"

# Using single quotes (actual newlines)
message='Line 1
Line 2
Line 3'
```

### Quoting in Command Substitution

Inside `$(...)`, quotes work normally:

```bash
result=$(echo "hello world")    # Quotes work inside $()
echo "$result"                  # Preserve spaces in result
```

### Complex Real-World Example

```sh
#!/bin/sh

# File with spaces
file="my document.txt"

# Check if exists (quote variable!)
if [ -f "$file" ]; then
    # Read content (quote command substitution!)
    content="$(cat "$file")"

    # Process with preserved formatting
    echo "File contents:"
    echo "$content"

    # Create backup with safe name
    backup="${file}.backup"
    cp "$file" "$backup"
fi
```

## Best Practices

1. **Default to double quotes for variables**: `echo "$var"` not `echo $var`

2. **Use single quotes for literal strings**: `grep 'pattern' file` not `grep pattern file` (unless you need expansions)

3. **Always quote `$@`**: `"$@"` preserves all arguments correctly

4. **Quote in tests**: `[ "$var" = "value" ]` not `[ $var = "value" ]`

5. **Use `$'...'` for special characters**: `echo $'Line 1\nLine 2'` not trying to echo actual newlines

6. **Quote glob patterns when you don't want expansion**: `echo "*.txt"` to show the pattern

7. **When in doubt, quote**: It's safer to quote too much than too little

## Summary: Quick Reference

| Mechanism | Preserves | Allows | Use When |
|-----------|-----------|--------|----------|
| `\` | Next character only | - | Single character escaping |
| `'...'` | Everything | Nothing | Complete literal strings |
| `"..."` | Most characters | `$`, `` ` ``, some `\` | Variables with spaces |
| `$'...'` | Most, with escapes | Escape sequences | Special characters needed |

## Availability: Quoting Across Different Builds

Quoting mechanisms are fundamental to shell parsing and are available in all builds, but the level of support and complexity varies.

### POSIX API Build (Full Quoting Support)

When built with the POSIX API, the shell provides **complete quoting support** as described throughout this document.

### UCRT API Build (Full Quoting Support)

When built with the Universal C Runtime (UCRT) API on Windows, the shell provides **full quoting support**. Quoting is a parsing-level feature that doesn't depend on operating system APIs.

**What's available:**
- ✅ All four quoting mechanisms work identically to POSIX
- ✅ Backslash escaping
- ✅ Single quotes
- ✅ Double quotes
- ✅ Dollar-single-quotes with all escape sequences

**Implementation details:**
Quoting is implemented during tokenization and parsing, which are pure string processing operations. These work the same regardless of the underlying OS.

**Windows-specific considerations:**

1. **Path separators**: Windows uses backslash (`\`) in paths, which can interact with shell quoting:
   ```sh
   # In shell syntax, backslash is an escape
   cd C:\Users          # Might not work as expected

   # Quote the path
   cd 'C:\Users'        # Works
   cd "C:\Users"        # Works
   cd C:\\Users         # Escaped backslashes work

   # Or use forward slashes (Windows accepts these)
   cd C:/Users          # Works
   ```

2. **Line endings**: Windows uses CRLF (`\r\n`), but the shell handles this transparently. `$'\r\n'` produces Windows-style line endings.

3. **Case sensitivity**: Windows filesystem is case-insensitive, but quoted strings remain case-sensitive in shell comparisons:
   ```bash
   if [ "$var" = "VALUE" ]; then    # Case-sensitive comparison
   ```

### ISO C Build (Full Quoting Support)

When built with only ISO C standard library functions, the shell provides **full quoting support**. Like UCRT, quoting is a parsing feature independent of system APIs.

**Limitations are elsewhere:**
The ISO C build's limitations are in:
- Process control (no subshells, pipelines)
- File operations (no directory enumeration)
- Signal handling (minimal)

But quoting itself works fully because it's pure string processing during parsing.


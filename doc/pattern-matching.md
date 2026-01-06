# Pattern Matching and Filename Expansion

When you type `*.txt` or `file?.log` in the shell, you're using **pattern matching**—a powerful feature that lets you specify multiple files with a single expression. This is also called **globbing** or **filename expansion**, and it's one of the shell's most useful capabilities.

Understanding how patterns work is essential for writing effective shell commands and scripts. In this section, we'll explore how the shell interprets patterns, what special characters mean, and how pattern matching works with filenames.

## What Are Patterns?

A **pattern** is a string that can match one or more other strings. Patterns use special characters that have meanings beyond their literal value:

- `?` matches any single character
- `*` matches any string (zero or more characters)
- `[...]` matches any one of the enclosed characters

When the shell sees a pattern in a command, it attempts to match it against existing filenames and expands the pattern to the list of matching files.

## Single Character Patterns

### Ordinary Characters

Most characters in a pattern are **ordinary characters** that match themselves. For example:
- `a` matches the letter 'a'
- `file` matches the literal string "file"
- `123` matches the digits "123"

Any character can be an ordinary character except:
- NUL (the null byte)
- Shell special characters that require quoting (like `$`, `` ` ``, `|`, etc.)
- The three pattern special characters: `?`, `*`, `[`

### Quoting and Escaping

To match a special character literally, you need to quote or escape it:

```bash
# Match a file literally named "file*.txt"
ls 'file*.txt'    # Single quotes
ls file\*.txt     # Backslash escape
ls "file*.txt"    # Double quotes
```

The backslash (`\`) escapes the following character, making it match literally:
- `\*` matches a literal asterisk
- `\?` matches a literal question mark
- `\\` matches a literal backslash

**Important**: If a pattern ends with an unescaped backslash (like `file\`), the behavior is undefined. Don't do this!

### The Three Special Pattern Characters

When **unquoted** and **not inside a bracket expression**, these characters have special meanings:

#### 1. Question Mark `?` - Match Any Single Character

The `?` matches exactly one character (any character):

```bash
file?.txt         # Matches: file1.txt, fileA.txt, file_.txt
                  # Doesn't match: file.txt (no character), file10.txt (two characters)

test?.log         # Matches: test1.log, testX.log
                  # Doesn't match: test.log, test12.log
```

#### 2. Asterisk `*` - Match Any String

The `*` matches zero or more characters:

```bash
*.txt             # Matches any file ending in .txt
file*             # Matches any file starting with "file"
*test*            # Matches any file containing "test"
*                 # Matches all files (in current directory)
```

**Greedy matching**: The `*` matches as many characters as possible while still allowing the rest of the pattern to match:

```bash
# Pattern: a*b
# String: "axxxbyyy"
# The * matches "xxxbyyy" first, but that prevents "b" from matching
# So it backtracks and matches "xxx", allowing "b" to match the second 'b'
```

#### 3. Left Square Bracket `[` - Bracket Expression

The `[` starts a **bracket expression** that matches any one of the characters inside:

```bash
file[123].txt     # Matches: file1.txt, file2.txt, file3.txt
[abc]test         # Matches: atest, btest, ctest
```

A `[` that doesn't form a valid bracket expression matches itself literally:
```bash
file[.txt         # Matches literally "file[.txt" (no closing ])
```

## Bracket Expressions

Bracket expressions are powerful patterns for matching single characters from a set.

### Basic Bracket Expressions

```bash
[abc]             # Matches: a, b, or c
[0-9]             # Matches: any digit (range)
[a-z]             # Matches: any lowercase letter
[A-Za-z]          # Matches: any letter
```

### Non-Matching Lists (Negation)

Use `!` at the start to match any character **except** those listed:

```bash
[!abc]            # Matches any character except a, b, or c
[!0-9]            # Matches any non-digit
file[!123].txt    # Matches: file4.txt, fileA.txt, but not file1.txt
```

**Note**: The POSIX standard uses `!` for negation in patterns, not `^` (which is used in regular expressions). Starting a bracket expression with `^` produces unspecified results.

### Character Classes

Bracket expressions support character classes like `[:digit:]`, `[:alpha:]`, etc.:

```bash
[[:digit:]]       # Matches any digit
[[:alpha:]]       # Matches any letter
[[:alnum:]]       # Matches any alphanumeric character
[[:space:]]       # Matches any whitespace
[[:punct:]]       # Matches any punctuation
```

Common character classes:
- `[:lower:]` - lowercase letters
- `[:upper:]` - uppercase letters
- `[:alpha:]` - any letter
- `[:digit:]` - digits 0-9
- `[:alnum:]` - letters and digits
- `[:space:]` - whitespace characters
- `[:punct:]` - punctuation

Example:
```bash
file[[:digit:]].txt         # Matches: file0.txt through file9.txt
[[:upper:]]*                # Matches files starting with uppercase letter
```

### Ranges

Ranges match any character between two endpoints:

```bash
[a-z]             # Lowercase letters
[A-Z]             # Uppercase letters
[0-9]             # Digits
[a-zA-Z0-9]       # Alphanumeric
```

Be careful with ranges—they depend on the locale's collating sequence. In some locales, `[a-z]` might include uppercase letters!

## Multiple Character Patterns

Patterns can be combined to match longer strings:

### Concatenation

Simply place patterns next to each other:

```bash
file*.txt         # "file" + * + ".txt"
                  # Matches: file.txt, file123.txt, fileABC.txt

test[0-9][0-9].log  # "test" + digit + digit + ".log"
                    # Matches: test00.log, test99.log, test42.log
```

### Asterisk Rules

When `*` appears with other patterns:

1. `*` matches zero or more characters
2. Multiple `*` characters work together
3. Each `*` matches the maximum number of characters that still allows the rest of the pattern to match

Examples:
```bash
*.*               # Matches files with a dot (most files)
a*b*c             # Matches strings with a, then b, then c
test*.log         # Matches test.log, testABC.log, test123.log
```

## Filename Expansion: Special Rules

When patterns are used for filename expansion (globbing), additional rules apply:

### Rule 1: Slashes Must Be Explicit

The `/` character must be matched explicitly—it's **never** matched by `*`, `?`, or bracket expressions:

```bash
*/*.txt           # Matches .txt files in subdirectories
                  # NOT files with / in their name

# To match "dir/file.txt":
dir/file.txt      # Explicit slash
*/file.txt        # First component can vary, slash is explicit
```

Example: The pattern `a[b/c]d` doesn't match `a/d`. It only matches the literal filename `a[b/c]d` (because the `/` in the bracket expression makes it invalid, so the `[` is treated as ordinary).

### Rule 2: Leading Dots Must Be Explicit

If a filename starts with `.` (a hidden file), the dot must be matched explicitly:

```bash
.*                # Matches hidden files (dot is explicit)
*                 # Does NOT match hidden files (dot not explicit)

.??*              # Matches hidden files with at least 2 chars after dot
```

**The dot is leading if it's:**
- At the start of a filename: `.bashrc`
- Immediately after a slash: `dir/.config`

**Examples:**
```bash
.*                # Matches: .bashrc, .config, .hidden
*                 # Matches: file.txt, README
                  # Doesn't match: .bashrc, .hidden

dir/.*            # Matches hidden files in dir/
dir/*             # Matches non-hidden files in dir/
```

**Bracket expressions with leading dots:**
- `[!a]` won't match a leading dot (it's a non-matching list)
- `[%-0]` won't match a leading dot (it's a range)
- `[[:punct:]]` won't match a leading dot (it's a character class)
- `[.abc]` might or might not match a leading dot (unspecified)

### Rule 3: Pattern Matching Against Existing Files

When a pattern contains special characters (`*`, `?`, `[`), the shell:

1. **Searches** for matching filenames in the filesystem
2. **Requires permissions**:
   - Read permission for directories with `*`, `?`, or `[` patterns
   - Search permission for intermediate directories
3. **Sorts** matches according to the current locale's collating sequence
4. **Replaces** the pattern with the sorted list of matches

If the pattern matches no files, it's **left unchanged** (the pattern string itself is used).

**Permission example:**
```bash
# Pattern: /foo/bar/x*/bam
# Permissions needed:
# - Search: /
# - Search: /foo
# - Search and read: /bar (to see x* entries)
# - Search: each x* directory
```

### Rule 4: No Matches = No Expansion

If a pattern doesn't match any files, it stays as-is:

```bash
$ ls *.nonexistent
ls: cannot access '*.nonexistent': No such file or directory
# The pattern "*.nonexistent" was passed literally to ls
```

This can be surprising! Some shells have options to change this behavior (like bash's `nullglob` and `failglob`).

### Rule 5: No Special Characters = No Expansion

If a pattern doesn't contain any unquoted `*`, `?`, or `[`, it's left unchanged—no filename matching happens:

```bash
file.txt          # No special chars -> not a pattern
test              # Passed as-is
```

## Practical Examples

### Matching Files by Extension

```bash
*.txt             # All .txt files
*.{txt,log}       # Not POSIX! (bash brace expansion)
*.[tl][xo][tg]    # POSIX alternative: matches .txt or .log
```

### Matching Files with Specific Patterns

```bash
test[0-9].log     # test0.log through test9.log
file-[a-z][a-z].txt  # file-ab.txt, file-xy.txt, etc.
backup.*          # backup.tar, backup.zip, etc.
```

### Excluding Files

```bash
*[!~]             # All files not ending in ~
[!.]*.txt         # .txt files not starting with dot
```

### Finding Files in Subdirectories

```bash
*/*.txt           # .txt files in immediate subdirectories
*/*/*.txt         # .txt files two levels deep
```

### Working with Hidden Files

```bash
.*                # All hidden files (including . and ..)
.??*              # Hidden files with at least 2 chars after dot
.[!.]*            # Hidden files except . and ..
```

## Common Pitfalls

### Pitfall 1: Forgetting to Quote Patterns

```bash
# WRONG - Shell expands before find sees it
find . -name *.txt

# RIGHT - Quote so find gets the pattern
find . -name '*.txt'
```

### Pitfall 2: Matching . and ..

```bash
.*                # Matches ., .., and other hidden files
```

This can be dangerous with `rm -rf .*`! Use `.??*` or `.[!.]*` instead.

### Pitfall 3: Patterns with No Matches

```bash
# If *.bak matches nothing, rm gets the literal string
rm *.bak          # Error: cannot remove '*.bak'
```

Check before deleting:
```bash
if ls *.bak > /dev/null 2>&1; then
    rm *.bak
fi
```

### Pitfall 4: Case Sensitivity

Patterns are case-sensitive:
```bash
*.txt             # Doesn't match file.TXT or file.Txt
```

To match case-insensitively in POSIX:
```bash
*.[tT][xX][tT]    # Matches .txt, .TXT, .Txt, etc.
```

### Pitfall 5: Special Characters in Filenames

If filenames contain spaces, quotes, or special characters, patterns can behave unexpectedly:

```bash
# File: "my file.txt"
ls *.txt          # Shell expands to: ls my file.txt
                  # ls sees: "my" and "file.txt" (two arguments!)
```

Always quote variables containing patterns:
```bash
pattern="*.txt"
ls "$pattern"     # Wrong: quotes prevent expansion
ls $pattern       # Right: pattern expands
```

But quote when using the results:
```bash
for file in *.txt; do
    echo "Processing: $file"    # Quote $file
done
```

## Advanced Pattern Techniques

### Combining Patterns

```bash
*.[ch]            # .c or .h files
*.[ch]pp          # .cpp or .hpp files
file[0-9][0-9]    # file00 through file99
```

### Multiple Asterisks

```bash
a*b*c             # Has 'a', then 'b', then 'c' somewhere
*test*            # Contains "test" anywhere
**                # Same as * (multiple asterisks are redundant)
```

### Character Ranges with Classes

```bash
[[:lower:]]*      # Files starting with lowercase
[[:digit:]][[:digit:]]*.txt  # Files starting with two digits
```

## Best Practices

1. **Quote patterns in commands**: When passing patterns to commands like `find`, `grep`, or in variables, quote them to prevent premature expansion.

2. **Test patterns first**: Before using patterns with dangerous commands like `rm`, test with `ls` or `echo`:
   ```bash
   echo *.bak      # See what would be matched
   rm *.bak        # Then delete
   ```

3. **Be explicit with hidden files**: Don't rely on `.*` alone. Use `.??*` or explicitly list what you want.

4. **Check for no matches**: When a pattern might not match anything, check first or handle the error.

5. **Use bracket expressions for portability**: Instead of bash-specific features, use POSIX bracket expressions.

6. **Remember locale effects**: Ranges like `[a-z]` depend on your locale. Use character classes for reliable behavior.

## Summary: Pattern Quick Reference

| Pattern | Matches | Example | Matches | Doesn't Match |
|---------|---------|---------|---------|---------------|
| `?` | Any single character | `file?.txt` | `file1.txt` | `file.txt` |
| `*` | Zero or more characters | `*.txt` | `any.txt`, `.txt` (if not leading) | |
| `[abc]` | One of: a, b, c | `file[123].txt` | `file1.txt` | `file4.txt` |
| `[!abc]` | Not a, b, or c | `file[!0-9].txt` | `fileA.txt` | `file5.txt` |
| `[a-z]` | Range: a through z | `[0-9][0-9]` | `42`, `00` | `5`, `123` |
| `[[:class:]]` | Character class | `[[:digit:]]` | `0-9` | `a` |
| `/` | Must be explicit | `*/*.txt` | `dir/a.txt` | Files with `/` in name |
| `.` (leading) | Must be explicit | `.*` | `.bashrc` | (matched explicitly) |

## Availability: Pattern Matching Across Different Builds

Pattern matching and filename expansion capabilities vary significantly depending on which API the shell is built with.

### POSIX API Build (Full Pattern Matching)

When built with the POSIX API, the shell provides **complete pattern matching and filename expansion** as described throughout this document.

**What's available:**
- ✅ Full pattern syntax: `*`, `?`, `[...]`
- ✅ Bracket expressions with ranges: `[a-z]`, `[0-9]`
- ✅ Non-matching lists: `[!abc]`
- ✅ Character classes: `[[:alpha:]]`, `[[:digit:]]`, etc.
- ✅ Filename expansion (globbing) with directory traversal
- ✅ Proper handling of leading dots (hidden files)
- ✅ Explicit slash matching rules
- ✅ Locale-aware collating and sorting
- ✅ Permission checking during expansion
- ✅ No matches = pattern left unchanged

**Implementation:**
- Uses `opendir()`, `readdir()`, `closedir()` to enumerate directory contents
- Uses `fnmatch()` or equivalent for pattern matching
- Full filesystem access for finding matching files

This provides the complete, predictable globbing behavior you expect from a POSIX shell.

### UCRT API Build (Full Pattern Matching via UCRT Primitives)

When built with the Universal C Runtime (UCRT) API on Windows, the shell provides **full pattern matching functionality**. While UCRT doesn't have POSIX functions like `fnmatch()`, it has equivalent primitives that allow building a fully compliant implementation.

**What's available:**
- ✅ Full pattern syntax: `*`, `?`, `[...]`
- ✅ Bracket expressions: `[a-z]`, `[!abc]`, `[[:digit:]]`
- ✅ Filename expansion with directory traversal
- ✅ Leading dot handling
- ✅ All pattern matching rules as described

**Implementation details:**
- Uses `_findfirst()`, `_findnext()`, `_findclose()` for directory enumeration (equivalent to POSIX `opendir`/`readdir`)
- Pattern matching can be implemented in the shell itself using the same algorithms
- Windows filesystem is case-insensitive by default, but patterns remain case-sensitive in the shell
- Path separators: both `/` and `\` work, but pattern rules apply to `/` as documented

**Windows-specific considerations:**

1. **Case insensitivity**: Windows filesystems are typically case-insensitive, so:
   ```bash
   file.txt          # Matches file.txt, FILE.TXT, File.Txt
   ```
   But the pattern itself is still case-sensitive:
   ```bash
   *.TXT             # Only looks for uppercase .TXT
   *.[tT][xX][tT]    # Explicitly matches all cases
   ```

2. **Path separators**: Windows uses `\`, but POSIX shells use `/`. The shell should handle both:
   ```bash
   */*.txt           # Works
   *\*.txt           # Also works on Windows
   ```

3. **Hidden files**: Windows uses file attributes, not leading dots, for hidden files. The leading dot rule still applies to filenames that start with `.`, but Windows hidden files without leading dots won't be hidden from `*`.

4. **Reserved names**: Windows has reserved names like `CON`, `PRN`, `AUX`, etc. These should be handled according to Windows rules.

**Practical outcome**: Pattern matching in the UCRT build should be functionally identical to the POSIX build for most use cases. The underlying implementation differs, but the behavior is the same.

### ISO C Build (No Pattern Matching)

When built with only ISO C standard library functions, the shell has **no pattern matching or filename expansion capability**. ISO C provides no way to enumerate directory contents.

**What's available:**
- ❌ No pattern matching at all
- ❌ No filename expansion
- ❌ No directory enumeration
- ❌ No globbing

**What this means:**

Patterns are passed literally to commands without expansion:
```bash
$ ls *.txt
# Pattern "*.txt" is passed unchanged to the system() call
# The behavior depends on the system's command interpreter:
#   - On Unix: the system shell might expand it
#   - On Windows cmd.exe: it won't expand it
#   - Result: unpredictable and system-dependent
```

**Practical implications:**

1. **Shell cannot expand patterns**: The shell itself has no globbing capability
   ```bash
   *.txt             # Passed as literal string "*.txt"
   ```

2. **System command interpreter might expand**: When using `system()`, the system's shell might do expansion:
   ```bash
   # On Unix/Linux with system shell:
   ls *.txt          # System shell might expand this
   
   # On Windows with cmd.exe:
   dir *.txt         # cmd.exe will expand this
   
   # But behavior is completely system-dependent
   ```

3. **No control over expansion**: The shell has no way to control or predict pattern expansion behavior

4. **Quoting doesn't help**: Even quoted patterns can't be expanded by the shell because there's no expansion mechanism at all

**Recommendation**: Don't rely on pattern matching in the ISO C build. Use explicit filenames:
```bash
# Instead of:
ls *.txt          # Unpredictable

# Use:
ls file1.txt file2.txt file3.txt    # Explicit
```

Or write scripts that explicitly enumerate what they need without patterns.

### Summary Table

| Feature | POSIX | UCRT | ISO C |
|---------|-------|------|-------|
| Pattern syntax (`*`, `?`, `[...]`) | ✅ Full | ✅ Full | ❌ None |
| Bracket expressions | ✅ Yes | ✅ Yes | ❌ No |
| Character classes | ✅ Yes | ✅ Yes | ❌ No |
| Filename expansion | ✅ Yes | ✅ Yes | ❌ No |
| Directory traversal | ✅ Yes | ✅ Yes | ❌ No |
| Hidden file rules | ✅ Yes | ✅ Yes | ❌ No |
| Slash matching rules | ✅ Yes | ✅ Yes | ❌ No |
| No match behavior | ✅ Controlled | ✅ Controlled | ⚠️ System-dependent |
| Case sensitivity | ✅ Yes | ⚠️ Filesystem-dependent | ⚠️ System-dependent |
| Predictable behavior | ✅ Yes | ✅ Yes | ❌ No |

### Choosing Your Build for Pattern Matching

- **Need reliable pattern matching?** Both POSIX and UCRT builds provide full, predictable globbing
- **On Windows?** Use the UCRT build—it provides complete pattern matching with Windows filesystem integration
- **Maximum portability?** Use the POSIX build if available; UCRT is a close second
- **Using ISO C?** Avoid patterns entirely—use explicit filenames or accept unpredictable system-dependent behavior

For any script that relies on pattern matching and filename expansion, avoid the ISO C build. Both POSIX and UCRT builds provide reliable, well-defined behavior.
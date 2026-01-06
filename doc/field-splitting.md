# Field Splitting

**Field splitting** is how the shell breaks text into separate words (fields) after expansions. It's one of the most important—and often misunderstood—aspects of shell behavior. Understanding field splitting is crucial for writing correct shell scripts, especially when dealing with filenames that contain spaces.

Field splitting happens **after** parameter expansion, command substitution, and arithmetic expansion, but only on the **unquoted** results of those expansions.

## What Is Field Splitting?

When you write:
```bash
files="file1.txt file2.txt file3.txt"
echo $files
```

The shell expands `$files` to get the string `file1.txt file2.txt file3.txt`, then **splits** it into three separate fields (words) because of the spaces. Each field becomes a separate argument to `echo`.

Without field splitting, `echo` would receive one argument. With field splitting, it receives three.

## When Field Splitting Happens

Field splitting occurs **only on**:
- Results of **unquoted** parameter expansion
- Results of **unquoted** command substitution
- Results of **unquoted** arithmetic expansion

**It does NOT occur on**:
- Quoted expansions: `"$var"`, `"$(command)"`, `"$((expr))"`
- Literal text in the command line
- The results of expansions that occurred within double quotes

Example:
```bash
var="one two three"

# Unquoted - field splitting happens
echo $var              # Three arguments: "one" "two" "three"

# Quoted - no field splitting
echo "$var"            # One argument: "one two three"

# Literal - no field splitting
echo one two three     # Three arguments (three literal words)
```

## The IFS Variable

**IFS** (Internal Field Separator) controls how field splitting works. It's a variable that contains the characters used as delimiters.

### Default IFS Value

If `IFS` is **unset**, it behaves as if set to:
```
<space><tab><newline>
```

These three characters are the default field separators.

```bash
unset IFS
var="one two	three
four"
echo $var              # Splits on space, tab, and newline
# Output: one two three four (all on one line)
```

### Setting IFS

You can change what characters split fields:

```bash
# Split on colons only
IFS=:
path="/usr/bin:/usr/local/bin:/bin"
for dir in $path; do
    echo "Dir: $dir"
done
# Output:
# Dir: /usr/bin
# Dir: /usr/local/bin
# Dir: /bin
```

### Empty IFS

If `IFS` is set to an **empty string**, field splitting **does not occur**:

```bash
IFS=""
var="one two three"
echo $var              # One argument: "one two three"
```

However, entirely empty fields (that resulted from expansions) are still removed.

### IFS Not Set vs. Empty

- **IFS unset**: Splits on space, tab, newline (default behavior)
- **IFS empty**: No splitting occurs

## IFS White Space vs. Other IFS Characters

IFS characters fall into two categories:

### IFS White Space

These are special: space, tab, and newline when they appear in IFS.

**Properties**:
- Multiple consecutive IFS white space characters are treated as a single delimiter
- Leading and trailing IFS white space is discarded
- They don't create empty fields

```bash
IFS=" "
var="  one   two  "     # Leading, trailing, and multiple spaces
for word in $var; do
    echo "[$word]"
done
# Output:
# [one]
# [two]
# Note: No empty fields
```

**Other white-space characters**: Implementation may treat other locale-specific whitespace characters as IFS white space, but this is implementation-defined.

### Other IFS Characters

Non-whitespace characters in IFS:

**Properties**:
- Each occurrence is a separate delimiter
- Can create empty fields
- Leading/trailing ones create empty fields

```bash
IFS=:
var=":one::two:"        # Leading, trailing, and consecutive colons
for word in $var; do
    echo "[$word]"
done
# Output:
# []                    # Empty field from leading :
# [one]
# []                    # Empty field from ::
# [two]
# []                    # Empty field from trailing :
```

## How Field Splitting Works

The algorithm processes each field containing expansion results:

### Step-by-Step Process

1. **Start with empty candidate field**
2. **Process input byte by byte**:
   - **Byte from non-expansion**: Append to candidate, continue
   - **Expansion result, not IFS**: Append to candidate, continue
   - **Expansion result, IFS white space**: Skip it, continue
   - **Expansion result, non-whitespace IFS**: Note it was seen, continue
3. **Delimit field** if candidate is non-empty or non-whitespace IFS was seen
4. **Start new candidate field**
5. **Repeat** until input is exhausted

### Special Cases

**Empty input or all IFS white space**: Results in **zero fields** (not one empty field)

```bash
var=""
for word in $var; do    # Loop doesn't execute
    echo "[$word]"
done
# No output
```

**Non-whitespace IFS at boundaries**: Creates empty fields

```bash
IFS=:
var=":word:"
for word in $var; do
    echo "[$word]"
done
# Output:
# []
# [word]
# []
```

## Practical Examples

### Example 1: Default Splitting (Space, Tab, Newline)

```bash
#!/bin/bash
# Default IFS behavior
data="apple banana cherry"

for item in $data; do
    echo "Item: $item"
done
# Output:
# Item: apple
# Item: banana
# Item: cherry
```

### Example 2: Splitting on Custom Delimiter

```bash
#!/bin/bash
# Split CSV data
IFS=,
data="John,Doe,30,Engineer"

set -- $data
echo "First: $1"
echo "Last: $2"
echo "Age: $3"
echo "Job: $4"
```

### Example 3: Reading Colon-Separated Paths

```bash
#!/bin/bash
# Process PATH variable
old_IFS="$IFS"
IFS=:

for dir in $PATH; do
    echo "Directory: $dir"
done

IFS="$old_IFS"
```

### Example 4: Processing Lines from a File

```bash
#!/bin/bash
# WRONG way - subject to field splitting
for line in $(cat file.txt); do
    echo "Line: $line"        # Splits on spaces within lines!
done

# RIGHT way - quote to prevent splitting
while IFS= read -r line; do
    echo "Line: $line"
done < file.txt
```

### Example 5: Handling Filenames with Spaces

```bash
#!/bin/bash
# WRONG - breaks on spaces
files=$(ls *.txt)
for file in $files; do        # Splits filenames with spaces
    echo "File: $file"
done

# RIGHT - quote to preserve filenames
for file in *.txt; do         # Globbing, not expansion
    echo "File: $file"
done

# Or if you must use command substitution:
while IFS= read -r file; do
    echo "File: $file"
done < <(ls *.txt)
```

### Example 6: Mixed IFS Characters

```bash
#!/bin/bash
# Use both whitespace and non-whitespace IFS
IFS=' :'
data="one two:three  four:five"

for item in $data; do
    echo "[$item]"
done
# Output:
# [one]
# [two]
# [three]
# [four]
# [five]
```

## The Quoting Solution

**The most important rule**: Quote your expansions to prevent field splitting:

```bash
# Unquoted - field splitting happens
var="file with spaces.txt"
cp $var backup/                  # WRONG: splits into 3 arguments

# Quoted - no field splitting
cp "$var" backup/                # RIGHT: one argument
```

**Always quote** unless you explicitly want field splitting:
- `"$var"` - no splitting
- `"$(command)"` - no splitting
- `"$((expr))"` - no splitting

## Special Behaviors

### Empty Fields Removed

When IFS is empty and expansion results in empty string, it's removed:

```bash
IFS=""
empty=""
echo before $empty after
# Output: before after (no empty field)
```

### Quote Removal Happens Later

Field splitting happens **before** quote removal. Fields with any quoting characters are never empty at the field splitting stage, so they survive.

### Multiple Expansions

If a field contains results from multiple expansions, each expansion's result is subject to field splitting independently:

```bash
a="one two"
b="three four"
echo $a $b
# Results in: "one" "two" "three" "four"
```

## Common Pitfalls

### Pitfall 1: Unquoted Variables

```bash
# WRONG
filename="my document.txt"
cat $filename              # Tries: cat "my" "document.txt"

# RIGHT
cat "$filename"            # cat "my document.txt"
```

### Pitfall 2: Unquoted Command Substitution

```bash
# WRONG
for file in $(ls *.txt); do
    # Breaks if filenames have spaces
done

# RIGHT
for file in *.txt; do
    # Use globbing instead
done
```

### Pitfall 3: Forgetting to Save/Restore IFS

```bash
# WRONG
IFS=:
# ... do stuff ...
# IFS still : for rest of script!

# RIGHT
old_IFS="$IFS"
IFS=:
# ... do stuff ...
IFS="$old_IFS"
```

### Pitfall 4: Setting IFS in a Pipeline

```bash
# Surprising - IFS change lost after pipeline
echo "a:b:c" | IFS=: read x y z
echo "$x"                  # Empty! (pipeline ran in subshell)

# Better - use while loop
IFS=: read x y z <<< "a:b:c"
echo "$x"                  # "a"
```

### Pitfall 5: Expecting Empty Fields with Whitespace IFS

```bash
# Whitespace IFS doesn't create empty fields
IFS=" "
var="  one   two  "
for word in $var; do
    echo "[$word]"
done
# Output: [one] [two] (no empty fields)

# Non-whitespace IFS does create empty fields
IFS=":"
var="::one::two::"
for word in $var; do
    echo "[$word]"
done
# Output: [] [] [one] [] [two] [] []
```

### Pitfall 6: Word Splitting in Arithmetic

```bash
# Can cause confusion
var="2 + 2"
echo $((var))              # ERROR: tries to evaluate "2" (after split)

# Quote it
echo $(($var))             # ERROR: invalid (spaces in expression)

# Or use variable directly
num=4
echo $((num))              # 4
```

## Best Practices

1. **Quote all expansions by default**:
   ```bash
   echo "$var"
   cp "$file" "$dest"
   for line in "$(cat file)"; do ...
   ```

2. **Only leave unquoted when you want splitting**:
   ```bash
   options="-v -r -f"
   command $options file    # Want -v, -r, -f as separate args
   ```

3. **Save and restore IFS**:
   ```bash
   old_IFS="$IFS"
   IFS=:
   # ...
   IFS="$old_IFS"
   ```

4. **Use local IFS in functions** (if your shell supports it):
   ```bash
   process_csv() {
       local IFS=,
       # IFS change only affects this function
   }
   ```

5. **Prefer alternative approaches**:
   ```bash
   # Instead of changing IFS for one operation:
   IFS=: read -r a b c <<< "$data"
   
   # Or use parameter expansion:
   first="${data%%:*}"
   ```

6. **Use arrays for lists** (if available, not POSIX):
   ```bash
   # bash arrays avoid field splitting
   files=("file1.txt" "file with spaces.txt" "file3.txt")
   for file in "${files[@]}"; do
       cp "$file" backup/
   done
   ```

7. **Test with troublesome data**:
   ```bash
   # Test scripts with spaces, tabs, newlines in data
   filename="file with	tabs
   and newlines.txt"
   ```

## Debugging Field Splitting

To see how the shell splits fields, use `printf`:

```bash
var="one two three"

# See the splits
printf "[%s]\n" $var
# Output:
# [one]
# [two]
# [three]

# Without splitting
printf "[%s]\n" "$var"
# Output:
# [one two three]
```

Or use `set -x` to see how the shell interprets commands:

```bash
set -x
var="one two three"
echo $var
# Shows: + echo one two three

echo "$var"
# Shows: + echo 'one two three'
```

## Availability: Field Splitting Across Different Builds

Field splitting is a core shell parsing feature that works identically across all builds.

### POSIX API Build (Full Field Splitting)

- ✅ Full field splitting support
- ✅ IFS variable works as specified
- ✅ White space vs. non-whitespace IFS distinction
- ✅ All splitting rules apply
- ✅ Empty vs. unset IFS behavior

### UCRT API Build (Full Field Splitting)

- ✅ Identical to POSIX build
- ✅ All field splitting rules work the same

**No platform differences**: Field splitting is pure string processing in the shell parser. It doesn't involve OS APIs.

**Windows considerations**:
- Path separators (`;` vs. `:` in PATH) don't affect field splitting
- Field splitting works on any characters you specify in IFS

### ISO C Build (Full Field Splitting)

- ✅ Full field splitting support
- ✅ Identical to POSIX and UCRT

**Why it works**: Field splitting only requires:
- String scanning (character by character)
- Character comparison (is byte in IFS?)
- String building (accumulating candidate fields)
- List management (collecting output fields)

All of these are basic string operations available in any C implementation.

### Summary Table

| Feature | POSIX | UCRT | ISO C |
|---------|-------|------|-------|
| Field splitting | ✅ Full | ✅ Full | ✅ Full |
| IFS variable | ✅ Yes | ✅ Yes | ✅ Yes |
| White space IFS | ✅ Yes | ✅ Yes | ✅ Yes |
| Non-whitespace IFS | ✅ Yes | ✅ Yes | ✅ Yes |
| Empty IFS | ✅ Yes | ✅ Yes | ✅ Yes |
| Unset IFS | ✅ Yes | ✅ Yes | ✅ Yes |
| Quote prevention | ✅ Yes | ✅ Yes | ✅ Yes |
| All algorithm steps | ✅ Yes | ✅ Yes | ✅ Yes |

### Choosing Your Build for Field Splitting

**Excellent news**: Field splitting works identically in all three builds!

This is one of the most portable shell features because:
- Pure string processing
- No OS interaction required
- No external dependencies
- Built into shell parser

**All builds fully support**:
- Customizing IFS
- White space vs. non-whitespace distinction
- Empty field behavior
- Quote prevention of splitting
- All edge cases and special behaviors

**Scripts using field splitting are fully portable** across all builds. The same field splitting bugs and best practices apply everywhere.

**Recommendation**: Field splitting is a fundamental feature you can rely on. The key is understanding when it happens (unquoted expansions) and how to control it (quoting and IFS).

The most important rule remains the same on all platforms: **Quote your expansions** unless you explicitly want field splitting.

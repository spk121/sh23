# Reserved Words

**Reserved words** are special words that have specific meanings to the shell and control the flow and structure of shell commands. They're the keywords of the shell command language—words like `if`, `then`, `while`, and `do` that tell the shell what kind of command structure you're building.

Understanding when the shell recognizes reserved words (and when it doesn't) is crucial for writing correct shell scripts and avoiding confusing errors.

## The Standard Reserved Words

The following words are always reserved when recognized in the proper context:

```
!         {         }         case
do        done      elif      else
esac      fi        for       if
in        then      until     while
```

These 16 words form the core of the shell's control structures:
- **Conditionals**: `if`, `then`, `elif`, `else`, `fi`
- **Loops**: `for`, `while`, `until`, `do`, `done`, `in`
- **Pattern matching**: `case`, `esac`
- **Grouping**: `{`, `}`
- **Negation**: `!`

## When Reserved Words Are Recognized

Here's the critical point: these words are **only** reserved words in specific positions. In other positions, they're just ordinary words.

The shell recognizes a word as reserved **only when**:
1. **None of the characters in the word is quoted**, AND
2. **The word appears in one of these positions:**

### Position 1: First Word of a Command

The very first word of a command is checked for reserved word status:

```bash
if [ -f file ]; then      # 'if' is first word → reserved
    echo "File exists"
fi                        # 'fi' is first word → reserved
```

This is how the shell knows you're starting a control structure.

### Position 2: After Most Reserved Words

After most reserved words, the next word is also checked:

```bash
if [ -f file ]; then      # 'then' follows 'if' → reserved
    echo "exists"
elif [ -d dir ]; then     # 'elif' follows 'then' → reserved
    echo "is directory"   # 'then' follows 'elif' → reserved
else                      # 'else' follows 'then' → reserved
    echo "not found"
fi                        # 'fi' follows 'else' → reserved
```

**Exception**: The words **after** `case`, `for`, and `in` are **not** checked for reserved word status:

```bash
case $var in              # 'in' is reserved, but next word is NOT checked
    pattern) commands ;;
esac

for var in list           # 'in' is reserved, but 'list' is NOT checked
```

This exception exists because these constructs need to use regular words (patterns, variable names, lists) immediately after.

### Position 3: Third Word of a `case` Command

In a `case` command, only the word `in` is valid as the third word:

```bash
case $variable in         # 'case' (1st), '$variable' (2nd), 'in' (3rd)
    pattern) commands ;;
esac
```

If the third word is anything other than `in`, it's a syntax error.

### Position 4: Third Word of a `for` Command

In a `for` command, only `in` or `do` are valid as the third word:

```bash
for var in item1 item2    # 'for' (1st), 'var' (2nd), 'in' (3rd)
do
    echo $var
done

# Or without 'in' list:
for var                   # 'for' (1st), 'var' (2nd)
do                        # 'do' (3rd position)
    echo $var
done
```

These are the only valid third-word options for `for` loops.

## How Quoting Affects Recognition

Quoting **any** character in a reserved word prevents it from being recognized as reserved:

```bash
if [ -f file ]; then      # 'if' and 'then' are reserved
    echo "works"
fi

'if' [ -f file ]; then    # 'if' is NOT reserved (quoted)
# Error: 'if' is treated as a command name

"if" [ -f file ]; then    # 'if' is NOT reserved (quoted)
# Error: tries to run command called 'if'

\if [ -f file ]; then     # 'if' is NOT reserved (escaped)
# Error: tries to run command called 'if'
```

This is useful when you want to use a reserved word as a regular word:

```bash
# Use 'if' as a variable name (not recommended, but possible)
'if'=value
echo $'if'

# Or as a command name
\if() {
    echo "This is a function named 'if'"
}
```

**Best practice**: Don't use reserved words as variable names or command names, even with quoting. It's confusing and error-prone.

## Optional Reserved Words (Implementation-Defined)

Some words **may** be recognized as reserved words in implementations that support extended features:

```
[[        ]]        function        namespace        select        time
```

Whether these are reserved depends on the shell implementation. Here's what they typically mean:

### `[[` and `]]` - Extended Test Command

Many shells (bash, zsh, ksh) support `[[ ]]` for enhanced conditional tests:

```bash
if [[ $var == pattern* ]]; then    # Pattern matching in bash
    echo "Matches"
fi
```

This is **not** in POSIX, so results are unspecified if you use it.

### `function` - Function Definition Keyword

Some shells allow the `function` keyword for defining functions:

```bash
function myfunction {      # bash-style
    echo "Hello"
}

# vs. POSIX-standard:
myfunction() {
    echo "Hello"
}
```

Portable scripts should use the `name()` syntax.

## Words Ending with Colon (`:`)

**All words that end with a colon character are reserved** when in positions where reserved words are recognized:

```bash
label:                   # Reserved (ends with ':')
my_function:             # Reserved (ends with ':')
```

Using these produces **unspecified results**. Don't use them.

This reservation is for potential future use in the standard. Some shells might use colon-suffixed words for labels or other features.

## Practical Examples

### Example 1: Reserved Word in Command Position

```bash
if [ -f file.txt ]; then
    echo "File exists"
fi
```

Recognition:
- `if` - First word of command → **reserved**
- `then` - After `if` → **reserved**
- `fi` - First word (after newline) → **reserved**

### Example 2: Same Word, Different Context

```bash
# 'do' as reserved word
while true; do           # 'do' is reserved (after 'while')
    break
done

# 'do' as a regular word
echo do                  # 'do' is NOT reserved (not in special position)
```

### Example 3: Quoting to Avoid Reserved Status

```bash
# This fails:
if [ -f file ]; then
    echo "exists"
fi

# Create a variable or function called 'if':
'if'=myvalue             # 'if' is NOT reserved (quoted)
echo $'if'               # Prints: myvalue

\then() {                # 'then' is NOT reserved (escaped)
    echo "function named then"
}
\then                    # Calls the function
```

### Example 4: `case` Third Word

```bash
# Correct:
case $var in             # 'in' is valid third word
    pattern) echo "match" ;;
esac

# Incorrect:
case $var then           # 'then' is not valid here
# Syntax error!
```

### Example 5: `for` Loop Variations

```bash
# With 'in' list:
for file in *.txt; do    # 'in' is third word
    echo $file
done

# Without 'in' (uses $@):
for arg                  # No third word yet
do                       # 'do' is third word
    echo $arg
done
```

## Common Pitfalls

### Pitfall 1: Using Reserved Words as Command Names

```bash
# This doesn't work:
if() {                   # Error: 'if' is reserved
    echo "function"
}

# This works (but is confusing):
\if() {                  # 'if' is escaped, not reserved
    echo "function"
}
```

### Pitfall 2: Forgetting Position Matters

```bash
echo if                  # Works: 'if' is not in reserved position
my_if=value             # Works: 'my_if' contains 'if' but isn't 'if'
```

Reserved words are only special in specific syntactic positions.

### Pitfall 3: Case-Sensitivity

Reserved words are case-sensitive:

```bash
IF [ -f file ]; then     # Error: 'IF' is not recognized
# Tries to run command 'IF'

If [ -f file ]; then     # Error: 'If' is not recognized
```

Only the exact lowercase versions are reserved.

### Pitfall 4: Aliases Can't Override Reserved Words

```bash
alias if='something'     # Alias defined
if [ -f file ]; then     # 'if' is still reserved!
```

Reserved word recognition happens before alias substitution in most contexts.

### Pitfall 5: Reserved Words in Unexpected Places

```bash
# This might surprise you:
{ if; }                  # 'if' is recognized as reserved!
# Error: incomplete 'if' command

# You need:
{ 'if'; }                # Now 'if' is a regular word
```

## Best Practices

1. **Never use reserved words as variable names**: Even with quoting, it's confusing
   ```bash
   # Bad:
   'if'=value
   
   # Good:
   condition=value
   ```

2. **Never use reserved words as function names**: Use descriptive names instead
   ```bash
   # Bad:
   \while() { echo "loop"; }
   
   # Good:
   run_loop() { echo "loop"; }
   ```

3. **Don't rely on optional reserved words**: Use POSIX-standard syntax for portability
   ```bash
   # Non-portable:
   function myfunc { echo "hello"; }
   
   # Portable:
   myfunc() { echo "hello"; }
   ```

4. **Understand position matters**: Reserved words are only special in specific places

5. **Avoid colon-suffixed words**: They're reserved for future use

## Summary: Reserved Word Recognition

**When recognized (all conditions must be true):**
- Word is not quoted (no `'`, `"`, `\`, or `$'`)
- Word is in a recognition position:
  - First word of command
  - After most reserved words (except `case`, `for`, `in`)
  - Third word of `case` (must be `in`)
  - Third word of `for` (must be `in` or `do`)

**The 16 standard reserved words:**
```
!  {  }  case  do  done  elif  else  esac  fi  for  if  in  then  until  while
```

**Optional/unspecified:**
- `[[`, `]]`, `function`, `namespace`, `select`, `time`
- Any word ending with `:`

**How to use reserved words as regular words:**
- Quote them: `'if'`, `"then"`, `\while`
- But better: just don't use reserved words as identifiers


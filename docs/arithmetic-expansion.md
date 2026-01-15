# Arithmetic Expansion

**Arithmetic expansion** lets you perform integer arithmetic directly in the shell, without calling external commands like `expr` or `bc`. It evaluates arithmetic expressions and substitutes the result, making it easy to do calculations in scripts.

This is one of the shell's most useful features for counter loops, calculations, and numeric operations.

## The Basic Format

Arithmetic expansion uses this syntax:

```bash
$((expression))
```

The shell evaluates the `expression` as arithmetic and replaces the entire `$((expression))` with the numeric result.

Examples:
```bash
echo $((2 + 2))              # Prints: 4
echo $((10 * 5))             # Prints: 50
echo $((100 / 3))            # Prints: 33 (integer division)
```

## How It Works

When the shell encounters `$((expression))`:

1. **Treats the expression as if in double quotes** (variables expand, commands substitute)
2. **Performs expansions** on tokens (parameter, command substitution, quote removal)
3. **Evaluates as arithmetic** using integer math
4. **Substitutes the numeric result** as a string

Example step-by-step:
```bash
x=5
result=$((x + 10))

# Step 1: Expression is "x + 10"
# Step 2: Expand x → "5 + 10"
# Step 3: Evaluate: 5 + 10 = 15
# Step 4: Replace with "15"
# Result: result=15
```

## Expansions Inside Arithmetic

Inside `$((expression))`, the shell performs:
- **Parameter expansion**: Variables are expanded
- **Command substitution**: `$(command)` is executed
- **Quote removal**: Quotes are processed

```bash
x=5
y=3
echo $((x + y))              # Expands x and y, then adds: 8

# Command substitution works
files=$(ls | wc -l)
echo $((files * 2))          # Uses command output

# Quotes work (though usually not needed)
echo $(("5" + "3"))          # 8
```

**Important**: Double quotes inside the expression are not treated specially. The entire expression acts like it's in double quotes.

## Variables in Arithmetic

You can reference variables with or without `$`:

```bash
x=10
echo $((x + 5))              # 15 (variable name)
echo $(($x + 5))             # 15 (with $)
```

Both forms work identically. The shell recognizes variable names in arithmetic context.

**Invalid integers**: If a variable contains non-numeric text, the expansion fails:
```bash
x="hello"
echo $((x + 1))              # ERROR: invalid arithmetic
```

The shell writes a diagnostic message to standard error.

## Integer Arithmetic Only

Arithmetic expansion uses **signed long integers** only:

```bash
echo $((10 / 3))             # 3 (integer division, no decimals)
echo $((5 + 2))              # 7
echo $((10 % 3))             # 1 (modulo/remainder)
```

**No floating point**: You can't do decimal math in standard arithmetic expansion:
```bash
echo $((10.5 + 2.3))         # ERROR: invalid
```

For floating point, use `bc`:
```bash
echo "10.5 + 2.3" | bc       # 12.8
```

## Arithmetic Operators

### Primary Expressions: `()`

Parentheses group operations and control precedence:

```bash
echo $((2 + 3 * 4))          # 14 (multiplication first)
echo $(((2 + 3) * 4))        # 20 (parentheses force addition first)
```

### Unary Operators

**Unary plus and minus**:
```bash
echo $((-5))                 # -5
echo $((+5))                 # 5
x=10
echo $((-x))                 # -10
```

**Bitwise NOT (`~`)**:
```bash
echo $((~5))                 # -6 (bitwise complement)
```

**Logical NOT (`!`)**:
```bash
echo $((! 0))                # 1 (true)
echo $((! 5))                # 0 (false)
```

**Increment/Decrement (prefix and postfix)**:

**Note**: `++` and `--` are **not required** by POSIX but are commonly supported:
```bash
x=5
echo $((++x))                # 6 (increment, then use)
echo $x                      # 6

x=5
echo $((x++))                # 5 (use, then increment)
echo $x                      # 6
```

These may not work in all shells.

### Multiplicative Operators: `*`, `/`, `%`

```bash
echo $((6 * 7))              # 42 (multiplication)
echo $((10 / 3))             # 3 (integer division)
echo $((10 % 3))             # 1 (modulo/remainder)
```

### Additive Operators: `+`, `-`

```bash
echo $((5 + 3))              # 8 (addition)
echo $((10 - 7))             # 3 (subtraction)
```

### Bitwise Shift Operators: `<<`, `>>`

```bash
echo $((8 << 2))             # 32 (left shift: 8 * 2^2)
echo $((32 >> 2))            # 8 (right shift: 32 / 2^2)
```

### Relational Operators: `<`, `<=`, `>`, `>=`

Return 1 (true) or 0 (false):

```bash
echo $((5 < 10))             # 1 (true)
echo $((5 > 10))             # 0 (false)
echo $((5 <= 5))             # 1 (true)
echo $((10 >= 5))            # 1 (true)
```

### Equality Operators: `==`, `!=`

```bash
echo $((5 == 5))             # 1 (true)
echo $((5 == 3))             # 0 (false)
echo $((5 != 3))             # 1 (true)
```

### Bitwise Operators: `&`, `^`, `|`

**Bitwise AND (`&`)**:
```bash
echo $((12 & 10))            # 8 (binary: 1100 & 1010 = 1000)
```

**Bitwise XOR (`^`)**:
```bash
echo $((12 ^ 10))            # 6 (binary: 1100 ^ 1010 = 0110)
```

**Bitwise OR (`|`)**:
```bash
echo $((12 | 10))            # 14 (binary: 1100 | 1010 = 1110)
```

### Logical Operators: `&&`, `||`

**Logical AND (`&&`)**: Returns 1 if both operands are non-zero:
```bash
echo $((5 && 3))             # 1 (both non-zero)
echo $((5 && 0))             # 0 (second is zero)
```

**Logical OR (`||`)**: Returns 1 if either operand is non-zero:
```bash
echo $((0 || 5))             # 1 (second is non-zero)
echo $((0 || 0))             # 0 (both zero)
```

### Conditional (Ternary) Operator: `?:`

```bash
x=5
echo $((x > 3 ? 10 : 20))    # 10 (x > 3 is true, so 10)

x=2
echo $((x > 3 ? 10 : 20))    # 20 (x > 3 is false, so 20)
```

Syntax: `condition ? value_if_true : value_if_false`

### Assignment Operators

Basic assignment (`=`) and compound assignments:

```bash
x=$((5))                     # x = 5
x=$((x + 10))                # x = 15

# Compound assignments (modify and assign)
x=10
: $((x += 5))                # x = 15 (x = x + 5)
: $((x -= 3))                # x = 12 (x = x - 3)
: $((x *= 2))                # x = 24 (x = x * 2)
: $((x /= 4))                # x = 6 (x = x / 4)
: $((x %= 5))                # x = 1 (x = x % 5)
```

Other compound assignments: `<<=`, `>>=`, `&=`, `^=`, `|=`

**Important**: Assignment changes the variable. Use `: $((...))` if you just want the side effect without printing.

## Numeric Constants

### Decimal (Base 10)

Regular numbers:
```bash
echo $((42))                 # 42
echo $((123))                # 123
```

### Octal (Base 8)

Numbers starting with `0`:
```bash
echo $((010))                # 8 (octal 10 = decimal 8)
echo $((077))                # 63 (octal 77 = decimal 63)
```

**Warning**: Leading zeros make numbers octal!
```bash
echo $((010 + 5))            # 13 (octal 10 = 8, plus 5)
```

### Hexadecimal (Base 16)

Numbers starting with `0x` or `0X`:
```bash
echo $((0x10))               # 16
echo $((0xFF))               # 255
echo $((0x1A))               # 26
```

### Signs

Numbers can have leading `+` or `-`:
```bash
echo $((+42))                # 42
echo $((-42))                # -42
```

## Variable Updates Persist

Changes to variables inside arithmetic expansion **persist** after the expansion:

```bash
x=5
echo $((x = 10))             # Prints: 10
echo $x                      # Prints: 10 (x was changed)

x=5
echo $((x += 5))             # Prints: 10
echo $x                      # Prints: 10 (x was modified)
```

This is like parameter expansion `${x=value}` where assignments take effect.

## Not Supported: Control Flow

**Selection statements** (`if`, `switch`) are **not supported** in arithmetic expansion:
```bash
# WRONG - doesn't work
echo $((if (x > 5) 10 else 20))  # ERROR
```

**Iteration statements** (`while`, `for`, `do-while`) are **not supported**:
```bash
# WRONG - doesn't work
echo $((for (i=0; i<10; i++) sum += i))  # ERROR
```

**Jump statements** (`goto`, `continue`, `break`, `return`) are **not supported**.

**Use the conditional operator instead**:
```bash
# RIGHT - use ternary operator
x=5
echo $((x > 3 ? 10 : 20))    # 10
```

## Not Required: `sizeof` and `++`/`--`

**The `sizeof()` operator** is not required by POSIX:
```bash
echo $((sizeof(int)))        # May not work
```

**Prefix and postfix `++` and `--`** are not required:
```bash
x=5
echo $((++x))                # May not work
echo $((x++))                # May not work
```

Many shells support these, but portable scripts should avoid them:
```bash
# Portable alternative to x++
x=$((x + 1))

# Portable alternative to ++x
x=$((x + 1))
echo $x
```

## Practical Examples

### Example 1: Counter Loop

```bash
#!/bin/bash
# Repeat a command 100 times
x=100
while [ $x -gt 0 ]; do
    echo "Iteration $x"
    x=$((x - 1))
done
```

### Example 2: Simple Calculator

```bash
#!/bin/bash
a=15
b=4

echo "Addition: $((a + b))"
echo "Subtraction: $((a - b))"
echo "Multiplication: $((a * b))"
echo "Division: $((a / b))"
echo "Modulo: $((a % b))"
```

### Example 3: Average Calculation

```bash
#!/bin/bash
total=0
count=0

for value in 10 20 30 40 50; do
    total=$((total + value))
    count=$((count + 1))
done

average=$((total / count))
echo "Average: $average"
```

### Example 4: Power of Two Check

```bash
#!/bin/bash
num=16

# Check if power of 2 using bitwise AND
if [ $((num & (num - 1))) -eq 0 ] && [ $num -ne 0 ]; then
    echo "$num is a power of 2"
else
    echo "$num is not a power of 2"
fi
```

### Example 5: File Size Calculation

```bash
#!/bin/bash
# Sum file sizes
total=0

for file in *.txt; do
    size=$(stat -c %s "$file")
    total=$((total + size))
done

echo "Total size: $total bytes"
echo "Total size: $((total / 1024)) KB"
```

### Example 6: Progress Calculation

```bash
#!/bin/bash
total=100
current=37

percent=$((current * 100 / total))
echo "Progress: $percent%"
```

## Extensions and Implementation Differences

The POSIX specification allows shells to extend arithmetic expansion:

### Larger Integer Types

Shells may use integer types larger than `signed long`:
- Many modern shells use 64-bit integers
- Allows larger values than 32-bit `long`

### Floating Point

Some shells may support floating point:
```bash
# May work in some shells (not POSIX)
echo $((10.5 + 2.3))
```

But this is **not portable**. Use `bc` for reliable floating point.

### Additional Operators

Some shells support more operators:
- Exponentiation (`**`)
- Additional functions

Don't rely on these for portable scripts.

## Invalid Expressions

If the expression is invalid, the expansion **fails** and the shell writes a diagnostic message:

```bash
# Invalid variable content
x="abc"
echo $((x + 1))
# Error: sh: arithmetic expression: expecting primary: "abc + 1"

# Invalid syntax
echo $((5 +))
# Error: sh: arithmetic expression: syntax error

# Division by zero (may error or be undefined)
echo $((5 / 0))
# Error or undefined behavior
```

## Common Pitfalls

### Pitfall 1: Leading Zeros (Octal Numbers)

```bash
# WRONG - leading zero makes it octal
echo $((010 + 020))          # 24 (octal 10 + octal 20 = 8 + 16)

# RIGHT - no leading zeros
echo $((10 + 20))            # 30
```

### Pitfall 2: Floating Point Not Supported

```bash
# WRONG - no floating point in POSIX
echo $((10.5 + 2.3))         # ERROR

# RIGHT - use bc for floating point
echo "10.5 + 2.3" | bc       # 12.8
```

### Pitfall 3: Division Truncates

```bash
# Surprising - integer division
echo $((10 / 3))             # 3 (not 3.333...)

# To get floating point result
echo "scale=2; 10 / 3" | bc  # 3.33
```

### Pitfall 4: Using Control Flow

```bash
# WRONG - control flow not supported
echo $((if (x > 5) 10))      # ERROR

# RIGHT - use conditional operator
echo $((x > 5 ? 10 : 0))     # Works
```

### Pitfall 5: Forgetting Variable Changes Persist

```bash
x=5
result=$((x = 10))           # x is now 10!
echo $x                      # Prints: 10 (not 5)

# If you don't want side effects, don't use assignment
result=$((x + 5))            # x unchanged
```

### Pitfall 6: Invalid Variable Content

```bash
count="5 items"
echo $((count + 1))          # ERROR: "5 items" is not a valid integer

# Make sure variables contain valid integers
count=5
echo $((count + 1))          # Works: 6
```

## Best Practices

1. **Use for integer arithmetic**: Perfect for counters, simple math
   ```bash
   count=$((count + 1))
   ```

2. **Avoid leading zeros**: Don't write `010` unless you mean octal
   ```bash
   # Bad:
   day=08                    # Octal! (error if 08 or 09)
   # Good:
   day=8
   ```

3. **Use `bc` for floating point**: Don't rely on shell extensions
   ```bash
   result=$(echo "scale=2; 10 / 3" | bc)
   ```

4. **Quote the expansion if using in strings**:
   ```bash
   echo "The result is: $((5 + 3))"
   ```

5. **Validate numeric input**: Check that variables are valid integers
   ```bash
   if [ "$input" -eq "$input" ] 2>/dev/null; then
       result=$((input * 2))
   else
       echo "Invalid number" >&2
   fi
   ```

6. **Use the ternary operator instead of if**: For conditional values
   ```bash
   max=$((a > b ? a : b))
   ```

7. **Avoid non-portable features**: Don't use `++`, `--`, or `sizeof`
   ```bash
   # Non-portable:
   x=$((++x))

   # Portable:
   x=$((x + 1))
   ```

## Availability: Arithmetic Expansion Across Different Builds

Arithmetic expansion is a shell parsing and evaluation feature. Support varies by build.

### POSIX API Build (Full Arithmetic Expansion)

- ✅ Full arithmetic expansion support
- ✅ All required operators work
- ✅ Signed long integer arithmetic
- ✅ Decimal, octal, hexadecimal constants
- ✅ All comparison operators
- ✅ Bitwise operators
- ✅ Logical operators
- ✅ Ternary conditional operator
- ✅ Assignment operators
- ✅ Variable updates persist

**May also support**:
- ⚠️ Larger integer types (64-bit)
- ⚠️ `++` and `--` operators (common but not required)

### UCRT API Build (Full Arithmetic Expansion)

- ✅ Identical to POSIX build
- ✅ All arithmetic operations work the same

**Why it works**: Arithmetic expansion is pure computation—no OS APIs involved. It's integer math performed by the shell itself.

**No platform differences**: Integer arithmetic behaves identically on Windows and Unix.

### ISO C Build (Full Arithmetic Expansion)

- ✅ Arithmetic expansion works fully
- ✅ All operators supported
- ✅ All required functionality available

**Why it works**: Arithmetic expansion only requires:
- Integer arithmetic (available in any C implementation)
- Variable lookup (part of shell state)
- String to integer conversion (`strtol()` in ISO C)
- Integer to string conversion (`sprintf()` in ISO C)

None of these require POSIX or Windows-specific APIs.

**Practical outcome**: Arithmetic expansion is one of the most portable shell features—it works identically in all three builds.

### Summary Table

| Feature | POSIX | UCRT | ISO C |
|---------|-------|------|-------|
| `$((expression))` | ✅ Yes | ✅ Yes | ✅ Yes |
| Integer arithmetic | ✅ Yes | ✅ Yes | ✅ Yes |
| All operators | ✅ Yes | ✅ Yes | ✅ Yes |
| Decimal constants | ✅ Yes | ✅ Yes | ✅ Yes |
| Octal constants | ✅ Yes | ✅ Yes | ✅ Yes |
| Hex constants | ✅ Yes | ✅ Yes | ✅ Yes |
| Variable expansion | ✅ Yes | ✅ Yes | ✅ Yes |
| Command substitution inside | ✅ Yes | ✅ Yes | ⚠️ Limited* |
| Assignment operators | ✅ Yes | ✅ Yes | ✅ Yes |
| Ternary operator | ✅ Yes | ✅ Yes | ✅ Yes |
| Bitwise operators | ✅ Yes | ✅ Yes | ✅ Yes |

*ISO C build limitation: Command substitution doesn't work (as we saw earlier), so `$(($(command)))` won't work. But pure arithmetic with variables works fine.

### Choosing Your Build for Arithmetic Expansion

**Excellent news**: Arithmetic expansion works identically in all builds!

This makes it one of the **most portable** shell features because:
- Pure integer math (no OS interaction)
- Uses standard C integer operations
- Variable lookup is built into shell
- No external commands needed

**All builds fully support**:
- Counters and loops
- Calculations
- All arithmetic operators
- Bitwise operations
- Conditional expressions

**The only limitation** (ISO C) is that you can't use command substitution inside arithmetic:
```bash
# Doesn't work in ISO C (command substitution unavailable)
result=$(($(cat file)))

# Works everywhere (pure arithmetic with variables)
num=42
result=$((num * 2))
```

**Recommendation**: Use arithmetic expansion freely in all builds. It's reliable, portable, and efficient. Just avoid embedding command substitutions if you need ISO C compatibility.

Arithmetic expansion is a core shell feature that you can depend on working consistently across all platforms and builds.

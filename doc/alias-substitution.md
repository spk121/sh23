# Alias Substitution

**Aliases** are shortcuts that let you define your own command names or abbreviate long commands. When you type an alias name, the shell replaces it with the alias's value before executing the command. This is called **alias substitution**, and it's one of the shell's most convenient features for customizing your environment.

However, alias substitution has subtle rules about when and how it happens. Understanding these rules is essential for using aliases effectively and avoiding surprises.

## What Are Aliases?

An alias is a name-value pair where:
- The **name** is what you type
- The **value** is what the shell substitutes

Basic example:
```bash
alias ll='ls -la'
ll                      # Shell replaces with: ls -la
```

Aliases let you:
- Create short names for frequently used commands
- Set default options for commands
- Define custom command combinations
- Customize your shell environment

## Defining Aliases

Use the `alias` built-in to create aliases:

```bash
alias name='value'
alias name=value        # Quotes optional if no special chars
```

Examples:
```bash
alias ll='ls -la'
alias ..='cd ..'
alias gs='git status'
alias rm='rm -i'        # Make rm interactive by default
```

To see all defined aliases:
```bash
alias                   # Lists all aliases
```

To remove an alias:
```bash
unalias name
unalias -a              # Remove all aliases
```

## When Alias Substitution Happens

The shell doesn't always substitute aliases—it only does so when **all** of the following conditions are met:

### Condition 1: The Token Contains No Quoting

If any part of the token is quoted, no alias substitution happens:

```bash
alias ll='ls -la'

ll                      # Substituted → ls -la
'll'                    # NOT substituted → literal "ll"
"ll"                    # NOT substituted → literal "ll"
l\l                     # NOT substituted → literal "ll"
```

**Why?** Quoting means "treat this literally." If you quote it, you're explicitly asking for the literal string, not alias expansion.

This lets you bypass aliases when needed:
```bash
alias rm='rm -i'        # Make rm interactive
\rm file.txt            # Bypass alias, use real rm without -i
```

### Condition 2: The Token Is a Valid Alias Name

The token must be a valid alias name as defined by POSIX (letters, digits, underscores; basically a valid identifier).

Valid:
```bash
alias myalias='echo hello'
alias my_alias='echo hello'
alias alias123='echo hello'
```

Invalid (these wouldn't be valid alias names):
```bash
alias my-alias='...'    # Hyphen not allowed
alias 123alias='...'    # Can't start with digit
alias my.alias='...'    # Dot not allowed
```

### Condition 3: An Alias With That Name Is in Effect

The alias must actually exist and be currently defined:

```bash
alias ll='ls -la'
ll                      # Works - alias exists

unalias ll
ll                      # Doesn't work - no such alias
```

### Condition 4: Not the Same Alias Being Substituted

To prevent infinite recursion, if an alias's value contains the same alias name, it won't be substituted again:

```bash
alias ls='ls -G'
ls                      # Becomes: ls -G
                        # The second "ls" is NOT substituted again
```

This prevents infinite loops:
```bash
alias foo='foo bar'
foo                     # Would become: foo bar
                        # "foo" in the value is NOT substituted
```

**Important**: This protection applies even in nested situations. If alias `a` contains alias `b`, and alias `b` contains alias `a`, they won't create a loop.

### Condition 5: Token Is in a Command Name Position

Aliases are only substituted when the token appears where a command name could appear:

**Substituted (command name position):**
```bash
alias ll='ls -la'

ll                      # Start of command
echo hello; ll          # After semicolon
ls | ll                 # After pipe
(ll)                    # In subshell
```

**NOT substituted (argument position):**
```bash
alias ll='ls -la'

echo ll                 # "ll" is an argument, prints "ll"
grep ll file            # "ll" is an argument to grep
```

**Special case - after blank-ending aliases:**
If an alias value ends with a blank (space or tab), the next token is **also** checked for alias substitution:

```bash
alias sudo='sudo '      # Note the trailing space!
alias ll='ls -la'

sudo ll                 # First: sudo → "sudo "
                        # Because it ends in space, check "ll"
                        # Second: ll → "ls -la"
                        # Result: sudo ls -la
```

This is crucial for aliases that prefix other commands!

### Reserved Words Exception

If a token would be recognized as a reserved word (like `if`, `then`, `do`, etc.) in its current position, whether it's subject to alias substitution is **unspecified**.

```bash
alias if='echo if'      # Probably won't work
if test; then           # "if" is likely recognized as keyword, not alias
```

**Best practice**: Don't create aliases with reserved word names. It's unpredictable and confusing.

## How Alias Substitution Works

When the shell decides to substitute an alias, here's what happens:

### Step 1: Replace the Token

The alias name is replaced with the alias value:

```bash
alias ll='ls -la'
ll /tmp
# Step 1: Replace "ll" with "ls -la"
# Becomes: ls -la /tmp
```

### Step 2: Re-tokenize

The alias value is processed as if it had been typed at that position. Token recognition starts over from the beginning of the alias value:

```bash
alias mygrep='grep -r'
mygrep pattern .
# After substitution: grep -r pattern .
# The shell re-tokenizes: "grep", "-r", "pattern", "."
```

### Step 3: Check for More Aliases

If the alias value ends with a blank (space or tab), the **next** token is also checked for alias substitution:

```bash
alias run='echo Running: '     # Trailing space!
alias cmd='my-command'

run cmd
# Step 1: run → "echo Running: "
# Step 2: Because value ends in space, check "cmd"
# Step 3: cmd → "my-command"
# Result: echo Running: my-command
```

This chaining continues until a token isn't an alias or doesn't end with a blank:

```bash
alias a='b '           # Trailing space
alias b='c '           # Trailing space
alias c='echo'

a hello
# a → "b " → check next
# b → "c " → check next
# c → "echo" → no trailing space, stop
# Result: echo hello
```

### Step 4: Optional Space After Substitution

After substituting an alias, the shell may behave as if an extra space was added after the original token. This ensures proper token separation.

For example:
```bash
alias ll='ls -la'
ll/tmp                  # Without added space, might parse as one token
# Shell may add space: "ls -la" /tmp
```

**Note**: A future POSIX version may disallow adding this space, so don't rely on this behavior.

## Practical Alias Examples

### Simple Command Abbreviations

```bash
alias l='ls'
alias la='ls -a'
alias ll='ls -la'
alias ..='cd ..'
alias ...='cd ../..'
```

### Commands with Default Options

```bash
alias grep='grep --color=auto'
alias ls='ls --color=auto'
alias rm='rm -i'        # Prompt before delete
alias cp='cp -i'        # Prompt before overwrite
alias mv='mv -i'        # Prompt before overwrite
```

### Git Shortcuts

```bash
alias gs='git status'
alias ga='git add'
alias gc='git commit'
alias gp='git push'
alias gl='git log --oneline'
```

### Complex Command Combinations

```bash
alias ports='netstat -tulanp'
alias meminfo='free -m -l -t'
alias psmem='ps auxf | sort -nr -k 4'
alias pscpu='ps auxf | sort -nr -k 3'
```

### Aliases That Take Arguments

Aliases don't directly support arguments, but you can use them at the end:

```bash
alias mkcd='mkdir -p && cd'
# WRONG: mkcd mydir → mkdir -p && cd mydir
# The "mydir" goes to mkdir, but cd gets nothing

# Better: use a function instead
mkcd() {
    mkdir -p "$1" && cd "$1"
}
```

**Important**: If you need parameters in the middle or complex logic, use functions instead of aliases.

### Aliases for Prefix Commands (Trailing Space!)

```bash
alias sudo='sudo '      # Trailing space enables alias after sudo
alias watch='watch '    # Trailing space enables alias after watch
alias time='time '      # Trailing space enables alias after time

alias ll='ls -la'
sudo ll                 # Works! → sudo ls -la
```

Without the trailing space, `ll` wouldn't be expanded after `sudo`.

## Alias Limitations and Gotchas

### Limitation 1: No Parameters in the Middle

You can't put parameters anywhere except at the end:

```bash
alias showfile='cat $1'     # DOESN'T WORK
showfile myfile.txt         # $1 is not replaced

# Use a function:
showfile() { cat "$1"; }
```

### Limitation 2: Aliases Aren't Inherited

Aliases are **not** inherited by:
- Subshells (commands in `()`)
- Background jobs
- Scripts you run
- Child processes

```bash
alias ll='ls -la'

(ll)                    # Might not work (alias not in subshell)
./script.sh             # Script doesn't have your aliases

# Aliases only exist in the current shell session
```

**Where aliases ARE available:**
- The current interactive shell
- Shell functions (they run in the current shell)

### Limitation 3: Timing of Alias Changes

Changes to aliases take effect by the completion of the current complete command:

```bash
alias test='echo OLD'
test; alias test='echo NEW'; test
# First test: echo OLD
# Second test: might be OLD or NEW (implementation-defined timing)
```

The change definitely takes effect by the next command line:
```bash
alias test='echo OLD'
test                    # echo OLD
alias test='echo NEW'
test                    # echo NEW (definitely new)
```

### Limitation 4: Reserved Words

Don't alias reserved words—behavior is unpredictable:

```bash
alias if='myif'         # BAD IDEA
alias for='myfor'       # BAD IDEA
```

### Limitation 5: Quoting Disables Substitution

Any quoting prevents alias substitution:

```bash
alias rm='rm -i'
rm file                 # Interactive
\rm file                # NOT interactive (alias bypassed)
'rm' file               # NOT interactive (alias bypassed)
```

This is useful for bypassing aliases but can be surprising.

## Aliases vs. Functions

When should you use an alias vs. a function?

**Use aliases for:**
- Simple command abbreviations
- Adding default options to commands
- Very short, simple substitutions

**Use functions for:**
- Anything with parameters in the middle
- Complex logic or multiple commands
- Anything needing variables or control structures
- Portability (functions are more standard)

Example comparison:
```bash
# Alias (simple)
alias ll='ls -la'

# Function (more complex)
mkcd() {
    mkdir -p "$1" && cd "$1"
}
```

## Best Practices

1. **Keep aliases simple**: If it's complex, use a function instead.

2. **Use trailing spaces for prefix commands**:
   ```bash
   alias sudo='sudo '
   ```

3. **Document your aliases**: Add comments in your `.bashrc` or `.zshrc`:
   ```bash
   # Git shortcuts
   alias gs='git status'
   ```

4. **Don't alias reserved words**: Stick to normal command names.

5. **Consider portability**: Aliases aren't inherited by scripts. For scripting, use functions.

6. **Quote alias values**: Always quote the value to prevent surprises:
   ```bash
   alias l='ls -la'      # GOOD
   alias l=ls -la        # BAD (only "l=ls" is the alias!)
   ```

7. **Test before committing**: Try new aliases interactively before adding to your config.

8. **Use `\` to bypass**: Remember `\command` bypasses the alias if needed.

## Checking and Debugging Aliases

**See all aliases:**
```bash
alias
```

**See specific alias:**
```bash
alias ll
# Output: alias ll='ls -la'
```

**Test what command will run:**
```bash
type ll
# Output: ll is aliased to `ls -la'
```

**Temporarily disable alias:**
```bash
\ll                     # Runs ls, not the alias
```

**Check if something is an alias:**
```bash
type -t ll
# Output: alias
```

## Common Pitfalls

### Pitfall 1: Forgetting Trailing Space for Prefix Commands

```bash
alias sudo='sudo'       # No trailing space
alias ll='ls -la'
sudo ll                 # Doesn't expand ll!

# Fix:
alias sudo='sudo '      # Add trailing space
```

### Pitfall 2: Using Aliases in Scripts

```bash
#!/bin/bash
alias ll='ls -la'       # Defined in script
ll                      # Might not work!
```

Scripts don't inherit your shell's aliases. Define the alias in the script or use functions.

### Pitfall 3: Complex Aliases That Need Functions

```bash
alias backup='cp $1 $1.backup'     # DOESN'T WORK

# Use function:
backup() { cp "$1" "$1.backup"; }
```

### Pitfall 4: Aliases That Hide Commands

```bash
alias cd='echo "Use pushd instead"'     # Breaks cd!
```

Be careful aliasing common commands—you might break expected behavior.

### Pitfall 5: Recursive Alias Attempts

```bash
alias ls='ls --color=auto'
# This works (second "ls" not substituted)

alias ls='ls --color=auto --format=long'
# This also works (same protection)

alias ll='ll -a'
# This DOESN'T work as expected (ll calls ll, but just once)
```

## Where to Define Aliases

Aliases are typically defined in shell configuration files:

**For Bash:**
- `~/.bashrc` - interactive non-login shells
- `~/.bash_profile` - login shells

**For Zsh:**
- `~/.zshrc` - interactive shells

**For sh (POSIX shell):**
- `~/.profile` - login shells
- Check your shell's documentation

Example `.bashrc` section:
```bash
# Navigation aliases
alias ..='cd ..'
alias ...='cd ../..'

# Listing aliases
alias l='ls'
alias la='ls -a'
alias ll='ls -la'

# Safety aliases
alias rm='rm -i'
alias cp='cp -i'
alias mv='mv -i'

# Git aliases
alias gs='git status'
alias ga='git add'
alias gc='git commit'
```

## Summary: Alias Quick Reference

| Concept | Details |
|---------|---------|
| **Define** | `alias name='value'` |
| **Remove** | `unalias name` |
| **List all** | `alias` |
| **Bypass** | `\name` or `'name'` or `"name"` |
| **Check** | `type name` |
| **Trailing space** | Next token also checked for alias |
| **Scope** | Current shell only, not inherited |
| **When substituted** | Command name position, unquoted |
| **Not substituted** | Quoted, argument position, recursive |

## Availability: Alias Substitution Across Different Builds

Alias substitution is a shell parsing and execution feature that's available in all builds, but practical utility varies.

### POSIX API Build (Full Alias Support)

When built with the POSIX API, the shell provides **complete alias support** as described throughout this document.

**What's available:**
- ✅ Full alias substitution with all five conditions
- ✅ Recursive alias prevention
- ✅ Trailing space chaining
- ✅ Command name position detection
- ✅ Proper integration with all shell features
- ✅ Alias persistence in shell sessions
- ✅ `alias` and `unalias` built-ins

**Practical use:**
Aliases work exactly as expected for interactive shell customization and command abbreviation.

### UCRT API Build (Full Alias Support)

When built with the Universal C Runtime (UCRT) API on Windows, the shell provides **full alias support**. Alias substitution is a parsing feature independent of OS APIs.

**What's available:**
- ✅ All alias features identical to POSIX
- ✅ Same substitution rules
- ✅ Same limitations (not inherited, etc.)

**Windows-specific considerations:**

1. **Path separators in aliases**: Be mindful of backslashes:
   ```bash
   alias cdwin='cd C:/Windows'       # Forward slash (preferred)
   alias cdwin='cd C:\\Windows'      # Escaped backslash
   alias cdwin='cd "C:\Windows"'     # Quoted path
   ```

2. **Case insensitivity**: Windows commands are case-insensitive, but aliases themselves are case-sensitive:
   ```bash
   alias DIR='dir /w'    # "DIR" and "dir" are different aliases
   DIR                   # Uses alias
   dir                   # Uses system dir command
   ```

3. **cmd.exe vs shell aliases**: Your shell aliases don't apply when commands are passed to `cmd.exe` or `system()`.

**Practical outcome**: Aliases work identically to POSIX. Just be aware of Windows path quoting conventions.

### ISO C Build (Full Alias Support)

When built with only ISO C standard library functions, the shell provides **full alias support**. Like quoting, alias substitution is pure parsing logic.

**What's available:**
- ✅ Full alias substitution
- ✅ All substitution rules work
- ✅ Proper parsing and token replacement

**Limitations are in application:**

The ISO C build can parse and substitute aliases, but there are fewer features to use them with:

**Works well:**
```bash
alias ll='system("ls -la")'     # If that's how commands work
alias cls='system("clear")'
```

**Limited utility:**
```bash
alias mygrep='grep -r'    # If commands go through system(),
mygrep pattern .          # the alias expands first, then system() gets
                          # "grep -r pattern ."
```

**Won't work (features don't exist):**
```bash
alias bg='bg %1'          # No job control
alias cdtemp='cd /tmp'    # No cd (no chdir() in ISO C)
```

**Practical implications:**

Aliases **function correctly** in the ISO C build, but they're less useful because:
- Limited command execution (only `system()`)
- No directory changing
- No job control
- No pipelines or redirection

Aliases that just abbreviate command names or add options will still work:
```bash
alias l='ls'              # Works if ls is available
alias vi='vim'            # Works
```

But aliases that rely on shell features (cd, job control, pipelines) won't be useful.

### Summary Table

| Feature | POSIX | UCRT | ISO C |
|---------|-------|------|-------|
| Alias substitution | ✅ Full | ✅ Full | ✅ Full |
| Substitution rules | ✅ All | ✅ All | ✅ All |
| Trailing space chaining | ✅ Yes | ✅ Yes | ✅ Yes |
| Recursion prevention | ✅ Yes | ✅ Yes | ✅ Yes |
| `alias` built-in | ✅ Yes | ✅ Yes | ✅ Yes |
| `unalias` built-in | ✅ Yes | ✅ Yes | ✅ Yes |
| Practical utility | ✅ High | ✅ High | ⚠️ Limited (fewer features to alias) |

### Choosing Your Build for Aliases

**Good news**: Alias substitution works identically in all builds!

The differences are in **what you can usefully alias**, not in how aliases work:

- **POSIX build**: Full utility—alias any command, built-in, or complex command
- **UCRT build**: Full utility—same as POSIX, just be aware of Windows path quoting
- **ISO C build**: Alias works correctly, but limited by what shell features exist

**Recommendations:**

- **All builds**: Use aliases for simple command abbreviations and adding default options
- **POSIX/UCRT**: Use aliases freely for shell customization
- **ISO C**: Aliases work but are less useful—focus on simple command name shortcuts

Scripts should avoid relying on aliases in all builds (use functions instead), but interactive use of aliases is fully supported everywhere.
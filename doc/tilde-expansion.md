# Tilde Expansion

**Tilde expansion** is a convenient shorthand for referring to home directories. When you type `~` at the start of a word, the shell replaces it with a directory path—usually your home directory, but it can also reference other users' home directories.

This is one of the most commonly used shell features, making it easy to reference home directories without typing full paths.

## Basic Tilde Expansion

The simplest form: `~` by itself expands to your home directory.

```sh
cd ~                    # Same as: cd $HOME
ls ~/documents          # Same as: ls $HOME/documents
echo ~                  # Prints: /home/username
```

This works because the shell replaces `~` with the value of the `HOME` variable.

## What Is a Tilde-Prefix?

A **tilde-prefix** is what the shell looks for to perform tilde expansion. It has specific rules:

**A tilde-prefix consists of:**
1. An **unquoted** `~` character
2. At the **beginning** of a word
3. Followed by all characters up to the first unquoted `/` (or end of word if no `/`)

Examples of tilde-prefixes:
```sh
~              # Tilde-prefix: ~
~/documents    # Tilde-prefix: ~
~user          # Tilde-prefix: ~user
~user/files    # Tilde-prefix: ~user
```

Examples that are **not** tilde-prefixes:
```sh
'~'            # Quoted - not expanded
echo ~         # 'echo' prevents ~ from being at start of word
a~b            # Not at beginning of word
~/~            # Second ~ is not at beginning of word
```

## The Two Forms of Tilde Expansion

### Form 1: Just `~` (Your Home Directory)

When the tilde-prefix is **only** the `~` character with nothing after it:

```sh
~              # Expands to: $HOME
~/             # Expands to: $HOME/
~/documents    # Expands to: $HOME/documents
```

**What it does**: Replaces `~` with the value of the `HOME` variable.

**If `HOME` is unset**: Results are unspecified (shell-dependent behavior). Don't rely on this working.

**If `HOME` is empty**: Produces an empty field (not zero fields).
```sh
HOME=""
cd ~           # Changes to current directory (empty path)
```

### Form 2: `~username` (Another User's Home Directory)

When the tilde-prefix has characters after the `~`:

```sh
~john          # Expands to: /home/john
~jane/docs     # Expands to: /home/jane/docs
```

**What it does**: The characters after `~` are treated as a login name. The shell looks up that user's home directory (using something like the `getpwnam()` function) and replaces the tilde-prefix with that directory path.

**Requirements for the login name**:
- Must be a **portable login name** (letters, digits, underscores, hyphens)
- Can't contain special characters or quoting

**If the login name doesn't exist**: Results are unspecified (may produce an error, or leave unexpanded).

## Where Tilde Expansion Happens

Tilde expansion happens in two contexts:

### Context 1: Regular Words

At the beginning of any word in a command:

```sh
cd ~               # Tilde-prefix at start of word
ls ~/documents     # Tilde-prefix at start of word
echo ~user         # Tilde-prefix at start of word
```

### Context 2: Variable Assignments

Tilde expansion also happens in **variable assignments**, and there are special rules:

**Rule 1**: Immediately after the `=` sign:
```sh
dir=~/documents              # Expands to: dir=$HOME/documents
path=~user/bin               # Expands to: path=/home/user/bin
```

**Rule 2**: After each **unquoted colon** (`:`) in the value:
```sh
PATH=~/bin:/usr/bin:~/local/bin
# Expands to: PATH=/home/you/bin:/usr/bin:/home/you/local/bin
```

This is particularly useful for `PATH` variable:
```sh
PATH=~/bin:$PATH             # Add your ~/bin to PATH
PATH=$PATH:~/scripts         # Append ~/scripts to PATH
```

**Tilde-prefix termination in assignments**: A tilde-prefix ends at the first unquoted `:` or `/`, or the end of the value.

Examples:
```sh
# Single tilde expansion at start
dir=~/documents              # Expands: dir=$HOME/documents

# Multiple expansions with colons
PATH=~/bin:~user/bin:/usr/bin
# Expands: PATH=/home/you/bin:/home/user/bin:/usr/bin

# Colon terminates tilde-prefix
var=~user:~/other            # Both ~ expanded
```

## Quoting and Tilde Expansion

Quoting **prevents** tilde expansion. Any quote character makes the `~` literal:

```sh
# Not expanded (quoted):
echo '~'                     # Prints: ~
echo "~"                     # Prints: ~
echo \~                      # Prints: ~

# Expanded (unquoted):
echo ~                       # Prints: /home/username
```

**Partial quoting also prevents expansion**:
```sh
echo ~"user"                 # NOT expanded - quotes after ~
echo "~"/documents           # NOT expanded - quotes around ~
```

The entire tilde-prefix must be unquoted for expansion to work.

## Login Names and Portability

The characters after `~` must form a **portable login name**. This means:
- Letters (a-z, A-Z)
- Digits (0-9)
- Underscore (`_`)
- Hyphen (`-`)
- Period (`.`)

**Invalid login names** (results unspecified):
```sh
~"john"        # Contains quotes
~'john'        # Contains quotes
~$var          # Contains $
~\user         # Contains backslash
~my/name       # Contains slash
```

These won't work portably because the characters in the tilde-prefix (after the `~` is removed) contain special characters.

**Why this matters**: The shell doesn't do further word expansion on the login name part. It takes the literal characters after `~` and looks them up directly.

## How the Expansion Is Treated

After tilde expansion happens, the resulting path is **treated as if quoted** to prevent further processing:

```sh
# If ~john expands to "/home/john smith"
cd ~john              # Works even though path has a space
```

The expanded path won't undergo:
- Field splitting (spaces won't split it into multiple words)
- Pathname expansion (wildcards won't be expanded)

**Trailing slash handling**: If a `/` follows the tilde-prefix and the expanded pathname ends with `/`, the trailing `/` from the pathname should be omitted (though this may become required in future POSIX versions).

Example:
```sh
# If home directory is "/home/user/"
~/documents           # Expands to: /home/user/documents (not /home/user//documents)
```

## Special Case: Empty HOME

When `HOME` is set to the empty string and you use just `~`:

```sh
HOME=""
echo ~               # Produces an empty field (not zero fields)
```

This is different from an unset `HOME` (which is unspecified). An empty `HOME` explicitly produces an empty result.

## Practical Examples

### Example 1: Navigating Directories

```sh
cd ~                 # Go to your home directory
cd ~/documents       # Go to your documents folder
cd ~                 # Back to home
cd -                 # Back to previous directory
```

### Example 2: Referencing Files

```sh
cat ~/.bashrc        # Read your bash config
ls ~/.ssh/           # List SSH keys
cp file ~/backup/    # Copy to your backup folder
```

### Example 3: Other Users' Directories

```sh
ls ~john/public      # List john's public folder
cp file ~jane/       # Copy to jane's home directory
```

### Example 4: PATH Configuration

```sh
# Add your personal bin directory to PATH
PATH=~/bin:$PATH

# Add multiple personal directories
PATH=~/bin:~/scripts:$PATH

# Using another user's directory
PATH=~admin/tools:$PATH
```

### Example 5: Variable Assignments

```sh
# Simple assignment
backup_dir=~/backups

# Multiple tildes with colons
search_path=~/lib:~system/lib:/usr/lib

# In compound assignments
config_file=~/.config/myapp/settings.conf
```

## Common Pitfalls

### Pitfall 1: Quoting Prevents Expansion

```sh
# WRONG - tilde is quoted
cd "~"               # Tries to cd to directory literally named "~"

# RIGHT
cd ~                 # Expands to home directory
cd "$HOME"           # Alternative that works
```

### Pitfall 2: Tilde Not at Start of Word

```sh
# WRONG - not at beginning
path=prefix~/suffix  # Tilde is not at start, no expansion

# RIGHT
path=~/suffix        # Tilde at start, expands
```

### Pitfall 3: Variables Don't Expand in Tilde-Prefix

```sh
user="john"
# WRONG - $user is part of the tilde-prefix
ls ~$user            # NOT expanded (contains $)

# RIGHT - use variable substitution instead
ls ~john             # Direct username works
# Or:
ls "$(eval echo ~$user)"  # Ugly but works
# Better:
home=$(getent passwd "$user" | cut -d: -f6)
ls "$home"
```

### Pitfall 4: Assuming Unset HOME Works

```sh
unset HOME
cd ~                 # Results unspecified! May error or behave oddly

# Better: check first
if [ -n "$HOME" ]; then
    cd ~
else
    echo "HOME not set" >&2
fi
```

### Pitfall 5: Expansion in Double Quotes

Many people think `~` expands in double quotes. It doesn't:

```sh
# WRONG - quoted, doesn't expand
echo "~"             # Prints: ~

# RIGHT
echo ~               # Prints: /home/username
echo "$HOME"         # Alternative that works in quotes
```

### Pitfall 6: Tilde in Middle of Path

```sh
# WRONG - only at beginning of word
path=/prefix/~/suffix    # Tilde not expanded

# RIGHT - if you need tilde expansion
path=~/suffix            # Tilde at start
# Or build path differently:
path="$HOME/suffix"
```

## Best Practices

1. **Use `~` for interactive commands**: It's convenient and readable
   ```sh
   cd ~/projects
   vim ~/.vimrc
   ```

2. **Use `$HOME` in scripts for clarity**: More explicit and always works
   ```sh
   #!/bin/sh
   config_file="$HOME/.config/myapp.conf"
   ```

3. **Check if `HOME` is set**: If you must use `~`, verify `HOME` exists
   ```sh
   if [ -z "$HOME" ]; then
       echo "Error: HOME not set" >&2
       exit 1
   fi
   ```

4. **Don't rely on `~user` for portability**: User lookup might not work everywhere
   ```sh
   # Less portable:
   cp file ~john/

   # More portable:
   john_home=$(getent passwd john | cut -d: -f6)
   cp file "$john_home/"
   ```

5. **Quote the result if you use it further**: Even though tilde expansion is treated as quoted, be explicit
   ```sh
   home_dir=~
   cd "$home_dir"    # Quote it when using
   ```

## Summary: Tilde Expansion Rules

**When it happens**:
- `~` at the beginning of a word
- After `=` or `:` in assignments
- Must be unquoted

**What it expands to**:
- `~` alone → `$HOME`
- `~username` → That user's home directory

**Special rules**:
- Login name must be portable (no special characters)
- Result is treated as quoted (protected from splitting/globbing)
- Trailing slashes are handled specially
- Empty `HOME` produces empty field

**Won't expand if**:
- Quoted in any way
- Not at start of word (or after `=`/`:` in assignments)
- Contains special characters in the login name

## Availability: Tilde Expansion Across Different Builds

Tilde expansion is a word expansion feature that depends on environment variables and user database access. Support varies by build.

### POSIX API Build (Full Tilde Expansion)

- ✅ `~` expands to `$HOME`
- ✅ `~username` expands to user's home directory
- ✅ Multiple expansions in assignments
- ✅ User lookup via `getpwnam()` or equivalent
- ✅ All expansion contexts work
- ✅ Proper quoting treatment

**Full functionality as specified.**

### UCRT API Build (Partial Tilde Expansion)

- ✅ `~` expands to `$HOME`
- ⚠️ `~username` expansion is **limited**

**What works**:
```sh
cd ~                 # Expands to $HOME
ls ~/documents       # Works
PATH=~/bin:$PATH     # Works in assignments
```

**What's limited**:

1. **User lookup**: Windows doesn't have `/etc/passwd` or `getpwnam()`
   - `~username` may not work at all
   - Or may use Windows-specific user lookup methods
   - Results are implementation-dependent

2. **HOME variable**:
   - Typically set to `%USERPROFILE%` equivalent
   - Should work for `~` expansion
   - May be `C:\Users\username`

**Practical implications**:

Basic `~` works fine:
```sh
cd ~                 # Works
ls ~/documents       # Works
```

But user-specific expansion is unreliable:
```sh
ls ~john/documents   # May not work (no reliable user lookup)
```

**Best practice on UCRT**: Only use `~` for your own home directory, not `~username` for other users.

### ISO C Build (No Tilde Expansion)

- ❌ Tilde expansion is **limited**

**Why**: ISO C provides:
- No `getpwnam()` for user lookup
- No directory traversal or user database access

**What this means**:
If a `$HOME` variable is set, it will be used for `~` expansion, but, in practice, the shell always sets `HOME` to `.` (current directory) in ISO C builds, so `~` does not expand meaningfully.

The `~user` form cannot be supported at all.

### Summary Table

| Feature | POSIX | UCRT | ISO C |
|---------|-------|------|-------|
| `~` → `$HOME` | ✅ Yes | ✅ Yes | ❌ No |
| `~/path` | ✅ Yes | ✅ Yes | ❌ No |
| `~username` | ✅ Yes | ⚠️ Limited | ❌ No |
| In assignments (`var=~`) | ✅ Yes | ✅ Yes | ❌ No |
| Multiple in PATH (`~/bin:~/scripts`) | ✅ Yes | ✅ Yes | ❌ No |
| User database lookup | ✅ Yes | ❌ No | ❌ No |
| Treated as quoted after expansion | ✅ Yes | ✅ Yes | N/A |


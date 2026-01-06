#  Shell Grammar

The shell doesn't just read your commands as plain text—it breaks them down step by step into meaningful pieces. This process is called **parsing**, and it follows strict rules defined in the POSIX standard. Part of the reason that the shell command language can be so tricky is that so much of the interpretation of an input depends on context when things look ambiguous.

In this section, we'll walk through how the shell turns your typed input into tokens (small units like words or operators), decides what those tokens really are (e.g., a command name, a variable assignment, or a keyword like `if`), and then builds complete commands from them. Despite best efforts to simplify, this section is inherently complex because of the many special cases and rules involved. That's just how shells work!

## Step 1: Breaking Input into Tokens (Lexical Conventions)

When you type a line and press Enter, the shell first scans the characters to split everything into tokens (basic building blocks). It uses rules similar to those in "Token Recognition" (section 2.3 of the standard).

Here's how it classifies tokens, in the order it checks:

- **Operators**: Things like `&&`, `||`, `;`, `&`, `>>`, `<<`, etc., become special operator tokens right away.
  - Example: `&&` → recognized as "AND_IF" (logical AND).
- **I/O Numbers**: If a token is just digits and the delimiter character (the character that follows the token) is `<` or `>`, it becomes an **IO_NUMBER** (used for redirecting specific file descriptors).
  - Example: In `2> error.log`, the `2` is an IO_NUMBER.
- **I/O Locations**: If something looks like `{var}< file` (starts with `{`, ends with `}`, at least 3 characters, and the delimiter is `<` or `>`), it might become IO_LOCATION for advanced redirection.
  - Most everyday shells don't use this much.
- **Everything else**: Becomes a generic **TOKEN** (later turned into a WORD).

Most of what you type ends up initially as a **TOKEN**. With these tokens, the shell must look at the context to decide if that TOKEN is actually:
- A regular command or argument (WORD),
- A variable assignment (ASSIGNMENT_WORD, like `var=value`),
- A name (NAME, like in `for var`),
- Or a reserved keyword (like `if`, `then`, `do`).

**Important**: Word expansions (like `$var`, globbing `*`, etc.) happen **right before running the command**, not when parsing. So the structure is decided first, then the values are filled in.

## Step 2: Turning Tokens into Specific Types (The Special Rules)

The shell applies a set of numbered rules in specific situations to re-classify a TOKEN. These rules help it recognize keywords and assignments at the right moments. When multiple rules could apply, the highest-numbered rule wins.

Here are the most useful rules explained with examples:

### Rule 1: Reserved Words (Command Name Position)

**When this applies**: When a TOKEN appears where a reserved word could be valid (e.g., start of a command, after `;`, after `|`, or in control structures where keywords are expected).

If the TOKEN is exactly one of the reserved words (`if`, `then`, `else`, `elif`, `fi`, `do`, `done`, `case`, `esac`, `for`, `while`, `until`, `{`, `}`, `!`, `in`), it becomes that reserved word token.

- Example: After typing `if`, the shell treats it as the keyword "If", not a command name.
- **Critical note**: Quotes prevent this! `'if'` is just the string "if", not the keyword.
- Reserved words are only recognized when properly delimited (e.g., after a newline or `;`, not in the middle of unquoted text).

### Rule 2: Filenames in Redirections

**When this applies**: When a WORD appears immediately after a redirection operator (`<`, `>`, `>>`, `<&`, `>&`, `<>`, `>|`).

The shell performs pathname expansion (globbing) on this word during the redirection expansion phase. The expansion must result in exactly one field (or the result is unspecified).

- Example: `> *.txt` expands to a single filename (or fails if multiple matches).
- This is different from normal word expansion—the restriction to one field is enforced.

### Rule 3: Here-Document Delimiters

**When this applies**: When a WORD appears after `<<` or `<<-` as the here-document delimiter.

Quote removal is applied to the word to determine the delimiter that marks the end of the here-document (which begins after the next newline).

- Example: `<<"EOF"` means the delimiter is literally `EOF` (the quotes are removed).
- Example: `<<'EOF'` also gives delimiter `EOF`.
- If the delimiter has no quotes: `<<EOF`, the here-document content undergoes parameter expansion, command substitution, and arithmetic expansion. If it had quotes (anywhere in it), the content is literal.

### Rule 4: Case Statement Termination

**When this applies**: When parsing a case statement and looking for the closing `esac`.

If the TOKEN is exactly the reserved word `esac`, it becomes the Esac token. Otherwise, it's treated as WORD.

This rule ensures `esac` properly closes case statements even in contexts where it might be ambiguous.

### Rule 5: Names in `for` Loops

**When this applies**: When parsing the variable name immediately after `for`.

If the TOKEN meets the requirements for a valid name (letters, digits, underscores; must start with a letter or underscore), it becomes a **NAME** token. Otherwise, it's treated as WORD (which would cause a syntax error).

- Example: `for count in 1 2 3` → `count` is NAME.
- Invalid: `for 123 in ...` → treated as WORD, resulting in a syntax error.

### Rule 6: Third Word of `for` and `case`

**When this applies**: After a linebreak following the variable name in a `for` loop, or after the word being matched in a `case` statement.

**For `case` statements only**: If the TOKEN is exactly `in`, it becomes the reserved word In. Otherwise, it's WORD.

**For `for` loops only**: If the TOKEN is exactly `in` or `do`, it becomes the corresponding reserved word (In or Do). Otherwise, it's WORD.

- Example: In `for var` followed by a newline, the next word `in` is recognized as the keyword.
- Example: In `for var` followed by a newline, if you write `do` instead of `in`, that's also recognized (creating a loop with no iteration list).

Note: This rule only applies after a linebreak (newline). That's why this works:
```sh
for i
do
  echo $i
done
```

### Rule 7: Variable Assignments Before Commands

This is one of the trickiest (and most important) parts! Rule 7 has two sub-rules:

**Rule 7a: First word of a simple command**

If the TOKEN is exactly a reserved word, it becomes that reserved word token. Otherwise, Rule 7b applies.

**Rule 7b: Assignment word detection**

If the TOKEN contains an unquoted `=` character (as determined during token recognition—so not inside `${}` or `$()` or arithmetic expansion), then:

1. If the TOKEN begins with `=`, it becomes WORD (not an assignment).
2. If all characters before the first `=` form a valid name, the TOKEN becomes **ASSIGNMENT_WORD**.
3. Otherwise, it's implementation-defined whether it's WORD or ASSIGNMENT_WORD.

If there's no unquoted `=`, the TOKEN becomes WORD.

When an ASSIGNMENT_WORD is recognized in the prefix position of a simple command, the variable is set according to the assignment rules.

- Example: `DEBUG=1 ./script.sh` → `DEBUG=1` is recognized as ASSIGNMENT_WORD and sets DEBUG only for that command execution.
- Example: `VAR=value echo hello` → VAR is set for the echo command.
- Example: `=bad command` → Not an assignment (starts with =).
- Example: `123=bad command` → Not an assignment (doesn't start with a valid name character).

**Critical understanding**: Assignments are only recognized in the "cmd_prefix" position—before the command name. Once the command name is established, subsequent words are arguments, not assignments:
- `export VAR=value` → `VAR=value` is an argument to export, not processed as assignment by Rule 7.
- `VAR=value export` → `VAR=value` is an assignment, then export is the command name.

### Rule 8: Function Names

**When this applies**: When parsing the name in a function definition (`fname () { ... }`).

If the TOKEN is exactly a reserved word, it becomes that reserved word token (which would cause a syntax error in this position). Otherwise, if the TOKEN meets the requirements for a valid name, it becomes NAME. Otherwise, Rule 7 applies.

This prevents you from accidentally defining functions with reserved word names:
- Invalid: `if() { echo "function"; }` → `if` is recognized as reserved word, syntax error.
- Valid: `myfunction() { echo "hello"; }` → `myfunction` becomes NAME.

### Rule 9: Inside Function Bodies

**When this applies**: When parsing the body of a function definition.

Word expansion and assignment classification never occur within the function body during parsing. Each TOKEN that might normally be expanded or classified as an assignment is instead kept as a single WORD consisting only of the literal characters from the token recognition phase.

This means the function body is stored literally as written, without any interpretation:
- The expansions and assignment recognition happen later, when the function is actually executed.
- This is why you can define a function with `$var` in it, and it will use whatever value `$var` has when the function runs, not when it's defined.

## Step 3: Building the Full Command (The Grammar Overview)

Once tokens are classified, the shell builds larger structures following a hierarchy. Here's a breakdown of the main parts:

### Top-Level Structure

- **program**: The whole input—a sequence of complete commands separated by newlines.
- **complete_command**: A list of commands with a separator (`;`, `&`) or without (just newline).
- **list**: Commands chained with `&&` (run next only if previous succeeds) or `||` (run next only if previous fails). These are called "and-or lists."
- **and_or**: A pipeline, optionally preceded by another and_or with `&&` or `||`.
- **pipeline**: Commands connected by `|` (pipe output of one to input of next). Can start with `!` to invert the exit status.
- **pipe_sequence**: One or more commands separated by `|`.

### Command Types

The **command** production can be one of:
1. **simple_command**: The most common—optional assignments + optional command name + optional arguments + optional redirections.
   - Example: `VAR=hello grep hello file.txt > output`
   - Example: `VAR=value` (assignment with no command)
   - Example: `> output` (redirection with no command—valid, though unusual)
2. **compound_command**: Control structures like loops, conditionals, groups.
3. **compound_command redirect_list**: A compound command with redirections applied.
4. **function_definition**: `name() { commands; }` or similar syntax.

### Compound Commands You'll Use Most

- **brace_group**: `{ commands; }` – runs commands in current shell (no subshell). Note: The opening `{` must be followed by a space/newline, and the `}` must be preceded by `;` or newline.
- **subshell**: `( commands )` – runs commands in a child shell (forked process). Variable assignments inside don't affect the parent.
- **for_clause**: `for name in words; do commands; done` or `for name; do commands; done` (iterates over `"$@"`).
- **case_clause**: `case word in pattern) commands ;; esac` – pattern matching with multiple branches.
- **if_clause**: `if commands; then commands; [elif commands; then commands;]... [else commands;] fi`
- **while_clause**: `while commands; do commands; done` – loop while condition succeeds.
- **until_clause**: `until commands; do commands; done` – loop until condition succeeds.

### Redirections

Redirections can appear almost anywhere in a command:
- Before the command name: `< input command arg`
- After the command name: `command < input arg`
- At the end: `command arg < input`
- Multiple redirections: `command < input > output 2> errors`

Common redirection operators:
- `< file` – redirect standard input from file
- `> file` – redirect standard output to file (overwrite)
- `>> file` – redirect standard output to file (append)
- `2> file` – redirect standard error (file descriptor 2) to file
- `<& digit` – duplicate input file descriptor
- `>& digit` – duplicate output file descriptor
- `<> file` – open file for reading and writing on standard input
- `>| file` – redirect output to file, overriding noclobber option

**Here-documents**: `<<DELIM` followed by content on subsequent lines until a line containing only DELIM:
```sh
cat <<EOF
This is content
that goes to cat
EOF
```

### Separators and Operators

- `;` → sequential execution (wait for command to finish)
- `&` → background execution (don't wait)
- `&&` → logical AND (run next only if previous succeeds, exit status 0)
- `||` → logical OR (run next only if previous fails, exit status non-zero)
- `|` → pipe (connect output to input)
- Newlines also act as command separators in most contexts

### Grammar Structure Summary

Here's how these pieces fit together:

```
program
  ├─ complete_command
  │    └─ list (and_or chains)
  │         └─ and_or (with && or ||)
  │              └─ pipeline (with |)
  │                   └─ command
  │                        ├─ simple_command
  │                        ├─ compound_command
  │                        └─ function_definition
  └─ (more complete_commands)
```

## Quick Tips for Everyday Scripting

- **Always separate keywords with spaces or newlines**—quotes hide them! Writing `{echo hello}` won't work; you need `{ echo hello; }`.

- **Use variable assignments before commands** for temporary environment changes: `DEBUG=1 ./script` sets DEBUG only for that one execution.

- **Remember that expansions happen late**: the grammatical structure is determined first, then word expansions are performed just before execution. This is why `[ $var = value ]` can fail if `$var` is empty (it becomes `[ = value ]`), but `[[ $var = value ]]` works (different parsing rules in `[[`).

- **Write multi-line loops/conditionals with proper `do` and `done`** after newlines—the shell will recognize them correctly:
  ```sh
  for file in *.txt
  do
    process "$file"
  done
  ```

- **Be aware of when quotes matter**: Reserved words must be unquoted to be recognized. Variable assignments must have unquoted `=` to be recognized.

- **Function definitions are parsed but not expanded**: You can use variables and expansions in function bodies, and they'll be evaluated when the function runs, not when it's defined.

## Understanding Why Shell Scripts Are Picky

This parsing process is why shell scripts are so powerful yet sometimes frustrating. The interpretation of almost everything depends on:
- **Position** (is this the first word? after a redirect? after `for`?)
- **Context** (are we in a function definition? in a case pattern?)
- **Quoting** (does this contain quotes? is the `=` quoted?)
- **Whitespace** (are keywords properly separated? is there a linebreak?)

When you understand these parsing rules, you'll have much better intuition about why certain shell constructs work or don't work, and you'll be able to write more reliable scripts.

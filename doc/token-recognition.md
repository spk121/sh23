# Token Recognition

In order to understand how shell parses the Shell Command Language, we must first understand what a **token** is and how the shell recognizes these tokens. A token is basically one piece of the command line that has a specific meaning to the shell. Tokens can be keywords, operators, identifiers, literals, etc.

One surprisingly complex aspect of token recognition is deciding where one token ends and the next begins. It is complicated by the various quoting mechanisms provided by the shell, which can change how characters are interpreted.

## How the Shell Reads Input

The shell reads its input **line by line**. Each line can be of unlimited length (there's no hard limit on how long a command line can be). These lines are then broken down into tokens using a set of specific rules.

The shell operates in two main parsing modes:
1. **Ordinary token recognition** - The normal mode for parsing commands
2. **Here-document processing** - A special mode for reading here-document content

Most of the time, you're in ordinary token recognition mode. Here-document processing is a special case that happens when the shell encounters `<<` or `<<-` operators.

## The Two-Phase Parsing Model

Understanding shell parsing requires understanding that it happens in phases:

**Phase 1: Token Recognition (What we're discussing here)**
- The shell reads characters and groups them into tokens
- Quoting is recognized but not processed
- Substitutions are identified but not performed
- The shell determines token boundaries

**Phase 2: Token Processing (Happens later)**
- Tokens are categorized by the grammar
- Substitutions are actually performed
- Word expansion happens
- Commands are executed

This is crucial: during token recognition, when you write `$HOME`, the shell doesn't replace it with your home directory yet. It just recognizes "this is a parameter expansion" and includes the literal characters `$HOME` in the token.

## Here-Documents: The Special Case

Before we dive into ordinary token recognition, let's understand the special case of here-documents.

When the shell sees a here-document operator (like `<<EOF`), it needs to know where the here-document content is. The rules are:

1. **Recognition**: When `<<` is seen, the shell recognizes an `io_here` token
2. **Delimiter**: The word after `<<` becomes the delimiter (like `EOF`)
3. **Content location**: The here-document content starts on the line **after the next newline**
4. **Saving tokens**: Any tokens found while looking for that newline are saved for later processing

Example to illustrate:
```bash
cat <<EOF > output.txt
Content here
EOF
```

What happens:
1. `cat` is tokenized
2. `<<EOF` is recognized as a here-document
3. `> output.txt` is found while looking for newline - **saved for later**
4. After the newline, the shell switches to here-document mode
5. `Content here` is read as here-document content
6. `EOF` delimiter ends the here-document
7. Now the saved `> output.txt` is processed

**Multiple here-documents:**
```bash
cat <<END1 <<END2
First doc
END1
Second doc
END2
```

The second here-document starts immediately after the first one's delimiter.

**Important**: If a saved token contains a newline character, the behavior is unspecified. Don't rely on this working.

## Ordinary Token Recognition: The Rules

When not processing here-documents, the shell applies a set of rules **in order** to each character it reads. These rules determine:
- When a token starts
- What gets included in the token
- When a token ends (is "delimited")

### Starting a Token

A token starts:
- At the very beginning of input, or
- After the previous token has been delimited

The token begins with the first character that:
- Hasn't already been included in a token, and
- Isn't discarded by the rules (like blanks between tokens)

### Building and Ending a Token

Once started, characters are added to the token until one of the rules says "this token is done" (delimited). The token consists of **exactly** the characters between its start and end delimiters, including any quote characters.

**Important**: If a token is delimited but contains no characters, that empty token is **discarded**.

### The Rules (In Order of Application)

The shell checks these rules **in order** for each character. The first rule that applies is used:

#### Rule 1: End of Input
**When it applies**: The shell reaches the end of input (no more characters to read)

**What happens**: The current token (if any) is delimited

Example:
```bash
echo hello
```
After reading `hello`, hitting end-of-line delimits the `hello` token.

#### Rule 2: Operator Continuation
**When it applies**:
- The previous character was part of an operator, AND
- The current character is not quoted, AND
- The current character can combine with the previous characters to form a (longer) operator

**What happens**: The current character becomes part of that operator token

Example:
```bash
command >>file
```
- First `>` starts an operator
- Second `>` can combine with first to make `>>`
- Both become one `>>` operator token

Valid multi-character operators: `&&`, `||`, `;;`, `<<`, `>>`, `<&`, `>&`, `<<-`, `<>`, `>|`, `;&`

#### Rule 3: Operator Completion
**When it applies**:
- The previous character was part of an operator, AND
- The current character cannot combine with it to form a longer operator

**What happens**: The operator containing the previous character is delimited

Example:
```bash
command >file
```
- `>` starts an operator
- `f` (from `file`) can't combine with `>` to make a longer operator
- The `>` operator is delimited
- `f` starts a new token

#### Rule 4: Quoting
**When it applies**: The current character is an **unquoted**:
- Backslash (`\`), or
- Single quote (`'`), or
- Double quote (`"`), or
- Start of dollar-single-quote (`$'`)

**What happens**:
- The quoting affects subsequent characters up to the end of the quoted text
- The token is **not** delimited by the end of the quoted text
- The token contains the literal characters, including the quote characters themselves
- No substitutions are performed during tokenization

Example:
```bash
echo "hello world"
```
- `echo` is one token
- Space starts `"hello world"` token
- `"` starts quoting
- Characters `hello world` are included (with quotes)
- Closing `"` ends quoting but doesn't delimit the token
- Next space delimits the token
- Token value: `"hello world"` (including quotes!)

**Critical understanding**: During tokenization, quotes are **preserved** in the token. Quote removal happens later, during word expansion.

#### Rule 5: Substitutions (Parameter, Command, Arithmetic)
**When it applies**: The current character is an unquoted `$` or backtick (`` ` ``)

**What happens**:
- The shell identifies what kind of substitution this is:
  - `$var` or `${var}` - parameter expansion
  - `$(cmd)` or `` `cmd` `` - command substitution
  - `$((expr))` - arithmetic expansion
- The shell reads **sufficient input** to find the end of the substitution
- Nested quoting and substitutions are **recursively** processed
- The entire substitution (including delimiters like `${}` or `$()`) is included literally in the token
- The token is **not** delimited by the end of the substitution

Example:
```bash
echo "Result: $(date)"
```
- Token starts with `"`
- When `$` is encountered, shell identifies `$(date)` command substitution
- Reads until closing `)` to find end
- Entire `$(date)` is included in token
- Token continues after closing `)`
- Token value: `"Result: $(date)"` (literal - substitution happens later!)

**Nested constructs**:
```bash
echo "$(echo "nested")"
```
The shell recursively processes the inner `"nested"` as quoted text within the command substitution.

**Here-documents in substitutions**:
```bash
var=$(cat <<EOF
content
EOF
)
```
If the `)` comes before the here-document content's newline, behavior is unspecified. Don't do this.

#### Rule 6: New Operator
**When it applies**:
- The current character is not quoted, AND
- It can be used as the **first** character of a new operator

**What happens**:
- The current token (if any) is delimited
- The current character starts a new operator token

Example:
```bash
echo hello>file
```
- `echo` is delimited by space
- `hello` token is being built
- `>` can start an operator
- `hello` is delimited
- `>` starts a new (operator) token

Operator-starting characters: `|`, `&`, `;`, `<`, `>`, `(`, `)`

#### Rule 7: Blank (Whitespace)
**When it applies**: The current character is an **unquoted** blank (space or tab)

**What happens**:
- Any token containing the previous character is delimited
- The current blank character is **discarded**

Example:
```bash
echo hello world
```
- Space after `echo` delimits the `echo` token, space is discarded
- Space after `hello` delimits `hello`, space is discarded
- Space after `world` (at end of line) delimits `world`, space is discarded

This is why spaces separate arguments!

**Important**: Spaces inside quotes are **not** blanks (they're quoted), so they don't delimit:
```bash
echo "hello world"
```
The space is part of the token because it's quoted.

#### Rule 8: Append to Word
**When it applies**: The previous character was part of a word

**What happens**: The current character is appended to that word

Example:
```bash
echo hello
```
- `h` starts a word
- `e`, `l`, `l`, `o` are each appended to that word

#### Rule 9: Comment
**When it applies**: The current character is `#`

**What happens**:
- The `#` and all subsequent characters up to (but excluding) the next newline are **discarded** as a comment
- The newline itself is not part of the comment

Example:
```bash
echo hello # this is a comment
echo world
```
- `# this is a comment` is discarded
- The newline ends the first command
- `echo world` is the next command

**Important**: `#` only starts a comment in certain positions - generally at the start of a word. In the middle of a word or after other characters, it's just a regular character:
```bash
echo hello#world    # '#' is part of the word
```

#### Rule 10: Start New Word
**When it applies**: None of the above rules applied

**What happens**: The current character starts a new word token

Example:
```bash
echo abc
```
- `a` doesn't match other rules, so it starts a new word
- `b` and `c` are appended (Rule 8)

## After Tokenization: Categorization

Once a token is delimited, it's categorized according to the grammar rules (see the Shell Grammar section). This is where the shell decides:
- Is this token a keyword like `if` or `then`?
- Is it an assignment like `var=value`?
- Is it a regular word?
- Is it a redirection?

This categorization happens **after** token recognition is complete.

## Execution Timing

When the shell is parsing a program (like a script), it follows this pattern:
1. Tokenize and parse until a complete command is recognized
2. **Execute** that complete command
3. Then tokenize and parse the next complete command

Example:
```bash
command1; command2; command3
```
1. Tokenize up to first `;`, recognize `command1` as complete
2. Execute `command1`
3. Continue tokenizing, recognize `command2`
4. Execute `command2`
5. And so on...

This is why you can see output from earlier commands while later commands are still being parsed in an interactive shell.

## Practical Examples

### Example 1: Simple Command with Quotes

```bash
echo "hello world" > file.txt
```

Tokenization:
1. `echo` - word token
2. Space - delimits `echo`, discarded
3. `"hello world"` - one token (quotes don't delimit, space is quoted)
4. Space - delimits previous token, discarded
5. `>` - operator token (delimits if word was being built)
6. Space - discarded
7. `file.txt` - word token
8. Newline - delimits `file.txt`

Result: Four tokens: `echo`, `"hello world"`, `>`, `file.txt`

### Example 2: Substitution

```bash
echo $(date)
```

Tokenization:
1. `echo` - word token
2. Space - delimits `echo`, discarded
3. `$` starts substitution
4. Shell reads to closing `)`, finds `$(date)`
5. Token includes literal `$(date)`
6. Newline - delimits token

Result: Two tokens: `echo`, `$(date)`

Later, during expansion, `$(date)` is replaced with the output of the `date` command.

### Example 3: Complex Operators

```bash
command >>file 2>&1
```

Tokenization:
1. `command` - word token
2. Space - delimits
3. `>` starts operator
4. Second `>` combines: `>>` operator
5. Space - delimits `>>` operator (Rule 3)
6. `file` - word token
7. Space - delimits
8. `2` starts word
9. `>` can start operator - `2` is delimited (becomes IO_NUMBER later)
10. `>` starts operator
11. `&` combines with `>`: `>&` operator
12. `1` - word token
13. Newline - delimits

Result: `command`, `>>`, `file`, `2`, `>&`, `1`

### Example 4: Quotes and Variables Together

```bash
var="hello $USER"
```

Tokenization:
1. `var` starts word
2. `=` is part of word (becomes ASSIGNMENT_WORD later)
3. `"` starts quoting
4. `hello $USER` are included literally
5. `"` ends quoting but doesn't delimit
6. Newline - delimits token

Result: One token: `var="hello $USER"`

Note: `$USER` is literal in the token. Expansion happens later.

### Example 5: Comments

```bash
echo hello # comment
# another comment
echo world
```

Tokenization:
1. `echo` - token
2. Space - delimits, discarded
3. `hello` - token
4. Space - delimits, discarded
5. `#` - starts comment
6. ` comment` - discarded
7. Newline - ends comment, delimits `echo hello` command
8. `#` - starts comment
9. ` another comment` - discarded
10. Newline - ends comment
11. `echo` - starts new token
12. ...

## Common Pitfalls and Surprises

### Pitfall 1: Quotes Are Preserved During Tokenization

```bash
var="hello"
```

Many people expect the token to be `var=hello` (without quotes). But during tokenization, the token is actually `var="hello"` with the quotes. Quote removal happens later during expansion.

### Pitfall 2: Substitutions Aren't Performed Yet

```bash
echo $HOME
```

The token is literally `$HOME`, not `/home/user`. The expansion happens in a later phase.

### Pitfall 3: Spaces in Unquoted Variables

```bash
var="hello world"
echo $var          # Two tokens during expansion!
echo "$var"        # One token
```

During tokenization, both create one token: `$var` and `"$var"`. But during expansion, the unquoted version splits into two words.

### Pitfall 4: Empty Tokens Are Discarded

```bash
echo ""
```

During tokenization: `echo`, `""` (two tokens)
But `""` expands to empty string, which is handled specially to avoid being discarded in some contexts.

### Pitfall 5: Comments Only Work in Specific Positions

```bash
var=value#comment     # This doesn't work!
var=value #comment    # This works
```

The `#` must be at a position where a new token could start (after whitespace) to begin a comment.

## Summary: The Token Recognition Process

1. **Read character by character** from input
2. **Apply rules in order** to each character
3. **Build tokens** according to the rules
4. **Preserve literal characters** including quotes and substitution syntax
5. **Delimit tokens** when rules indicate
6. **Discard empty tokens**
7. **Pass tokens to grammar** for categorization
8. **Execute complete commands** as they're recognized

Token recognition is the foundation of shell parsing. Understanding it helps explain why certain shell constructs work the way they do, and why quoting and spacing are so important.


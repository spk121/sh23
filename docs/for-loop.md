# The for Loop

A **for loop** allows you to execute a sequence of commands repeatedly, once for each item in a list. It is ideal for iterating over a known set of values, such as filenames, arguments, or any explicit list of words.

The loop uses the reserved words `do` and `done` to delimit the body of commands.

## Syntax

```
for name [ in word ... ]
do
compound-list
done
```

- `name` — the loop variable that will take on each value in turn.
- `in word ...` — optional list of words to iterate over. If omitted, the loop defaults to iterating over the positional parameters (`"$@"`).
- `compound-list` — the commands to execute for each iteration.

### Word Expansion

The words after `in` (if present) are subject to normal shell expansions (tilde expansion, parameter expansion, command substitution, arithmetic expansion, and field splitting). The resulting fields become the items in the iteration list.

If the expansion produces **no items**, the loop body is **not executed at all**.

## Examples

### Basic loop over explicit words

```sh
for fruit in apple banana cherry
do
echo "I like $fruit"
done
```


Output:

```
I like apple
I like banana
I like cherry
```


### Default behavior (no `in` clause) — iterates over positional parameters

```sh
for arg
do
echo "Argument: $arg"
done
```

This is equivalent to:
```sh
for arg in "$@"
do
echo "Argument: $arg"
done
```

This behavior is useful in scripts to process all command-line arguments.

### Looping over files (using glob expansion)

```sh
for file in *.txt
do
echo "Processing $file"
wc -l "$file"
done
```

If no `.txt` files exist, the loop body is skipped entirely.

### Looping over command substitution results

```sh
for user in $(cat userlist.txt)
do
echo "Adding user: $user"
adduser "$user"
done
```

**Caution**: This splits on whitespace and is fragile with filenames containing spaces. Prefer `while read` for line-by-line processing when possible.

## Exit Status

- If the word list expands to **at least one item**: the exit status is the exit status of the **last** `compound-list` executed.
- If the word list expands to **no items**: the exit status is **0** (success, loop simply did nothing).

This behavior makes it safe to use in scripts without extra checks for empty lists.

## Common Patterns

| Use Case                          | Example Code Fragment                          |
|-----------------------------------|------------------------------------------------|
| Process all arguments             | `for arg in "$@"; do ...; done`                |
| Iterate over numbers (with seq)   | `for i in $(seq 1 10); do echo $i; done`       |
| Safe filename handling            | `for file in *.mp3; do [[ -e $file ]] || continue; ...; done` |

## Availability

| API   | Availability                  |
|-------|-------------------------------|
| POSIX | Available                     |
| UCRT  | Available                     |
| ISO C | Available               |


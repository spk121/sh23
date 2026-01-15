# Shell Introduction

The shell is a command language interpreter. The name of the language it interprets is the Shell Command Language. The Shell Command Language is defined in the The Open Group Base Specifications Issue 8, IEEE Std 1003.1-2024, Copyright - 2001-2024 The IEEE and The Open Group.

For most users, the usage of the shell command is quite straightforward: limited to entering commands interactively into a prompt, or occassionally writing a short script to perform a repetitive task, but, the Shell Command Language is quite powerful in its own right; it, however, quite arcane for those of use used to scripting in Python.

This document will describe the Shell Command Language, with its many intricacies.

## Briefest Introduction of the Shell Command Language

In shell, you can do the following:

Run a command.
```sh
ls
```

Run a command with arguments.
```sh
ls -l /home/user
```

Redirect input and output.
```sh
ls > filelist.txt
cat < filelist.txt
```

Redirect using file descriptors (stdin=0, stdout=1, stderr=2).
```sh
command 2> errors.txt
command > output.txt 2>&1
```

Use pipes to connect commands.
```sh
ls -l | grep ".txt"
```

Run commands in the background.
```sh
long_running_command &
```

Chain commands with logical operators.
```sh
mkdir mydir && cd mydir
command || echo "Command failed"
```

Use variables.
```sh
filename="filelist.txt"
cat "$filename"
```

Use single quotes to preserve literal values.
```sh
echo 'Special chars: $HOME * ? are literal'
```

Use double quotes to preserve spaces but allow expansions.
```sh
echo "Current directory: $PWD"
```

Use backslash to escape special characters.
```sh
echo "Price: \$100"
```

Use arithmetic expansion.
```sh
result=$((5 + 3 * 2))
echo "Result: $result"
```

Use conditionals with if/then/else.
```sh
if [ -f "$filename" ]; then
  echo "File exists"
else
  echo "File does not exist"
fi
```

Use case statements for pattern matching.
```sh
case "$option" in
  start) echo "Starting..." ;;
  stop)  echo "Stopping..." ;;
  *)     echo "Unknown option" ;;
esac
```

Use for loops to iterate.
```sh
for file in *.txt; do
  echo "Processing $file"
done
```

Use while loops for repeated execution.
```sh
while [ $count -lt 10 ]; do
  echo "Count: $count"
  count=$((count + 1))
done
```

Use until loops (inverse of while).
```sh
until [ -f ready.txt ]; do
  echo "Waiting..."
  sleep 1
done
```

Group commands with braces (runs in current shell).
```sh
{ echo "Starting"; ls; echo "Done"; }
```

Group commands with parentheses (runs in subshell).
```sh
(cd /tmp && echo "In temp directory: $(pwd)")
echo "Still in original directory: $(pwd)"
```

Define functions.
```sh
greet() {
  echo "Hello, $1!"
}
greet "World"
```

Define functions that take multiple arguments.
```sh
add() {
  echo $(($1 + $2))
}
add 5 3
```

Use command substitution.
```sh
current_date=$(date)
echo "Today is $current_date"
```

These are just the basics. The shell command language has many more features and capabilities that you can explore in the subsequent sections of this documentation.

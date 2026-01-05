# Shell Language Documentation

## Index and Table of Contents

This document provides an overview of the sh23 shell command and shell language. This shell aims to be POSIX-compliant with very few extensions.  It provides different feature sets depending on the C library API available at build time: POSIX, UCRT, or a library strictly conforming to ISO C. The POSIX API mode is the most feature-rich, while the ISO C mode is the most limited.

### Core Language Constructs

- [Shell Introduction](shell-introduction.md)
- [Shell Overview](shell-overview.md)
- [Reserved Words](reserved-words.md)
- [Parameters and Variables](parameters-variables.md)
- [Quoting](quoting.md)
- [Word Expansions](word-expansions.md)
- [Alias Substitution](alias-substitution.md)
- [Redirection](redirection.md)
- [Command Execution](command-execution.md)
- [Compound Commands](compound-commands.md)
  - [Lists](lists.md) including AND-OR lists, sequential lists, and asynchronous lists
  - [Conditional Constructs](conditional-constructs.md)
  - [Pipelines](pipelines.md)
  - [Looping Constructs](looping-constructs.md)
  - [Grouping Commands](grouping-commands.md) including subshells
- [Arithmetic Evaluation](arithmetic-evaluation.md)
- [Functions](functions.md)
- [Job Control](job-control.md)
- [Signals and Traps](signals-traps.md)

### Builtins and Options

- [set — Shell Options](set-options.md)
- [Builtin Commands](builtins.md)

### Appendices
- [Symbol Index](symbol-index.md)
- [Availability Notes](availability.md)
- [Glossary](glossary.md)

---

*Last updated: TBD*
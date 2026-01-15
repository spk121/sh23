# Shell Language Documentation

## Index and Table of Contents

This document provides an overview of the sh23 shell command and shell language. This shell aims to be POSIX-compliant with very few extensions.  It provides different feature sets depending on the C library API available at build time: POSIX, UCRT, or a library strictly conforming to ISO C. The POSIX API mode is the most feature-rich, while the ISO C mode is the most limited.

### Core Language Constructs

- [Shell Command Language Introduction](shell-introduction.md)
- [Quoting](quoting.md)
- [Token Recognition](token-recognition.md)
- [Alias Substitution](alias-substitution.md)
- [Reserved Words](reserved-words.md)
- [Parameters](parameters.md)
- [Variables](variables.md)
- Expansions
  - [Overview of Expansions](expansions-overview.md) - TODO
  - [Tilde Expansion](tilde-expansion.md)
  - [Parameter Expansion](parameter-expansion.md)
  - [Command Substitution](command-substitution.md)
  - [Arithmetic Expansion](arithmetic-expansion.md)
  - [Field Splitting](field-splitting.md)
- [Redirection and Here Documents](redirection-and-heredoc.md)
- [Exit Status and Errors](exit-status.md)
- [Simple Commands](simple-commands.md)
- [Pipelines](pipelines.md)
- [Lists](lists.md)
- [Grouping Commands](grouping-commands.md)
- [For Loop](for-loop.md)
- [While and Until Loops](while-until.md)
- [Case Conditional](case-conditional.md)
- [If Conditional](if-conditional.md)
- [Functions](functions.md)
- [Shell Grammar](shell-grammar.md)
- [Job Control](job-control.md)
- [Signals and Error Handling](signals.md)
- [Shell Execution Environment](shell-environment.md)
- [Pattern Matching](pattern-matching.md)

### Built-in Commands
- [Builtin Commands Overview](builtin-commands-overview.md) - TODO
- [break — Exit from Loop](break-exit-loop.md) - TODO
- [colon — No-op Command](colon-noop.md) - TODO
- [continue — Continue Loop](continue-loop.md) - TODO
- [dot — Source File](dot-source-file.md) - TODO
- [eval — Evaluate Command](eval-evaluate-command.md) - TODO
- [exec — Execute Command](exec-execute-command.md) - TODO
- [exit — Exit Shell](exit-shell.md) - TODO
- [export — Set Environment Variables](export-set-environment-variables.md) - TODO
- [readonly — Set Read-Only Variables](readonly-set-readonly-variables.md) - TODO
- [return — Return from Function](return-from-function.md) - TODO
- [set — Shell Options](set-options.md)
- [shift — Shift Positional Parameters](shift-positional-parameters.md) - TODO
- [times — Print CPU Times](times-print-cpu-times.md) - TODO
- [trap — Set Signal Handlers](trap-set-signal-handlers.md) - TODO
- [unset — Unset Variables and Functions](unset-unset-variables-functions.md) - TODO

### Platform-Specific Features
- [Platform-Specific Features Overview](platform-overview.md) - TODO

### Interactive Features
- [Interactive Shell Overview](interactive-shell-overview.md) - TODO
- [Command History](command-history.md) - TODO
- [Job Control in Interactive Shells](job-control-interactive-shells.md) - TODO
- [Prompt Customization](prompt-customization.md) - TODO
- [Tab Completion](tab-completion.md) - TODO
- [Shell Options for Interactive Use](shell-options-interactive-use.md) - TODO
- [Signal Handling in Interactive Shells](signal-handling-interactive-shells.md) - TODO
- [Aliases in Interactive Shells](aliases-interactive-shells.md) - TODO
- [Environment Variables for Interactive Use](environment-variables-interactive-use.md) - TODO
- [Scripting vs Interactive Modes](scripting-vs-interactive-modes.md) - TODO
- [Debugging Interactive Shell Sessions](debugging-interactive-shell-sessions.md) - TODO
- [Security Considerations in Interactive Shells](security-considerations-interactive-shells.md) - TODO

### Appendices
- [Symbol Index](symbol-index.md)
- [Availability Notes](availability.md)
- [Glossary](glossary.md)

---

*Last updated: TBD*
# sh23

[![CI](https://github.com/spk121/sh23/actions/workflows/ci.yml/badge.svg)](https://github.com/spk121/sh23/actions/workflows/ci.yml)

This intends to be a POSIX shell, but, it will have a compile-time option to
that it can be built using only ISO C.

When built in ISO C mode
- no pipes or redirection
- no directory management
- all commands launched in the foreground


## Layered Structure

```
Raw Input
    ↓
Lexer → TOKEN_WORD with parts (unexpanded $(), ${}, $(( ))
    ↓
Tokenizer → Alias expansion + re-lexing → clean token stream
    ↓
Parser → Builds AST (knows about if/then/fi, for/in, function, etc.)
    ↓
Expander → Performs:
    • Parameter expansion
    • Command substitution $(...) and `...`
    • Arithmetic expansion $(())
    • Field splitting
    • Quote removal
    • Glob expansion
    ↓
Executor
```

## Lexer

## Tokenizer

Only responsible for alias expansion and re-lexing of alias values.
It needs to careful with recursion and the blank-following alias rule.

## Parser

Does reserved word recognition: recognizing when strings are keywords. It
builds the AST.

## Expander

First it does parameter expansion, command substitution, arithmetic expansion.

Then it does field splitting.

Then it does and quote removal and glob expansion.

## Executor

Actually executes the AST and runs the commands.

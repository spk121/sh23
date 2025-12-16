# String Usage Audit for sh23

## Executive Summary

This document provides a comprehensive audit of `string_t` usage throughout the sh23 codebase. The audit identifies **71 opportunities** to replace low-level C string operations with higher-level methods provided by the `string_t` class defined in `src/string_t.h`.

### Purpose

The `string_t` class provides a rich set of methods for string manipulation that offer:
- **Better safety**: Automatic bounds checking and memory management
- **Better performance**: Optimized implementations with capacity management
- **Better readability**: Clear, expressive method names
- **Better maintainability**: Consistent API across the codebase

### Findings Summary

Total findings: **71**

| Pattern Type | Count | Severity |
|--------------|-------|----------|
| strcmp() usage | 53 | Low |
| strlen() usage | 11 | Medium |
| strncmp() usage | 3 | Medium |
| sprintf/snprintf usage | 3 | Low |
| strchr() usage | 1 | Medium |

## Recommended string_t Methods Reference

For reference, here are the key `string_t` methods that can replace common C string operations:

| C Function | string_t Replacement | Description |
|------------|---------------------|-------------|
| `strlen(s)` | `string_length(str)` | Get string length (only if `s` is a `string_t`) |
| `strcmp(s1, s2)` | `string_compare_cstr(str, cstr)` (returns int like strcmp) or `string_eq(str1, str2)` (returns bool for equality only) | Compare strings - use `compare_cstr` when comparing string_t with C string and need ordering, `eq` for equality between two string_t |
| `strncmp(s1, s2, n)` | `string_starts_with_cstr(str, prefix)` or `string_ends_with_cstr(str, suffix)` | Check prefix/suffix - clearer intent than length-based comparison |
| `strstr(s1, s2)` | `string_contains_cstr(str, substr)` (bool) or `string_find_cstr(str, substr)` (int, -1 if not found vs NULL) | Find substring - use contains for existence check, find for position |
| `strcpy(dest, src)` | `string_set_cstr(str, cstr)` | Copy string (only if `dest` is a `string_t`) |
| `strcat(dest, src)` | `string_append_cstr(str, cstr)` | Append string (only if `dest` is a `string_t`) |
| `strchr(s, c)` | `string_find_cstr(str, "c")` with single-char string | Find character (returns index -1 vs pointer NULL) |
| `strrchr(s, c)` | `string_rfind_cstr(str, "c")` with single-char string | Find last character (returns index -1 vs pointer NULL) |
| `s[0] == '\0'` or `*s == 0` | `string_empty(str)` | Check if empty (only if `s` is a `string_t`) |
| `sprintf(buf, fmt, ...)` | `string_printf(str, fmt, ...)` | Formatted string (only if creating/modifying a `string_t`) |

### Important Notes About Replacements

**⚠️ Context Matters**: The recommendations in this audit are pattern-based and must be evaluated in context:

1. **Type checking required**: Many findings involve C string operations on `char*` variables. The `string_t` methods can only be used when operating on actual `string_t` objects. Converting between types just to use these methods may not be beneficial.

2. **Return value differences**: Pay special attention to return value semantics:
   - `strcmp()` returns int (negative/0/positive), while `string_eq()` returns bool (true/false)
   - `strstr()` returns pointer (NULL if not found), while `string_find_cstr()` returns int (-1 if not found)
   - `strchr()` returns pointer (NULL if not found), while `string_find_cstr()` returns int (-1 if not found)

3. **Function purpose**: Use the right replacement for the use case:
   - For ordering comparison: use `string_compare_cstr()` (returns int like strcmp)
   - For equality testing: use `string_eq()` (returns bool)
   - For existence check: use `string_contains_cstr()` (returns bool)
   - For position finding: use `string_find_cstr()` (returns index)

4. **Performance consideration**: Converting between C strings and string_t just to use these methods may add overhead. The recommendations are most beneficial when the variables are already string_t objects.

## Detailed Findings by File

The following sections detail each finding, organized by source file.

### ast.c

Total findings in this file: **1**

#### Line 696: sprintf/snprintf usage

**Severity**: Low

**Current code**:
```c
snprintf(buf, sizeof(buf), "%d", node->data.redirection.io_number);
```

**Recommendation**: Consider using string_printf() if creating or modifying string_t

**Suggested replacement**: `string_printf()`

---

### executor.c

Total findings in this file: **1**

#### Line 891: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(fd.name, ".") == 0 || strcmp(fd.name, "..") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

### expander.c

Total findings in this file: **3**

#### Line 808: sprintf/snprintf usage

**Severity**: Low

**Current code**:
```c
snprintf(err_msg, sizeof(err_msg), "%s: parameter not set", string_cstr(param_name));
```

**Recommendation**: Consider using string_printf() if creating or modifying string_t

**Suggested replacement**: `string_printf()`

---

#### Line 817: sprintf/snprintf usage

**Severity**: Low

**Current code**:
```c
snprintf(err_msg, sizeof(err_msg), "%s: parameter not set", string_cstr(param_name));
```

**Recommendation**: Consider using string_printf() if creating or modifying string_t

**Suggested replacement**: `string_printf()`

---

#### Line 1673: strlen() usage

**Severity**: Medium

**Current code**:
```c
size_t len = strlen(input);
```

**Recommendation**: Consider using string_length() if operating on string_t

**Suggested replacement**: `string_length()`

---

### function_store.c

Total findings in this file: **3**

#### Line 62: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (func && strcmp(func->name, name) == 0) {
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 84: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (func && strcmp(func->name, name) == 0) {
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 97: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (func && strcmp(func->name, name) == 0) {
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

### lexer.c

Total findings in this file: **7**

#### Line 283: strlen() usage

**Severity**: Medium

**Current code**:
```c
Expects_gt(strlen(str), 0);
```

**Recommendation**: Consider using string_length() if operating on string_t

**Suggested replacement**: `string_length()`

---

#### Line 285: strlen() usage

**Severity**: Medium

**Current code**:
```c
int len = strlen(str);
```

**Recommendation**: Consider using string_length() if operating on string_t

**Suggested replacement**: `string_length()`

---

#### Line 288: strncmp() usage

**Severity**: Medium

**Current code**:
```c
return strncmp(&string_data(lx->input)[lx->pos], str, len) == 0;
```

**Recommendation**: Consider using string_starts_with_cstr(), string_ends_with_cstr(), or string_compare_cstr_substring()

**Suggested replacement**: `string_starts_with_cstr(), string_ends_with_cstr(), or string_compare_cstr_substring()`

---

#### Line 296: strlen() usage

**Severity**: Medium

**Current code**:
```c
Expects_gt(strlen(str), 0);
```

**Recommendation**: Consider using string_length() if operating on string_t

**Suggested replacement**: `string_length()`

---

#### Line 298: strlen() usage

**Severity**: Medium

**Current code**:
```c
int len = strlen(str);
```

**Recommendation**: Consider using string_length() if operating on string_t

**Suggested replacement**: `string_length()`

---

#### Line 302: strncmp() usage

**Severity**: Medium

**Current code**:
```c
return (strncmp(input_data, str, len) == 0);
```

**Recommendation**: Consider using string_starts_with_cstr(), string_ends_with_cstr(), or string_compare_cstr_substring()

**Suggested replacement**: `string_starts_with_cstr(), string_ends_with_cstr(), or string_compare_cstr_substring()`

---

#### Line 442: strlen() usage

**Severity**: Medium

**Current code**:
```c
Expects_gt(strlen(str), 0);
```

**Recommendation**: Consider using string_length() if operating on string_t

**Suggested replacement**: `string_length()`

---

### lexer_normal.c

Total findings in this file: **3**

#### Line 42: strlen() usage

**Severity**: Medium

**Current code**:
```c
int len = strlen(op);
```

**Recommendation**: Consider using string_length() if operating on string_t

**Suggested replacement**: `string_length()`

---

#### Line 46: strncmp() usage

**Severity**: Medium

**Current code**:
```c
return (strncmp(&input_data[lx->pos], op, len) == 0);
```

**Recommendation**: Consider using string_starts_with_cstr(), string_ends_with_cstr(), or string_compare_cstr_substring()

**Suggested replacement**: `string_starts_with_cstr(), string_ends_with_cstr(), or string_compare_cstr_substring()`

---

#### Line 70: strlen() usage

**Severity**: Medium

**Current code**:
```c
int len = strlen(normal_mode_operators[(int)type]);
```

**Recommendation**: Consider using string_length() if operating on string_t

**Suggested replacement**: `string_length()`

---

### main.c

Total findings in this file: **1**

#### Line 46: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(line, "exit\n") == 0 || strcmp(line, "exit\r\n") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

### token.c

Total findings in this file: **48**

#### Line 329: strlen() usage

**Severity**: Medium

**Current code**:
```c
Expects_gt(strlen(str), 0);
```

**Recommendation**: Consider using string_length() if operating on string_t

**Suggested replacement**: `string_length()`

---

#### Line 377: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(word, p->word) == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 413: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(word, target_word) == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 648: strcmp() usage

**Severity**: Low

**Current code**:
```c
return (strcmp(word, "if") == 0 || strcmp(word, "then") == 0 || strcmp(word, "else") == 0 ||
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 649: strcmp() usage

**Severity**: Low

**Current code**:
```c
strcmp(word, "elif") == 0 || strcmp(word, "fi") == 0 || strcmp(word, "do") == 0 ||
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 650: strcmp() usage

**Severity**: Low

**Current code**:
```c
strcmp(word, "done") == 0 || strcmp(word, "case") == 0 || strcmp(word, "esac") == 0 ||
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 651: strcmp() usage

**Severity**: Low

**Current code**:
```c
strcmp(word, "while") == 0 || strcmp(word, "until") == 0 || strcmp(word, "for") == 0 ||
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 652: strcmp() usage

**Severity**: Low

**Current code**:
```c
strcmp(word, "in") == 0 || strcmp(word, "{") == 0 || strcmp(word, "}") == 0 ||
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 653: strcmp() usage

**Severity**: Low

**Current code**:
```c
strcmp(word, "!") == 0);
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 660: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(word, "if") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 662: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(word, "then") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 664: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(word, "else") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 666: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(word, "elif") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 668: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(word, "fi") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 670: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(word, "do") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 672: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(word, "done") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 674: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(word, "case") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 676: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(word, "esac") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 678: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(word, "while") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 680: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(word, "until") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 682: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(word, "for") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 684: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(word, "in") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 686: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(word, "{") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 688: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(word, "}") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 690: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(word, "!") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 700: strcmp() usage

**Severity**: Low

**Current code**:
```c
return (strcmp(str, "&&") == 0 || strcmp(str, "||") == 0 || strcmp(str, ";;") == 0 ||
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 701: strcmp() usage

**Severity**: Low

**Current code**:
```c
strcmp(str, "<<") == 0 || strcmp(str, ">>") == 0 || strcmp(str, "<&") == 0 ||
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 702: strcmp() usage

**Severity**: Low

**Current code**:
```c
strcmp(str, ">&") == 0 || strcmp(str, "<>") == 0 || strcmp(str, "<<-") == 0 ||
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 703: strcmp() usage

**Severity**: Low

**Current code**:
```c
strcmp(str, ">|") == 0 || strcmp(str, "|") == 0 || strcmp(str, ";") == 0 ||
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 704: strcmp() usage

**Severity**: Low

**Current code**:
```c
strcmp(str, "&") == 0 || strcmp(str, "(") == 0 || strcmp(str, ")") == 0 ||
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 705: strcmp() usage

**Severity**: Low

**Current code**:
```c
strcmp(str, ">") == 0 || strcmp(str, "<") == 0);
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 712: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(str, "&&") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 714: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(str, "||") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 716: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(str, ";;") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 718: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(str, "<<") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 720: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(str, ">>") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 722: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(str, "<&") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 724: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(str, ">&") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 726: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(str, "<>") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 728: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(str, "<<-") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 730: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(str, ">|") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 732: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(str, "|") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 734: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(str, ";") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 736: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(str, "&") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 738: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(str, "(") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 740: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(str, ")") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 742: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(str, ">") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 744: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(str, "<") == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

### tokenizer.c

Total findings in this file: **3**

#### Line 156: strcmp() usage

**Severity**: Low

**Current code**:
```c
if (strcmp(tok->expanded_aliases[i], alias_name) == 0)
```

**Recommendation**: Consider using string_compare_cstr() or string_eq() if comparing C strings to look up constants

**Suggested replacement**: `string_compare_cstr() or string_eq()`

---

#### Line 186: strlen() usage

**Severity**: Medium

**Current code**:
```c
size_t len = strlen(alias_value);
```

**Recommendation**: Consider using string_length() if operating on string_t

**Suggested replacement**: `string_length()`

---

#### Line 479: strlen() usage

**Severity**: Medium

**Current code**:
```c
if (word_text != NULL && strlen(word_text) > 0)
```

**Recommendation**: Consider using string_length() if operating on string_t

**Suggested replacement**: `string_length()`

---

### variable_store.c

Total findings in this file: **1**

#### Line 31: strchr() usage

**Severity**: Medium

**Current code**:
```c
char *eq = strchr(name, '=');
```

**Recommendation**: Consider using string_find_cstr() with a single-character string if `name` is a string_t. If `name` is a char*, this is the appropriate method to use.

**Suggested replacement**: `string_find_cstr(str, "=")` (if operating on string_t)

---

## Implementation Priority

We recommend addressing these findings in the following priority order:

1. **High Priority** (Security/Safety):
   - `strcpy/strncpy` replacements (buffer overflow risks)
   - `strcat/strncat` replacements (buffer overflow risks)
   - `strstr` replacements (safer searching)

2. **Medium Priority** (Code Quality):
   - `strncmp` replacements (clearer intent with starts_with/ends_with)
   - `strchr/strrchr` replacements (better error handling)
   - `strlen` replacements (when operating on string_t)

3. **Low Priority** (Consistency):
   - `strcmp` replacements (for consistency)
   - `sprintf/snprintf` replacements (when creating string_t objects)
   - Manual empty checks replacements

## Notes

- **Not all findings require action**: Many uses of C string functions in this audit operate on `char*` variables, not `string_t` objects. The recommendations are most relevant when the code is already working with `string_t` types.
- **Manual verification required**: This audit was generated automatically by pattern matching. Each finding must be manually reviewed to verify:
  - The variable types involved (is it actually a `string_t`?)
  - The semantic correctness of the replacement (does the return type change matter?)
  - The performance impact (is conversion worthwhile?)
- **Prioritize actual string_t operations**: Focus first on cases where `string_t` objects are being manipulated using C string functions, as these represent the clearest improvement opportunities.
- **Consider the context**: Some C string operations are appropriate when interfacing with external APIs, parsing C string literals, or working in performance-critical sections where string_t overhead is undesirable.
- The recommendations focus on cases where string_t methods would provide clearer, safer, or more efficient code when working with string_t objects.

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
| `strlen(s)` | `string_length(str)` | Get string length |
| `strcmp(s1, s2)` | `string_compare_cstr(str, cstr)` or `string_eq(str1, str2)` | Compare strings (cstr for C string, eq for string_t) |
| `strncmp(s1, s2, n)` | `string_starts_with_cstr(str, prefix)` | Check prefix |
| `strstr(s1, s2)` | `string_contains_cstr(str, substr)` (bool) or `string_find_cstr(str, substr)` (index) | Find substring - use contains for existence check, find for position |
| `strcpy(dest, src)` | `string_set_cstr(str, cstr)` | Copy string |
| `strcat(dest, src)` | `string_append_cstr(str, cstr)` | Append string |
| `strchr(s, c)` | `string_find_first_of_cstr(str, chars)` | Find first occurrence (returns index, not pointer) |
| `strrchr(s, c)` | `string_find_last_of_cstr(str, chars)` | Find last occurrence (returns index, not pointer) |
| `s[0] == '\0'` or `*s == 0` | `string_empty(str)` | Check if empty |
| `sprintf(buf, fmt, ...)` | `string_printf(str, fmt, ...)` | Formatted string |

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

**Recommendation**: Consider using string_find_first_of_cstr() or string_find_cstr()

**Suggested replacement**: `string_find_first_of_cstr() or string_find_cstr()`

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

- Not all findings require immediate action. Some uses of C string functions are appropriate when working with C string literals or external APIs.
- The recommendations focus on cases where string_t methods would provide clearer, safer, or more efficient code.
- Each recommendation should be evaluated in context to ensure it improves the code.
- The audit was performed automatically and findings should be manually reviewed before implementation.

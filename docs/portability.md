# Portability

## Compiler Support

ISO C is a standardized programming language, and most modern C compilers
support it to a large extent. However, there may be some differences in the
level of support for specific features or versions of the standard.

### `#elifdef` and `#elifndef` Preprocessor Directives

The `#elifdef` preprocessor directive is absolutely part of the ISO C standard,
despite what AI says and what Intellisense might suggest.

Microsoft's CL compiler supports `#elifdef` and `#elifndef` directives starting
in VS 2022 17.10.

GCC has supported `#elifdef` and `#elifndef` directives since at least version
12.1.

Strictly speaking, POSIX's compiler front end is named `c17` and is only required
to support C17, which does not include `#elifdef` and `#elifndef`.

### `constexpr`

The C version of `constexpr` is different from C++'s `constexpr`, and is more
limited and specific in its usage.  In practice, `constexpr` is just
`const` with the additional feature that it can be used in constant expression,
like array sizes.

In ISO C, `constexpr` was introduced in the C23 standard.

Microsoft's CL compiler does not support `constexpr`.

GCC has supported `constexpr` since version 13.1.

### `()` means `(void)`

In ISO C, a function declared with empty parentheses, such as `void func();`,
is treated as a function that takes no arguments.
This differs from previous version of the C standard, such as K&R C, where empty
parentheses did not explicitly indicate that a function takes no arguments.

## Platform Support

###  Environment Variables: `getenv()`, `setenv()`, and `unsetenv()`

POSIX shell requires support for managing environment variables.

In ISO C, the function `getenv()` is defined in `<stdlib.h>`. The string that
is returned points to an internal object that should not be modified or freed
and is valid until the next call to `getenv()`.  Since there is no standard way
to set environment variables in ISO C, the standard does not specify whether the
returned string remains valid after modifying the environment through platform-
specific means.

In POSIX 2024, the function `getenv()` is also defined in `<stdlib.h>`. The
standard specifies that the returned string remains valid until the next call
to `setenv()`, `putenv()`, or `unsetenv()`.

In UCRT, the function `getenv()` is defined in `<stdlib.h>`. The returned
string remains valid until the next call to `setenv()`, `putenv()`, or
`unsetenv()`.

### Environment Variable Length: `ARG_MAX`

POSIX shell requires support for environment variables of at least a certain
length.

In POSIX 2024, the constant `ARG_MAX` is defined in `<limits.h>`. It specifies
the maximum length of the arguments to the `exec` functions, including the
environment variables. The minimum required value for `ARG_MAX` is 4096 bytes.

In UCRT, the constant `_MAX_ARG` is defined in `<stdlib.h>`. It specifies the
maximum length for environmental strings.

### Process IDs: `getpid()`, `pid_t`

POSIX shell requires support for getting the Process ID of the current
process.

In ISO C, there is no standard function to get the Process ID.

In POSIX 2024, the function `getpid()` is defined in `<unistd.h>`. It return
the Process ID of the calling process as a value of type `pid_t`. The standard
notes that a `pid_t` is a signed integer and is defined in `<sys/types.h>`.
However, it does not specify the exact size of range of `pid_t`

In UCRT, the equivalent function id `_getpid()`, which is defined in
`<process.h>`.  It returns the Process ID obtained from the operating system
as an `int`.


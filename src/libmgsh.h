#ifndef LIBMGSH_H
#define LIBMGSH_H

// The public API for the execution engine library is defined in exec.h and frame.h.
// There are also some common internal types.
// This header includes those headers to provide a single inclusion point for
// users of the execution engine.

#ifndef MGSH_API
#error "To include 'libmgsh.h', must define MGSH_API to one of MGSH_POSIX, MGSH_UCRT, or MGSH_ISO_C"
#else
#if MGSH_API == MGSH_POSIX
#define POSIX_API
#undef UCRT_API
#elif MGSH_API == MGSH_UCRT
#define UCRT_API
#undef POSIX_API
#elif MGSH_API == MGSH_ISO_C
#undef POSIX_API
#undef UCRT_API
#endif

#define MGSH_PUBLIC_API

#include "string.h"
#include "string_list.h"
#include "getopt.h"
#include "getopt_string.h"
#include "exec.h"
#include "frame.h"

#endif /* LIBMGSH_H */

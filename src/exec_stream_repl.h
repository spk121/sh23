#ifndef EXEC_STREAM_REPL_H
#define EXEC_STREAM_REPL_H

#include "exec_internal.h"

/* Reads a single physical line from @p fp, hands it to exec_string_core(),
 * and maps the result to a frame_exec_status_t.
 *
 * On FRAME_EXEC_INCOMPLETE the ctx is *preserved* — the caller must keep
 * it alive and pass it back on the next call so that lexer state (unclosed
 * quotes, heredocs) and accumulated tokens survive.
 *
 * On any terminal status (OK, ERROR, EMPTY, NOT_IMPL) the ctx is reset
 * (lexer cleared, accumulated tokens freed) so it is ready for the next
 * independent command.  The caller may destroy the ctx at that point or
 * reuse it.
 *
 * @param frame      Current execution frame
 * @param fp         Input stream (one physical line is consumed per call)
 * @param tokenizer  Persistent tokenizer (carries compound-command state)
 * @param ctx        Caller-owned execution context (lexer + accumulated tokens)
 * @return FRAME_EXEC_OK, FRAME_EXEC_ERROR, or FRAME_EXEC_INCOMPLETE
 */
frame_exec_status_t exec_stream_core_ex(exec_frame_t* frame, FILE* fp,
    tokenizer_t* tokenizer,
    exec_string_ctx_t* ctx);

/* Creates a transient exec_string_ctx_t, delegates to exec_stream_core_ex(),
 * and always destroys the ctx before returning.  INCOMPLETE is mapped to OK
 * so existing callers (exec_execute_stream, frame_execute_stream) continue
 * to work without change.
 */
frame_exec_status_t exec_stream_core(exec_frame_t* frame, FILE* fp,
    tokenizer_t* tokenizer);

/* Repeatedly reads lines from @p fp, parses, and executes them.  When
 * @p interactive is true the function prints PS1/PS2 prompts and handles
 * ignoreeof and SIGINT gracefully.
 *
 * The key difference from exec_execute_stream() is that an
 * exec_string_ctx_t is kept alive across INCOMPLETE lines, so lexer state
 * (unclosed quotes, heredoc bodies, line-continuation backslashes) and
 * accumulated parser tokens survive between calls to exec_stream_core_ex().
 *
 * After every completed command the function:
 *   - Reaps background jobs (printing status in interactive mode).
 *   - Runs pending trap handlers, preserving $? per POSIX.
 *
 * @param executor    The executor (must already be created)
 * @param fp          Input stream (typically stdin)
 * @param interactive If true, print prompts and tolerate ignoreeof/SIGINT
 * @return EXEC_OK on clean EOF, EXEC_ERROR on fatal error,
 *         EXEC_EXIT if the `exit` builtin was executed
 */
exec_status_t exec_execute_stream_repl(exec_t* executor, FILE* fp,
    bool interactive);

#endif /* EXEC_STREAM_REPL_H */
//
// exec_stream_repl.c — Refactored stream core + REPL loop
//

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "exec_stream_repl.h"
#include "exec.h"
#include "frame.h"
#include "tokenizer.h"
#include "xalloc.h"
#include "string_t.h"
#include "logging.h"



frame_exec_status_t exec_stream_core_ex(exec_frame_t* frame, FILE* fp,
    tokenizer_t* tokenizer,
    exec_string_ctx_t* ctx)
{
    Expects_not_null(frame);
    Expects_not_null(fp);
    Expects_not_null(tokenizer);
    Expects_not_null(ctx);
    Expects_not_null(ctx->lexer);
    Expects_not_null(frame->executor);

    frame_exec_status_t final_status = FRAME_EXEC_OK;

    /* Read a single logical line of any size, efficiently */
#define LINE_CHUNK_SIZE 4096
    char chunk[LINE_CHUNK_SIZE];
    char* line_buf = NULL;
    size_t line_buf_size = 0;
    size_t line_len = 0;

    while (fgets(chunk, sizeof(chunk), fp) != NULL)
    {
        size_t chunk_len = strlen(chunk);

        if (chunk_len > 0 && chunk[chunk_len - 1] == '\n')
        {
            if (line_len == 0)
            {
                /* Fast path: entire line fits in chunk */
                exec_string_status_t status =
                    exec_string_core(frame, chunk, tokenizer, ctx);
                if (ctx->line_num > 0)
                    frame->source_line = ctx->line_num;

                switch (status)
                {
                case EXEC_STRING_OK:
                case EXEC_STRING_EMPTY:
                    final_status = FRAME_EXEC_OK;
                    break;
                case EXEC_STRING_INCOMPLETE:
                    final_status = FRAME_EXEC_INCOMPLETE;
                    break;
                case EXEC_STRING_ERROR:
                    final_status = FRAME_EXEC_ERROR;
                    break;
                }
                return final_status;
            }
            else
            {
                /* Append final chunk to accumulated buffer */
                if (line_len + chunk_len + 1 > line_buf_size)
                {
                    line_buf_size = (line_len + chunk_len + 1) * 2;
                    line_buf = (char*)xrealloc(line_buf, line_buf_size);
                }
                memcpy(line_buf + line_len, chunk, chunk_len);
                line_len += chunk_len;
                line_buf[line_len] = '\0';
                break; /* Line complete — fall through to process it */
            }
        }
        else
        {
            /* No newline yet — accumulate */
            if (line_buf == NULL)
            {
                line_buf_size = (chunk_len + 1) * 2;
                line_buf = (char*)xmalloc(line_buf_size);
                memcpy(line_buf, chunk, chunk_len);
                line_len = chunk_len;
                line_buf[line_len] = '\0';
            }
            else
            {
                if (line_len + chunk_len + 1 > line_buf_size)
                {
                    line_buf_size = (line_len + chunk_len + 1) * 2;
                    line_buf = (char*)xrealloc(line_buf, line_buf_size);
                }
                memcpy(line_buf + line_len, chunk, chunk_len);
                line_len += chunk_len;
                line_buf[line_len] = '\0';
            }
        }
    }

    /* Process whatever we accumulated (may be empty on pure EOF) */
    if (line_len > 0)
    {
        exec_string_status_t status =
            exec_string_core(frame, line_buf, tokenizer, ctx);
        if (ctx->line_num > 0)
            frame->source_line = ctx->line_num;

        switch (status)
        {
        case EXEC_STRING_OK:
        case EXEC_STRING_EMPTY:
            final_status = FRAME_EXEC_OK;
            break;
        case EXEC_STRING_INCOMPLETE:
            final_status = FRAME_EXEC_INCOMPLETE;
            break;
        case EXEC_STRING_ERROR:
            final_status = FRAME_EXEC_ERROR;
            break;
        }
    }

    if (line_buf)
        xfree(line_buf);

    return final_status;
}

frame_exec_status_t exec_stream_core(exec_frame_t* frame, FILE* fp,
    tokenizer_t* tokenizer)
{
    Expects_not_null(frame);
    Expects_not_null(fp);
    Expects_not_null(tokenizer);
    Expects_not_null(frame->executor);

    exec_string_ctx_t* ctx = exec_string_ctx_create();
    if (!ctx || !ctx->lexer)
    {
        frame_set_error_printf(frame, "Failed to create execution context");
        if (ctx)
            exec_string_ctx_destroy(&ctx);
        return FRAME_EXEC_ERROR;
    }

    frame_exec_status_t status = exec_stream_core_ex(frame, fp, tokenizer, ctx);

    exec_string_ctx_destroy(&ctx);

    /* Collapse INCOMPLETE → OK for callers that don't care */
    if (status == FRAME_EXEC_INCOMPLETE)
        status = FRAME_EXEC_OK;

    return status;
}

exec_status_t exec_execute_stream_repl(exec_t* executor, FILE* fp,
    bool interactive)
{
    Expects_not_null(executor);
    Expects_not_null(fp);

    /* ------------------------------------------------------------------
     * Ensure the frame stack is initialized
     * ------------------------------------------------------------------ */
    if (!executor->top_frame_initialized)
    {
        executor->top_frame = exec_frame_create_top_level(executor);
        executor->top_frame_initialized = true;
    }
    if (!executor->current_frame)
    {
        executor->current_frame = executor->top_frame;
    }

    /* ------------------------------------------------------------------
     * Create / reuse the persistent tokenizer
     * ------------------------------------------------------------------ */
    if (!executor->tokenizer)
    {
        executor->tokenizer = tokenizer_create(executor->aliases);
        if (!executor->tokenizer)
        {
            exec_set_error(executor, "Failed to create tokenizer");
            return EXEC_ERROR;
        }
    }

    /* ------------------------------------------------------------------
     * Create the persistent string-execution context.
     * This is what survives across INCOMPLETE lines — it holds the lexer
     * (with its quote/heredoc state) and any accumulated tokens from
     * partial parses.
     * ------------------------------------------------------------------ */
    exec_string_ctx_t* ctx = exec_string_ctx_create();
    if (!ctx || !ctx->lexer)
    {
        if (ctx)
            exec_string_ctx_destroy(&ctx);
        exec_set_error(executor, "Failed to create execution context");
        return EXEC_ERROR;
    }

    /* ------------------------------------------------------------------
     * REPL state
     * ------------------------------------------------------------------ */
#define IGNOREEOF_MAX 10
    int consecutive_eof = 0;
    bool need_continuation = false;
    exec_status_t final_result = EXEC_OK;

    /* ------------------------------------------------------------------
     * Main loop — one physical line per iteration
     * ------------------------------------------------------------------ */
    for (;;)
    {
        /* ---- 1. Prompt ---- */
        if (interactive)
        {
            if (need_continuation)
            {
                const char* ps2 = exec_get_ps2(executor);
                fprintf(stderr, "%s", ps2);
            }
            else
            {
                char* ps1 = frame_render_ps1(executor->current_frame);
                fprintf(stderr, "%s", ps1 ? ps1 : "$ ");
                xfree(ps1);
            }
            fflush(stderr);
        }

        /* ---- 2. Read & execute one line ---- */
        frame_exec_status_t line_status = exec_stream_core_ex(
            executor->current_frame,
            fp,
            executor->tokenizer,
            ctx);

        /* ---- 3. EOF handling ---- */
        if (feof(fp))
        {
            if (need_continuation)
            {
                /*
                 * EOF inside a multi-line construct.  POSIX XCU 2.3:
                 * "If the shell is not interactive, the shell need not
                 *  perform this check." Either way, an unterminated
                 *  construct is a syntax error.
                 */
                if (interactive)
                    fprintf(stderr, "\n%s: syntax error: unexpected end of file\n",
                        string_cstr(executor->shell_name));
                final_result = EXEC_ERROR;
                break;
            }

            if (interactive && executor->opt.ignoreeof)
            {
                consecutive_eof++;
                if (consecutive_eof < IGNOREEOF_MAX)
                {
                    int remaining = IGNOREEOF_MAX - consecutive_eof;
                    fprintf(stderr,
                        "Use \"exit\" to leave the shell "
                        "(or press Ctrl-D %d more time%s).\n",
                        remaining, remaining == 1 ? "" : "s");
                    clearerr(fp);
                    continue;
                }
                /* Too many consecutive EOFs — fall through to exit */
            }

            /* Clean EOF */
            final_result = EXEC_OK;
            break;
        }

        /* Got input — reset consecutive-EOF counter */
        consecutive_eof = 0;

        /* ---- 4. Dispatch on line status ---- */
        if (line_status == FRAME_EXEC_INCOMPLETE)
        {
            /*
             * The lexer or parser needs more input.  The ctx holds the
             * incomplete lexer state and/or accumulated tokens; the
             * tokenizer may also be buffering compound-command tokens.
             * Loop back to read the next line (prompting with PS2).
             */
            need_continuation = true;
            continue;
        }

        /* Command complete (OK, EMPTY, or ERROR) — reset continuation */
        need_continuation = false;

        /*
         * The ctx was reset internally by exec_string_core on success
         * (exec_string_ctx_reset clears the lexer and accumulated
         * tokens).  On error the ctx may have stale state, so reset
         * it explicitly to be safe.
         */
        if (line_status == FRAME_EXEC_ERROR)
        {
            exec_string_ctx_reset(ctx);

            if (interactive)
            {
                const char* err = exec_get_error(executor);
                if (err)
                {
                    fprintf(stderr, "%s: %s\n",
                        string_cstr(executor->shell_name), err);
                    exec_clear_error(executor);
                }
                /* Interactive shells keep going after errors */
            }
            else
            {
                /* Non-interactive + errexit: abort on non-zero status */
                if (executor->opt.errexit && executor->last_exit_status != 0)
                {
                    final_result = EXEC_ERROR;
                    break;
                }
            }
        }

        /* ---- 5. Check for exit / top-level return ---- */
        if (executor->current_frame &&
            executor->current_frame->pending_control_flow == EXEC_FLOW_RETURN &&
            executor->current_frame == executor->top_frame)
        {
            /* A top-level 'return' is equivalent to 'exit' */
            final_result = EXEC_EXIT;
            break;
        }

        /* ---- 6. Reap background jobs ---- */
        exec_reap_background_jobs(executor, interactive);

        /* ---- 7. Process pending traps ---- */
        for (int signo = 1; signo < NSIG; signo++)
        {
            if (!executor->trap_pending[signo])
                continue;

            executor->trap_pending[signo] = 0;

            const trap_action_t* trap_action =
                trap_store_get(executor->traps, signo);
            if (!trap_action || !trap_action->action)
                continue;

            /* POSIX: $? is preserved across trap execution */
            int saved_exit_status = executor->last_exit_status;

            exec_frame_t* trap_frame = exec_frame_push(
                executor->current_frame,
                EXEC_FRAME_TRAP,
                executor,
                NULL);

            exec_result_t trap_result = exec_command_string(
                trap_frame,
                string_cstr(trap_action->action));

            exec_frame_pop(&executor->current_frame);

            executor->last_exit_status = saved_exit_status;

            if (trap_result.status == EXEC_EXIT)
            {
                final_result = EXEC_EXIT;
                goto done;
            }

            if (trap_result.status == EXEC_ERROR && interactive)
            {
                const char* err = exec_get_error(executor);
                if (err)
                {
                    fprintf(stderr, "%s: trap handler: %s\n",
                        string_cstr(executor->shell_name), err);
                    exec_clear_error(executor);
                }
            }
        }

        /* ---- 8. SIGINT handling ---- */
        if (executor->sigint_received)
        {
            executor->sigint_received = 0;

            if (interactive)
            {
                fprintf(stderr, "\n");
                need_continuation = false;

                /*
                 * Discard all partial state: reset the ctx (lexer +
                 * accumulated tokens) and recreate the tokenizer so
                 * any buffered compound-command tokens are flushed.
                 */
                exec_string_ctx_reset(ctx);
                tokenizer_destroy(&executor->tokenizer);
                executor->tokenizer = tokenizer_create(executor->aliases);
            }
            else
            {
                executor->last_exit_status = 128 + SIGINT;
                final_result = EXEC_ERROR;
                break;
            }
        }
    } /* for (;;) */

done:
    exec_string_ctx_destroy(&ctx);
    return final_result;
}


//
// REPL loop for a specific frame (not the global executor)
// Returns FRAME_EXEC_OK on clean EOF, FRAME_EXEC_ERROR on fatal error, FRAME_EXEC_NOT_IMPL if not implemented
frame_exec_status_t exec_stream_repl(exec_frame_t* frame, FILE* fp, bool interactive)
{
    Expects_not_null(frame);
    Expects_not_null(fp);
    exec_t* executor = frame->executor;
    Expects_not_null(executor);

    // Use or create a persistent tokenizer for this frame
    if (!executor->tokenizer)
    {
        executor->tokenizer = tokenizer_create(executor->aliases);
        if (!executor->tokenizer)
        {
            exec_set_error(executor, "Failed to create tokenizer");
            return FRAME_EXEC_ERROR;
        }
    }

    exec_string_ctx_t* ctx = exec_string_ctx_create();
    if (!ctx || !ctx->lexer)
    {
        if (ctx)
            exec_string_ctx_destroy(&ctx);
        exec_set_error(executor, "Failed to create execution context");
        return FRAME_EXEC_ERROR;
    }

#define IGNOREEOF_MAX 10
    int consecutive_eof = 0;
    bool need_continuation = false;
    frame_exec_status_t final_status = FRAME_EXEC_OK;

    for (;;)
    {
        // 1. Prompt
        if (interactive)
        {
            if (need_continuation)
            {
                const char* ps2 = exec_get_ps2(executor);
                fprintf(stderr, "%s", ps2);
            }
            else
            {
                char* ps1 = frame_render_ps1(frame);
                fprintf(stderr, "%s", ps1 ? ps1 : "$ ");
                xfree(ps1);
            }
            fflush(stderr);
        }

        // 2. Read & execute one line
        frame_exec_status_t line_status = exec_stream_core_ex(
            frame,
            fp,
            executor->tokenizer,
            ctx);

        // 3. EOF handling
        if (feof(fp))
        {
            if (need_continuation)
            {
                if (interactive)
                    fprintf(stderr, "\n%s: syntax error: unexpected end of file\n",
                        string_cstr(executor->shell_name));
                final_status = FRAME_EXEC_ERROR;
                break;
            }

            if (interactive && executor->opt.ignoreeof)
            {
                consecutive_eof++;
                if (consecutive_eof < IGNOREEOF_MAX)
                {
                    int remaining = IGNOREEOF_MAX - consecutive_eof;
                    fprintf(stderr,
                        "Use \"exit\" to leave the shell (or press Ctrl-D %d more time%s).\n",
                        remaining, remaining == 1 ? "" : "s");
                    clearerr(fp);
                    continue;
                }
            }
            final_status = FRAME_EXEC_OK;
            break;
        }
        consecutive_eof = 0;

        // 4. Dispatch on line status
        if (line_status == FRAME_EXEC_INCOMPLETE)
        {
            need_continuation = true;
            continue;
        }
        need_continuation = false;

        if (line_status == FRAME_EXEC_ERROR)
        {
            exec_string_ctx_reset(ctx);
            if (interactive)
            {
                const char* err = exec_get_error(executor);
                if (err)
                {
                    fprintf(stderr, "%s: %s\n",
                        string_cstr(executor->shell_name), err);
                    exec_clear_error(executor);
                }
            }
            else if (executor->opt.errexit && executor->last_exit_status != 0)
            {
                final_status = FRAME_EXEC_ERROR;
                break;
            }
        }

        // 5. Check for exit / top-level return
        if (frame->pending_control_flow == EXEC_FLOW_RETURN && frame == executor->top_frame)
        {
            final_status = FRAME_EXEC_NOT_IMPL; // or FRAME_EXEC_OK/EXIT as appropriate
            break;
        }

        // 6. Reap background jobs
        exec_reap_background_jobs(executor, interactive);

        // 7. Process pending traps
        for (int signo = 1; signo < NSIG; signo++)
        {
            if (!executor->trap_pending[signo])
                continue;
            executor->trap_pending[signo] = 0;
            const trap_action_t* trap_action = trap_store_get(executor->traps, signo);
            if (!trap_action || !trap_action->action)
                continue;
            int saved_exit_status = executor->last_exit_status;
            exec_frame_t* trap_frame = exec_frame_push(
                frame,
                EXEC_FRAME_TRAP,
                executor,
                NULL);
            exec_result_t trap_result = exec_command_string(
                trap_frame,
                string_cstr(trap_action->action));
            exec_frame_pop(&frame);
            executor->last_exit_status = saved_exit_status;
            if (trap_result.status == EXEC_ERROR && interactive)
            {
                const char* err = exec_get_error(executor);
                if (err)
                {
                    fprintf(stderr, "%s: trap handler: %s\n",
                        string_cstr(executor->shell_name), err);
                    exec_clear_error(executor);
                }
            }
        }

        // 8. SIGINT handling
        if (executor->sigint_received)
        {
            executor->sigint_received = 0;
            if (interactive)
            {
                fprintf(stderr, "\n");
                need_continuation = false;
                exec_string_ctx_reset(ctx);
                tokenizer_destroy(&executor->tokenizer);
                executor->tokenizer = tokenizer_create(executor->aliases);
            }
            else
            {
                executor->last_exit_status = 128 + SIGINT;
                final_status = FRAME_EXEC_ERROR;
                break;
            }
        }
    }
    exec_string_ctx_destroy(&ctx);
    return final_status;
}
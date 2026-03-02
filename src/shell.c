#include <stdio.h>
#include <string.h>

#include "shell.h"

#include "exec.h"
#include "frame.h"
#include "logging.h"
#include "positional_params.h"
#include "string_t.h"
#include "xalloc.h"

struct shell_t
{
    shell_mode_t mode;         // Interactive, non-interactive, etc.
    string_t *script_filename; // Script file name (if any)
    string_t *command_string;  // Command string (if any)
    exec_t *executor;          // Points to the currently executing exec_t
};

static sh_status_t shell_execute_script_file(shell_t *sh);
static sh_status_t shell_execute_interactive(shell_t *sh);
static sh_status_t shell_execute_command_string(shell_t *sh);
static sh_status_t shell_execute_stdin(shell_t *sh);
static FILE *open_mem_stream(const char *buf, size_t len);

shell_t *shell_create(const shell_cfg_t *cfg)
{
    Expects_not_null(cfg);

    shell_t *sh = xcalloc(1, sizeof(shell_t));
    if (!sh)
        return NULL;

    sh->mode = cfg->mode;

    /* Initialize script filename if provided */
    if (cfg->command_file)
    {
        sh->script_filename = string_create_from_cstr(cfg->command_file);
    }
    else
    {
        sh->script_filename = NULL;
    }

    /* Initialize command string if provided */
    if (cfg->command_string)
    {
        sh->command_string = string_create_from_cstr(cfg->command_string);
    }
    else
    {
        sh->command_string = NULL;
    }

    sh->executor = exec_create(&cfg->exec_cfg);
    if (!sh->executor)
    {
        if (sh->script_filename)
            string_destroy(&sh->script_filename);
        if (sh->command_string)
            string_destroy(&sh->command_string);
        xfree(sh);
        return NULL;
    }
    return sh;
}

void shell_destroy(shell_t **sh)
{
    Expects_not_null(sh);
    if (!*sh)
        return;

    /* Clean up string fields */
    if ((*sh)->command_string)
    {
        string_destroy(&(*sh)->command_string);
    }
    if ((*sh)->script_filename)
    {
        string_destroy(&(*sh)->script_filename);
    }

    exec_destroy(&(*sh)->executor);
    xfree(*sh);
    *sh = NULL;
}

// Mode dispatch: Handle high-level logic, delegate executor calls
sh_status_t shell_execute(shell_t *sh)
{
    Expects_not_null(sh);
    switch (sh->mode)
    {
    case SHELL_MODE_SCRIPT_FILE:
        if (!sh->script_filename)
        {
            exec_set_error(sh->executor, "No script file specified");
            return SH_INTERNAL_ERROR;
        }
        return shell_execute_script_file(sh);
    case SHELL_MODE_INTERACTIVE:
        exec_setup_interactive_execute(sh->executor);
        return shell_execute_interactive(sh);
    case SHELL_MODE_COMMAND_STRING:
        if (!sh->command_string)
        {
            exec_set_error(sh->executor, "No command string specified");
            return SH_INTERNAL_ERROR;
        }
        return shell_execute_command_string(sh);
    case SHELL_MODE_STDIN:
        exec_setup_interactive_execute(sh->executor);
        return shell_execute_stdin(sh);
    case SHELL_MODE_UNKNOWN:
    case SHELL_MODE_INVALID_UID_GID:
    default:
        exec_set_error(sh->executor, "Invalid shell mode");
        return SH_INTERNAL_ERROR;
    }
}

void shell_cleanup(void *sh_ptr)
{
    if (!sh_ptr)
        return;
    shell_t *sh = (shell_t *)sh_ptr;

    if (sh->command_string)
    {
        string_destroy(&sh->command_string);
    }
    if (sh->script_filename)
    {
        string_destroy(&sh->script_filename);
    }

    // Clean up the executor
    exec_destroy(&sh->executor);

    // Free the shell structure
    // Note: We don't null out sh_ptr since it's owned by the arena
    xfree(sh);
}

// TODO: Implement these functions for REPL and script execution
sh_status_t shell_feed_line(shell_t *sh, const char *line, int line_num)
{
    Expects_not_null(sh);
    Expects_not_null(line);
    (void)line_num; // unused for now

    // TODO: Implement lexer -> parser -> executor pipeline
    // For now, just return OK
    return SH_OK;
}

static sh_status_t shell_execute_script_file(shell_t *sh)
{
    Expects_not_null(sh);
    Expects_not_null(sh->script_filename);

    FILE *fp = fopen(string_cstr(sh->script_filename), "r");
    if (!fp)
    {
        exec_set_error(sh->executor, "Cannot open file: %s", string_cstr(sh->script_filename));
        return SH_RUNTIME_ERROR;
    }

    /* Set $0 to the script filename */
    exec_frame_t *frame = sh->executor->current_frame;
    if (frame && frame->positional_params)
    {
        positional_params_set_arg0(frame->positional_params, sh->script_filename);
    }

    exec_status_t status = exec_execute_stream(sh->executor, fp);
    fclose(fp);

    // Convert exec_status_t to sh_status_t
    switch (status)
    {
    case EXEC_OK:
        return SH_OK;
    case EXEC_ERROR:
        return SH_RUNTIME_ERROR;
    case EXEC_NOT_IMPL:
    case EXEC_OK_INTERNAL_FUNCTION_STORED:
    default:
        return SH_INTERNAL_ERROR;
    }
}

sh_status_t shell_execute_command_string(shell_t *sh)
{
    Expects_not_null(sh);
    Expects_not_null(sh->command_string);

    const char *cmd = string_cstr(sh->command_string);
    size_t len = string_length(sh->command_string);

    FILE *mem = open_mem_stream(cmd, len);
    if (!mem)
    {
        exec_set_error(sh->executor, "Failed to create memory stream for command string");
        return SH_RUNTIME_ERROR;
    }

    exec_status_t status = exec_execute_stream(sh->executor, mem);
    fclose(mem);

    switch (status)
    {
    case EXEC_OK:
        return SH_OK;
    case EXEC_ERROR:
        return SH_RUNTIME_ERROR;
    case EXEC_NOT_IMPL:
    case EXEC_OK_INTERNAL_FUNCTION_STORED:
    default:
        return SH_INTERNAL_ERROR;
    }
}

/**
 * Open a read-only memory-backed FILE* over a buffer.
 * On POSIX, uses fmemopen.  On other platforms, falls back to tmpfile().
 * Returns NULL on failure.  Caller must fclose() the result.
 */
static FILE *open_mem_stream(const char *buf, size_t len)
{
#ifdef POSIX_API
    return fmemopen((void *)buf, len, "r");
#else
    /* Portable fallback: write to a tmpfile and rewind.
     * This is only used on non-POSIX platforms where fmemopen is absent. */
    FILE *tmp = tmpfile();
    if (!tmp)
        return NULL;
    if (fwrite(buf, 1, len, tmp) != len)
    {
        fclose(tmp);
        return NULL;
    }
    rewind(tmp);
    return tmp;
#endif
}

/**
 * Read a single line from stdin into a caller-provided buffer, with dynamic
 * fallback for lines that exceed the buffer.
 *
 * @param static_buf   Caller's stack buffer
 * @param static_size  Size of the stack buffer
 * @param out_buf      Receives a pointer to the line (either static_buf or a
 *                     heap allocation that the caller must free)
 * @param out_len      Receives the length of the line (excluding NUL)
 * @return true on success, false on EOF or error
 */
static bool read_line(char *static_buf, size_t static_size, char **out_buf, size_t *out_len)
{
    if (!fgets(static_buf, (int)static_size, stdin))
        return false;

    size_t len = strlen(static_buf);

    /* If the line fits (terminated by newline or EOF hit before buffer full),
     * we're done — no allocation needed. */
    if (len < static_size - 1 || static_buf[len - 1] == '\n')
    {
        *out_buf = static_buf;
        *out_len = len;
        return true;
    }

    /* Line was truncated — switch to dynamic accumulation. */
    size_t cap = static_size * 2;
    char *dyn = xmalloc(cap);
    memcpy(dyn, static_buf, len);

    for (;;)
    {
        if (len + static_size > cap)
        {
            cap *= 2;
            dyn = xrealloc(dyn, cap);
        }

        if (!fgets(dyn + len, (int)(cap - len), stdin))
            break; /* EOF mid-line; return what we have */

        len += strlen(dyn + len);

        if (dyn[len - 1] == '\n')
            break; /* got a full line */
    }

    *out_buf = dyn;
    *out_len = len;
    return true;
}

static sh_status_t shell_execute_interactive(shell_t *sh)
{
    Expects_not_null(sh);
    extern char *frame_render_ps1(const exec_frame_t *frame);

    char *ps1_prompt = frame_render_ps1(sh->executor->current_frame);
    if (!ps1_prompt)
        ps1_prompt = xstrdup("$ ");

    fprintf(stdout, "%s", ps1_prompt);
    fflush(stdout);

    exec_reap_background_jobs(sh->executor, true);

    /* Static buffer handles the common case (lines < 4 KB) without any
     * heap allocation.  read_line() falls back to malloc for longer input. */
    char line_buf[4096];
    char *line = NULL;
    size_t line_len = 0;

    while (read_line(line_buf, sizeof(line_buf), &line, &line_len))
    {
        /* Feed the line to the executor via a memory-backed FILE*. */
        FILE *mem = open_mem_stream(line, line_len);
        if (!mem)
        {
            fprintf(stderr, "Failed to create memory stream for command\n");
        }
        else
        {
            exec_execute_stream(sh->executor, mem);
            fclose(mem);
        }

        /* Free the line only if it was dynamically allocated (not the
         * static stack buffer). */
        if (line != line_buf)
            xfree(line);

        /* Report errors */
        const char *error = exec_get_error(sh->executor);
        if (error && error[0])
        {
            fprintf(stderr, "%s\n", error);
            exec_clear_error(sh->executor);
        }
        else if (sh->executor && sh->executor->last_exit_status == 127)
        {
            fprintf(stderr, "command not found\n");
            exec_clear_error(sh->executor);
        }

        /* Reap background jobs */
        exec_reap_background_jobs(sh->executor, true);

        /* Print next prompt */
        xfree(ps1_prompt);
        ps1_prompt = frame_render_ps1(sh->executor->current_frame);
        if (!ps1_prompt)
            ps1_prompt = xstrdup("$ ");
        fprintf(stdout, "%s", ps1_prompt);
        fflush(stdout);
    }

    xfree(ps1_prompt);
    return SH_OK;
}

sh_status_t shell_execute_stdin(shell_t *sh)
{
    Expects_not_null(sh);
    exec_status_t status = exec_execute_stream(sh->executor, stdin);
    // Convert exec_status_t to sh_status_t
    switch (status)
    {
    case EXEC_OK:
        return SH_OK;
    case EXEC_ERROR:
        return SH_RUNTIME_ERROR;
    case EXEC_NOT_IMPL:
    case EXEC_OK_INTERNAL_FUNCTION_STORED:
    default:
        return SH_INTERNAL_ERROR;
    }
}

const char *shell_last_error(shell_t *sh)
{
    Expects_not_null(sh);
    Expects_not_null(sh->executor);

    return exec_get_error(sh->executor);
}

void shell_reset_error(shell_t *sh)
{
    Expects_not_null(sh);
    Expects_not_null(sh->executor);

    exec_clear_error(sh->executor);
}

sh_status_t shell_run_script(shell_t *sh, const char *script)
{
    Expects_not_null(sh);
    Expects_not_null(script);

    // TODO: Implement script execution
    // Parse entire script buffer and execute
    return SH_OK;
}

const char *shell_get_ps1(const shell_t *sh)
{
    Expects_not_null(sh);
    Expects_not_null(sh->executor);

    return exec_get_ps1(sh->executor);
}

const char *shell_get_ps2(const shell_t *sh)
{
    Expects_not_null(sh);
    Expects_not_null(sh->executor);

    return exec_get_ps2(sh->executor);
}

exec_t *shell_get_exec(shell_t *sh)
{
    Expects_not_null(sh);
    return sh->executor;
}

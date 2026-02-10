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
    shell_mode_t mode;    // Interactive, non-interactive, etc.
    string_t *script_filename; // Script file name (if any)
    string_t *command_string; // Command string (if any)
    exec_t *executor; // Points to the currently executing exec_t
};

static sh_status_t shell_execute_script_file(shell_t *sh);
static sh_status_t shell_execute_interactive(shell_t *sh);
static sh_status_t shell_execute_command_string(shell_t *sh);
static sh_status_t shell_execute_stdin(shell_t *sh);

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
sh_status_t shell_execute(shell_t* sh)
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
    (void)line_num;  // unused for now

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
    // Write command string to a temp file and execute it
    FILE *tmp = tmpfile();
    if (!tmp)
    {
        exec_set_error(sh->executor, "Failed to create temp file for command string");
        return SH_RUNTIME_ERROR;
    }
    size_t len = string_length(sh->command_string);
    if (fwrite(string_cstr(sh->command_string), 1, len, tmp) != len)
    {
        fclose(tmp);
        exec_set_error(sh->executor, "Failed to write command string to temp file");
        return SH_RUNTIME_ERROR;
    }
    rewind(tmp);
    exec_status_t status = exec_execute_stream(sh->executor, tmp);
    fclose(tmp);
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

extern void exec_reap_background_jobs(exec_t *executor, bool notify);

static sh_status_t shell_execute_interactive(shell_t *sh)
{
    Expects_not_null(sh);

    const char *ps1 = shell_get_ps1(sh);
    string_t *ps1_str = string_create_from_cstr(ps1);
    string_t *expanded_ps1_str = expand_string(sh->executor->current_frame,
        ps1_str, EXPAND_ALL);
    if (expanded_ps1_str)
    {
        ps1 = string_cstr(expanded_ps1_str);
    }
    string_destroy(&ps1_str);

    // Simple REPL: read line, execute, repeat
    // For now we execute each complete line - no PS2 multi-line support yet
    char line_buffer[4096];

    fprintf(stdout, "%s", ps1);
    fflush(stdout);

    exec_reap_background_jobs(sh->executor, true);
    while (fgets(line_buffer, sizeof(line_buffer), stdin) != NULL)
    {
        // Check for exit command
        if (strcmp(line_buffer, "exit\n") == 0 || strcmp(line_buffer, "exit\r\n") == 0)
        {
            break;
        }

        // Write line to a temp file and execute it
        FILE *tmp = tmpfile();
        if (!tmp)
        {
            fprintf(stderr, "Failed to create temp file for command\n");
            fprintf(stdout, "%s", ps1);
            fflush(stdout);
            continue;
        }

        size_t len = strlen(line_buffer);
        if (fwrite(line_buffer, 1, len, tmp) != len)
        {
            fclose(tmp);
            fprintf(stderr, "Failed to write command to temp file\n");
            fprintf(stdout, "%s", ps1);
            fflush(stdout);
            continue;
        }

        rewind(tmp);

        // Execute the command
        exec_status_t status = exec_execute_stream(sh->executor, tmp);
        fclose(tmp);

        // Print error if any
        if (status != EXEC_OK)
        {
            const char *error = exec_get_error(sh->executor);
            if (error && error[0])
            {
                fprintf(stderr, "%s\n", error);
            }
        }

        // Check to see of any background jobs have terminated
        exec_reap_background_jobs(sh->executor, true);

        // Print next prompt
        fprintf(stdout, "%s", ps1);
        fflush(stdout);
    }

    string_destroy(&expanded_ps1_str);
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

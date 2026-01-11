#include "shell.h"
#include "logging.h"
#include "xalloc.h"
#include "exec.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct shell_t
{
    exec_t *root_exec;
    exec_t *current_exec; // Points to the currently executing exec_t
    shell_cfg_t cfg; // Store configuration for mode detection
};

shell_t *shell_create(const shell_cfg_t *cfg)
{
    Expects_not_null(cfg);

    shell_t *sh = xcalloc(1, sizeof(shell_t));
    if (!sh)
        return NULL;

    // Store configuration
    sh->cfg = *cfg;

    // Build exec configuration from shell configuration
    exec_cfg_t exec_cfg = {
        .argc = 0,  // Will be set from cfg->arguments
        .argv = cfg->arguments,
        .envp = cfg->envp,
        .opt = {
            .allexport = cfg->flags.allexport,
            .errexit = cfg->flags.errexit,
            .ignoreeof = cfg->flags.ignoreeof,
            .noclobber = cfg->flags.noclobber,
            .noglob = cfg->flags.noglob,
            .noexec = cfg->flags.noexec,
            .nounset = cfg->flags.nounset,
            .pipefail = cfg->flags.pipefail,
            .verbose = cfg->flags.verbose,
            .vi = cfg->flags.vi,
            .xtrace = cfg->flags.xtrace,
        }
    };

    // Count arguments
    if (cfg->arguments)
    {
        int count = 0;
        while (cfg->arguments[count])
            count++;
        exec_cfg.argc = count;
    }

    sh->root_exec = exec_create_from_cfg(&exec_cfg);
    if (!sh->root_exec)
    {
        xfree(sh);
        return NULL;
    }

    // Initially, current_exec points to root_exec
    sh->current_exec = sh->root_exec;

    return sh;
}

void shell_destroy(shell_t **sh)
{
    Expects_not_null(sh);
    if (!*sh)
        return;

    exec_destroy(&(*sh)->root_exec);
    xfree(*sh);
    *sh = NULL;
}

void shell_cleanup(void *sh_ptr)
{
    if (!sh_ptr)
        return;
    shell_t *sh = (shell_t *)sh_ptr;

    // Clean up the executor
    exec_destroy(&sh->root_exec);

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

static sh_status_t shell_execute_script_file(shell_t *sh, const char *filename)
{
    Expects_not_null(sh);
    Expects_not_null(filename);

    FILE *fp = fopen(filename, "r");
    if (!fp)
    {
        exec_set_error(sh->root_exec, "Cannot open file: %s", filename);
        return SH_RUNTIME_ERROR;
    }

    exec_status_t status = exec_execute_stream(sh->root_exec, fp);
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

extern void exec_reap_background_jobs(exec_t *executor);

static sh_status_t shell_execute_interactive(shell_t *sh)
{
    Expects_not_null(sh);

    const char *ps1 = shell_get_ps1(sh);

    // Simple REPL: read line, execute, repeat
    // For now we execute each complete line - no PS2 multi-line support yet
    char line_buffer[4096];

    fprintf(stdout, "%s", ps1);
    fflush(stdout);

    exec_reap_background_jobs(sh->current_exec);
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
        exec_status_t status = exec_execute_stream(sh->root_exec, tmp);
        fclose(tmp);

        // Print error if any
        if (status != EXEC_OK)
        {
            const char *error = exec_get_error(sh->root_exec);
            if (error && error[0])
            {
                fprintf(stderr, "%s\n", error);
            }
        }

        // Print next prompt
        fprintf(stdout, "%s", ps1);
        fflush(stdout);
    }

    return SH_OK;
}

sh_status_t shell_execute(shell_t *sh)
{
    Expects_not_null(sh);

    switch (sh->cfg.mode)
    {
    case SHELL_MODE_SCRIPT_FILE:
        if (!sh->cfg.command_file)
        {
            exec_set_error(sh->root_exec, "No script file specified");
            return SH_INTERNAL_ERROR;
        }
        return shell_execute_script_file(sh, sh->cfg.command_file);

    case SHELL_MODE_INTERACTIVE:
        return shell_execute_interactive(sh);

    case SHELL_MODE_COMMAND_STRING:
        {
            if (!sh->cfg.command_string)
            {
                exec_set_error(sh->root_exec, "No command string specified");
                return SH_INTERNAL_ERROR;
            }

            // Create a memory stream from the command string
            // For now, write to a temporary file (portable solution)
            // TODO: Use fmemopen on POSIX or create a string stream wrapper
            FILE *tmp = tmpfile();
            if (!tmp)
            {
                exec_set_error(sh->root_exec, "Failed to create temporary file");
                return SH_RUNTIME_ERROR;
            }

            size_t len = strlen(sh->cfg.command_string);
            if (fwrite(sh->cfg.command_string, 1, len, tmp) != len)
            {
                fclose(tmp);
                exec_set_error(sh->root_exec, "Failed to write command string");
                return SH_RUNTIME_ERROR;
            }

            rewind(tmp);
            exec_status_t status = exec_execute_stream(sh->root_exec, tmp);
            fclose(tmp);

            return (status == EXEC_OK) ? SH_OK : SH_RUNTIME_ERROR;
        }

    case SHELL_MODE_STDIN:
        {
            exec_status_t status = exec_execute_stream(sh->root_exec, stdin);
            return (status == EXEC_OK) ? SH_OK : SH_RUNTIME_ERROR;
        }

    case SHELL_MODE_UNKNOWN:
    case SHELL_MODE_INVALID_UID_GID:
    default:
        exec_set_error(sh->root_exec, "Invalid shell mode");
        return SH_INTERNAL_ERROR;
    }
}

const char *shell_last_error(shell_t *sh)
{
    Expects_not_null(sh);
    Expects_not_null(sh->root_exec);

    return exec_get_error(sh->root_exec);
}

void shell_reset_error(shell_t *sh)
{
    Expects_not_null(sh);
    Expects_not_null(sh->root_exec);

    exec_clear_error(sh->root_exec);
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
    Expects_not_null(sh->root_exec);

    return exec_get_ps1(sh->root_exec);
}

const char *shell_get_ps2(const shell_t *sh)
{
    Expects_not_null(sh);
    Expects_not_null(sh->root_exec);

    return exec_get_ps2(sh->root_exec);
}

exec_t *shell_get_current_exec(shell_t *sh)
{
    Expects_not_null(sh);
    return sh->current_exec;
}

void shell_set_current_exec(shell_t *sh, exec_t *ex)
{
    Expects_not_null(sh);
    Expects_not_null(ex);
    sh->current_exec = ex;
}

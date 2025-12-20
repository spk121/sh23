#include "lexer.h"
#include "logging.h"
#include "shell.h"
#include "tokenizer.h"
#include "xalloc.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    arena_start();
    log_init();

    // Initialize shell with some default aliases
    alias_store_t *initial_aliases = alias_store_create();
    alias_store_add_cstr(initial_aliases, "ll", "ls -l");

    // Configure shell
    shell_config_t cfg = {.ps1 = "shell> ",
                          .ps2 = "> ",
                          .initial_aliases = initial_aliases,
                          .initial_funcs = NULL,
                          .initial_vars = NULL};

    // Create shell (shell takes ownership of initial_aliases)
    shell_t *sh = shell_create(&cfg);
    // Don't destroy initial_aliases - shell owns it now

    sh_status_t status = SH_OK;
    int line_no = 0;

    // Display initial prompt
    fputs(shell_get_ps1(sh), stdout);
    fflush(stdout);

    // Main REPL loop
    char line[1024];
    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        line_no++;

        // Check for exit command
        if (strcmp(line, "exit\n") == 0 || strcmp(line, "exit\r\n") == 0)
            break;

        // Feed the line to the shell
        status = shell_feed_line(sh, line, line_no);

        // Handle the result
        switch (status)
        {
        case SH_OK:
            // Command completed successfully
            fputs(shell_get_ps1(sh), stdout);
            fflush(stdout);
            break;

        case SH_INCOMPLETE:
            // More input needed (e.g., unclosed quotes or compound command)
            fputs(shell_get_ps2(sh), stdout);
            fflush(stdout);
            break;

        case SH_SYNTAX_ERROR:
            // Syntax error in the input
            if (shell_last_error(sh) != NULL)
                fprintf(stderr, "Syntax error: %s\n", shell_last_error(sh));
            else
                fprintf(stderr, "Syntax error\n");
            shell_reset_error(sh);
            fputs(shell_get_ps1(sh), stdout);
            fflush(stdout);
            break;

        case SH_RUNTIME_ERROR:
        case SH_INTERNAL_ERROR:
            // Runtime or internal error
            if (shell_last_error(sh) != NULL)
                fprintf(stderr, "Error: %s\n", shell_last_error(sh));
            else
                fprintf(stderr, "Error\n");
            shell_reset_error(sh);
            fputs(shell_get_ps1(sh), stdout);
            fflush(stdout);
            break;

        case SH_FATAL:
            // Fatal error - reinitialize shell
            fprintf(stderr, "Fatal error: %s\n", shell_last_error(sh));
            shell_reset_error(sh);
            fprintf(stderr, "Reinitializing shell.\n");
            shell_destroy(&sh);
            sh = shell_create(&cfg);
            fputs(shell_get_ps1(sh), stdout);
            fflush(stdout);
            break;

        default:
            // Unknown status
            fprintf(stderr, "Unexpected shell status: %d\n", status);
            fputs(shell_get_ps1(sh), stdout);
            fflush(stdout);
            break;
        }
    }

    // Cleanup
    shell_destroy(&sh);
    arena_end();
    return 0;
}

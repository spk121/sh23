#include <stdio.h>
#include <string.h>
#include "lexer.h"
#include "xalloc.h"
#include "tokenizer.h"
#include "shell.h"

int main(void)
{
    arena_start();
    log_init();

    alias_store_t *initial_aliases = alias_store_create(); // empty for now
    alias_store_add_cstr(initial_aliases, "ll", "ls -l");

    shell_config_t cfg = {
        .ps1 = "shell> ",
        .ps2 = "> ",
        .initial_aliases = initial_aliases,
        .initial_funcs = NULL,
        .initial_vars = NULL
    };
    
    shell_t *sh = shell_create(&cfg);
    alias_store_destroy(&initial_aliases);

    sh_status_t status = SH_OK;

    printf("%s ", shell_get_ps1(sh));
    while(true)
    {
        char line[1024];
        int line_no = 1;

        if (!fgets(line, sizeof(line), stdin))
            break;
        if (strcmp(line, "exit\n") == 0)
            break;
        status = shell_feed_line(sh, line, line_no++);

        switch(status)
        {
            case SH_OK:
                printf("%s ", shell_get_ps1(sh));
                break;
            case SH_INCOMPLETE:
                printf("%s ", shell_get_ps2(sh));
                break;
            case SH_SYNTAX_ERROR:
                printf("Syntax error: %s\n", shell_last_error(sh));
                shell_reset_error(sh);
                printf("%s ", shell_get_ps1(sh));
                break;
            case SH_RUNTIME_ERROR:
            case SH_INTERNAL_ERROR:
                printf("Runtime error: %s\n", shell_last_error(sh));
                shell_reset_error(sh);
                printf("%s ", shell_get_ps1(sh));
                break;
            case SH_FATAL:
                printf("Fatal error: %s\n", shell_last_error(sh));
                shell_reset_error(sh);
                printf("Reinitializing shell.\n");
                shell_destroy(sh);
                sh = shell_create(&cfg);
                break;
            default:
                break;
        }
    }

    shell_destroy(sh);
    arena_end();
    return 0;
}

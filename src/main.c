#include "lexer.h"
#include "xalloc.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    arena_start();

    char line[1024];
    const char *PS1 = "shell> ";
    const char *PS2 = "> "; // continuation prompt

    lexer_t *lx = NULL;
    token_list_t *tokens = token_list_create();

    while (1)
    {
        // Use PS1 if no lexer or lexer is in normal mode, PS2 if incomplete
        if (lx == NULL || lexer_current_mode(lx) == LEX_NORMAL)
        {
            printf("%s", PS1);
        }
        else
        {
            printf("%s", PS2);
        }

        if (!fgets(line, sizeof(line), stdin))
            break;

        if (lx == NULL)
        {
            lx = lexer_create(line, strlen(line));
        }
        else
        {
            // Append new input to existing lexer state
            lx = lexer_append_input(lx, line, strlen(line));
        }

        int num_tokens_read = 0;
        lex_status_t status = lexer_tokenize(lx, tokens, &num_tokens_read);

        if (num_tokens_read > 0)
        {
            string_t *dbg = token_list_to_string(tokens);
            printf("%s\n", string_data(dbg));
            string_destroy(dbg);
        }
        if (status == LEX_OK)
        {
            // Completed successfully
            printf("Lexing complete.\n");
            lexer_destroy(lx);
            token_list_reinitialize(tokens);
            lx = NULL; // reset for next line
        }
        if (status == LEX_ERROR)
        {
            printf("Error: %s\n", lexer_get_error(lx));
            lexer_destroy(lx);
            token_list_reinitialize(tokens);
            lx = NULL;
        }
        else if (status == LEX_INCOMPLETE)
        {
            // Do not destroy lexer — keep state alive
            // Next loop iteration will print PS2 and append more input
            continue;
        }
    }
    token_list_destroy(tokens);
    if (lx != NULL)
    {
        lexer_destroy(lx);
    }

    arena_end();
    return 0;
}

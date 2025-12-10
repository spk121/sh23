#include "shell.h"
#include "logging.h"
#include "xalloc.h"
#include "string_t.h"
#include "alias_store.h"
#include "function_store.h"
#include "variable_store.h"
#include "lexer.h"
#include "parser.h"
#include "expander.h"
#include "executor.h"
#include "ast.h"
#include "tokenizer.h"

// Internal helpers used by shell_feed_line/shell_run_script
static sh_status_t sh_lex(shell_t *sh, token_list_t **out_tokens);
static sh_status_t sh_parse(shell_t *sh, token_list_t *tokens, ast_t **out_ast);
static sh_status_t sh_expand(shell_t *sh, ast_t *ast, ast_t **out_expanded);
static sh_status_t sh_execute(shell_t *sh, ast_t *ast);

static char *normalize_newlines(const char *input)
{
    Expects_not_null(input);

    string_t *str = string_create();
    const char *p = input;
    while (*p != '\0')
    {
        if (*p == '\r')
        {
            // Convert \r\n to \n
            if (*(p + 1) == '\n')
            {
                string_append_char(str, '\n');
                p += 2;
            }
            else
            {
                string_append_char(str, '\n');
                p++;
            }
        }
        else
        {
            string_append_char(str, *p);
            p++;
        }
    }
    if (string_back(str) != '\n')
    {
        // If input does not end with newline, append one
        string_append_char(str, '\n');
    }
    return string_release(&str);
}

shell_t *shell_create(const shell_config_t *cfg)
{
    shell_t *sh = xcalloc(1, sizeof(shell_t));

    sh->ps1 = string_create_from_cstr(cfg && cfg->ps1 ? cfg->ps1 : "shell> ");
    sh->ps2 = string_create_from_cstr(cfg && cfg->ps2 ? cfg->ps2 : "> ");

    // Stores
    if (cfg && cfg->initial_aliases)
        ; //sh->aliases = alias_store_clone(cfg->initial_aliases);
    else
        sh->aliases = alias_store_create();
    
    if (cfg && cfg->initial_funcs)
        ; //sh->funcs = function_store_clone(cfg->initial_funcs);
    else
        sh->funcs = function_store_create();

    if (cfg && cfg->initial_vars)
        ; //sh->vars = variable_store_clone(cfg->initial_vars);
    else
        sh->vars = variable_store_create("mgsh");

    // Components
    sh->lexer = lexer_create();
    sh->parser = parser_create();
    sh->expander = expander_create();
    sh->executor = executor_create();
    sh->error = string_create();
    return sh;
}

void shell_destroy(shell_t *sh)
{
    if (sh == NULL)
        return;

    if (sh->ps1 != NULL)
        string_destroy(&sh->ps1);
    if (sh->ps2 != NULL)
        string_destroy(&sh->ps2);
    if (sh->lexer != NULL)
        lexer_destroy(sh->lexer);
    if (sh->parser != NULL)
        parser_destroy(sh->parser);
    if (sh->expander != NULL)
        expander_destroy(sh->expander);
    if (sh->executor != NULL)
        executor_destroy(sh->executor);
    if (sh->aliases != NULL)
        alias_store_destroy(&sh->aliases);
    if (sh->funcs != NULL)
        function_store_destroy(sh->funcs);
    if (sh->vars != NULL)
        variable_store_destroy(sh->vars);
    if (sh->error != NULL)
        string_destroy(&sh->error);

    xfree(sh);
}

sh_status_t shell_feed_line(shell_t *sh, const char *line, int line_num)
{
    Expects_not_null(sh);
    Expects_not_null(line);
    Expects_not_null(sh->lexer);
    
    // Append line to lexer
    if (sh->eol_norm) {
        char *buf = normalize_newlines(line);
        lexer_append_input_cstr(sh->lexer, buf);
        xfree(buf);
    }
    else
        lexer_append_input_cstr(sh->lexer, line);

    if (line_num > 0)
    {
        lexer_set_line_no(sh->lexer, line_num);
    }

    // Lexing, first pass
    token_list_t *tokens = token_list_create();
    int num_tokens_read = 0;

    lex_status_t lex_status = lexer_tokenize(sh->lexer, tokens, &num_tokens_read);
    if (log_level() == LOG_DEBUG)
    {
        string_t *token_str = token_list_to_string(tokens);
        log_debug("shell_feed_line: lexed tokens: %s", string_data(token_str));
        string_destroy(&token_str);
    }
    if (lex_status == LEX_ERROR)
    {
        string_set_cstr(sh->error, lexer_get_error(sh->lexer));
        token_list_destroy(tokens);
        return SH_SYNTAX_ERROR;
    }
    else if (lex_status == LEX_INCOMPLETE)
    {
        // What's the best strategy here? Can we buffer
        // the tokens we've already read? Or should we just
        // return SH_INCOMPLETE and keep appending to the
        // lexer until we get a complete line?
        // For now, we'll just return SH_INCOMPLETE
        // and start over next time.
        log_debug("shell_feed_line: lexer returned LEX_INCOMPLETE");
        token_list_destroy(tokens);
        return SH_INCOMPLETE;
    }
    else if (lex_status == LEX_NEED_HEREDOC)
    {
        // Handle heredoc
        log_debug("shell_feed_line: lexer returned LEX_NEED_HEREDOC");
        token_list_destroy(tokens);
        return SH_INCOMPLETE;
    }
    else if (lex_status != LEX_OK)
    {
        log_error("shell_feed_line: unexpected lexer status: %d", lex_status);
        token_list_destroy(tokens);
        return SH_INTERNAL_ERROR;
    }

    // Got a complete input, so reset the lexer and
    // continue with the tokenizer.
    lexer_reset(sh->lexer);

    token_list_t *out_tokens = token_list_create();
    tok_status_t tok_status;
    tokenizer_t *tokenizer = tokenizer_create(sh->aliases);
    tok_status = tokenizer_process(tokenizer, tokens, out_tokens);
    if (tok_status != TOK_OK)
    {
        string_set_cstr(sh->error, tokenizer_get_error(tokenizer));
        token_list_destroy(out_tokens);
        token_list_destroy(tokens);
        tokenizer_destroy(tokenizer);
        return SH_SYNTAX_ERROR;
    }

    tokenizer_destroy(tokenizer);
    token_list_destroy(tokens);

    // Tokenize into AST
    ast_t *ast = NULL;

#if 0
    // Tokenize
    token_list_t *tokens = token_list_create();
    int num_tokens_read = 0;
    lex_status_t lex_status = lexer_tokenize(sh->lexer, tokens, &num_tokens_read);

    if (lex_status == LEX_ERROR)
    {
        string_set_cstr(sh->error, lexer_get_error(sh->lexer));
        token_list_destroy(tokens);
        return SH_SYNTAX_ERROR;
    }

    if (lex_status == LEX_INCOMPLETE)
    {
        token_list_destroy(tokens);
        return SH_INCOMPLETE;
    }

    // Parse
    ast_node_t *ast = NULL;
    parse_status_t parse_status = parser_parse(sh->parser, tokens, &ast);
    if (parse_status == PARSE_SYNTAX_ERROR)
    {
        string_set_cstr(sh->error, parser_get_error(sh->parser));
        token_list_destroy(tokens);
        return SH_SYNTAX_ERROR;
    }
    // TBD: Execute AST here

    // Cleanup
    if (ast != NULL)
        ast_destroy(ast);
    token_list_destroy(tokens);
#endif
    return SH_OK;
}

const char *shell_last_error(shell_t *sh)
{
    Expects_not_null(sh);

    if (string_length(sh->error) == 0)
    {
        log_debug("Requested last error when no error message set.");
        return NULL;
    }
    return string_data(sh->error);
}

void shell_reset_error(shell_t *sh)
{
    Expects_not_null(sh);

    string_clear(sh->error);
}

const char *shell_get_ps1(const shell_t *sh)
{
    Expects_not_null(sh);
    Expects_not_null(sh->ps1);

    return string_data(sh->ps1);
}

const char *shell_get_ps2(const shell_t *sh)
{
    Expects_not_null(sh);
    Expects_not_null(sh->ps2);

    return string_data(sh->ps2);
}
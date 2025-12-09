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

shell_t *shell_create(const shell_config_t *cfg)
{
    shell_t *sh = xcalloc(1, sizeof(shell_t));

    sh->ps1 = string_create_from_cstr(cfg && cfg->ps1 ? cfg->ps1 : "shell> ");
    sh->ps2 = string_create_from_cstr(cfg && cfg->ps2 ? cfg->ps2 : "> ");

    // Debug level
    sh->debug_level = cfg ? cfg->debug_level : 0;

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
    sh->error = string_create_empty(256);
    return sh;
}

void shell_destroy(shell_t *sh)
{
    if (sh == NULL)
        return;

    if (sh->ps1 != NULL)
        string_destroy(sh->ps1);
    if (sh->ps2 != NULL)
        string_destroy(sh->ps2);
    if (sh->lexer != NULL)
        lexer_destroy(sh->lexer);
    if (sh->parser != NULL)
        parser_destroy(sh->parser);
    if (sh->expander != NULL)
        expander_destroy(sh->expander);
    if (sh->executor != NULL)
        executor_destroy(sh->executor);
    if (sh->aliases != NULL)
        alias_store_destroy(sh->aliases);
    if (sh->funcs != NULL)
        function_store_destroy(sh->funcs);
    if (sh->vars != NULL)
        variable_store_destroy(sh->vars);
    if (sh->error != NULL)
        string_destroy(sh->error);

    xfree(sh);
}

sh_status_t shell_feed_line(shell_t *sh, const char *line)
{
    Expects_not_null(sh);
    Expects_not_null(line);
    Expects_not_null(sh->lexer);

    // Append line to lexer
    lexer_append_input_cstr_normalize_newlines(sh->lexer, line);
    token_list_t *out_tokens = token_list_create();

    string_t *token_str = token_list_to_string(out_tokens);
    log_debug("Lexed tokens: %s", string_data(token_str));
    string_destroy(token_str);
    token_list_destroy(out_tokens);

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
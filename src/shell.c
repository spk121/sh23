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
#if 0
static sh_status_t sh_lex(shell_t *sh, token_list_t **out_tokens);
static sh_status_t sh_parse(shell_t *sh, token_list_t *tokens, ast_t **out_ast);
static sh_status_t sh_expand(shell_t *sh, ast_t *ast, ast_t **out_expanded);
static sh_status_t sh_execute(shell_t *sh, ast_t *ast);
#endif

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
        sh->aliases = (alias_store_t *)cfg->initial_aliases;
    else
        sh->aliases = alias_store_create();
    
    if (cfg && cfg->initial_funcs)
        sh->funcs = (function_store_t *)cfg->initial_funcs;
    else
        sh->funcs = function_store_create();

    if (cfg && cfg->initial_vars)
        sh->vars = (variable_store_t *)cfg->initial_vars;
    else
        sh->vars = variable_store_create();

    // Components
    sh->lexer = lexer_create();
    sh->parser = parser_create();
    sh->expander = expander_create();
    sh->executor = executor_create();
    sh->error = string_create();
    
    // Hook executor callbacks into expander
    expander_set_variable_store(sh->expander, sh->vars);
    expander_set_command_subst_callback(sh->expander, executor_command_subst_callback, sh->executor, NULL);
    expander_set_pathname_expansion_callback(sh->expander, executor_pathname_expansion_callback, sh);
    
    return sh;
}

void shell_destroy(shell_t **sh)
{
    Expects_not_null(sh);
    shell_t *s = *sh;
    
    if (s == NULL)
        return;

    if (s->ps1 != NULL)
        string_destroy(&s->ps1);
    if (s->ps2 != NULL)
        string_destroy(&s->ps2);
    if (s->lexer != NULL)
        lexer_destroy(&s->lexer);
    if (s->parser != NULL)
        parser_destroy(&s->parser);
    if (s->expander != NULL)
        expander_destroy(&s->expander);
    if (s->executor != NULL)
        executor_destroy(&s->executor);
    if (s->aliases != NULL)
        alias_store_destroy(&s->aliases);
    if (s->funcs != NULL)
        function_store_destroy(&s->funcs);
    if (s->vars != NULL)
        variable_store_destroy(&s->vars);
    if (s->error != NULL)
        string_destroy(&s->error);

    xfree(s);
    *sh = NULL;
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
        string_t *token_str = token_list_to_string(tokens, 0);
        log_debug("shell_feed_line: lexed tokens: %s", string_data(token_str));
        string_destroy(&token_str);
    }
    if (lex_status == LEX_ERROR)
    {
        string_set_cstr(sh->error, lexer_get_error(sh->lexer));
        token_list_destroy(&tokens);
        return SH_SYNTAX_ERROR;
    }
    else if (lex_status == LEX_INCOMPLETE)
    {
        // Lexer needs more input (e.g., unclosed quotes)
        // Keep tokens for now - they may be needed when heredoc completes
        log_debug("shell_feed_line: lexer returned LEX_INCOMPLETE");
        // Don't destroy tokens - lexer may reference them for heredoc processing
        return SH_INCOMPLETE;
    }
    else if (lex_status == LEX_NEED_HEREDOC)
    {
        // Heredoc delimiter seen, waiting for heredoc body
        log_debug("shell_feed_line: lexer returned LEX_NEED_HEREDOC");
        // Don't destroy tokens - they'll be needed when heredoc is complete
        return SH_INCOMPLETE;
    }
    else if (lex_status != LEX_OK)
    {
        log_error("shell_feed_line: unexpected lexer status: %d", lex_status);
        token_list_destroy(&tokens);
        return SH_INTERNAL_ERROR;
    }

    // Got a complete input, so reset the lexer and
    // continue with the tokenizer.
    lexer_reset(sh->lexer);

    // Check if this is just heredoc completion tokens (shouldn't be parsed standalone)
    // If we only have a WORD (heredoc content) followed by END_OF_HEREDOC, skip parsing
    // These tokens were already part of a previous command
    if (token_list_size(tokens) == 2)
    {
        token_t *first = token_list_get(tokens, 0);
        token_t *second = token_list_get(tokens, 1);
        if (token_get_type(first) == TOKEN_WORD && 
            token_get_type(second) == TOKEN_END_OF_HEREDOC)
        {
            log_debug("shell_feed_line: skipping heredoc completion tokens");
            token_list_destroy(&tokens);
            return SH_OK;
        }
    }

    // The tokenizer doesn't need persistent state, so
    // we can create a new one each line.
    token_list_t *out_tokens = token_list_create();
    tok_status_t tok_status;
    tokenizer_t *tokenizer = tokenizer_create(sh->aliases);

    // The tokenizer process will spawn a new internal
    // lexer to re-lex the input. TODO: decide if I should
    // just use the existing lexer and reset it instead.
    tok_status = tokenizer_process(tokenizer, tokens, out_tokens);
    if (tok_status != TOK_OK)
    {
        string_set_cstr(sh->error, tokenizer_get_error(tokenizer));
        token_list_destroy(&out_tokens);
        token_list_destroy(&tokens);
        tokenizer_destroy(&tokenizer);
        return SH_SYNTAX_ERROR;
    }

    tokenizer_destroy(&tokenizer);
    token_list_destroy(&tokens);

    // Parse into AST
    parser_t *parser = parser_create();
    parser->tokens = tokens;
    gnode_t *gast = NULL;
    parse_status_t parse_status = parser_parse_program(parser, &gast);

    if (parse_status == PARSE_ERROR)
    {
        string_set_cstr(sh->error, parser_get_error(sh->parser));
        // token_list_destroy(&out_tokens);
        return SH_SYNTAX_ERROR;
    }
    else if (parse_status == PARSE_INCOMPLETE)
    {
        // Parser needs more input (e.g., incomplete if clause, multiline command)
        // Keep the tokens for now - they'll be cleaned up later
        log_debug("shell_feed_line: parser returned PARSE_INCOMPLETE");
        // Don't destroy out_tokens - parser may have references
        return SH_INCOMPLETE;
    }
    else if (parse_status == PARSE_EMPTY)
    {
        // Empty input (just whitespace/newlines)
        log_debug("shell_feed_line: parser returned PARSE_EMPTY");
        token_list_destroy(&out_tokens);
        return SH_OK;
    }
    else if (parse_status != PARSE_OK)
    {
        log_error("shell_feed_line: unexpected parser status: %d", parse_status);
        token_list_destroy(&out_tokens);
        return SH_INTERNAL_ERROR;
    }
 
    // Now convert grammar AST to shell AST
    ast_node_t *ast = ast_lower(gast);
    g_node_destroy(&gast);

    // AST now owns the tokens - release them from the list without destroying
    token_list_release_tokens(out_tokens);
    xfree(out_tokens->tokens);
    xfree(out_tokens);
 
    // Debug: print the AST
    if (log_level() == LOG_DEBUG)
    {
        ast_print(ast);
    }

    // Expand the AST (parameter expansion, command substitution, etc.)
    ast_node_t *expanded_ast = expander_expand_ast(sh->expander, ast);
    if (expanded_ast == NULL)
    {
        log_error("shell_feed_line: expander returned NULL");
        ast_node_destroy(&ast);
        return SH_INTERNAL_ERROR;
    }

    // Debug: print the expanded AST
    if (log_level() == LOG_DEBUG)
    {
        log_debug("shell_feed_line: expanded AST:");
        ast_print(expanded_ast);
    }

    // Execute the expanded AST
    exec_status_t exec_status = executor_execute(sh->executor, expanded_ast);
    // Propagate the last exit status into the expander so subsequent `$?`
    // expansions see the most recent command status (including the POSIX
    // assignment-only + command-substitution case).
    expander_set_last_exit_status(sh->expander, executor_get_exit_status(sh->executor));
    
    // Cleanup AST (expanded_ast may be the same pointer as ast)
    ast_node_destroy(&ast);

    // Convert execution status to shell status
    switch (exec_status)
    {
        case EXEC_OK:
            return SH_OK;
        case EXEC_ERROR:
            return SH_RUNTIME_ERROR;
        case EXEC_NOT_IMPL:
            string_set_cstr(sh->error, "feature not yet implemented");
            return SH_RUNTIME_ERROR;
        default:
            log_error("shell_feed_line: unexpected executor status: %d", exec_status);
            return SH_INTERNAL_ERROR;
    }

#if 0
    // Tokenize
    token_list_t *tokens = token_list_create();
    int num_tokens_read = 0;
    lex_status_t lex_status = lexer_tokenize(sh->lexer, tokens, &num_tokens_read);

    if (lex_status == LEX_ERROR)
    {
        string_set_cstr(sh->error, lexer_get_error(sh->lexer));
        token_list_destroy(&tokens);
        return SH_SYNTAX_ERROR;
    }

    if (lex_status == LEX_INCOMPLETE)
    {
        token_list_destroy(&tokens);
        return SH_INCOMPLETE;
    }

    // Parse
    ast_node_t *ast = NULL;
    parse_status_t parse_status = parser_parse(sh->parser, tokens, &ast);
    if (parse_status == PARSE_SYNTAX_ERROR)
    {
        string_set_cstr(sh->error, parser_get_error(sh->parser));
        token_list_destroy(&tokens);
        return SH_SYNTAX_ERROR;
    }
    // TBD: Execute AST here

    // Cleanup
    if (ast != NULL)
        ast_node_destroy(&ast);
    token_list_destroy(&tokens);
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
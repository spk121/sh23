#ifndef SHELL_T_H
#define SHELL_T_H

#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "expander.h"
#include "executor.h"
#include "alias_store.h"
#include "variable_store.h"
#include "function_store.h"
#include "string_t.h"

// Status/result conventions applied everywhere
typedef enum {
    SH_OK,
    SH_INCOMPLETE,    // more input required (multiline)
    SH_SYNTAX_ERROR,
    SH_RUNTIME_ERROR,
    SH_FATAL
} sh_status_t;

typedef enum {
    SH_SILENT,
    SH_ERROR,
    SH_WARNING,
    SH_DEBUG,
    SH_INFO
} sh_debug_level_t;

typedef struct {
    // Prompt strings (configurable)
    string_t *ps1;  // "shell> "
    string_t *ps2;  // "> "

    // Stateful components
    lexer_t         *lexer;       // persists across lines when SH_INCOMPLETE
    parser_t        *parser;      // optionally persistent for here-docs or compound constructs
    expander_t      *expander;
    executor_t      *executor;

    // Stores
    alias_store_t      *aliases;
    function_store_t   *funcs;
    variable_store_t   *vars;

    // Error buffer for top-level reporting
    string_t        *error;       // last error message

    // Debug level
    sh_debug_level_t debug_level;
} shell_t;

typedef struct {
    const char      *ps1;
    const char      *ps2;
    sh_debug_level_t debug_level;

    const alias_store_t    *initial_aliases;
    const function_store_t *initial_funcs;
    const variable_store_t *initial_vars;
} shell_config_t;

shell_t *shell_create(const shell_config_t *cfg);
void     shell_destroy(shell_t *sh);

// Feeds a single line and processes if complete.
// Returns SH_INCOMPLETE if the shell expects more input.
sh_status_t shell_feed_line(shell_t *sh, const char *line);

// If shell_feed_line returns SH_OK and a command is complete,
// the shell has already executed it. Errors are reported via sh->error.
const char *shell_last_error(shell_t *sh);

// Clear the last error message.
void shell_reset_error(shell_t *sh);

// Process a full script buffer (no prompt logic).
sh_status_t shell_run_script(shell_t *sh, const char *script);

// Various getters/setters and helpers
const char *shell_get_ps1(const shell_t *sh);
const char *shell_get_ps2(const shell_t *sh);

// Internal helpers used by shell_feed_line/shell_run_script
sh_status_t sh_lex(shell_t *sh, token_list_t **out_tokens);
sh_status_t sh_parse(shell_t *sh, token_list_t *tokens, ast_t **out_ast);
sh_status_t sh_expand(shell_t *sh, ast_t *ast, ast_t **out_expanded);
sh_status_t sh_execute(shell_t *sh, ast_t *ast);


#endif
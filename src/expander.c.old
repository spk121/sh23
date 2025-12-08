#include "expander.h"
#include "logging.h"
#include "xalloc.h"
#include "string_t.h"


struct expander_t
{   
    string_t *ifs;
    // Any state needed for expansion can go here
};

expander_t *expander_create(void)
{
    expander_t *exp = xcalloc(1, sizeof(expander_t));
    exp->ifs = string_create(" \t\n");
    // Initialize any state as needed
    return exp;
}

void expander_destroy(expander_t *expander)
{
    if (expander == NULL)
        return;

    // Clean up any state as needed

    xfree(expander);
}

bool expander_try_sync_ifs_from_variable_store(expander_t *exp, variable_store_t *vars)
{
    Expects_not_null(exp);
    Expects_not_null(vars);

    variable_t *ifs_var = variable_store_get(vars, "IFS");
    if (ifs_var == NULL || ifs_var->value == NULL)
    {
        return false; // IFS not set
    }

    // Update expander's IFS
    expander_set_ifs(exp, ifs_var->value);
    return true;
}

void expander_set_ifs(expander_t *exp, const string_t *ifs)
{
    Expects_not_null(exp);
    Expects_not_null(ifs);

    if (exp->ifs != NULL)
    {
        string_destroy(exp->ifs);
    }
    exp->ifs = string_clone(ifs);
}

const string_t *expander_get_ifs(const expander_t *exp)
{
    Expects_not_null(exp);
    return exp->ifs;
}

// Main entry point
ast_node_t *expander_expand_ast(expander_t *exp, ast_node_t *node)
{
    Expects_not_null(exp);
    Expects_not_null(node);

    // Placeholder: actual expansion logic goes here
    log_debug("expander_expand_ast: expanding AST node of type %d", node->type);

    // For now, just return the node unchanged
    return node;
}

string_t *expander_expand_word(expander_t *exp, token_t *word_token)
{
    Expects_not_null(exp);
    Expects_not_null(word_token);

    // 1. Build raw string (param + cmd subst + arith)
    string_t *raw = expander_perform_tilde_and_expansions(word_token);

    // 2. Field splitting (only if word was unquoted)
    string_list_t *fields = field_split(raw, exp->ifs);

    // 3. Glob each field (only if unquoted part)
    for (i = 0; i < string_list_size(fields); i++)
    {
        if (field_was_unquoted(string_list_get(fields, i)))
        {
            string_list_t *globbed = glob_expand(string_list_get(fields, i));
            string_list_take_replace(fields, i, globbed);
        }
    }

    string_destroy(raw);
    return fields
}

/**
 * Perform tilde expansion + steps 1–3 of word expansion:
 *   1. Parameter expansion
 *   2. Command substitution
 *   3. Arithmetic expansion
 *
 * This runs in exact POSIX order.
 * The result is a single string (no field splitting yet).
 *
 * @param exp     The expander context
 * @param word    The TOKEN_WORD to expand
 * @return        Newly allocated string_t with the fully expanded result
 *                Caller must string_destroy() it.
 */
string_t *expander_perform_tilde_and_expansions(expander_t *exp, token_t *word)
{
    Expects_not_null(exp);
    Expects_not_null(word);
    Expects(token_get_type(word) == TOKEN_WORD);

    string_t *result = string_create_empty(128);
    part_list_t *parts = token_get_parts(word);

    for (int i = 0; i < part_list_size(parts); i++)
    {
        part_t *part = part_list_get(parts, i);

        switch (part_get_type(part))
        {
            case PART_LITERAL:
            {
                // Tilde expansion: only at start of word or after : or = in assignment
                // For simplicity: do it if this is the first part and unquoted
                const char *text = string_data(part->text);
                if (i == 0 && !part_was_quoted(part))
                {
                    string_t *expanded = tilde_expand(text);
                    string_append(result, expanded);
                    string_destroy(expanded);
                }
                else
                {
                    string_append(result, part->text);
                }
                break;
            }

            case PART_PARAMETER:
            {
                string_t *value = parameter_expand(exp, part);
                if (value)
                {
                    string_append(result, value);
                    string_destroy(value);
                }
                // else: unset → empty (or error if ${var?})
                break;
            }

            case PART_COMMAND_SUBST:
            {
                // Execute $(...) or `...` and append output
                // Trailing newlines are removed (POSIX requirement)
                string_t *output = command_substitution_execute(exp, part->text);
                if (output)
                {
                    // Remove trailing newlines
                    while (string_length(output) > 0 &&
                           string_back(output) == '\n')
                    {
                        string_pop_back(output);
                    }
                    string_append(result, output);
                    string_destroy(output);
                }
                break;
            }

            case PART_ARITHMETIC:
            {
                long value = arithmetic_evaluate(exp, part->text);
                char buf[32];
                snprintf(buf, sizeof(buf), "%ld", value);
                string_append_cstr(result, buf);
                break;
            }

            default:
                // Should never happen
                break;
        }
    }

    return result;
}

// 1. Parameter expansion
string_t *parameter_expand(expander_t *exp, part_t *part)
{
    // Placeholder implementation
    // Actual implementation would handle various parameter expansion forms
    const char *param_name = string_data(part->param_name);
    log_debug("parameter_expand: expanding parameter '%s'", param_name);

    // For now, just return an empty string
    return string_create("");
}

// 2. Command substitution
string_t *command_substitution_execute(expander_t *exp, const char *command_text)
{
    // Placeholder implementation
    log_debug("command_substitution_execute: executing command '%s'", command_text);

    // For now, just return an empty string
    return string_create("");
}

// 3. Arithmetic expansion
long arithmetic_evaluate(expander_t *exp, const char *expr_text)
{
    // Placeholder implementation
    log_debug("arithmetic_evaluate: evaluating expression '%s'", expr_text);

    // For now, just return 0
    return 0;
}

// Bonus: Tilde expansion
string_t *tilde_expand(const char *text, variable_store_t *vars)
{
    if (!text || text[0] != '~')
        return string_create_from_cstr(text);

    // ~ alone or ~/...
    if (text[1] == '/' || text[1] == '\0')
    {
        const char *home = var_store_get_cstr(vars, "HOME");
        if (!home || !home[0])
            return string_create_from_cstr(text);  // fallback: no expansion

        string_t *result = string_create_from_cstr(home);
        if (text[1] == '/')
            string_append_cstr(result, text + 1);  // skip ~/
        return result;
    }
    else if (strcmp(text, "~+") == 0)
        return string_create_from_cstr(var_store_get_cstr(vars, "PWD"));
    else if (strcmp(text, "~-") == 0)
        return string_create_from_cstr(var_store_get_cstr(vars, "OLDPWD"));

    // ~user or ~user/...
    const char *slash = strchr(text, '/');
    size_t name_len = slash ? (size_t)(slash - text - 1) : strlen(text + 1);

    char *username = xstrndup(text + 1, name_len);
    struct passwd *pw = getpwnam(username);
    xfree(username);

    if (!pw || !pw->pw_dir)
        return string_create_from_cstr(text);  // no such user → no expansion

    string_t *result = string_create_from_cstr(pw->pw_dir);
    if (slash)
        string_append_cstr(result, slash);  // append /path
    return result;
}

string_t *parameter_expand(expander_t *exp, part_t *part)
{
    const char *name = string_data(part->name);
    const char *value = var_store_get_cstr(exp->vars, name);

    switch (part->param_kind)
    {
        case PARAM_PLAIN:
        case PARAM_BRACED:
            return value ? string_create_from_cstr(value) : string_create_empty(0);

        case PARAM_LENGTH:
            return string_create_from_long(value ? strlen(value) : 0);

        case PARAM_USE_DEFAULT:        // ${var:-word}
        case PARAM_USE_DEFAULT_NULL:   // ${var-word}
            if (value && value[0]) return string_create_from_cstr(value);
            return expander_expand_word_recursive(exp, part->word);

        case PARAM_ASSIGN_DEFAULT:     // ${var:=word}
        case PARAM_ASSIGN_DEFAULT_NULL:
            if (!value || !value[0])
            {
                string_t *def = expander_expand_word_recursive(exp, part->word);
                var_store_set(exp->vars, name, string_data(def));
                string_destroy(def);
            }
            return string_create_from_cstr(var_store_get_cstr(exp->vars, name));

        case PARAM_ERROR_IF_UNSET:     // ${var:?word}
        case PARAM_ERROR_IF_NULL:
            if (!value || !value[0])
            {
                string_t *msg = expander_expand_word_recursive(exp, part->word);
                fprintf(stderr, "shell: %s: %s\n", name, string_data(msg));
                string_destroy(msg);
                exit(1);
            }
            return string_create_from_cstr(value);

        case PARAM_USE_ALTERNATE:      // ${var:+word}
            if (value && value[0])
                return expander_expand_word_recursive(exp, part->word);
            return string_create_empty(0);

        case PARAM_REMOVE_SMALL_PREFIX:   // ${var#pattern}
        case PARAM_REMOVE_LARGE_PREFIX:   // ${var##pattern}
        case PARAM_REMOVE_SMALL_SUFFIX:   // ${var%pattern}
        case PARAM_REMOVE_LARGE_SUFFIX:   // ${var%%pattern}
            return pattern_remove(exp, value ? value : "", part);

        case PARAM_SUBSTRING:             // ${var:offset:length}
            return substring_expand(value ? value : "", part);

        default:
            return string_create_empty(0);
    }
}

string_t *command_substitution_execute(expander_t *exp, const char *command_text)
{
    // 1. Re-lex the raw text
    lexer_t *sub_lexer = lexer_create();
    lexer_append_input_cstr(sub_lexer, command_text);
    token_list_t *tokens = token_list_create();

    lex_status_t status;
    while ((status = lexer_tokenize_one(sub_lexer)) == LEX_OK)
        ; // keep going
    if (status == LEX_INCOMPLETE)
        status = LEX_OK; // EOF is fine

    // 2. Run through tokenizer (alias expansion)
    token_list_t *tokenized = token_list_create();
    tokenizer_process(exp->tokenizer, tokens, tokenized);

    // 3. Parse into AST
    parser_t *sub_parser = parser_create();
    ast_node_t *sub_ast = NULL;
    parse_status_t parse_status = parser_parse(sub_parser, tokenized, &sub_ast);

    parser_destroy(sub_parser);
    token_list_destroy(tokenized);
    token_list_destroy(tokens);
    lexer_destroy(sub_lexer);

    if (parse_status != PARSE_OK)
    {
        ast_node_destroy(sub_ast);
        return string_create_from_cstr(""); // or error?
    }

    // 4. Execute in a subshell and capture stdout
    string_t *output = execute_and_capture_stdout(sub_ast);

    ast_node_destroy(sub_ast);
    return output;
}

void execute_and_capture_stdout(ast_node_t *node)
{
    int pipefd[2];
    pipe(pipefd);
    pid_t pid = fork();

    if (pid == 0)
    {
        // Child: redirect stdout to pipe
        dup2(pipefd[1], 1);
        close(pipefd[0]);
        close(pipefd[1]);

        execute_ast(node);  // your normal executor
        _exit(0);
    }

    close(pipefd[1]);
    string_t *output = string_create_empty(256);
    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
    string_append_data(output, buf, n);

    close(pipefd[0]);
    waitpid(pid, NULL, 0);
    return output;
}

long arithmetic_evaluate(expander_t *exp, const char *expr_text)
{
    // Step 1: Re-lex the raw text inside $(())
    lexer_t *lx = lexer_create();
    lexer_append_input_cstr(lx, expr_text);

    // Step 2: Tokenize + alias expansion (rare, but allowed)
    token_list_t *tokens = lexer_fully_tokenize(lx);  // your existing code
    token_list_t *aliased = token_list_create();
    tokenizer_process(exp->tokenizer, tokens, aliased);

    // Step 3: Parse into AST (using your full parser!)
    parser_t *p = parser_create();
    ast_node_t *ast = NULL;
    parser_parse(p, aliased, &ast);

    // Step 4: Expand the AST — this performs:
    //    • Parameter expansion
    //    • Command substitution
    //    • Quote removal
    //    → Result: one big string like "1 + 2 * 3"
    string_t *expanded = expander_perform_tilde_and_expansions_on_ast(ast);

    // Step 5: Now evaluate the final arithmetic string
    long result = arithmetic_eval_simple(expanded->data);

    // Cleanup
    string_destroy(expanded);
    ast_node_destroy(ast);
    parser_destroy(p);
    token_list_destroy(aliased);
    token_list_destroy(tokens);
    lexer_destroy(lx);

    return result;
}

long arithmetic_eval_simple(const char *expr)
{
    // Placeholder implementation
    log_debug("arithmetic_eval_simple: evaluating expression '%s'", expr);

    // For now, just return 0
    return 0;
}

string_list_t *field_split(expander_t *exp, string_t *raw, bool was_unquoted)
{
    if (!was_unquoted || !raw || string_length(raw) == 0)
    {
        // Rule 1 & 7: quoted or IFS=null → no split
        string_list_t *list = string_list_create();
        string_list_append(list, raw);  // takes ownership
        return list;
    }

    const char *ifs = var_store_get_cstr(exp->vars, "IFS");
    if (!ifs || ifs[0] == '\0')
    {
        // Rule 7: IFS null → no splitting
        string_list_t *list = string_list_create();
        string_list_append(list, raw);
        return list;
    }

    // Build IFS whitespace set
    bool ifs_whitespace[256] = {0};
    bool has_non_whitespace = false;
    for (const char *p = ifs; *p; p++)
    {
        unsigned char c = *p;
        ifs_whitespace[c] = true;
        if (!isspace(c))
            has_non_whitespace = true;
    }

    string_list_t *fields = string_list_create();
    string_t *current = string_create_empty(64);
    const char *data = string_data(raw);
    size_t len = string_length(raw);

    for (size_t i = 0; i < len; )
    {
        unsigned char c = data[i];

        if (ifs_whitespace[c])
        {
            // Whitespace in IFS
            if (isspace(c))
            {
                // Rule 3: collapse whitespace
                while (i < len && isspace((unsigned char)data[i]))
                    i++;
                if (string_length(current) > 0)
                {
                    string_list_append(fields, current);
                    current = string_create_empty(64);
                }
                continue;
            }
            else
            {
                // Non-whitespace delimiter
                if (string_length(current) > 0 || !has_non_whitespace)
                {
                    string_list_append(fields, current);
                    current = string_create_empty(64);
                }
                i++;
                continue;
            }
        }

        string_append_ascii_char(current, c);
        i++;
    }

    // Final field
    if (string_length(current) > 0 || has_non_whitespace)
    {
        string_list_append(fields, current);
    }
    else
    {
        string_destroy(current);
    }

    string_destroy(raw);
    return fields;
}

bool field_was_unquoted(const string_t *field)
{
    // Placeholder implementation
    // Actual implementation would track quoting state during expansion
    return true; // Assume unquoted for now
}

string_list_t *glob_expand(const string_t *field)
{
    // Placeholder implementation
    log_debug("glob_expand: performing globbing on field '%s'", string_data(field));

    // For now, just return a single-field list with the original field
    string_list_t *list = pathname_expand(string_data(field));
    string_list_clone_append(list, field);
    return list;
}

string_list_t *pathname_expand(const char *pattern)
{
    if (shell_option_no_glob) // set -f
        return string_list_from_cstr(pattern);

    // Use POSIX fnmatch() + opendir()/readdir()
    // This is the only correct way

    string_list_t *results = string_list_create();
    glob_t globbuf;
    int flags = GLOB_NOCHECK | GLOB_NOSORT;  // NOCHECK = return pattern on no match

    int ret = glob(pattern, flags, NULL, &globbuf);
    if (ret == 0)
    {
        for (size_t i = 0; i < globbuf.gl_pathc; i++)
            string_list_append_cstr(results, globbuf.gl_pathv[i]);
    }
    else if (ret == GLOB_NOMATCH)
    {
        string_list_append_cstr(results, pattern);
    }

    globfree(&globbuf);
    return results;
}

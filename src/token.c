#include "token.h"
#include "logging.h"
#include "xalloc.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

#define INITIAL_LIST_CAPACITY 8

/* ============================================================================
 * Token Lifecycle Functions
 * ============================================================================ */

token_t *token_create(token_type_t type)
{
    token_t *token = (token_t *)xcalloc(1, sizeof(token_t));

    token->type = type;
    if (type == TOKEN_WORD)
    {
        token->parts = part_list_create();
    }
    return token;
}

token_t *token_create_word(void)
{
    return token_create(TOKEN_WORD);
}

void token_destroy(token_t **token)
{
    Expects_not_null(token);
    token_t *t = *token;
    Expects_not_null(t);

    if (t->parts != NULL)
        part_list_destroy(&t->parts);

    if (t->heredoc_delimiter != NULL)
        string_destroy(&t->heredoc_delimiter);

    if (t->heredoc_content != NULL)
        string_destroy(&t->heredoc_content);

    if (t->assignment_name != NULL)
        string_destroy(&t->assignment_name);

    if (t->assignment_value != NULL)
        part_list_destroy(&t->assignment_value);

    xfree(t);
    *token = NULL;
}

/* ============================================================================
 * Token Accessors
 * ============================================================================ */

token_type_t token_get_type(const token_t *token)
{
    Expects_not_null(token);
    return token->type;
}

void token_set_type(token_t *token, token_type_t type)
{
    Expects_not_null(token);
    token->type = type;
}

part_list_t *token_get_parts(token_t *token)
{
    Expects_not_null(token);
    return token->parts;
}

const part_list_t *token_get_parts_const(const token_t *token)
{
    Expects_not_null(token);
    return token->parts;
}

int token_part_count(const token_t *token)
{
    Expects_not_null(token);
    Expects_not_null(token->parts);

    return token->parts->size;
}

part_t *token_get_part(const token_t *token, int index)
{
    Expects_not_null(token);
    Expects_not_null(token->parts);
    Expects_lt(index, token->parts->size);

    return token->parts->parts[index];
}

bool token_is_last_part_literal(const token_t *token)
{
    Expects_not_null(token);
    Expects_not_null(token->parts);
    Expects_gt(token->parts->size, 0);

    part_t *last_part = token->parts->parts[token->parts->size - 1];
    return last_part->type == PART_LITERAL;
}

bool token_was_quoted(const token_t *token)
{
    Expects_not_null(token);

#ifdef DEBUG
    if (token->type == TOKEN_WORD)
    {
        Expects_not_null(token->parts);
        int part_count = (int)token->parts->size;
        part_t **parts = token->parts->parts;
        for (int i = 0; i < part_count; i++)
        {
            if (parts[i]->was_single_quoted || parts[i]->was_double_quoted)
            {
                Expects(token->was_quoted);
            }
        }
    }
#endif
    return token->was_quoted;
}

void token_set_quoted(token_t *token, bool was_quoted)
{
    Expects_not_null(token);
    token->was_quoted = was_quoted;
}

/* ============================================================================
 * Token Part Management
 * ============================================================================ */

void token_add_part(token_t *token, part_t *part)
{
    Expects_not_null(token);
    Expects_eq(token->type, TOKEN_WORD);
    Expects_not_null(token->parts);
    Expects_not_null(part);

    part_list_append(token->parts, part);
}

void token_add_literal_part(token_t *token, const string_t *text)
{
    Expects_not_null(token);
    Expects_not_null(text);

    token_add_part(token, part_create_literal(text));
}

void token_append_parameter(token_t *token, const string_t *param_name)
{
    Expects_not_null(token);
    Expects_not_null(param_name);

    token_add_part(token, part_create_parameter(param_name));
    token->needs_expansion = true;
    token->needs_field_splitting = true;
}

void token_append_command_subst(token_t *token, const string_t *expr_text)
{
    Expects_not_null(token);
    Expects_not_null(expr_text);

    token_add_part(token, part_create_command_subst(expr_text));
    token->needs_expansion = true;
    token->needs_field_splitting = true;
}

void token_append_arithmetic(token_t *token, const string_t *expr_text)
{
    Expects_not_null(token);
    Expects_not_null(expr_text);

    token_add_part(token, part_create_arithmetic(expr_text));
    token->needs_expansion = true;
    token->needs_field_splitting = true;
}

void token_append_tilde(token_t *token, const string_t *text)
{
    Expects_not_null(token);
    Expects_not_null(text);

    token_add_part(token, part_create_tilde(text));
    token->needs_expansion = true;
}

static bool string_contains_glob(const string_t *str)
{
    Expects_not_null(str);
    Expects_not_null(str->data);

    return (string_find_cstr(str, "*") != -1 ||
            string_find_cstr(str, "?") != -1 ||
            string_find_cstr(str, "[") != -1);
}

void token_recompute_expansion_flags(token_t *token)
{
    Expects_not_null(token);

    // Reset all flags
    token->needs_expansion = false;
    token->needs_field_splitting = false;
    token->needs_pathname_expansion = false;

    if (token->type != TOKEN_WORD || token->parts == NULL)
    {
        return;
    }

    for (int i = 0; i < token->parts->size; i++)
    {
        part_t *part = token->parts->parts[i];
        if (part == NULL)
            continue;

        switch (part->type)
        {
        case PART_PARAMETER:
        case PART_COMMAND_SUBST:
        case PART_ARITHMETIC:
            token->needs_expansion = true;
            // If not quoted, field splitting applies
            if (!part->was_single_quoted && !part->was_double_quoted)
            {
                token->needs_field_splitting = true;
            }
            break;

        case PART_TILDE:
            token->needs_expansion = true;
            break;

        case PART_LITERAL:
            if (!part->was_single_quoted && !part->was_double_quoted)
            {
                // crude check for glob characters
                const string_t *txt = part->text;
                if (txt != NULL && string_contains_glob(txt))
                {
                    token->needs_pathname_expansion = true;
                }
            }
            break;
        }
    }
}

void token_append_char_to_last_literal_part(token_t *token, char c)
{
    Expects_not_null(token);
    Expects_eq(token->type, TOKEN_WORD);
    Expects_not_null(token->parts);
    Expects_gt(token->parts->size, 0);

    part_t *last_part = token->parts->parts[token->parts->size - 1];

    Expects_not_null(last_part);
    Expects_eq(last_part->type, PART_LITERAL);

    string_append_char(last_part->text, c);
}

void token_append_cstr_to_last_literal_part(token_t *token, const char *str)
{
    Expects_not_null(token);
    Expects_eq(token->type, TOKEN_WORD);
    Expects_not_null(token->parts);
    Expects_gt(token->parts->size, 0);
    Expects_not_null(str);
    Expects_gt(strlen(str), 0);

    part_t *last_part = token->parts->parts[token->parts->size - 1];

    Expects_not_null(last_part);
    Expects_eq(last_part->type, PART_LITERAL);
    string_append_cstr(last_part->text, str);
}

/* ============================================================================
 * Reserved Word Recognition
 * ============================================================================ */

struct reserved_word_entry
{
    const char *word;
    token_type_t type;
};

static struct reserved_word_entry reserved_words[] = {
    {"if", TOKEN_IF},     {"then", TOKEN_THEN},   {"else", TOKEN_ELSE},   {"elif", TOKEN_ELIF},
    {"fi", TOKEN_FI},     {"do", TOKEN_DO},       {"done", TOKEN_DONE},   {"case", TOKEN_CASE},
    {"esac", TOKEN_ESAC}, {"while", TOKEN_WHILE}, {"until", TOKEN_UNTIL}, {"for", TOKEN_FOR},
    {"in", TOKEN_IN},     {"{", TOKEN_LBRACE},    {"}", TOKEN_RBRACE},    {"!", TOKEN_BANG},
    {NULL, TOKEN_WORD} // sentinel
};

bool token_try_promote_to_reserved_word(token_t *tok, bool allow_in)
{
    Expects_not_null(tok);
    Expects_eq(token_get_type(tok), TOKEN_WORD);

    // A reserved word must be a single literal part
    // and not quoted.
    if (token_was_quoted(tok) || token_part_count(tok) != 1)
        return false;

    const part_t *first_part = tok->parts->parts[0];

    // Only literal parts can be reserved words
    if (part_get_type(first_part) != PART_LITERAL)
        return false;

    const char *word = string_cstr(first_part->text);
    struct reserved_word_entry *p;
    token_type_t new_type = TOKEN_WORD;
    for (p = reserved_words; p->word != NULL; p++)
    {
        if (strcmp(word, p->word) == 0)
        {
            // Special case: "in" only in proper context
            if (p->type == TOKEN_IN && !allow_in)
                continue;
            new_type = p->type;
            break;
        }
    }
    if (new_type != TOKEN_WORD)
    {
        tok->type = new_type;
        // Now that we've specialized the token,
        // we no longer need the parts. Is it worth freeing them here?
        return true;
    }
    return false;
}

bool token_try_promote_to_bang(token_t *tok)
{
    Expects_not_null(tok);
    Expects_eq(token_get_type(tok), TOKEN_WORD);

    // A reserved word must be a single literal part
    // and not quoted.
    if (token_was_quoted(tok) || token_part_count(tok) != 1)
        return false;

    const part_t *first_part = tok->parts->parts[0];

    // Only literal parts can be reserved words
    if (part_get_type(first_part) != PART_LITERAL)
        return false;

    const char *word = string_cstr(first_part->text);
    if (strcmp(word, "!") == 0)
    {
        tok->type = TOKEN_BANG;
        part_list_destroy(&tok->parts);
        return true;
    }
    return false;
}

bool token_try_promote_to_lbrace(token_t *tok)
{
    Expects_not_null(tok);
    Expects_eq(token_get_type(tok), TOKEN_WORD);

    // A reserved word must be a single literal part
    // and not quoted.
    if (token_was_quoted(tok) || token_part_count(tok) != 1)
        return false;

    const part_t *first_part = tok->parts->parts[0];

    // Only literal parts can be reserved words
    if (part_get_type(first_part) != PART_LITERAL)
        return false;

    const char *word = string_cstr(first_part->text);
    if (strcmp(word, "{") == 0)
    {
        tok->type = TOKEN_LBRACE;
        part_list_destroy(&tok->parts);
        return true;
    }
    return false;
}


/* ============================================================================
 * Token Location Tracking
 * ============================================================================ */

void token_set_location(token_t *token, int first_line, int first_column, int last_line,
                        int last_column)
{
    Expects_not_null(token);

    token->first_line = first_line;
    token->first_column = first_column;
    token->last_line = last_line;
    token->last_column = last_column;
}

int token_get_first_line(const token_t *token)
{
    Expects_not_null(token);

    return token->first_line;
}

int token_get_first_column(const token_t *token)
{
    Expects_not_null(token);

    return token->first_column;
}

/* ============================================================================
 * Token Utility Functions
 * ============================================================================ */

const char *token_type_to_string(token_type_t type)
{
    switch (type)
    {
    case TOKEN_EOF:
        return "EOF";
    case TOKEN_WORD:
        return "WORD";
    case TOKEN_AND_IF:
        return "&&";
    case TOKEN_OR_IF:
        return "||";
    case TOKEN_DSEMI:
        return ";;";
    case TOKEN_DLESS:
        return "<<";
    case TOKEN_DGREAT:
        return ">>";
    case TOKEN_LESSAND:
        return "<&";
    case TOKEN_GREATAND:
        return ">&";
    case TOKEN_LESSGREAT:
        return "<>";
    case TOKEN_DLESSDASH:
        return "<<-";
    case TOKEN_CLOBBER:
        return ">|";
    case TOKEN_PIPE:
        return "|";
    case TOKEN_SEMI:
        return ";";
    case TOKEN_AMPER:
        return "&";
    case TOKEN_LPAREN:
        return "(";
    case TOKEN_RPAREN:
        return ")";
    case TOKEN_GREATER:
        return ">";
    case TOKEN_LESS:
        return "<";
    case TOKEN_IF:
        return "if";
    case TOKEN_THEN:
        return "then";
    case TOKEN_ELSE:
        return "else";
    case TOKEN_ELIF:
        return "elif";
    case TOKEN_FI:
        return "fi";
    case TOKEN_DO:
        return "do";
    case TOKEN_DONE:
        return "done";
    case TOKEN_CASE:
        return "case";
    case TOKEN_ESAC:
        return "esac";
    case TOKEN_WHILE:
        return "while";
    case TOKEN_UNTIL:
        return "until";
    case TOKEN_FOR:
        return "for";
    case TOKEN_IN:
        return "in";
    case TOKEN_BANG:
        return "!";
    case TOKEN_LBRACE:
        return "{";
    case TOKEN_RBRACE:
        return "}";
    case TOKEN_NEWLINE:
        return "NEWLINE";
    case TOKEN_IO_NUMBER:
        return "IO_NUMBER";
    case TOKEN_ASSIGNMENT_WORD:
        return "ASSIGNMENT_WORD";
    case TOKEN_REDIRECT:
        return "REDIRECT";
    case TOKEN_END_OF_HEREDOC:
        return "END_OF_HEREDOC";
    default:
        return "UNKNOWN";
    }
}

string_t *token_to_string(const token_t *token)
{
    Expects_not_null(token);

    string_t *result = string_create();

    string_append_cstr(result, "Token(");
    string_append_cstr(result, token_type_to_string(token->type));

    if (token->type == TOKEN_WORD && token->parts != NULL)
    {
        string_append_cstr(result, ", parts=[");
        for (int i = 0; i < token->parts->size; i++)
        {
            if (i > 0)
            {
                string_append_cstr(result, ", ");
            }
            string_t *part_str = part_to_string(token->parts->parts[i]);
            string_append(result, part_str);
            string_destroy(&part_str);
        }
        string_append_cstr(result, "]");
    }
    if (token->type == TOKEN_ASSIGNMENT_WORD)
    {
        string_append_cstr(result, ", name=");
        string_append(result, token->assignment_name);
        string_append_cstr(result, ", value=[");
        for (int i = 0; i < token->assignment_value->size; i++)
        {
            if (i > 0)
            {
                string_append_cstr(result, ", ");
            }
            string_t *part_str = part_to_string(token->assignment_value->parts[i]);
            string_append(result, part_str);
            string_destroy(&part_str);
        }
        string_append_cstr(result, "]");
    }

    string_append_cstr(result, ")");
    return result;
}

bool token_is_reserved_word(const char *word)
{
    Expects_not_null(word);

    return (strcmp(word, "if") == 0 || strcmp(word, "then") == 0 || strcmp(word, "else") == 0 ||
            strcmp(word, "elif") == 0 || strcmp(word, "fi") == 0 || strcmp(word, "do") == 0 ||
            strcmp(word, "done") == 0 || strcmp(word, "case") == 0 || strcmp(word, "esac") == 0 ||
            strcmp(word, "while") == 0 || strcmp(word, "until") == 0 || strcmp(word, "for") == 0 ||
            strcmp(word, "in") == 0 || strcmp(word, "{") == 0 || strcmp(word, "}") == 0 ||
            strcmp(word, "!") == 0);
}

token_type_t token_string_to_reserved_word(const char *word)
{
    Expects_not_null(word);

    if (strcmp(word, "if") == 0)
        return TOKEN_IF;
    if (strcmp(word, "then") == 0)
        return TOKEN_THEN;
    if (strcmp(word, "else") == 0)
        return TOKEN_ELSE;
    if (strcmp(word, "elif") == 0)
        return TOKEN_ELIF;
    if (strcmp(word, "fi") == 0)
        return TOKEN_FI;
    if (strcmp(word, "do") == 0)
        return TOKEN_DO;
    if (strcmp(word, "done") == 0)
        return TOKEN_DONE;
    if (strcmp(word, "case") == 0)
        return TOKEN_CASE;
    if (strcmp(word, "esac") == 0)
        return TOKEN_ESAC;
    if (strcmp(word, "while") == 0)
        return TOKEN_WHILE;
    if (strcmp(word, "until") == 0)
        return TOKEN_UNTIL;
    if (strcmp(word, "for") == 0)
        return TOKEN_FOR;
    if (strcmp(word, "in") == 0)
        return TOKEN_IN;
    if (strcmp(word, "{") == 0)
        return TOKEN_LBRACE;
    if (strcmp(word, "}") == 0)
        return TOKEN_RBRACE;
    if (strcmp(word, "!") == 0)
        return TOKEN_BANG;

    return TOKEN_WORD;
}

bool token_is_operator(const char *str)
{
    Expects_not_null(str);

    return (strcmp(str, "&&") == 0 || strcmp(str, "||") == 0 || strcmp(str, ";;") == 0 ||
            strcmp(str, "<<") == 0 || strcmp(str, ">>") == 0 || strcmp(str, "<&") == 0 ||
            strcmp(str, ">&") == 0 || strcmp(str, "<>") == 0 || strcmp(str, "<<-") == 0 ||
            strcmp(str, ">|") == 0 || strcmp(str, "|") == 0 || strcmp(str, ";") == 0 ||
            strcmp(str, "&") == 0 || strcmp(str, "(") == 0 || strcmp(str, ")") == 0 ||
            strcmp(str, ">") == 0 || strcmp(str, "<") == 0);
}

token_type_t token_string_to_operator(const char *str)
{
    Expects_not_null(str);

    if (strcmp(str, "&&") == 0)
        return TOKEN_AND_IF;
    if (strcmp(str, "||") == 0)
        return TOKEN_OR_IF;
    if (strcmp(str, ";;") == 0)
        return TOKEN_DSEMI;
    if (strcmp(str, "<<") == 0)
        return TOKEN_DLESS;
    if (strcmp(str, ">>") == 0)
        return TOKEN_DGREAT;
    if (strcmp(str, "<&") == 0)
        return TOKEN_LESSAND;
    if (strcmp(str, ">&") == 0)
        return TOKEN_GREATAND;
    if (strcmp(str, "<>") == 0)
        return TOKEN_LESSGREAT;
    if (strcmp(str, "<<-") == 0)
        return TOKEN_DLESSDASH;
    if (strcmp(str, ">|") == 0)
        return TOKEN_CLOBBER;
    if (strcmp(str, "|") == 0)
        return TOKEN_PIPE;
    if (strcmp(str, ";") == 0)
        return TOKEN_SEMI;
    if (strcmp(str, "&") == 0)
        return TOKEN_AMPER;
    if (strcmp(str, "(") == 0)
        return TOKEN_LPAREN;
    if (strcmp(str, ")") == 0)
        return TOKEN_RPAREN;
    if (strcmp(str, ">") == 0)
        return TOKEN_GREATER;
    if (strcmp(str, "<") == 0)
        return TOKEN_LESS;

    return TOKEN_EOF;
}

/* ============================================================================
 * Part Lifecycle Functions
 * ============================================================================ */

part_t *part_create_literal(const string_t *text)
{
    Expects_not_null(text);

    part_t *part = (part_t *)xcalloc(1, sizeof(part_t));

    part->type = PART_LITERAL;
    part->text = string_create_from(text);
    return part;
}

part_t *part_create_parameter(const string_t *param_name)
{
    Expects_not_null(param_name);

    part_t *part = (part_t *)xcalloc(1, sizeof(part_t));
    part->type = PART_PARAMETER;
    part->param_name = string_create_from(param_name);

    return part;
}

part_t *part_create_command_subst(const string_t *expr_text)
{
    Expects_not_null(expr_text);

    part_t *part = (part_t *)xcalloc(1, sizeof(part_t));
    part->type = PART_COMMAND_SUBST;
    part->text = string_create_from(expr_text);
    return part;
}

part_t *part_create_arithmetic(const string_t *expr_text)
{
    Expects_not_null(expr_text);

    part_t *part = (part_t *)xcalloc(1, sizeof(part_t));

    part->type = PART_ARITHMETIC;
    part->text = string_create_from(expr_text);
    return part;
}

part_t *part_create_tilde(const string_t *text)
{
    Expects_not_null(text);

    part_t *part = (part_t *)xcalloc(1, sizeof(part_t));
    part->type = PART_TILDE;
    part->text = string_create_from(text);
    return part;
}

void part_destroy(part_t **part)
{
    Expects_not_null(part);
    part_t *p = *part;
    Expects_not_null(p);

    if (p->word != NULL)
        string_destroy(&p->word);
    if (p->text != NULL)
        string_destroy(&p->text);
    if (p->param_name != NULL)
        string_destroy(&p->param_name);
    if (p->nested != NULL)
        token_list_destroy(&p->nested);

    xfree(p);
    *part = NULL;
}

/* ============================================================================
 * Part Accessors
 * ============================================================================ */

part_type_t part_get_type(const part_t *part)
{
    Expects_not_null(part);
    return part->type;
}

const string_t *part_get_text(const part_t *part)
{
    Expects_not_null(part);
    return part->text;
}

const string_t *part_get_param_name(const part_t *part)
{
    Expects_not_null(part);
    return part->param_name;
}

token_list_t *part_get_nested(const part_t *part)
{
    Expects_not_null(part);
    return part->nested;
}

bool part_was_single_quoted(const part_t *part)
{
    Expects_not_null(part);
    return part->was_single_quoted;
}

bool part_was_double_quoted(const part_t *part)
{
    Expects_not_null(part);
    return part->was_double_quoted;
}

void part_set_quoted(part_t *part, bool single_quoted, bool double_quoted)
{
    Expects_not_null(part);
    part->was_single_quoted = single_quoted;
    part->was_double_quoted = double_quoted;
}

/* ============================================================================
 * Part Utility Functions
 * ============================================================================ */

const char *part_type_to_string(part_type_t type)
{
    switch (type)
    {
    case PART_LITERAL:
        return "LITERAL";
    case PART_PARAMETER:
        return "PARAMETER";
    case PART_COMMAND_SUBST:
        return "COMMAND_SUBST";
    case PART_ARITHMETIC:
        return "ARITHMETIC";
    case PART_TILDE:
        return "TILDE";
    default:
        return "UNKNOWN";
    }
}

static char c0ctl[32][4] = {
    "NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL", "BS", "HT", "LF", "VT",
    "FF", "CR", "SO", "SI", "DLE", "DC1", "DC2", "DC3", "DC4", "NAK", "SYN", "ETB",
    "CAN", "EM", "SUB", "ESC"
};

static void string_append_escaped_string(string_t *str, const string_t *text)
{
    Expects_not_null(str);
    Expects_not_null(text);

    for (int i = 0; i < string_length(text); i++)
    {
        char c = string_at(text, i);
        switch (c)
        {
        case '\n':
            string_append_cstr(str, "\\n");
            break;
        case '\t':
            string_append_cstr(str, "\\t");
            break;
        case '\r':
            string_append_cstr(str, "\\r");
            break;
        case '\b':
            string_append_cstr(str, "\\b");
            break;
        case '\a':
            string_append_cstr(str, "\\a");
            break;
        case '\f':
            string_append_cstr(str, "\\f");
            break;
        case '\\':
            string_append_cstr(str, "\\\\");
            break;
        default:
            if (c < ' ')
            {
                string_append_char(str, '<');
                string_append_cstr(str, c0ctl[(int)c]);
                string_append_char(str, '>');
            }
            else
            string_append_char(str, c);
            break;
        }
    }
}

string_t *part_to_string(const part_t *part)
{
    Expects_not_null(part);

    string_t *result = string_create();
    string_append_cstr(result, part_type_to_string(part->type));
    string_append_cstr(result, "(");

    switch (part->type)
    {
    case PART_LITERAL:
    case PART_TILDE:
        if (part->text != NULL)
        {
            string_append_cstr(result, "\"");
            string_append_escaped_string(result, part->text);
            string_append_cstr(result, "\"");
        }
        break;
    case PART_PARAMETER:
        if (part->param_name != NULL)
        {
            string_append_cstr(result, "${");
            string_append_escaped_string(result, part->param_name);
            string_append_cstr(result, "}");
        }
        break;
    case PART_COMMAND_SUBST:
        string_append_cstr(result, "$(...)");
        break;
    case PART_ARITHMETIC:
        string_append_cstr(result, "$((...))");
        break;
    }

    string_append_cstr(result, ")");
    return result;
}

/* ============================================================================
 * Part List Functions
 * ============================================================================ */

part_list_t *part_list_create(void)
{
    part_list_t *list = (part_list_t *)xcalloc(1, sizeof(part_list_t));

    list->parts = (part_t **)xcalloc(INITIAL_LIST_CAPACITY, sizeof(part_t *));
    list->size = 0;
    list->capacity = INITIAL_LIST_CAPACITY;

    return list;
}

void part_list_destroy(part_list_t **list)
{
    Expects_not_null(list);
    part_list_t *l = *list;
    Expects_not_null(l);
    Expects_not_null(l->parts);

    for (int i = 0; i < l->size; i++)
    {
        part_destroy(&l->parts[i]);
    }

    xfree(l->parts);
    xfree(l);
    *list = NULL;
}

int part_list_append(part_list_t *list, part_t *part)
{
    Expects_not_null(list);
    Expects_not_null(part);

    if (list->size >= list->capacity)
    {
        int new_capacity = list->capacity * 2;
        part_t **new_parts = (part_t **)xrealloc(list->parts, new_capacity * sizeof(part_t *));
        list->parts = new_parts;
        list->capacity = new_capacity;
    }

    list->parts[list->size++] = part;
    return 0;
}

int part_list_size(const part_list_t *list)
{
    Expects_not_null(list);
    return list->size;
}

part_t *part_list_get(const part_list_t *list, int index)
{
    Expects_not_null(list);
    Expects_not_null(list->parts);
    Expects_lt(index, list->size);

    return list->parts[index];
}

int part_list_remove(part_list_t *list, int index)
{
    Expects_not_null(list);
    Expects_not_null(list->parts);
    Expects_lt(index, list->size);

    part_destroy(&list->parts[index]);
    // Shift remaining parts down
    for (int i = index; i < list->size - 1; i++)
    {
        list->parts[i] = list->parts[i + 1];
    }

    list->size--;
    return 0;
}

void part_list_reinitialize(part_list_t *list)
{
    Expects_not_null(list);

    // Destroy all existing parts
    for (int i = 0; i < list->size; i++)
    {
        part_destroy(&list->parts[i]);
    }

    // Free the old backing array
    xfree(list->parts);
    list->parts = NULL;

    // Allocate a fresh array with initial capacity
    list->parts = (part_t **)xcalloc(INITIAL_LIST_CAPACITY, sizeof(part_t *));
    list->size = 0;
    list->capacity = INITIAL_LIST_CAPACITY;
}

/* ============================================================================
 * Token List Functions
 * ============================================================================ */

token_list_t *token_list_create(void)
{
    token_list_t *list = (token_list_t *)xcalloc(1, sizeof(token_list_t));
    list->tokens = (token_t **)xcalloc(INITIAL_LIST_CAPACITY, sizeof(token_t *));
    list->size = 0;
    list->capacity = INITIAL_LIST_CAPACITY;

    return list;
}

void token_list_destroy(token_list_t **list)
{
    Expects_not_null(list);
    token_list_t *l = *list;
    Expects_not_null(l);
    Expects_not_null(l->tokens);

    for (int i = 0; i < l->size; i++)
    {
        token_destroy(&l->tokens[i]);
    }

    xfree(l->tokens);
    xfree(l);
    *list = NULL;
}

int token_list_append(token_list_t *list, token_t *token)
{
    Expects_not_null(list);
    Expects_not_null(token);

    if (list->size >= list->capacity)
    {
        int new_capacity = list->capacity * 2;
        token_t **new_tokens = (token_t **)xrealloc(list->tokens, new_capacity * sizeof(token_t *));
        list->tokens = new_tokens;
        list->capacity = new_capacity;
    }

    list->tokens[list->size++] = token;
    return 0;
}

int token_list_size(const token_list_t *list)
{
    Expects_not_null(list);

    return list->size;
}

token_t *token_list_get(const token_list_t *list, int index)
{
    Expects_not_null(list);
    Expects_not_null(list->tokens);
    Expects_lt(index, list->size);

    return list->tokens[index];
}

int token_list_remove(token_list_t *list, int index)
{
    Expects_not_null(list);
    Expects_not_null(list->tokens);
    Expects_lt(index, list->size);

    token_destroy(&list->tokens[index]);

    // Shift remaining tokens down
    for (int i = index; i < list->size - 1; i++)
    {
        list->tokens[i] = list->tokens[i + 1];
    }

    list->size--;
    return 0;
}

token_t *token_list_get_last(const token_list_t *list)
{
    Expects_not_null(list);
    Expects_not_null(list->tokens);
    Expects_gt(list->size, 0);

    return list->tokens[list->size - 1];
}

void token_list_clear(token_list_t *list)
{
    Expects_not_null(list);

    // Destroy all existing tokens
    for (int i = 0; i < list->size; i++)
    {
        token_destroy(&list->tokens[i]);
    }

    // Free the old backing array
    xfree(list->tokens);
    list->tokens = NULL;

    // Allocate a fresh array with initial capacity
    list->tokens = (token_t **)xcalloc(INITIAL_LIST_CAPACITY, sizeof(token_t *));
    list->size = 0;
    list->capacity = INITIAL_LIST_CAPACITY;
}

void token_list_release_tokens(token_list_t *list)
{
    Expects_not_null(list);

    // Clear all token pointers without destroying them
    for (int i = 0; i < list->size; i++)
    {
        list->tokens[i] = NULL;
    }
    list->size = 0;
}

token_t **token_list_release(token_list_t *list, int *out_size)
{
    Expects_not_null(list);

    if (list->size == 0)
    {
        if (out_size)
            *out_size = 0;
        return NULL;
    }

    token_t **tokens = list->tokens;
    if (out_size)
        *out_size = list->size;

    // Reset the list to empty state
    list->tokens = (token_t **)xcalloc(INITIAL_LIST_CAPACITY, sizeof(token_t *));
    list->size = 0;
    list->capacity = INITIAL_LIST_CAPACITY;

    return tokens;
}

int token_list_ensure_capacity(token_list_t *list, int needed_capacity)
{
    Expects_not_null(list);

    if (needed_capacity <= list->capacity)
        return 0;

    // Grow capacity to at least needed_capacity
    int new_capacity = list->capacity;
    while (new_capacity < needed_capacity)
    {
        // Check for overflow before doubling
        // We can safely double if capacity <= INT_MAX/2
        if (new_capacity <= INT_MAX / 2)
        {
            new_capacity *= 2;
        }
        else
        {
            // Can't double, try to allocate exactly what's needed
            if (needed_capacity > INT_MAX)
                return -1;
            new_capacity = needed_capacity;
            break;
        }
    }

    token_t **new_tokens = (token_t **)xrealloc(list->tokens, new_capacity * sizeof(token_t *));
    if (new_tokens == NULL)
        return -1;

    list->tokens = new_tokens;
    list->capacity = new_capacity;
    return 0;
}

int token_list_insert_range(token_list_t *list, int index, token_t **tokens, int count)
{
    Expects_not_null(list);
    Expects_not_null(tokens);
    Expects_le(index, list->size);

    if (count <= 0)
        return 0;

    // Ensure we have enough capacity
    int needed_capacity = list->size + count;
    if (token_list_ensure_capacity(list, needed_capacity) != 0)
        return -1;

    // Shift existing tokens to make room
    for (int i = list->size - 1; i >= index; i--)
    {
        list->tokens[i + count] = list->tokens[i];
    }

    // Insert the new tokens
    for (int i = 0; i < count; i++)
    {
        list->tokens[index + i] = tokens[i];
    }
    list->size += count;

    return 0;
}

string_t *token_list_to_string(const token_list_t *list)
{
    Expects_not_null(list);

    string_t *result = string_create();
    string_append_cstr(result, "TokenList[\n");
    for (int i = 0; i < list->size; i++)
    {
        string_append_cstr(result, "  ");
        string_t *token_str = token_to_string(list->tokens[i]);
        string_append(result, token_str);
        string_destroy(&token_str);
        if (i + 1 < list->size)
            string_append_cstr(result, ",\n");
        else
            string_append_cstr(result, "\n");
    }

    string_append_cstr(result, "]");
    return result;
}

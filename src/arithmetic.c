#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "alias_store.h"
#include "arithmetic.h"
#include "exec_expander.h"
#include "exec_frame.h"
#include "lexer.h"
#include "logging.h"
#include "parser.h"
#include "string_t.h"
#include "string_list.h"
#include "tokenizer.h"
#include "variable_store.h"
#include "xalloc.h"

// Ignore warning 4061: enumerator in switch of enum is not explicitly handled by a case label
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4061)
#endif

typedef struct {
    string_t *input;
    int pos;
    int padding;
    exec_frame_t *frame;
} math_parser_t;

// Token types for arithmetic lexer
typedef enum {
    MATH_TOKEN_NUMBER,
    MATH_TOKEN_VARIABLE,
    MATH_TOKEN_LPAREN,
    MATH_TOKEN_RPAREN,
    MATH_TOKEN_PLUS,
    MATH_TOKEN_MINUS,
    MATH_TOKEN_MULTIPLY,
    MATH_TOKEN_DIVIDE,
    MATH_TOKEN_MODULO,
    MATH_TOKEN_BIT_NOT,
    MATH_TOKEN_LOGICAL_NOT,
    MATH_TOKEN_LEFT_SHIFT,
    MATH_TOKEN_RIGHT_SHIFT,
    MATH_TOKEN_LESS,
    MATH_TOKEN_GREATER,
    MATH_TOKEN_LESS_EQUAL,
    MATH_TOKEN_GREATER_EQUAL,
    MATH_TOKEN_EQUAL,
    MATH_TOKEN_NOT_EQUAL,
    MATH_TOKEN_BIT_AND,
    MATH_TOKEN_BIT_XOR,
    MATH_TOKEN_BIT_OR,
    MATH_TOKEN_LOGICAL_AND,
    MATH_TOKEN_LOGICAL_OR,
    MATH_TOKEN_QUESTION,
    MATH_TOKEN_COLON,
    MATH_TOKEN_ASSIGN,
    MATH_TOKEN_MULTIPLY_ASSIGN,
    MATH_TOKEN_DIVIDE_ASSIGN,
    MATH_TOKEN_MODULO_ASSIGN,
    MATH_TOKEN_PLUS_ASSIGN,
    MATH_TOKEN_MINUS_ASSIGN,
    MATH_TOKEN_LEFT_SHIFT_ASSIGN,
    MATH_TOKEN_RIGHT_SHIFT_ASSIGN,
    MATH_TOKEN_AND_ASSIGN,
    MATH_TOKEN_XOR_ASSIGN,
    MATH_TOKEN_OR_ASSIGN,
    MATH_TOKEN_COMMA,
    MATH_TOKEN_EOF
} math_token_type_t;

typedef struct {
    math_token_type_t type;
    long number;    // For MATH_TOKEN_NUMBER
    string_t *variable; // For MATH_TOKEN_VARIABLE (caller frees)
} math_token_t;

static bool is_alpha_or_underscore(char c) {
    return isalpha(c) || c == '_';
}

static bool is_alnum_or_underscore(char c) {
    return isalnum(c) || c == '_';
}

// Initialize parser
static void parser_init(math_parser_t *parser, exec_frame_t *frame, const string_t *input) {
    parser->input = string_create_from(input);
    parser->pos = 0;
    parser->frame = frame;
}

// Clean up parser resources
static void parser_cleanup(math_parser_t *parser) {
    if (parser && parser->input) {
        string_destroy(&parser->input);
        parser->input = NULL;
    }
}

// Skip whitespace
static void skip_whitespace(math_parser_t *parser) {
    while (parser->pos < string_length(parser->input) && isspace(string_at(parser->input, parser->pos))) {
        parser->pos++;
    }
}

// Get next token
static math_token_t get_token(math_parser_t *parser) {
    skip_whitespace(parser);
    math_token_t token = {0};

    if (string_length(parser->input) == parser->pos) {
        token.type = MATH_TOKEN_EOF;
        return token;
    }

    char c = string_at(parser->input, parser->pos);
    if (isdigit(c)) {
        // POSIX requires decimal, octal (0 prefix), and hexadecimal (0x/0X prefix)
        long value = 0;
        int len = string_length(parser->input);

        if (c == '0' && parser->pos + 1 < len) {
            char next = string_at(parser->input, parser->pos + 1);
            if (next == 'x' || next == 'X') {
                // Hexadecimal: 0x or 0X prefix
                parser->pos += 2;
                while (parser->pos < len) {
                    char h = string_at(parser->input, parser->pos);
                    if (h >= '0' && h <= '9') {
                        value = value * 16 + (h - '0');
                    } else if (h >= 'a' && h <= 'f') {
                        value = value * 16 + (h - 'a' + 10);
                    } else if (h >= 'A' && h <= 'F') {
                        value = value * 16 + (h - 'A' + 10);
                    } else {
                        break;
                    }
                    parser->pos++;
                }
            } else if (next >= '0' && next <= '7') {
                // Octal: leading 0
                parser->pos++;
                while (parser->pos < len) {
                    char o = string_at(parser->input, parser->pos);
                    if (o >= '0' && o <= '7') {
                        value = value * 8 + (o - '0');
                    } else {
                        break;
                    }
                    parser->pos++;
                }
            } else {
                // Just a single 0
                parser->pos++;
            }
        } else {
            // Decimal
            while (parser->pos < len) {
                char d = string_at(parser->input, parser->pos);
                if (d >= '0' && d <= '9') {
                    value = value * 10 + (d - '0');
                } else {
                    break;
                }
                parser->pos++;
            }
        }

        token.type = MATH_TOKEN_NUMBER;
        token.number = value;
        return token;
    }

    if (isalpha(c) || c == '_')
    {
        int endpos = string_find_first_not_of_predicate_at(parser->input, is_alnum_or_underscore,
                                                           parser->pos);
        token.type = MATH_TOKEN_VARIABLE;
        token.variable = string_substring(parser->input, parser->pos, endpos); // token owns it
        parser->pos = endpos;
        return token;
    }

    parser->pos++;
    switch (c) {
        case '(': token.type = MATH_TOKEN_LPAREN; break;
        case ')': token.type = MATH_TOKEN_RPAREN; break;
        case '+':
            if (string_at(parser->input, parser->pos) == '=') {
                token.type = MATH_TOKEN_PLUS_ASSIGN;
                parser->pos++;
            } else {
                token.type = MATH_TOKEN_PLUS;
            }
            break;
        case '-':
            if (string_at(parser->input, parser->pos) == '=') {
                token.type = MATH_TOKEN_MINUS_ASSIGN;
                parser->pos++;
            } else {
                token.type = MATH_TOKEN_MINUS;
            }
            break;
        case '*':
            if (string_at(parser->input, parser->pos) == '=') {
                token.type = MATH_TOKEN_MULTIPLY_ASSIGN;
                parser->pos++;
            } else {
                token.type = MATH_TOKEN_MULTIPLY;
            }
            break;
        case '/':
            if (string_at(parser->input, parser->pos) == '=') {
                token.type = MATH_TOKEN_DIVIDE_ASSIGN;
                parser->pos++;
            } else {
                token.type = MATH_TOKEN_DIVIDE;
            }
            break;
        case '%':
            if (string_at(parser->input, parser->pos) == '=') {
                token.type = MATH_TOKEN_MODULO_ASSIGN;
                parser->pos++;
            } else {
                token.type = MATH_TOKEN_MODULO;
            }
            break;
        case '~': token.type = MATH_TOKEN_BIT_NOT; break;
        case '!':
            if (string_at(parser->input, parser->pos) == '=') {
                token.type = MATH_TOKEN_NOT_EQUAL;
                parser->pos++;
            } else {
                token.type = MATH_TOKEN_LOGICAL_NOT;
            }
            break;
        case '<':
            if (string_at(parser->input, parser->pos) == '<') {
                parser->pos++;
                if (string_at(parser->input, parser->pos) == '=') {
                    token.type = MATH_TOKEN_LEFT_SHIFT_ASSIGN;
                    parser->pos++;
                } else {
                    token.type = MATH_TOKEN_LEFT_SHIFT;
                }
            } else if (string_at(parser->input, parser->pos) == '=') {
                token.type = MATH_TOKEN_LESS_EQUAL;
                parser->pos++;
            } else {
                token.type = MATH_TOKEN_LESS;
            }
            break;
        case '>':
            if (string_at(parser->input, parser->pos) == '>') {
                parser->pos++;
                if (string_at(parser->input, parser->pos) == '=') {
                    token.type = MATH_TOKEN_RIGHT_SHIFT_ASSIGN;
                    parser->pos++;
                } else {
                    token.type = MATH_TOKEN_RIGHT_SHIFT;
                }
            } else if (string_at(parser->input, parser->pos) == '=') {
                token.type = MATH_TOKEN_GREATER_EQUAL;
                parser->pos++;
            } else {
                token.type = MATH_TOKEN_GREATER;
            }
            break;
        case '=':
            if (string_at(parser->input, parser->pos) == '=') {
                token.type = MATH_TOKEN_EQUAL;
                parser->pos++;
            } else {
                token.type = MATH_TOKEN_ASSIGN;
            }
            break;
        case '&':
            if (string_at(parser->input, parser->pos) == '&') {
                token.type = MATH_TOKEN_LOGICAL_AND;
                parser->pos++;
            } else if (string_at(parser->input, parser->pos) == '=') {
                token.type = MATH_TOKEN_AND_ASSIGN;
                parser->pos++;
            } else {
                token.type = MATH_TOKEN_BIT_AND;
            }
            break;
        case '^':
            if (string_at(parser->input, parser->pos) == '=') {
                token.type = MATH_TOKEN_XOR_ASSIGN;
                parser->pos++;
            } else {
                token.type = MATH_TOKEN_BIT_XOR;
            }
            break;
        case '|':
            if (string_at(parser->input, parser->pos) == '|') {
                token.type = MATH_TOKEN_LOGICAL_OR;
                parser->pos++;
            } else if (string_at(parser->input, parser->pos) == '=') {
                token.type = MATH_TOKEN_OR_ASSIGN;
                parser->pos++;
            } else {
                token.type = MATH_TOKEN_BIT_OR;
            }
            break;
        case '?': token.type = MATH_TOKEN_QUESTION; break;
        case ':': token.type = MATH_TOKEN_COLON; break;
        case ',': token.type = MATH_TOKEN_COMMA; break;
        default:
            token.type = MATH_TOKEN_EOF; // Invalid character
            break;
    }
    return token;
}

// Free token
static void free_token(math_token_t *token) {
    if (token->type == MATH_TOKEN_VARIABLE && token->variable) {
        xfree(token->variable);
    }
}

// Parse and evaluate expression
static ArithmeticResult parse_expression(math_parser_t *parser, int min_precedence);
static ArithmeticResult parse_primary(math_parser_t *parser);

// Helper to create error result
static ArithmeticResult make_error(const char *msg) {
    ArithmeticResult result = {0};
    result.failed = 1;
    result.error = string_create_from_cstr(msg);
    return result;
}

// Helper to create value result
static ArithmeticResult make_value(long value) {
    ArithmeticResult result = {0};
    result.value = value;
    result.failed = 0;
    return result;
}

// Get operator precedence (higher number = higher precedence)
static int get_precedence(math_token_type_t type) {
    switch (type) {
        case MATH_TOKEN_COMMA:
            return 1;
        case MATH_TOKEN_QUESTION:
            return 3;
        case MATH_TOKEN_LOGICAL_OR:
            return 4;
        case MATH_TOKEN_LOGICAL_AND:
            return 5;
        case MATH_TOKEN_BIT_OR:
            return 6;
        case MATH_TOKEN_BIT_XOR:
            return 7;
        case MATH_TOKEN_BIT_AND:
            return 8;
        case MATH_TOKEN_EQUAL:
        case MATH_TOKEN_NOT_EQUAL:
            return 9;
        case MATH_TOKEN_LESS:
        case MATH_TOKEN_GREATER:
        case MATH_TOKEN_LESS_EQUAL:
        case MATH_TOKEN_GREATER_EQUAL:
            return 10;
        case MATH_TOKEN_LEFT_SHIFT:
        case MATH_TOKEN_RIGHT_SHIFT:
            return 11;
        case MATH_TOKEN_PLUS:
        case MATH_TOKEN_MINUS:
            return 12;
        case MATH_TOKEN_MULTIPLY:
        case MATH_TOKEN_DIVIDE:
        case MATH_TOKEN_MODULO:
            return 13;
        default:
            return 0;
    }
}

// Check if operator is right-associative
static bool is_right_associative(math_token_type_t type) {
    return type == MATH_TOKEN_QUESTION;
}

// Unified expression parser using precedence climbing
// Handles unary operators, binary operators, ternary operator, and comma operator
static ArithmeticResult parse_expression(math_parser_t *parser, int min_precedence) {
    ArithmeticResult left;
    
    // Handle unary operators (prefix)
    int saved_pos = parser->pos;
    math_token_t token = get_token(parser);
    if (token.type == MATH_TOKEN_PLUS || token.type == MATH_TOKEN_MINUS ||
        token.type == MATH_TOKEN_BIT_NOT || token.type == MATH_TOKEN_LOGICAL_NOT) {
        math_token_type_t unary_op = token.type;
        free_token(&token);
        
        ArithmeticResult expr = parse_expression(parser, 14); // Unary has highest precedence
        if (expr.failed) return expr;

        switch (unary_op) {
            case MATH_TOKEN_PLUS:        /* No-op */ break;
            case MATH_TOKEN_MINUS:       expr.value = -expr.value; break;
            case MATH_TOKEN_BIT_NOT:     expr.value = ~expr.value; break;
            case MATH_TOKEN_LOGICAL_NOT: expr.value = !expr.value; break;
            default: break;
        }
        left = expr;
    } else {
        // Not a unary operator, rewind and parse primary
        parser->pos = saved_pos;
        free_token(&token);
        left = parse_primary(parser);
        if (left.failed) return left;
    }

    // Handle binary and ternary operators using precedence climbing
    while (1) {
        saved_pos = parser->pos;
        token = get_token(parser);
        int prec = get_precedence(token.type);
        
        if (prec < min_precedence) {
            parser->pos = saved_pos;
            free_token(&token);
            break;
        }

        math_token_type_t op = token.type;
        free_token(&token);

        // Special handling for comma operator
        if (op == MATH_TOKEN_COMMA) {
            int next_min_prec = is_right_associative(op) ? prec : prec + 1;
            ArithmeticResult right = parse_expression(parser, next_min_prec);
            if (right.failed) {
                arithmetic_result_free(&left);
                return right;
            }
            // Comma operator: evaluate both, discard right, return left
            arithmetic_result_free(&right);
            continue;
        }

        // Special handling for ternary operator
        if (op == MATH_TOKEN_QUESTION) {
            ArithmeticResult true_expr = parse_expression(parser, 0); // Allow comma in branches
            if (true_expr.failed) {
                arithmetic_result_free(&left);
                return true_expr;
            }

            token = get_token(parser);
            if (token.type != MATH_TOKEN_COLON) {
                free_token(&token);
                arithmetic_result_free(&left);
                arithmetic_result_free(&true_expr);
                return make_error("Expected ':' in ternary expression");
            }
            free_token(&token);

            ArithmeticResult false_expr = parse_expression(parser, prec); // Right-associative
            if (false_expr.failed) {
                arithmetic_result_free(&left);
                arithmetic_result_free(&true_expr);
                return false_expr;
            }

            ArithmeticResult result = left.value ? true_expr : false_expr;
            arithmetic_result_free(&left);
            arithmetic_result_free(left.value ? &false_expr : &true_expr);
            left = result;
            continue;
        }

        // Handle short-circuit evaluation for logical operators
        if (op == MATH_TOKEN_LOGICAL_OR && left.value) {
            return left;
        }
        if (op == MATH_TOKEN_LOGICAL_AND && !left.value) {
            return left;
        }

        // Standard binary operators
        int next_min_prec = is_right_associative(op) ? prec : prec + 1;
        ArithmeticResult right = parse_expression(parser, next_min_prec);
        if (right.failed) {
            arithmetic_result_free(&left);
            return right;
        }

        // Apply the operator
        switch (op) {
            case MATH_TOKEN_MULTIPLY:
                left.value *= right.value;
                break;
            case MATH_TOKEN_DIVIDE:
                if (right.value == 0) {
                    arithmetic_result_free(&left);
                    arithmetic_result_free(&right);
                    return make_error("Division by zero");
                }
                left.value /= right.value;
                break;
            case MATH_TOKEN_MODULO:
                if (right.value == 0) {
                    arithmetic_result_free(&left);
                    arithmetic_result_free(&right);
                    return make_error("Modulo by zero");
                }
                left.value %= right.value;
                break;
            case MATH_TOKEN_PLUS:
                left.value += right.value;
                break;
            case MATH_TOKEN_MINUS:
                left.value -= right.value;
                break;
            case MATH_TOKEN_LEFT_SHIFT:
                left.value <<= right.value;
                break;
            case MATH_TOKEN_RIGHT_SHIFT:
                left.value >>= right.value;
                break;
            case MATH_TOKEN_LESS:
                left.value = left.value < right.value;
                break;
            case MATH_TOKEN_GREATER:
                left.value = left.value > right.value;
                break;
            case MATH_TOKEN_LESS_EQUAL:
                left.value = left.value <= right.value;
                break;
            case MATH_TOKEN_GREATER_EQUAL:
                left.value = left.value >= right.value;
                break;
            case MATH_TOKEN_EQUAL:
                left.value = left.value == right.value;
                break;
            case MATH_TOKEN_NOT_EQUAL:
                left.value = left.value != right.value;
                break;
            case MATH_TOKEN_BIT_AND:
                left.value &= right.value;
                break;
            case MATH_TOKEN_BIT_XOR:
                left.value ^= right.value;
                break;
            case MATH_TOKEN_BIT_OR:
                left.value |= right.value;
                break;
            case MATH_TOKEN_LOGICAL_AND:
                left.value = left.value && right.value;
                break;
            case MATH_TOKEN_LOGICAL_OR:
                left.value = left.value || right.value;
                break;
            default:
                arithmetic_result_free(&left);
                arithmetic_result_free(&right);
                return make_error("Unknown binary operator");
        }
        
        arithmetic_result_free(&right);
    }
    return left;
}

// Primary (number, variable, parenthesized expression, assignment)
static ArithmeticResult parse_primary(math_parser_t *parser)
{
    math_token_t token = get_token(parser);

    if (token.type == MATH_TOKEN_NUMBER)
    {
        return make_value(token.number);
    }

    if (token.type == MATH_TOKEN_VARIABLE)
    {
        // Peek ahead to see if an assignment operator follows
        int saved_pos = parser->pos;
        math_token_t next_token = get_token(parser);
        parser->pos = saved_pos;
        free_token(&next_token);

        // If not followed by assignment operator, treat as variable read
        if (next_token.type != MATH_TOKEN_ASSIGN && next_token.type != MATH_TOKEN_MULTIPLY_ASSIGN &&
            next_token.type != MATH_TOKEN_DIVIDE_ASSIGN &&
            next_token.type != MATH_TOKEN_MODULO_ASSIGN &&
            next_token.type != MATH_TOKEN_PLUS_ASSIGN &&
            next_token.type != MATH_TOKEN_MINUS_ASSIGN &&
            next_token.type != MATH_TOKEN_LEFT_SHIFT_ASSIGN &&
            next_token.type != MATH_TOKEN_RIGHT_SHIFT_ASSIGN &&
            next_token.type != MATH_TOKEN_AND_ASSIGN && next_token.type != MATH_TOKEN_XOR_ASSIGN &&
            next_token.type != MATH_TOKEN_OR_ASSIGN)
        {
            // Variable read
            const string_t *value = exec_frame_get_variable(parser->frame, token.variable);
            long num = value ? string_atol(value) : 0;
            free_token(&token);
            return make_value(num);
        }

        // Assignment: save variable name, then consume the operator
        string_t *var_name = token.variable;
        token.variable = NULL; // detach so free_token won't destroy it

        math_token_t op_token = get_token(parser);
        math_token_type_t assign_type = op_token.type;
        free_token(&op_token);

        ArithmeticResult right = parse_expression(parser, 0);
        if (right.failed)
        {
            string_destroy(&var_name);
            return right;
        }

        long value = right.value;
        const string_t *var_value = exec_frame_get_variable(parser->frame, var_name);
        long var_num = var_value ? string_atol(var_value) : 0;

        switch (assign_type)
        {
        case MATH_TOKEN_MULTIPLY_ASSIGN:
            value = var_num * value;
            break;
        case MATH_TOKEN_DIVIDE_ASSIGN:
            if (value == 0)
            {
                string_destroy(&var_name);
                arithmetic_result_free(&right);
                return make_error("Division by zero in assignment");
            }
            value = var_num / value;
            break;
        case MATH_TOKEN_MODULO_ASSIGN:
            if (value == 0)
            {
                string_destroy(&var_name);
                arithmetic_result_free(&right);
                return make_error("Modulo by zero in assignment");
            }
            value = var_num % value;
            break;
        case MATH_TOKEN_PLUS_ASSIGN:
            value = var_num + value;
            break;
        case MATH_TOKEN_MINUS_ASSIGN:
            value = var_num - value;
            break;
        case MATH_TOKEN_LEFT_SHIFT_ASSIGN:
            value = var_num << value;
            break;
        case MATH_TOKEN_RIGHT_SHIFT_ASSIGN:
            value = var_num >> value;
            break;
        case MATH_TOKEN_AND_ASSIGN:
            value = var_num & value;
            break;
        case MATH_TOKEN_XOR_ASSIGN:
            value = var_num ^ value;
            break;
        case MATH_TOKEN_OR_ASSIGN:
            value = var_num | value;
            break;
        default:
            break; // MATH_TOKEN_ASSIGN uses right.value directly
        }

        // Update variable in the frame
        string_t *value_str = string_from_long(value);
        exec_frame_set_variable((exec_frame_t *)parser->frame, var_name, value_str);
        string_destroy(&value_str);
        string_destroy(&var_name);

        return make_value(value);
    }

    if (token.type == MATH_TOKEN_LPAREN)
    {
        ArithmeticResult expr = parse_expression(parser, 0);
        if (expr.failed)
            return expr;

        token = get_token(parser);
        if (token.type != MATH_TOKEN_RPAREN)
        {
            arithmetic_result_free(&expr);
            free_token(&token);
            return make_error("Expected ')'");
        }
        return expr;
    }

    free_token(&token);
    return make_error("Expected number, variable, or '('");
}

/**
 * Recursively expand an arithmetic expression through the full lex-parse-expand chain.
 * This implements the POSIX-compliant expansion for arithmetic expressions:
 * 1. Re-lex the expression text
 * 2. Tokenize (with alias expansion)
 * 3. Parse into AST
 * 4. Expand the AST (parameter expansion, command substitution, quote removal)
 * 5. Return the fully expanded string
 */
static string_t *arithmetic_expand_expression(exec_frame_t *frame, const string_t *expr_text)
{
    // Step 1: Re-lex the raw text inside $(())
    lexer_t *lx = lexer_create();
    if (!lx) {
        log_error("arithmetic_expand_expression: failed to create lexer");
        return NULL;
    }

    lexer_append_input(lx, expr_text);

    // Step 2: Tokenize
    token_list_t *tokens = token_list_create();
    if (!tokens) {
        lexer_destroy(&lx);
        log_error("arithmetic_expand_expression: failed to create token list");
        return NULL;
    }

    lex_status_t lex_status = lexer_tokenize(lx, tokens, NULL);
    if (lex_status != LEX_OK) {
        log_warn("arithmetic_expand_expression: lexer failed with status %d", lex_status);
        token_list_destroy(&tokens);
        lexer_destroy(&lx);
        return NULL;
    }

    // Step 3: Tokenize with alias expansion
    // For arithmetic expansion, we typically don't need alias expansion,
    // but we include it for POSIX compliance
    alias_store_t *aliases = alias_store_create();
    tokenizer_t *tokenizer = tokenizer_create(aliases);
    token_list_t *aliased_tokens = token_list_create();

    if (!tokenizer || !aliased_tokens) {
        token_list_destroy(&aliased_tokens);
        if (tokenizer) tokenizer_destroy(&tokenizer);
        alias_store_destroy(&aliases);
        token_list_destroy(&tokens);
        lexer_destroy(&lx);
        log_error("arithmetic_expand_expression: failed to create tokenizer");
        return NULL;
    }

    tok_status_t tok_status = tokenizer_process(tokenizer, tokens, aliased_tokens);
    if (tok_status != TOK_OK) {
        log_warn("arithmetic_expand_expression: tokenizer returned status %d", tok_status);
    }

    // Step 4: Expand each word token
    // We need to expand parameters, command substitutions, etc.
    string_t *result = string_create();

    for (int i = 0; i < token_list_size(aliased_tokens); i++) {
        const token_t *tok = token_list_get(aliased_tokens, i);

        if (token_get_type(tok) == TOKEN_WORD) {
            // Expand the word using the frame-based API
            string_list_t *expanded_words = expand_word(frame, tok);

            // Concatenate all expanded words without adding spaces
            // Arithmetic expressions should not have field splitting
            for (int j = 0; j < string_list_size(expanded_words); j++) {
                const string_t *word = string_list_at(expanded_words, j);
                string_append(result, word);
            }

            string_list_destroy(&expanded_words);
        }
        // For non-word tokens, we skip them as arithmetic expressions
        // should only contain word tokens
    }

    // Cleanup
    token_list_destroy(&aliased_tokens);
    tokenizer_destroy(&tokenizer);
    alias_store_destroy(&aliases);
    token_list_destroy(&tokens);
    lexer_destroy(&lx);

    return result;
}

// Evaluate arithmetic expression
ArithmeticResult arithmetic_evaluate(exec_frame_t *frame, const string_t *expression) {
    // Validate inputs
    if (!frame) {
        return make_error("Execution frame is NULL");
    }
    if (!expression) {
        return make_error("Expression is NULL");
    }

    // Step 1-4: Perform full recursive expansion
    string_t *expanded_str = arithmetic_expand_expression(frame, expression);
    if (!expanded_str) {
        return make_error("Failed to expand arithmetic expression");
    }

    // Step 5: Parse and evaluate the fully expanded expression
    math_parser_t parser;
    parser_init(&parser, frame, expanded_str);
    ArithmeticResult result = parse_expression(&parser, 0); // Start with minimum precedence

    // Check for trailing tokens
    math_token_t token = get_token(&parser);
    if (token.type != MATH_TOKEN_EOF && !result.failed) {
        arithmetic_result_free(&result);
        result = make_error("Unexpected tokens after expression");
    }
    free_token(&token);

    // Clean up parser and expanded string
    parser_cleanup(&parser);
    string_destroy(&expanded_str);
    return result;
}

// Free ArithmeticResult
void arithmetic_result_free(ArithmeticResult *result) {
    if (!result) {
        return;
    }
    if (result->error) {
        string_destroy(&result->error);
        result->error = NULL;
    }
    result->failed = 0;
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

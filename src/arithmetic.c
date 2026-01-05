#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "arithmetic.h"
#include "string_t.h"
#include "expander.h"
#include "variable_store.h"
#include "xalloc.h"
#include "lexer.h"
#include "parser.h"
#include "tokenizer.h"
#include "alias_store.h"
#include "logging.h"

typedef struct {
    string_t *input;
    int pos;
    expander_t *exp;
    variable_store_t *vars;
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

// Initialize parser
static void parser_init(math_parser_t *parser, expander_t *exp, variable_store_t *vars, const string_t *input) {
    parser->input = string_create_from(input);
    parser->pos = 0;
    parser->exp = exp;
    parser->vars = vars;
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
        // FIXME: need to handle hex, octal?
        int endpos;
        long value = string_atol_at(parser->input, parser->pos, &endpos);
        token.type = MATH_TOKEN_NUMBER;
        token.number = value;
        parser->pos = endpos;
        return token;
    }

    if (isalpha(c) || c == '_') {
        // Parse variable name
        int endpos = string_find_first_not_of_predicate_at(parser->input, is_alpha_or_underscore, parser->pos);
        string_t *var = string_substring(parser->input, parser->pos, endpos);
        token.type = MATH_TOKEN_VARIABLE;
        token.variable = var;
        string_destroy(&var);
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
        free(token->variable);
    }
}

// Parse and evaluate expression (comma expression)
static ArithmeticResult parse_comma(math_parser_t *parser);
static ArithmeticResult parse_ternary(math_parser_t *parser);
static ArithmeticResult parse_logical_or(math_parser_t *parser);
static ArithmeticResult parse_logical_and(math_parser_t *parser);
static ArithmeticResult parse_bit_or(math_parser_t *parser);
static ArithmeticResult parse_bit_xor(math_parser_t *parser);
static ArithmeticResult parse_bit_and(math_parser_t *parser);
static ArithmeticResult parse_equality(math_parser_t *parser);
static ArithmeticResult parse_comparison(math_parser_t *parser);
static ArithmeticResult parse_shift(math_parser_t *parser);
static ArithmeticResult parse_additive(math_parser_t *parser);
static ArithmeticResult parse_multiplicative(math_parser_t *parser);
static ArithmeticResult parse_unary(math_parser_t *parser);
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

// Comma expression (handles assignments)
static ArithmeticResult parse_comma(math_parser_t *parser) {
    ArithmeticResult left = parse_ternary(parser);
    if (left.failed) return left;

    int saved_pos = parser->pos;
    math_token_t token = get_token(parser);
    if (token.type != MATH_TOKEN_COMMA) {
        parser->pos = saved_pos; // Rewind to start of token
        return left;
    }

    ArithmeticResult right = parse_comma(parser);
    if (right.failed) {
        arithmetic_result_free(&left);
        return right;
    }

    arithmetic_result_free(&right);
    return left;
}

// Ternary expression
static ArithmeticResult parse_ternary(math_parser_t *parser) {
    ArithmeticResult cond = parse_logical_or(parser);
    if (cond.failed) return cond;

    int saved_pos = parser->pos;
    math_token_t token = get_token(parser);
    if (token.type != MATH_TOKEN_QUESTION) {
        parser->pos = saved_pos; // Rewind to start of token
        return cond;
    }

    ArithmeticResult true_expr = parse_comma(parser);
    if (true_expr.failed) {
        arithmetic_result_free(&cond);
        return true_expr;
    }

    token = get_token(parser);
    if (token.type != MATH_TOKEN_COLON) {
        arithmetic_result_free(&cond);
        arithmetic_result_free(&true_expr);
        return make_error("Expected ':' in ternary expression");
    }

    ArithmeticResult false_expr = parse_comma(parser);
    if (false_expr.failed) {
        arithmetic_result_free(&cond);
        arithmetic_result_free(&true_expr);
        return false_expr;
    }

    ArithmeticResult result = cond.value ? true_expr : false_expr;
    arithmetic_result_free(&cond);
    arithmetic_result_free(true_expr.value == result.value ? &false_expr : &true_expr);
    return result;
}

// Logical OR
static ArithmeticResult parse_logical_or(math_parser_t *parser) {
    ArithmeticResult left = parse_logical_and(parser);
    if (left.failed) return left;

    while (1) {
        int saved_pos = parser->pos;
        math_token_t token = get_token(parser);
        if (token.type != MATH_TOKEN_LOGICAL_OR) {
            parser->pos = saved_pos; // Rewind to start of token
            break;
        }

        if (left.value) {
            // Short-circuit
            return left;
        }

        ArithmeticResult right = parse_logical_and(parser);
        if (right.failed) {
            arithmetic_result_free(&left);
            return right;
        }

        left.value = left.value || right.value;
        arithmetic_result_free(&right);
    }
    return left;
}

// Logical AND
static ArithmeticResult parse_logical_and(math_parser_t *parser) {
    ArithmeticResult left = parse_bit_or(parser);
    if (left.failed) return left;

    while (1) {
        int saved_pos = parser->pos;
        math_token_t token = get_token(parser);
        if (token.type != MATH_TOKEN_LOGICAL_AND) {
            parser->pos = saved_pos; // Rewind to start of token
            break;
        }

        if (!left.value) {
            // Short-circuit
            return left;
        }

        ArithmeticResult right = parse_bit_or(parser);
        if (right.failed) {
            arithmetic_result_free(&left);
            return right;
        }

        left.value = left.value && right.value;
        arithmetic_result_free(&right);
    }
    return left;
}

// Bitwise OR
static ArithmeticResult parse_bit_or(math_parser_t *parser) {
    ArithmeticResult left = parse_bit_xor(parser);
    if (left.failed) return left;

    while (1) {
        int saved_pos = parser->pos;
        math_token_t token = get_token(parser);
        if (token.type != MATH_TOKEN_BIT_OR) {
            parser->pos = saved_pos; // Rewind to start of token
            break;
        }

        ArithmeticResult right = parse_bit_xor(parser);
        if (right.failed) {
            arithmetic_result_free(&left);
            return right;
        }

        left.value |= right.value;
        arithmetic_result_free(&right);
    }
    return left;
}

// Bitwise XOR
static ArithmeticResult parse_bit_xor(math_parser_t *parser) {
    ArithmeticResult left = parse_bit_and(parser);
    if (left.failed) return left;

    while (1) {
        int saved_pos = parser->pos;
        math_token_t token = get_token(parser);
        if (token.type != MATH_TOKEN_BIT_XOR) {
            parser->pos = saved_pos; // Rewind to start of token
            break;
        }

        ArithmeticResult right = parse_bit_and(parser);
        if (right.failed) {
            arithmetic_result_free(&left);
            return right;
        }

        left.value ^= right.value;
        arithmetic_result_free(&right);
    }
    return left;
}

// Bitwise AND
static ArithmeticResult parse_bit_and(math_parser_t *parser) {
    ArithmeticResult left = parse_equality(parser);
    if (left.failed) return left;

    while (1) {
        int saved_pos = parser->pos;
        math_token_t token = get_token(parser);
        if (token.type != MATH_TOKEN_BIT_AND) {
            parser->pos = saved_pos; // Rewind to start of token
            break;
        }

        ArithmeticResult right = parse_equality(parser);
        if (right.failed) {
            arithmetic_result_free(&left);
            return right;
        }

        left.value &= right.value;
        arithmetic_result_free(&right);
    }
    return left;
}

// Equality
static ArithmeticResult parse_equality(math_parser_t *parser) {
    ArithmeticResult left = parse_comparison(parser);
    if (left.failed) return left;

    while (1) {
        int saved_pos = parser->pos;
        math_token_t token = get_token(parser);
        if (token.type != MATH_TOKEN_EQUAL && token.type != MATH_TOKEN_NOT_EQUAL) {
            parser->pos = saved_pos; // Rewind to start of token
            break;
        }

        ArithmeticResult right = parse_comparison(parser);
        if (right.failed) {
            arithmetic_result_free(&left);
            return right;
        }

        if (token.type == MATH_TOKEN_EQUAL) {
            left.value = left.value == right.value;
        } else {
            left.value = left.value != right.value;
        }
        arithmetic_result_free(&right);
    }
    return left;
}

// Comparison
static ArithmeticResult parse_comparison(math_parser_t *parser)
{
    ArithmeticResult left = parse_shift(parser);
    if (left.failed)
	return left;

    while (1) {
        int saved_pos = parser->pos;
        math_token_t token = get_token(parser);
        if (token.type != MATH_TOKEN_LESS && token.type != MATH_TOKEN_GREATER &&
            token.type != MATH_TOKEN_LESS_EQUAL && token.type != MATH_TOKEN_GREATER_EQUAL)
	{
            parser->pos = saved_pos; // Rewind to start of token
            break;
        }

        ArithmeticResult right = parse_shift(parser);
        if (right.failed)
	{
            arithmetic_result_free(&left);
            return right;
        }

        switch (token.type)
	{
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
            default:
		break;
        }
        arithmetic_result_free(&right);
    }
    return left;
}

// Shift
static ArithmeticResult parse_shift(math_parser_t *parser) {
    ArithmeticResult left = parse_additive(parser);
    if (left.failed) return left;

    while (1) {
        int saved_pos = parser->pos;
        math_token_t token = get_token(parser);
        if (token.type != MATH_TOKEN_LEFT_SHIFT && token.type != MATH_TOKEN_RIGHT_SHIFT) {
            parser->pos = saved_pos; // Rewind to start of token
            break;
        }

        ArithmeticResult right = parse_additive(parser);
        if (right.failed) {
            arithmetic_result_free(&left);
            return right;
        }

        if (token.type == MATH_TOKEN_LEFT_SHIFT) {
            left.value <<= right.value;
        } else {
            left.value >>= right.value;
        }
        arithmetic_result_free(&right);
    }
    return left;
}

// Additive
static ArithmeticResult parse_additive(math_parser_t *parser) {
    ArithmeticResult left = parse_multiplicative(parser);
    if (left.failed) return left;

    while (1) {
        int saved_pos = parser->pos;
        math_token_t token = get_token(parser);
        if (token.type != MATH_TOKEN_PLUS && token.type != MATH_TOKEN_MINUS) {
            parser->pos = saved_pos; // Rewind to start of token
            break;
        }

        ArithmeticResult right = parse_multiplicative(parser);
        if (right.failed) {
            arithmetic_result_free(&left);
            return right;
        }

        if (token.type == MATH_TOKEN_PLUS) {
            left.value += right.value;
        } else {
            left.value -= right.value;
        }
        arithmetic_result_free(&right);
    }
    return left;
}

// Multiplicative
static ArithmeticResult parse_multiplicative(math_parser_t *parser) {
    ArithmeticResult left = parse_unary(parser);
    if (left.failed) return left;

    while (1) {
        int saved_pos = parser->pos;
        math_token_t token = get_token(parser);
        if (token.type != MATH_TOKEN_MULTIPLY && token.type != MATH_TOKEN_DIVIDE && token.type != MATH_TOKEN_MODULO) {
            parser->pos = saved_pos; // Rewind to start of token
            break;
        }

        ArithmeticResult right = parse_unary(parser);
        if (right.failed) {
            arithmetic_result_free(&left);
            return right;
        }

        if (token.type == MATH_TOKEN_MULTIPLY) {
            left.value *= right.value;
        } else if (token.type == MATH_TOKEN_DIVIDE) {
            if (right.value == 0) {
                arithmetic_result_free(&left);
                arithmetic_result_free(&right);
                return make_error("Division by zero");
            }
            left.value /= right.value;
        } else {
            if (right.value == 0) {
                arithmetic_result_free(&left);
                arithmetic_result_free(&right);
                return make_error("Modulo by zero");
            }
            left.value %= right.value;
        }
        arithmetic_result_free(&right);
    }
    return left;
}

// Unary
static ArithmeticResult parse_unary(math_parser_t *parser) {
    int saved_pos = parser->pos;
    math_token_t token = get_token(parser);
    if (token.type == MATH_TOKEN_PLUS || token.type == MATH_TOKEN_MINUS ||
        token.type == MATH_TOKEN_BIT_NOT || token.type == MATH_TOKEN_LOGICAL_NOT) {
        ArithmeticResult expr = parse_unary(parser);
        if (expr.failed) return expr;

        switch (token.type) {
            case MATH_TOKEN_PLUS:        /* No-op */ break;
            case MATH_TOKEN_MINUS:       expr.value = -expr.value; break;
            case MATH_TOKEN_BIT_NOT:     expr.value = ~expr.value; break;
            case MATH_TOKEN_LOGICAL_NOT: expr.value = !expr.value; break;
            default: break;
        }
        return expr;
    }
    // Token was not a unary operator, rewind and try primary
    parser->pos = saved_pos; // Rewind to start of token
    return parse_primary(parser);
}

// Primary (number, variable, parenthesized expression, assignment)
static ArithmeticResult parse_primary(math_parser_t *parser) {
    math_token_t token = get_token(parser);

    if (token.type == MATH_TOKEN_NUMBER) {
        return make_value(token.number);
    }

    if (token.type == MATH_TOKEN_VARIABLE) {
        // Check if this is a simple variable read (not followed by assignment operator)
        // We need to peek ahead to see if an assignment operator follows
        int saved_pos = parser->pos;
        math_token_t next_token = get_token(parser);
        parser->pos = saved_pos; // Restore position

        // If not followed by assignment operator, treat as variable read
        if (next_token.type != MATH_TOKEN_ASSIGN &&
            next_token.type != MATH_TOKEN_MULTIPLY_ASSIGN &&
            next_token.type != MATH_TOKEN_DIVIDE_ASSIGN &&
            next_token.type != MATH_TOKEN_MODULO_ASSIGN &&
            next_token.type != MATH_TOKEN_PLUS_ASSIGN &&
            next_token.type != MATH_TOKEN_MINUS_ASSIGN &&
            next_token.type != MATH_TOKEN_LEFT_SHIFT_ASSIGN &&
            next_token.type != MATH_TOKEN_RIGHT_SHIFT_ASSIGN &&
            next_token.type != MATH_TOKEN_AND_ASSIGN &&
            next_token.type != MATH_TOKEN_XOR_ASSIGN &&
            next_token.type != MATH_TOKEN_OR_ASSIGN) {
            // Variable read - get its value
            const string_t *value = variable_store_get_value(parser->vars, token.variable);
            long num = value ? string_atol(value) : 0;
            free_token(&token);
            return make_value(num);
        }
        // Otherwise, fall through to handle assignment below
    }

    if (token.type == MATH_TOKEN_LPAREN) {
        ArithmeticResult expr = parse_comma(parser);
        if (expr.failed) return expr;

        token = get_token(parser);
        if (token.type != MATH_TOKEN_RPAREN) {
            arithmetic_result_free(&expr);
            return make_error("Expected ')'");
        }
        return expr;
    }

    // Handle assignment (e.g., x=5, x+=2)
    if (token.type == MATH_TOKEN_VARIABLE) {
        token = get_token(parser);
        math_token_type_t assign_type = token.type;

        // Note: We've already checked that this is an assignment operator in the lookahead above
        // So this check should always pass

        ArithmeticResult right = parse_comma(parser);
        if (right.failed) {
            return right;
        }

        long value = right.value;
        const string_t *var_value = variable_store_get_value(parser->vars, token.variable);
        long var_num = var_value ? string_atol(var_value) : 0;
        switch (assign_type) {
            case MATH_TOKEN_MULTIPLY_ASSIGN:
                value *= var_num;
                break;
            case MATH_TOKEN_DIVIDE_ASSIGN:
                if (value == 0) {
                    arithmetic_result_free(&right);
                    return make_error("Division by zero in assignment");
                }

                value = var_num / value;
                break;
            case MATH_TOKEN_MODULO_ASSIGN:
                if (value == 0) {
                    arithmetic_result_free(&right);
                    return make_error("Modulo by zero in assignment");
                }
                value = var_num % value;
                break;
            case MATH_TOKEN_PLUS_ASSIGN:
                value += var_num;
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
                value &= var_num;
                break;
            case MATH_TOKEN_XOR_ASSIGN:
                value ^= var_num;
                break;
            case MATH_TOKEN_OR_ASSIGN:
                value |= var_num;
                break;
            default:
                break; // MATH_TOKEN_ASSIGN uses right.value directly
        }

        // Update variable
        string_t *value_str = string_from_long(value);
        variable_store_add(parser->vars, token.variable, value_str, false, false);
        string_destroy(&value_str);
        return right;
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
static string_t *arithmetic_expand_expression(expander_t *exp, variable_store_t *vars, const string_t *expr_text)
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

    // Set the variable store once before the loop
    // FIXME: use new expander API to set variable store
    // expander_set_variable_store(exp, vars);
    abort(); // Placeholder until expander API is updated

    for (int i = 0; i < token_list_size(aliased_tokens); i++) {
        token_t *tok = token_list_get(aliased_tokens, i);

        if (token_get_type(tok) == TOKEN_WORD) {
            // Expand the word
            string_list_t *expanded_words = expander_expand_word(exp, tok);

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
ArithmeticResult arithmetic_evaluate(expander_t *exp, variable_store_t *vars, const string_t *expression) {
    // Step 1-4: Perform full recursive expansion
    string_t *expanded_str = arithmetic_expand_expression(exp, vars, expression);
    if (!expanded_str) {
        return make_error("Failed to expand arithmetic expression");
    }

    // Step 5: Parse and evaluate the fully expanded expression
    math_parser_t parser;
    parser_init(&parser, exp, vars, expanded_str);
    ArithmeticResult result = parse_comma(&parser);

    // Check for trailing tokens
    math_token_t token = get_token(&parser);
    if (token.type != MATH_TOKEN_EOF && !result.failed) {
        arithmetic_result_free(&result);
        result = make_error("Unexpected tokens after expression");
    }
    free_token(&token);

    string_destroy(&expanded_str);
    return result;
}

// Free ArithmeticResult
void arithmetic_result_free(ArithmeticResult *result) {
    if (result->error) {
        free(result->error);
        result->error = NULL;
    }
    result->failed = 0;
}

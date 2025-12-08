#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "arithmetic.h"
#include "string.h"

typedef struct {
    const char *input;
    size_t pos;
    Executor *exec;
} Parser;

// Token types for arithmetic lexer
typedef enum {
    TOKEN_NUMBER,
    TOKEN_VARIABLE,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_MULTIPLY,
    TOKEN_DIVIDE,
    TOKEN_MODULO,
    TOKEN_BIT_NOT,
    TOKEN_LOGICAL_NOT,
    TOKEN_LEFT_SHIFT,
    TOKEN_RIGHT_SHIFT,
    TOKEN_LESS,
    TOKEN_GREATER,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER_EQUAL,
    TOKEN_EQUAL,
    TOKEN_NOT_EQUAL,
    TOKEN_BIT_AND,
    TOKEN_BIT_XOR,
    TOKEN_BIT_OR,
    TOKEN_LOGICAL_AND,
    TOKEN_LOGICAL_OR,
    TOKEN_QUESTION,
    TOKEN_COLON,
    TOKEN_ASSIGN,
    TOKEN_MULTIPLY_ASSIGN,
    TOKEN_DIVIDE_ASSIGN,
    TOKEN_MODULO_ASSIGN,
    TOKEN_PLUS_ASSIGN,
    TOKEN_MINUS_ASSIGN,
    TOKEN_LEFT_SHIFT_ASSIGN,
    TOKEN_RIGHT_SHIFT_ASSIGN,
    TOKEN_AND_ASSIGN,
    TOKEN_XOR_ASSIGN,
    TOKEN_OR_ASSIGN,
    TOKEN_COMMA,
    TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;
    long number;    // For TOKEN_NUMBER
    char *variable; // For TOKEN_VARIABLE (caller frees)
} Token;

// Initialize parser
static void parser_init(Parser *parser, Executor *exec, const char *input) {
    parser->input = input;
    parser->pos = 0;
    parser->exec = exec;
}

// Skip whitespace
static void skip_whitespace(Parser *parser) {
    while (isspace(parser->input[parser->pos])) {
        parser->pos++;
    }
}

// Get next token
static Token get_token(Parser *parser) {
    skip_whitespace(parser);
    Token token = {0};

    if (parser->input[parser->pos] == '\0') {
        token.type = TOKEN_EOF;
        return token;
    }

    char c = parser->input[parser->pos];
    if (isdigit(c)) {
        // Parse decimal, octal, or hexadecimal
        char *endptr;
        long value = strtol(parser->input + parser->pos, &endptr, 0);
        if (endptr == parser->input + parser->pos) {
            token.type = TOKEN_EOF; // Invalid number
        } else {
            token.type = TOKEN_NUMBER;
            token.number = value;
            parser->pos = endptr - parser->input;
        }
        return token;
    }

    if (isalpha(c) || c == '_') {
        // Parse variable name
        String *var = string_create();
        while (isalnum(parser->input[parser->pos]) || parser->input[parser->pos] == '_') {
            string_append_char(var, parser->input[parser->pos++]);
        }
        token.type = TOKEN_VARIABLE;
        token.variable = strdup(string_cstr(var));
        string_destroy(var);
        return token;
    }

    parser->pos++;
    switch (c) {
        case '(': token.type = TOKEN_LPAREN; break;
        case ')': token.type = TOKEN_RPAREN; break;
        case '+':
            if (parser->input[parser->pos] == '=') {
                token.type = TOKEN_PLUS_ASSIGN;
                parser->pos++;
            } else {
                token.type = TOKEN_PLUS;
            }
            break;
        case '-':
            if (parser->input[parser->pos] == '=') {
                token.type = TOKEN_MINUS_ASSIGN;
                parser->pos++;
            } else {
                token.type = TOKEN_MINUS;
            }
            break;
        case '*':
            if (parser->input[parser->pos] == '=') {
                token.type = TOKEN_MULTIPLY_ASSIGN;
                parser->pos++;
            } else {
                token.type = TOKEN_MULTIPLY;
            }
            break;
        case '/':
            if (parser->input[parser->pos] == '=') {
                token.type = TOKEN_DIVIDE_ASSIGN;
                parser->pos++;
            } else {
                token.type = TOKEN_DIVIDE;
            }
            break;
        case '%':
            if (parser->input[parser->pos] == '=') {
                token.type = TOKEN_MODULO_ASSIGN;
                parser->pos++;
            } else {
                token.type = TOKEN_MODULO;
            }
            break;
        case '~': token.type = TOKEN_BIT_NOT; break;
        case '!':
            if (parser->input[parser->pos] == '=') {
                token.type = TOKEN_NOT_EQUAL;
                parser->pos++;
            } else {
                token.type = TOKEN_LOGICAL_NOT;
            }
            break;
        case '<':
            if (parser->input[parser->pos] == '<') {
                parser->pos++;
                if (parser->input[parser->pos] == '=') {
                    token.type = TOKEN_LEFT_SHIFT_ASSIGN;
                    parser->pos++;
                } else {
                    token.type = TOKEN_LEFT_SHIFT;
                }
            } else if (parser->input[parser->pos] == '=') {
                token.type = TOKEN_LESS_EQUAL;
                parser->pos++;
            } else {
                token.type = TOKEN_LESS;
            }
            break;
        case '>':
            if (parser->input[parser->pos] == '>') {
                parser->pos++;
                if (parser->input[parser->pos] == '=') {
                    token.type = TOKEN_RIGHT_SHIFT_ASSIGN;
                    parser->pos++;
                } else {
                    token.type = TOKEN_RIGHT_SHIFT;
                }
            } else if (parser->input[parser->pos] == '=') {
                token.type = TOKEN_GREATER_EQUAL;
                parser->pos++;
            } else {
                token.type = TOKEN_GREATER;
            }
            break;
        case '=':
            if (parser->input[parser->pos] == '=') {
                token.type = TOKEN_EQUAL;
                parser->pos++;
            } else {
                token.type = TOKEN_ASSIGN;
            }
            break;
        case '&':
            if (parser->input[parser->pos] == '&') {
                token.type = TOKEN_LOGICAL_AND;
                parser->pos++;
            } else if (parser->input[parser->pos] == '=') {
                token.type = TOKEN_AND_ASSIGN;
                parser->pos++;
            } else {
                token.type = TOKEN_BIT_AND;
            }
            break;
        case '^':
            if (parser->input[parser->pos] == '=') {
                token.type = TOKEN_XOR_ASSIGN;
                parser->pos++;
            } else {
                token.type = TOKEN_BIT_XOR;
            }
            break;
        case '|':
            if (parser->input[parser->pos] == '|') {
                token.type = TOKEN_LOGICAL_OR;
                parser->pos++;
            } else if (parser->input[parser->pos] == '=') {
                token.type = TOKEN_OR_ASSIGN;
                parser->pos++;
            } else {
                token.type = TOKEN_BIT_OR;
            }
            break;
        case '?': token.type = TOKEN_QUESTION; break;
        case ':': token.type = TOKEN_COLON; break;
        case ',': token.type = TOKEN_COMMA; break;
        default:
            token.type = TOKEN_EOF; // Invalid character
            break;
    }
    return token;
}

// Free token
static void free_token(Token *token) {
    if (token->type == TOKEN_VARIABLE && token->variable) {
        free(token->variable);
    }
}

// Parse and evaluate expression (comma expression)
static ArithmeticResult parse_comma(Parser *parser);
static ArithmeticResult parse_ternary(Parser *parser);
static ArithmeticResult parse_logical_or(Parser *parser);
static ArithmeticResult parse_logical_and(Parser *parser);
static ArithmeticResult parse_bit_or(Parser *parser);
static ArithmeticResult parse_bit_xor(Parser *parser);
static ArithmeticResult parse_bit_and(Parser *parser);
static ArithmeticResult parse_equality(Parser *parser);
static ArithmeticResult parse_comparison(Parser *parser);
static ArithmeticResult parse_shift(Parser *parser);
static ArithmeticResult parse_additive(Parser *parser);
static ArithmeticResult parse_multiplicative(Parser *parser);
static ArithmeticResult parse_unary(Parser *parser);
static ArithmeticResult parse_primary(Parser *parser);

// Helper to create error result
static ArithmeticResult make_error(const char *msg) {
    ArithmeticResult result = {0};
    result.failed = 1;
    result.error = strdup(msg);
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
static ArithmeticResult parse_comma(Parser *parser) {
    ArithmeticResult left = parse_ternary(parser);
    if (left.failed) return left;

    Token token = get_token(parser);
    if (token.type != TOKEN_COMMA) {
        parser->pos -= 1; // Rewind
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
static ArithmeticResult parse_ternary(Parser *parser) {
    ArithmeticResult cond = parse_logical_or(parser);
    if (cond.failed) return cond;

    Token token = get_token(parser);
    if (token.type != TOKEN_QUESTION) {
        parser->pos -= 1; // Rewind
        return cond;
    }

    ArithmeticResult true_expr = parse_comma(parser);
    if (true_expr.failed) {
        arithmetic_result_free(&cond);
        return true_expr;
    }

    token = get_token(parser);
    if (token.type != TOKEN_COLON) {
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
static ArithmeticResult parse_logical_or(Parser *parser) {
    ArithmeticResult left = parse_logical_and(parser);
    if (left.failed) return left;

    while (1) {
        Token token = get_token(parser);
        if (token.type != TOKEN_LOGICAL_OR) {
            parser->pos -= 1; // Rewind
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
static ArithmeticResult parse_logical_and(Parser *parser) {
    ArithmeticResult left = parse_bit_or(parser);
    if (left.failed) return left;

    while (1) {
        Token token = get_token(parser);
        if (token.type != TOKEN_LOGICAL_AND) {
            parser->pos -= 1; // Rewind
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
static ArithmeticResult parse_bit_or(Parser *parser) {
    ArithmeticResult left = parse_bit_xor(parser);
    if (left.failed) return left;

    while (1) {
        Token token = get_token(parser);
        if (token.type != TOKEN_BIT_OR) {
            parser->pos -= 1; // Rewind
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
static ArithmeticResult parse_bit_xor(Parser *parser) {
    ArithmeticResult left = parse_bit_and(parser);
    if (left.failed) return left;

    while (1) {
        Token token = get_token(parser);
        if (token.type != TOKEN_BIT_XOR) {
            parser->pos -= 1; // Rewind
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
static ArithmeticResult parse_bit_and(Parser *parser) {
    ArithmeticResult left = parse_equality(parser);
    if (left.failed) return left;

    while (1) {
        Token token = get_token(parser);
        if (token.type != TOKEN_BIT_AND) {
            parser->pos -= 1; // Rewind
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
static ArithmeticResult parse_equality(Parser *parser) {
    ArithmeticResult left = parse_comparison(parser);
    if (left.failed) return left;

    while (1) {
        Token token = get_token(parser);
        if (token.type != TOKEN_EQUAL && token.type != TOKEN_NOT_EQUAL) {
            parser->pos -= 1; // Rewind
            break;
        }

        ArithmeticResult right = parse_comparison(parser);
        if (right.failed) {
            arithmetic_result_free(&left);
            return right;
        }

        if (token.type == TOKEN_EQUAL) {
            left.value = left.value == right.value;
        } else {
            left.value = left.value != right.value;
        }
        arithmetic_result_free(&right);
    }
    return left;
}

// Comparison
static ArithmeticResult parse_comparison(Parser *parser) {
    ArithmeticResult left = parse_shift(parser);
    if (left.failed) return left;

    while (1) {
        Token token = get_token(parser);
        if (token.type != TOKEN_LESS && token.type != TOKEN_GREATER &&
            token.type != TOKEN_LESS_EQUAL && token.type != TOKEN_GREATER_EQUAL) {
            parser->pos -= 1; // Rewind
            break;
        }

        ArithmeticResult right = parse_shift(parser);
        if (right.failed) {
            arithmetic_result_free(&left);
            return right;
        }

        switch (token.type) {
            case TOKEN_LESS:         left.value = left.value < right.value; break;
            case TOKEN_GREATER:      left.value = left.value > right.value; break;
            case TOKEN_LESS_EQUAL:   left.value = left.value <= right.value; break;
            case TOKEN_GREATER_EQUAL:left.value = left.value >= right.value; break;
            default: break;
        }
        arithmetic_result_free(&right);
    }
    return left;
}

// Shift
static ArithmeticResult parse_shift(Parser *parser) {
    ArithmeticResult left = parse_additive(parser);
    if (left.failed) return left;

    while (1) {
        Token token = get_token(parser);
        if (token.type != TOKEN_LEFT_SHIFT && token.type != TOKEN_RIGHT_SHIFT) {
            parser->pos -= 1; // Rewind
            break;
        }

        ArithmeticResult right = parse_additive(parser);
        if (right.failed) {
            arithmetic_result_free(&left);
            return right;
        }

        if (token.type == TOKEN_LEFT_SHIFT) {
            left.value <<= right.value;
        } else {
            left.value >>= right.value;
        }
        arithmetic_result_free(&right);
    }
    return left;
}

// Additive
static ArithmeticResult parse_additive(Parser *parser) {
    ArithmeticResult left = parse_multiplicative(parser);
    if (left.failed) return left;

    while (1) {
        Token token = get_token(parser);
        if (token.type != TOKEN_PLUS && token.type != TOKEN_MINUS) {
            parser->pos -= 1; // Rewind
            break;
        }

        ArithmeticResult right = parse_multiplicative(parser);
        if (right.failed) {
            arithmetic_result_free(&left);
            return right;
        }

        if (token.type == TOKEN_PLUS) {
            left.value += right.value;
        } else {
            left.value -= right.value;
        }
        arithmetic_result_free(&right);
    }
    return left;
}

// Multiplicative
static ArithmeticResult parse_multiplicative(Parser *parser) {
    ArithmeticResult left = parse_unary(parser);
    if (left.failed) return left;

    while (1) {
        Token token = get_token(parser);
        if (token.type != TOKEN_MULTIPLY && token.type != TOKEN_DIVIDE && token.type != TOKEN_MODULO) {
            parser->pos -= 1; // Rewind
            break;
        }

        ArithmeticResult right = parse_unary(parser);
        if (right.failed) {
            arithmetic_result_free(&left);
            return right;
        }

        if (token.type == TOKEN_MULTIPLY) {
            left.value *= right.value;
        } else if (token.type == TOKEN_DIVIDE) {
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
static ArithmeticResult parse_unary(Parser *parser) {
    Token token = get_token(parser);
    if (token.type == TOKEN_PLUS || token.type == TOKEN_MINUS ||
        token.type == TOKEN_BIT_NOT || token.type == TOKEN_LOGICAL_NOT) {
        ArithmeticResult expr = parse_unary(parser);
        if (expr.failed) return expr;

        switch (token.type) {
            case TOKEN_PLUS:        /* No-op */ break;
            case TOKEN_MINUS:       expr.value = -expr.value; break;
            case TOKEN_BIT_NOT:     expr.value = ~expr.value; break;
            case TOKEN_LOGICAL_NOT: expr.value = !expr.value; break;
            default: break;
        }
        return expr;
    }
    parser->pos -= 1; // Rewind
    return parse_primary(parser);
}

// Primary (number, variable, parenthesized expression, assignment)
static ArithmeticResult parse_primary(Parser *parser) {
    Token token = get_token(parser);

    if (token.type == TOKEN_NUMBER) {
        return make_value(token.number);
    }

    if (token.type == TOKEN_VARIABLE) {
        const char *value = variable_store_get_variable(parser->exec->vars, token.variable);
        char *endptr;
        long num = value ? strtol(value, &endptr, 10) : 0;
        if (value && *endptr == '\0') {
            free_token(&token);
            return make_value(num);
        }
        ArithmeticResult error = make_error("Invalid variable value");
        free_token(&token);
        return error;
    }

    if (token.type == TOKEN_LPAREN) {
        ArithmeticResult expr = parse_comma(parser);
        if (expr.failed) return expr;

        token = get_token(parser);
        if (token.type != TOKEN_RPAREN) {
            arithmetic_result_free(&expr);
            return make_error("Expected ')'");
        }
        return expr;
    }

    // Handle assignment (e.g., x=5, x+=2)
    if (token.type == TOKEN_VARIABLE) {
        char *var_name = token.variable;
        token = get_token(parser);
        TokenType assign_type = token.type;

        if (assign_type != TOKEN_ASSIGN &&
            assign_type != TOKEN_MULTIPLY_ASSIGN &&
            assign_type != TOKEN_DIVIDE_ASSIGN &&
            assign_type != TOKEN_MODULO_ASSIGN &&
            assign_type != TOKEN_PLUS_ASSIGN &&
            assign_type != TOKEN_MINUS_ASSIGN &&
            assign_type != TOKEN_LEFT_SHIFT_ASSIGN &&
            assign_type != TOKEN_RIGHT_SHIFT_ASSIGN &&
            assign_type != TOKEN_AND_ASSIGN &&
            assign_type != TOKEN_XOR_ASSIGN &&
            assign_type != TOKEN_OR_ASSIGN) {
            free(var_name);
            parser->pos -= 1; // Rewind
            return make_error("Expected assignment operator");
        }

        ArithmeticResult right = parse_comma(parser);
        if (right.failed) {
            free(var_name);
            return right;
        }

        long value = right.value;
        switch (assign_type) {
            case TOKEN_MULTIPLY_ASSIGN:
                value *= variable_store_get_variable(parser->exec->vars, var_name) ?
                         strtol(variable_store_get_variable(parser->exec->vars, var_name), NULL, 10) : 0;
                break;
            case TOKEN_DIVIDE_ASSIGN:
                if (value == 0) {
                    free(var_name);
                    arithmetic_result_free(&right);
                    return make_error("Division by zero in assignment");
                }
                value = (variable_store_get_variable(parser->exec->vars, var_name) ?
                         strtol(variable_store_get_variable(parser->exec->vars, var_name), NULL, 10) : 0) / value;
                break;
            case TOKEN_MODULO_ASSIGN:
                if (value == 0) {
                    free(var_name);
                    arithmetic_result_free(&right);
                    return make_error("Modulo by zero in assignment");
                }
                value = (variable_store_get_variable(parser->exec->vars, var_name) ?
                         strtol(variable_store_get_variable(parser->exec->vars, var_name), NULL, 10) : 0) % value;
                break;
            case TOKEN_PLUS_ASSIGN:
                value += variable_store_get_variable(parser->exec->vars, var_name) ?
                         strtol(variable_store_get_variable(parser->exec->vars, var_name), NULL, 10) : 0;
                break;
            case TOKEN_MINUS_ASSIGN:
                value = (variable_store_get_variable(parser->exec->vars, var_name) ?
                         strtol(variable_store_get_variable(parser->exec->vars, var_name), NULL, 10) : 0) - value;
                break;
            case TOKEN_LEFT_SHIFT_ASSIGN:
                value = (variable_store_get_variable(parser->exec->vars, var_name) ?
                         strtol(variable_store_get_variable(parser->exec->vars, var_name), NULL, 10) : 0) << value;
                break;
            case TOKEN_RIGHT_SHIFT_ASSIGN:
                value = (variable_store_get_variable(parser->exec->vars, var_name) ?
                         strtol(variable_store_get_variable(parser->exec->vars, var_name), NULL, 10) : 0) >> value;
                break;
            case TOKEN_AND_ASSIGN:
                value &= variable_store_get_variable(parser->exec->vars, var_name) ?
                         strtol(variable_store_get_variable(parser->exec->vars, var_name), NULL, 10) : 0;
                break;
            case TOKEN_XOR_ASSIGN:
                value ^= variable_store_get_variable(parser->exec->vars, var_name) ?
                         strtol(variable_store_get_variable(parser->exec->vars, var_name), NULL, 10) : 0;
                break;
            case TOKEN_OR_ASSIGN:
                value |= variable_store_get_variable(parser->exec->vars, var_name) ?
                         strtol(variable_store_get_variable(parser->exec->vars, var_name), NULL, 10) : 0;
                break;
            default:
                break; // TOKEN_ASSIGN uses right.value directly
        }

        // Update variable
        char value_str[32];
        snprintf(value_str, sizeof(value_str), "%ld", value);
        variable_store_set_variable(parser->exec->vars, var_name, value_str);
        free(var_name);
        return right;
    }

    free_token(&token);
    return make_error("Expected number, variable, or '('");
}

// Evaluate arithmetic expression
ArithmeticResult arithmetic_evaluate(Executor *exec, const char *expression) {
    // Preprocess: Handle parameter expansion and command substitution
    char *expanded = expand_string(exec, (char *)expression, NULL, 0);
    if (!expanded) {
        return make_error("Expansion failed");
    }

    Parser parser;
    parser_init(&parser, exec, expanded);
    ArithmeticResult result = parse_comma(&parser);

    // Check for trailing tokens
    Token token = get_token(&parser);
    if (token.type != TOKEN_EOF && !result.failed) {
        arithmetic_result_free(&result);
        result = make_error("Unexpected tokens after expression");
    }
    free_token(&token);

    free(expanded);
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

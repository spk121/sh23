#ifndef GPARSE_H
#define GPARSE_H
#include "parser.h"
#include "ast_grammar.h"

parse_status_t gparse_program(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_complete_commands(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_complete_command(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_list(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_and_or(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_pipeline(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_pipe_sequence(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_command(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_compound_command(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_subshell(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_compound_list(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_term(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_for_clause(parser_t *parser, gnode_t **out_node);
// name is done inline
parse_status_t gparse_in_clause(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_wordlist(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_case_clause(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_case_list_ns(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_case_list(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_case_item_ns(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_case_item(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_pattern_list(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_if_clause(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_else_part(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_while_clause(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_until_clause(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_function_definition(parser_t *parser, gnode_t **out_node);
// function_body is done inline
// fname is done inline
parse_status_t gparse_brace_group(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_do_group(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_simple_command(parser_t *parser, gnode_t **out_node);
// cmd_name is isline
// cmd_word is inline
// cmd_prefix is inline
// cmd_suffix is inline
parse_status_t gparse_redirect_list(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_io_redirect(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_io_file(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_filename(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_io_here(parser_t *parser, gnode_t **out_node);
// here_end is inline
// newline_list is inline
// linebreak is inline
parse_status_t gparse_separator_op(parser_t *parser, gnode_t **out_node);
parse_status_t gparse_separator(parser_t *parser, gnode_t **out_node);
// sequential_sep is inline
#endif

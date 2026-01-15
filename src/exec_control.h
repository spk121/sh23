#pragma once

#include "ast.h"
#include "exec_internal.h"
exec_status_t exec_execute_if_clause(exec_t *executor, const ast_node_t *node);
exec_status_t exec_execute_while_clause(exec_t *executor, const ast_node_t *node);
exec_status_t exec_execute_until_clause(exec_t *executor, const ast_node_t *node);
exec_status_t exec_execute_for_clause(exec_t *executor, const ast_node_t *node);
exec_status_t exec_execute_case_clause(exec_t *executor, const ast_node_t *node);


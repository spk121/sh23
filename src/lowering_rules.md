3. Lowering Rules (Complete Specification)
Below is the full lowering plan, node‑by‑node.

G_PROGRAM → AST_COMMAND_LIST
 
lower_program(g):
    if g.child == NULL:
        return empty AST_COMMAND_LIST
    else:
        return lower_complete_commands(g.child)

G_COMPLETE_COMMANDS → AST_COMMAND_LIST
 
lower_complete_commands(g):
    list = new AST_COMMAND_LIST
    for each child in g.list:
        append lower_complete_command(child)
    return list

G_COMPLETE_COMMAND → AST_COMMAND_LIST element

 
lower_complete_command(g):
    return lower_list(g.multi.a)

G_LIST → AST_COMMAND_LIST
 
lower_list(g):
    ast = new AST_COMMAND_LIST
    for each element in g.list:
        if element is G_AND_OR:
            append lower_and_or(element)
        else if element is G_SEPARATOR_OP:
            ignore
    return ast

G_AND_OR → AST_AND_OR_LIST or single command

 
lower_and_or(g):
    if g is a leaf pipeline:
        return lower_pipeline(g)
    else:
        ast = new AST_AND_OR_LIST
        recursively flatten:
            left = lower_and_or(g.multi.a)
            op   = g.multi.b.token (&& or ||)
            right= lower_pipeline(g.multi.c)
        return ast

G_PIPELINE → AST_PIPELINE or single command
 
lower_pipeline(g):
    seq = g.list.last
    commands = lower_pipe_sequence(seq)
    if Bang present:
        wrap first command in "negation" flag
    if commands.size == 1:
        return commands[0]
    else:
        return AST_PIPELINE(commands)

G_PIPE_SEQUENCE → list of commands
 
lower_pipe_sequence(g):
    commands = []
    for each element in g.list:
        if element is command:
            commands.append(lower_command(element))
    return commands

G_COMMAND → dispatch
 
command:
    simple_command
    | compound_command
    | compound_command redirect_list
    | function_definition

lower_command(g):
    if child is simple_command:
        return lower_simple_command
    if child is compound_command:
        cmd = lower_compound_command
        if redirect_list exists:
            return AST_REDIRECTED_COMMAND(cmd, redirects)
        else:
            return cmd
    if child is function_definition:
        return lower_function_definition

G_SIMPLE_COMMAND → AST_SIMPLE_COMMAND
 
lower_simple_command(g):
    ast = new AST_SIMPLE_COMMAND
    for each element in g.list:
        if element is assignment_word:
            ast.args.append(AST_WORD)
        else if element is WORD:
            ast.args.append(AST_WORD)
        else if element is io_redirect:
            ast.redirects.append(lower_io_redirect)
    return ast

G_IO_REDIRECT → AST_REDIRECTION

lower_io_redirect(g):
    op = lower operator token
    target = lower filename or here-doc
    fd = optional io_number
    return AST_REDIRECTION(fd, op, target)

G_SUBSHELL → AST_SUBSHELL
 
lower_subshell(g):
    return AST_SUBSHELL(lower_compound_list(g.child))
G_BRACE_GROUP → AST_BRACE_GROUP
 
lower_brace_group(g):
    return AST_BRACE_GROUP(lower_compound_list(g.child))

G_IF_CLAUSE → AST_IF_CLAUSE
 
lower_if_clause(g):
    cond = lower_compound_list(g.multi.a)
    then_body = lower_compound_list(g.multi.b)
    else_part = lower_else_part(g.multi.c)
    return AST_IF_CLAUSE(cond, then_body, else_part)
G_ELSE_PART → nested structure
 
else_part:
    else compound_list
    | elif compound_list then compound_list else_part
Lowering:

 
lower_else_part(g):
    if g is ELSE:
        return AST_ELSE(lower_compound_list)
    if g is ELIF:
        return AST_ELIF(cond, then_body, lower_else_part(next))

G_WHILE_CLAUSE / G_UNTIL_CLAUSE
 
lower_while_clause(g):
    return AST_WHILE_CLAUSE(cond, body)

lower_until_clause(g):
    return AST_UNTIL_CLAUSE(cond, body)

G_FOR_CLAUSE → AST_FOR_CLAUSE
 
lower_for_clause(g):
    name = WORD
    wordlist = list of WORD or NULL
    body = lower_do_group
    return AST_FOR_CLAUSE(name, wordlist, body)

G_CASE_CLAUSE → AST_CASE_CLAUSE
 
lower_case_clause(g):
    subject = WORD
    items = lower_case_list or lower_case_list_ns
    return AST_CASE_CLAUSE(subject, items)

G_CASE_ITEM → AST_CASE_ITEM
 
lower_case_item(g):
    patterns = list of WORD
    body = lower_compound_list or NULL
    terminator = token (DSEMI or SEMI_AND)
    return AST_CASE_ITEM(patterns, body, terminator)

G_FUNCTION_DEFINITION → AST_FUNCTION_DEF
 
lower_function_definition(g):
    name = WORD
    body = lower_compound_command
    redirects = optional redirect_list
    return AST_FUNCTION_DEF(name, body, redirects)

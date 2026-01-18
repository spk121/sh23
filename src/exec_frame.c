#include "exec_internal.h"

static void init_variable_store_by_policy(exec_frame_t *frame)
{
    Expects_not_null(frame);
    switch (frame->policy->variable_scope)
    {
    case EXEC_SCOPE_NONE:
    case EXEC_SCOPE_OWN:
        if (frame->policy->variable_store_init_with_envp)
            frame->variable_store = variable_store_create_from_envp(frame->executor->envp);
        else
            frame->variable_store = variable_store_create();
        break;
    case EXEC_SCOPE_COPY:
        if (frame->policy->variable_store_copy_export_only)
            frame->variable_store = variable_store_clone_exported(frame->parent->variable_store);
        else
            frame->variable_store = variable_store_clone(frame->parent->variable_store);
        break;
    case EXEC_SCOPE_SHARE:
        frame->variable_store = frame->parent->variable_store;
        break;
    case EXEC_SCOPE_SPECIAL:
    default:
        Expects(false);
        break;
    }
    if (frame->policy->variable_store_has_locals)
        frame->local_variable_store = variable_store_create();
}

static void init_positional_params_by_policy(exec_frame_t *frame)
{
    Expects_not_null(frame);
    switch (frame->policy->positional_params_scope)
    {
    case EXEC_SCOPE_NONE:
    case EXEC_SCOPE_OWN:
        if (frame->policy->positional_params_init_with_top_level_args && frame->executor->argv)
        {
            frame->positional_params = positional_params_create_from_argv(
                string_cstr(frame->executor->shell_name),
                frame->executor->argc,
                (const char **)frame->executor->argv);
        }
        else
            frame->positional_params = positional_params_create();
        break;
    case EXEC_SCOPE_COPY:
        frame->positional_params = positional_params_clone(frame->parent->positional_params);
        break;
    case EXEC_SCOPE_SHARE:
        frame->positional_params = frame->parent->positional_params;
        break;
    case EXEC_SCOPE_SPECIAL:
    default:
        Expects(false);
        break;
    }
}

static void init_fd_table_by_policy(exec_frame_t *frame)
{
    Expects_not_null(frame);
    switch (frame->policy->fd_table_scope)
    {
    case EXEC_SCOPE_NONE:
    case EXEC_SCOPE_OWN:
        frame->fd_table = fd_table_create();
        break;
    case EXEC_SCOPE_COPY:
        frame->fd_table = fd_table_clone(frame->parent->fd_table);
        break;
    case EXEC_SCOPE_SHARE:
        frame->fd_table = frame->parent->fd_table;
        break;
    case EXEC_SCOPE_SPECIAL:
    default:
        Expects(false);
        break;
    }
}

static void init_trap_store_by_policy(exec_frame_t *frame)
{
    Expects_not_null(frame);
    switch (frame->policy->trap_scope)
    {
    case EXEC_SCOPE_NONE:
    case EXEC_SCOPE_OWN:
        frame->trap_store = trap_store_create();
        break;
    case EXEC_SCOPE_COPY:
        frame->trap_store = trap_store_clone(frame->parent->trap_store);
        break;
    case EXEC_SCOPE_SHARE:
        frame->trap_store = frame->parent->trap_store;
        break;
    case EXEC_SCOPE_SPECIAL:
    default:
        Expects(false);
        break;
    }
}

static void init_opt_flags_scope_by_policy(exec_frame_t *frame)
{
    Expects_not_null(frame);
    switch (frame->policy->opt_flags_scope)
    {
    case EXEC_SCOPE_NONE:
    case EXEC_SCOPE_OWN:
        frame->opt_flags = exec_opt_flags_create();
        break;
    case EXEC_SCOPE_COPY:
        frame->opt_flags = exec_opt_flags_clone(frame->parent->opt_flags);
        break;
    case EXEC_SCOPE_SHARE:
        frame->opt_flags = frame->parent->opt_flags;
        break;
    case EXEC_SCOPE_SPECIAL:
    default:
        Expects(false);
        break;
    }
}

static string_t *get_working_directory_from_system(void)
{
#ifdef POSIX_API
    char cwd_buffer[PATH_MAX];
    char *cwd = getcwd(cwd_buffer, sizeof(cwd_buffer));
    return cwd ? string_create_from_cstr(cwd) : string_create_from_cstr("/");
#elifdef UCRT_API
    char cwd_buffer[_MAX_PATH];
    char *cwd = _getcwd(cwd_buffer, sizeof(cwd_buffer));
    return cwd ? string_create_from_cstr(cwd) : string_create_from_cstr("C:\\");
#else
    // ISO C has no standard way to get cwd
    return string_create_from_cstr(".");
#endif
}

static void init_working_directory_by_policy(exec_frame_t *frame)
{
    Expects_not_null(frame);
    switch (frame->policy->working_directory_scope)
    {
    case EXEC_SCOPE_NONE:
    case EXEC_SCOPE_OWN:
        if (frame->policy->working_directory_init_with_system)
            frame->working_directory = get_working_directory_from_system();
        else
            frame->working_directory = string_t_create();
        break;
    case EXEC_SCOPE_COPY:
        frame->working_directory = string_t_clone(frame->parent->working_directory);
        break;
    case EXEC_SCOPE_SHARE:
        frame->working_directory = frame->parent->working_directory;
        break;
    case EXEC_SCOPE_SPECIAL:
    default:
        Expects(false);
        break;
    }
}

static umask_t exec_get_umask_from_system(void)
{
#ifdef POSIX_API
    mode_t current_mask = umask(0);
    umask(current_mask);
    return current_mask;
#elifdef UCRT_API
    int current_mask = _umask(_S_IREAD | _S_IWRITE);
    _umask(current_mask);
    return current_mask;
#else
    return 0; // ISO C has no umask
#endif
}

static void init_umask_by_policy(exec_frame_t *frame)
{
    Expects_not_null(frame);
    switch (frame->policy->umask_scope)
    {
    case EXEC_SCOPE_NONE:
    case EXEC_SCOPE_OWN:
        umask_t *umask = xcalloc(1, sizeof(umask_t));
        if (frame->policy->umask_init_with_system)
            *umask = exec_get_umask_from_system();
        else if (frame->policy->umask_init_to_0022)
            *umask = 0022;
        else
            *umask = 0;
        frame->umask = umask;
        break;
    case EXEC_SCOPE_COPY:
        frame->umask = frame->parent->umask;
        break;
    case EXEC_SCOPE_SHARE:
        // umask is always per-process, so sharing means just using parent's value
        frame->umask = frame->parent->umask;
        break;
    case EXEC_SCOPE_SPECIAL:
    default:
        Expects(false);
        break;
    }
}

static void init_function_store_by_policy(exec_frame_t *frame)
{
    Expects_not_null(frame);
    switch (frame->policy->function_scope)
    {
    case EXEC_SCOPE_NONE:
    case EXEC_SCOPE_OWN:
        frame->function_store = func_store_create();
        break;
    case EXEC_SCOPE_COPY:
        frame->function_store = func_store_clone(frame->parent->function_store);
        break;
    case EXEC_SCOPE_SHARE:
        frame->function_store = frame->parent->function_store;
        break;
    case EXEC_SCOPE_SPECIAL:
    default:
        Expects(false);
        break;
    }
}

static exec_frame_t *exec_frame_create_and_push(exec_frame_t *parent, exec_frame_type_t type)
{
    Expects_not_null(parent);
    exec_frame_t *frame = xcalloc(1, sizeof(exec_frame_t));
    frame->type = type;
    frame->policy = &EXEC_FRAME_POLICIES[type];
    
    /* Forbidden policies */
    Expects_neq(frame->policy->variable_scope, EXEC_SCOPE_SPECIAL);
    Expects_neq(frame->policy->fd_table_scope, EXEC_SCOPE_SPECIAL);
    Expects_neq(frame->policy->trap_scope, EXEC_SCOPE_SPECIAL);
    Expects_neq(frame->policy->working_directory_scope, EXEC_SCOPE_SPECIAL);
    frame->executor = parent->executor;
    frame->parent = parent;

    init_variable_store_by_policy(frame);
    init_positional_params_by_policy(frame);
    if (frame->policy->position_can_override)
    {
        /* The positional parameters may be shared with parent, so we'll shallow copy .*/
        frame->saved_positional_params = frame->positional_params;
    }
    init_function_store_by_policy(frame);
    init_fd_table_by_policy(frame);
    init_trap_store_by_policy(frame);
    init_opt_flags_scope_by_policy(frame);
    init_working_directory_by_policy(frame);

    init_umask_by_policy(frame);
    frame->loop_depth = parent->loop_depth;
    frame->last_exit_status = 0;
    frame->last_bg_pid = 0;
    frame->source_name = string_t_create();
    frame->source_line = 0;
    frame->in_trap_handler = false;
}

static void exec_frame_destroy(exec_frame_t **frame_ptr)
{
    Expects_not_null(frame_ptr);
    exec_frame_t *frame = *frame_ptr;
    Expects_not_null(frame);

    // Free resources associated with the frame
    if (frame->source_name)
        string_destroy(&frame->source_name);
    if (frame->variable_store && frame->policy->variable_store != EXEC_SCOPE_SHARE)
        variable_store_destroy(&frame->variable_store);
    if (frame->local_variable_store)
        variable_store_destroy(&frame->local_variable_store);
    if (frame->function_store && frame->policy->function_scope != EXEC_SCOPE_SHARE)
        func_store_destroy(&frame->function_store);
    if (frame->fd_table && frame->policy->fd_table_scope != EXEC_SCOPE_SHARE)
        fd_table_destroy(&frame->fd_table);
    if (frame->trap_store && frame->policy->trap_scope != EXEC_SCOPE_SHARE)
        trap_store_destroy(&frame->trap_store);
    if (frame->opt_flags && frame->policy->opt_flags_scope != EXEC_SCOPE_SHARE)
        exec_opt_flags_destroy(&frame->opt_flags);
    if (frame->working_directory && frame->policy->working_directory_scope != EXEC_SCOPE_SHARE)
        string_destroy(&frame->working_directory);
    if (frame->umask && frame->policy->umask_scope != EXEC_SCOPE_SHARE)
        xfree(&frame->umask);
    if (frame->positional_params && frame->policy->positional_params_scope != EXEC_SCOPE_SHARE)
        positional_params_destroy(&frame->positional_params);
    if (frame->saved_positional_params)
        positional_params_destroy(&frame->saved_positional_params);
    frame->parent = NULL;
    xfree(frame);
    *frame_ptr = NULL;
}

static exec_frame_t* exec_frame_pop(exec_frame_t** frame_ptr)
{
    exec_frame_t *parent = *frame_ptr->parent;
    exec_frame_destroy(frame_ptr);
    return parent;
}

    /*
┌─────────────────────────────────────────────────────────────────────┐
│ Frame-creating operations:                                          │
│   - Subshell ( )                                                    │
│   - Function call                                                   │
│   - Loop (for/while/until)                                          │
│   - Dot script                                                      │
│   - Eval                                                            │
│   - Async (&)                                                       │
│   - Pipeline commands (implicit subshells)                          │
│   - Trap handler                                                    │
│                                                                     │
│   These call: exec_in_frame(parent, params)                         │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│ Frame-using operations (receive frame, don't create one):           │
│   - Compound list execution                                         │
│   - And-or list execution                                           │
│   - Pipeline orchestration                                          │
│   - Simple command (assignments, builtins, externals)               │
│   - Redirections                                                    │
│   - Word expansion                                                  │
│   - Variable assignment                                             │
│                                                                     │
│   These call: do_whatever(frame, ...)                               │
└─────────────────────────────────────────────────────────────────────┘


*/

/* The general entry point for "execute something in a frame" */
exec_result_t exec_in_frame(exec_frame_t *parent, exec_params_t *params)
{
    /* Fork if required */
    if (parent->policy->forks_process)
    {
#ifdef POSIX_API
        int pid = fork();
        if (pid < 0)
        {
            /* Fork failed */
            return (exec_result_t){.exit_status = -1,
                                   .has_exit_status = true,
                                   .flow = EXEC_FLOW_NONE,
                                   .flow_depth = 0};
        }
        else if (pid > 0)
        {
            if (parent->policy->is_background_job)
            {
                /* Parent process: wait for child to complete or record background job */

                /* Record background job */
                // FIXME: replace "background job" with actual command line
                int job_id = job_store_add(parent->executor->jobs,
                                           string_create_cstr("background job"), true);
                job_store_add_process(parent->executor->jobs, job_id, pid,
                                      string_create_cstr("background job"));
                return (exec_result_t){.exit_status = 0,
                                       .has_exit_status = true,
                                       .flow = EXEC_FLOW_NORMAL,
                                       .flow_depth = 0};
            }
            else
            {
                /* Parent process will wait for foreground job to complete */
                int status;
                waitpid(pid, &status, 0);
                int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                return (exec_result_t){.exit_status = exit_status,
                                       .has_exit_status = true,
                                       .flow = EXEC_FLOW_NORMAL,
                                       .flow_depth = 0};
            }
        }
        // else pid == 0: child process continues below
#elif defined(UCRT_API)
        int pid = _fork();
        if (pid < 0)
        {
            /* Fork failed */
            return (exec_result_t){.exit_status = -1,
                                   .has_exit_status = true,
                                   .flow = EXEC_FLOW_NONE,
                                   .flow_depth = 0};
        }
        else if (pid > 0)
        {
            if (parent->policy->is_background_job)
            {
                /* Parent process: wait for child to complete or record background job */
                /* Record background job */
                // FIXME: replace "background job" with actual command line
                int job_id = job_store_add(parent->executor->jobs,
                                           string_create_cstr("background job"), true);
                job_store_add_process(parent->executor->jobs, job_id, pid,
                                      string_create_cstr("background job"));
                return (exec_result_t){.exit_status = 0,
                                       .has_exit_status = true,
                                       .flow = EXEC_FLOW_NORMAL,
                                       .flow_depth = 0};
            }
            else
            {
                /* Parent process will wait for foreground job to complete */
                int status;
                waitpid(pid, &status, 0);
                int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                return (exec_result_t){.exit_status = exit_status,
                                       .has_exit_status = true,
                                       .flow = EXEC_FLOW_NORMAL,
                                       .flow_depth = 0};
            }
        }
        // else pid == 0: child process continues below
#else
        /* ISO C: no fork support */
        fprintf(stderr,
                "Warning: background execution not supported. Running '%s' in foreground.\n",
                "command"); // FIXME: actual command
        /* Continue as unforked process. */
#endif
    }

    /* Either unforked or child process continues */
    exec_frame_t *frame = exec_frame_create_and_push(parent, params->frame_type);

    /* Handle positional parameter overrides */
    if (params->arguments)
    {
        switch (frame->policy->positional_params_scope)
        {
        case EXEC_SCOPE_NONE:
            /* No positional parameters in this frame */
            log_error("exec_in_frame: cannot set positional parameters in NONE scope");
            return (exec_result_t){.exit_status = 1,
                                   .has_exit_status = true,
                                   .flow = EXEC_FLOW_NORMAL,
                                   .flow_depth = 0};
            break;
        case EXEC_SCOPE_OWN:
            frame->positional_params = positional_params_create_from_string_list(
                // what about $0?
                string_create_from_cstr("shell"), params->arguments);
            break;
        case EXEC_SCOPE_COPY:
            /* Handled in constructor */
            break;
        case EXEC_SCOPE_SHARE:
            if (frame->policy->position_can_override)
            {
                /* Shared positional params: temporarily override */
                frame->saved_positional_params = frame->positional_params;
            }
            frame->positional_params = positional_params_create_from_string_list(
                positional_params_copy_arg0(parent->positional_params), params->arguments);
            break;
        case EXEC_SCOPE_SPECIAL:
        default:
            log_error("exec_in_frame: unhandled positional_params_scope");
            return (exec_result_t){.exit_status = 1,
                                   .has_exit_status = true,
                                   .flow = EXEC_FLOW_NORMAL,
                                   .flow_depth = 0};
            break;
        }
    }

    /* Apply redirections */
    // FIXME: this is all wrong
    if (params->redirections)
    {
        switch (frame->policy->fd_table_scope)
        {
            case EXEC_SCOPE_NONE:
                log_error("exec_in_frame: cannot apply redirections in NONE scope");
                return (exec_result_t){.exit_status = 1,
                                       .has_exit_status = true,
                                       .flow = EXEC_FLOW_NORMAL,
                                       .flow_depth = 0};
                break;
            case EXEC_SCOPE_OWN:
            case EXEC_SCOPE_COPY:
            case EXEC_SCOPE_SHARE:
                exec_apply_redirections(frame, params->redirections);
                break;
            case EXEC_SCOPE_SPECIAL:
            default:
                log_error("exec_in_frame: unhandled fd_table_scope");
                return (exec_result_t){.exit_status = 1,
                                       .has_exit_status = true,
                                       .flow = EXEC_FLOW_NORMAL,
                                       .flow_depth = 0};
                break;
        }
    }

    /* Execute body */
    if (params->condition)
    {
        // While or until loop
        result = exec_execute_condition_loop(frame, params);
    }
    else if (params->iteration_words)
    {
        // For loop
        result = exec_execute_iteration_loop(frame, params);
    }
    else if (params->body)
    {
        // General body execution
        result = exec_execute_compound_list(frame, params->body);
    }
    else
    {
        // No body to execute
        result = (exec_result_t){
            .exit_status = 0, .has_exit_status = true, .flow = EXEC_FLOW_NORMAL, .flow_depth = 0};
    }

    /* Handle control flow */
    result = exec_handle_control_flow(frame, result);

    /* Restore redirections */
    if (params->redirections)
    {
        exec_restore_redirections(frame, params->redirections);
    }

    /* Restore positional parameters if overridden */
    if (frame->saved_positional_params)
    {
        if (frame->policy->position_params_scope == EXEC_SCOPE_SHARE)
        positional_params_destroy(&frame->positional_params);
        frame->positional_params = frame->saved_positional_params;
        frame->saved_positional_params = NULL;
    }

    /* Exit status */
    if (frame->policy->exit_affects_parent_exit_status && result.has_exit_status)
    {
        parent->last_exit_status = result.exit_status;
    }

    /* Cleanup and teardown */
    bool terminate_process = frame->policy->exit_terminates_process;
    bool process_was_forked = parent->policy->forks_process;
    exec_frame_pop(&frame);

    /* Exit process if required */
    if (terminate_process)
    {
        exit(result.has_exit_status ? result.exit_status : 0);
    }
    else if (process_was_forked)
    {
        /* We are a child process, so we need to exit with the correct status */
        _exit(result.has_exit_status ? result.exit_status : 0);
    }
    // else: we are not a child process of a fork, so just return

    return result;
}

static fd_table_t *get_fd_table(exec_frame_t *frame)
{
    Expects_not_null(frame);
    Expects_neq(frame->policy->fd_table_scope, EXEC_SCOPE_NONE);
    Expects_neq(frame->policy->fd_table_scope, EXEC_SCOPE_SPECIAL);
    /* For EXEC_SCOPE_SHARE, iterate up to find a valid jobs table. */
    exec_frame_t *target_frame = frame;
    while (target_frame && target_frame->policy->fd_table_scope == EXEC_SCOPE_SHARE)
    {
        target_frame = target_frame->parent;
    }
    Expects_not_null(target_frame);
    Expects_not_null(target_frame->open_fds);
    return target_frame->open_fds;
}

static void exec_apply_redirection(exec_frame_t *frame, ast_node_t *redirection,
                                     fd_table_t *fd_table)
{
    Expects_not_null(frame);
    Expects_not_null(redirection);
    Expects_not_null(fd_table);
    // This is a stub implementation.
    // A full implementation would handle various redirection types,
    // such as input/output redirection, appending, here-documents, etc.
    int fd = redirection->data.redirection.fd; // The file descriptor to redirect
    const char *target = redirection->data.redirection.target; // The target file or descriptor
    // For example, handle output redirection (">")
    if (redirection->data.redirection.type == REDIR_OUTPUT)
    {
        int new_fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (new_fd < 0)
        {
            exec_set_error(frame->executor, "Failed to open file for output redirection: %s", target);
            return;
        }
        fd_table_set_entry(fd_table, fd, new_fd);
    }
    // Handle other redirection types similarly...
}

void exec_apply_redirections(exec_frame_t *frame, ast_node_t *redirections)
{
    Expects_not_null(frame);
    Expects_not_null(redirections);

    fd_table_t *fd_table = get_fd_table(frame);

    for (ast_node_t *node = redirections; node; node = node->next)
    {
        // Apply each redirection to the frame's open_fds
        exec_apply_redirection(frame, node, fd_table);
    }
}

void exec_restore_redirections(exec_frame_t *frame, ast_node_t *redirections)
{
    Expects_not_null(frame);
    Expects_not_null(redirections);
    fd_table_t *fd_table = get_fd_table(frame);
    for (ast_node_t *node = redirections; node; node = node->next)
    {
        // Restore each redirection in the frame's open_fds
        int fd = node->data.redirection.fd;
        fd_table_restore_entry(fd_table, fd);
    }
}

static exec_frame_t *exec_frame_find_return_target(exec_frame_t *frame)
{
    Expects_not_null(frame);
    exec_frame_t *current = frame;
    while (current)
    {
        if (current->policy->is_return_target)
        {
            return current;
        }
        current = current->parent;
    }
    return NULL;
}

exec_result_t exec_handle_control_flow(exec_frame_t *frame, exec_result_t result)
{
    Expects_not_null(frame);
    switch (result.flow)
    {
    case EXEC_FLOW_NORMAL:
        exec_frame_t *return_target = exec_frame_find_return_target(frame);
        if (!return_target)
        {
            log_error("exec_handle_control_flow: no return target found");
            return (exec_result_t){.exit_status = 1,
                                   .has_exit_status = true,
                                   .flow = EXEC_FLOW_NORMAL,
                                   .flow_depth = 0};
        }
        if (result.flow_depth > 1)
        {
            // Propagate return up the stack
            return (exec_result_t){.exit_status = result.exit_status,
                                   .has_exit_status = result.has_exit_status,
                                   .flow = EXEC_FLOW_RETURN,
                                   .flow_depth = result.flow_depth - 1};
        }
        else
        {
            // Return to target frame
            return (exec_result_t){.exit_status = result.exit_status,
                                   .has_exit_status = result.has_exit_status,
                                   .flow = EXEC_FLOW_NORMAL,
                                   .flow_depth = 0};
        }
        break;
    case EXEC_FLOW_RETURN:
        /* Handle a function return. */
        break;
    case EXEC_FLOW_BREAK:
        /* Handle a break statement in a looping context. */
        break;
    case EXEC_FLOW_CONTINUE:
        /* Handle a continue statement in a looping context. */
        break;
    default:
        break;
    }
}

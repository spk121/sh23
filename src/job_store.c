// ============================================================================
// job_store.c
// Job control store implementation
// ============================================================================

#include "job_store.h"
#include "xalloc.h"
#include <string.h>

// ============================================================================
// Internal Helper Functions
// ============================================================================

/**
 * Create a new process node.
 */
#ifdef POSIX_API
static process_t *process_create(pid_t pid, string_t *command)
#else
static process_t *process_create(int pid, string_t *command)
#endif
{
    process_t *proc = xcalloc(1, sizeof(process_t));
    proc->pid = pid;
    proc->command = command;  // Takes ownership
    proc->state = JOB_RUNNING;
    proc->exit_status = 0;
    proc->next = NULL;
    return proc;
}

/**
 * Free a process and its command.
 */
static void process_destroy(process_t *proc)
{
    if (!proc)
        return;

    if (proc->command)
        string_destroy(&proc->command);

    xfree(proc);
}

/**
 * Free a linked list of processes.
 */
static void process_list_destroy(process_t *head)
{
    while (head)
    {
        process_t *next = head->next;
        process_destroy(head);
        head = next;
    }
}

/**
 * Create a new job node.
 */
static job_t *job_create(int job_id, string_t *command_line, bool is_background)
{
    job_t *job = xcalloc(1, sizeof(job_t));
    job->job_id = job_id;
    job->pgid = 0;
    job->processes = NULL;
    job->command_line = command_line;  // Takes ownership
    job->state = JOB_RUNNING;
    job->is_background = is_background;
    job->is_notified = false;
    job->next = NULL;
    return job;
}

/**
 * Free a job and all its processes.
 */
static void job_destroy(job_t *job)
{
    if (!job)
        return;

    process_list_destroy(job->processes);

    if (job->command_line)
        string_destroy(&job->command_line);

    xfree(job);
}

/**
 * Update job state based on its processes.
 * A job is:
 * - STOPPED if any process is stopped
 * - RUNNING if any process is running
 * - TERMINATED if any process was terminated
 * - DONE if all processes are done
 */
static void job_update_state(job_t *job)
{
    if (!job || !job->processes)
        return;

    bool has_running = false;
    bool has_stopped = false;
    bool has_terminated = false;
    bool all_done = true;

    for (process_t *proc = job->processes; proc; proc = proc->next)
    {
        switch (proc->state)
        {
            case JOB_RUNNING:
                has_running = true;
                all_done = false;
                break;
            case JOB_STOPPED:
                has_stopped = true;
                all_done = false;
                break;
            case JOB_TERMINATED:
                has_terminated = true;
                all_done = false;
                break;
            case JOB_DONE:
                // Continue checking
                break;
        }
    }

    // Determine overall state
    if (has_stopped)
        job->state = JOB_STOPPED;
    else if (has_running)
        job->state = JOB_RUNNING;
    else if (has_terminated)
        job->state = JOB_TERMINATED;
    else if (all_done)
        job->state = JOB_DONE;
}

// ============================================================================
// Lifecycle Functions
// ============================================================================

job_store_t *job_store_create(void)
{
    job_store_t *store = xcalloc(1, sizeof(job_store_t));
    store->jobs = NULL;
    store->next_job_id = 1;
    store->current_job = NULL;
    store->previous_job = NULL;
    store->job_count = 0;
    return store;
}

void job_store_destroy(job_store_t **store)
{
    if (!store || !*store)
        return;

    job_store_t *s = *store;

    // Free all jobs
    job_t *job = s->jobs;
    while (job)
    {
        job_t *next = job->next;
        job_destroy(job);
        job = next;
    }

    xfree(s);
    *store = NULL;
}

// ============================================================================
// Job Creation
// ============================================================================

int job_store_add(job_store_t *store, string_t *command_line, bool is_background)
{
    if (!store)
        return -1;

    int job_id = store->next_job_id++;
    job_t *new_job = job_create(job_id, command_line, is_background);

    // Add to front of list
    new_job->next = store->jobs;
    store->jobs = new_job;
    store->job_count++;

    // Update current/previous pointers
    if (is_background)
    {
        store->previous_job = store->current_job;
        store->current_job = new_job;
    }

    return job_id;
}

#ifdef POSIX_API
bool job_store_add_process(job_store_t *store, int job_id, pid_t pid, string_t *command)
#else
bool job_store_add_process(job_store_t *store, int job_id, int pid, string_t *command)
#endif
{
    if (!store)
        return false;

    job_t *job = job_store_find(store, job_id);
    if (!job)
        return false;

    process_t *new_proc = process_create(pid, command);

    // Add to end of process list
    if (!job->processes)
    {
        job->processes = new_proc;
        job->pgid = pid;  // First process sets the process group
    }
    else
    {
        process_t *last = job->processes;
        while (last->next)
            last = last->next;
        last->next = new_proc;
    }

    return true;
}

// ============================================================================
// Job Lookup
// ============================================================================

job_t *job_store_find(job_store_t *store, int job_id)
{
    if (!store)
        return NULL;

    for (job_t *job = store->jobs; job; job = job->next)
    {
        if (job->job_id == job_id)
            return job;
    }

    return NULL;
}

job_t *job_store_get_current(const job_store_t *store)
{
    return store ? store->current_job : NULL;
}

job_t *job_store_get_previous(const job_store_t *store)
{
    return store ? store->previous_job : NULL;
}

// ============================================================================
// Job State Management
// ============================================================================

bool job_store_set_state(job_store_t *store, int job_id, job_state_t new_state)
{
    if (!store)
        return false;

    job_t *job = job_store_find(store, job_id);
    if (!job)
        return false;

    job->state = new_state;
    return true;
}

#ifdef POSIX_API
bool job_store_set_process_state(job_store_t *store, pid_t pid,
                                   job_state_t new_state, int exit_status)
#else
bool job_store_set_process_state(job_store_t *store, int pid,
                                   job_state_t new_state, int exit_status)
#endif
{
    if (!store)
        return false;

    // Find the process in any job
    for (job_t *job = store->jobs; job; job = job->next)
    {
        for (process_t *proc = job->processes; proc; proc = proc->next)
        {
            if (proc->pid == pid)
            {
                proc->state = new_state;
                proc->exit_status = exit_status;

                // Update the overall job state
                job_update_state(job);
                return true;
            }
        }
    }

    return false;
}

bool job_store_mark_notified(job_store_t *store, int job_id)
{
    if (!store)
        return false;

    job_t *job = job_store_find(store, job_id);
    if (!job)
        return false;

    job->is_notified = true;
    return true;
}

// ============================================================================
// Job Removal
// ============================================================================

bool job_store_remove(job_store_t *store, int job_id)
{
    if (!store || !store->jobs)
        return false;

    job_t *prev = NULL;
    job_t *curr = store->jobs;

    while (curr)
    {
        if (curr->job_id == job_id)
        {
            // Update current/previous pointers if needed
            if (store->current_job == curr)
                store->current_job = store->previous_job;
            if (store->previous_job == curr)
                store->previous_job = NULL;

            // Remove from list
            if (prev)
                prev->next = curr->next;
            else
                store->jobs = curr->next;

            job_destroy(curr);
            store->job_count--;
            return true;
        }

        prev = curr;
        curr = curr->next;
    }

    return false;
}

size_t job_store_remove_completed(job_store_t *store)
{
    if (!store)
        return 0;

    size_t removed = 0;
    job_t *prev = NULL;
    job_t *curr = store->jobs;

    while (curr)
    {
        job_t *next = curr->next;

        if (job_is_completed(curr) && curr->is_notified)
        {
            // Update current/previous pointers if needed
            if (store->current_job == curr)
                store->current_job = store->previous_job;
            if (store->previous_job == curr)
                store->previous_job = NULL;

            // Remove from list
            if (prev)
                prev->next = next;
            else
                store->jobs = next;

            job_destroy(curr);
            store->job_count--;
            removed++;

            // Don't update prev since we removed curr
        }
        else
        {
            prev = curr;
        }

        curr = next;
    }

    return removed;
}

// ============================================================================
// Utility Functions
// ============================================================================

size_t job_store_count(const job_store_t *store)
{
    return store ? store->job_count : 0;
}

bool job_is_running(const job_t *job)
{
    if (!job)
        return false;

    for (const process_t *proc = job->processes; proc; proc = proc->next)
    {
        if (proc->state == JOB_RUNNING)
            return true;
    }

    return false;
}

bool job_is_completed(const job_t *job)
{
    if (!job || !job->processes)
        return false;

    for (const process_t *proc = job->processes; proc; proc = proc->next)
    {
        if (proc->state == JOB_RUNNING || proc->state == JOB_STOPPED)
            return false;
    }

    return true;
}

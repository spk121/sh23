// ============================================================================
// job_store.h
// Job control store for managing background jobs and processes
// ============================================================================

#ifndef JOB_STORE_H
#define JOB_STORE_H

#include "string_t.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef POSIX_API
#include <sys/types.h>  // For pid_t
#endif

// ============================================================================
// Job State
// ============================================================================

typedef enum job_state_t
{
    JOB_RUNNING,     // Job is currently running
    JOB_STOPPED,     // Job is stopped (suspended)
    JOB_DONE,        // Job completed successfully
    JOB_TERMINATED   // Job was terminated by signal
} job_state_t;

// ============================================================================
// Process - Individual process within a pipeline
// ============================================================================

typedef struct process_t
{
    struct process_t *next; // Next process in pipeline
    string_t *command;      // Command string for this process
#ifdef POSIX_API
    pid_t pid;              // Process ID
#else
    int pid;                // Process ID (or 0 if not available)
#endif
    int exit_status;        // Exit status (if done) or signal number (if terminated)
    job_state_t state;      // Current state of this process
    int padding;
} process_t;

// ============================================================================
// Job - A pipeline or single command (possibly backgrounded)
// ============================================================================

typedef struct job_t
{
    int job_id;             // Job number (for %1, %2, etc.)
#ifdef POSIX_API
    pid_t pgid;             // Process group ID
#else
    int pgid;               // Process group ID (or 0 if not available)
#endif
    process_t *processes;   // Linked list of processes in this job
    string_t *command_line; // Full command line as typed by user
    job_state_t state;      // Overall state of the job
    bool is_background;     // Whether job was started with &
    bool is_notified;       // Whether user has been notified of status change
    char padding[2];
    struct job_t *next;     // Next job in the job list
} job_t;

// ============================================================================
// Job Store - Table of all jobs
// ============================================================================

typedef struct job_store_t
{
    job_t *jobs;            // Linked list of jobs
    job_t *current_job;  // Job referenced by %% or %+
    job_t *previous_job; // Job referenced by %-
    size_t job_count;    // Number of jobs in the list
    int next_job_id;     // Next job ID to assign
    int padding;
} job_store_t;

// ============================================================================
// Lifecycle Functions
// ============================================================================

/**
 * Create a new job store.
 *
 * @return A newly allocated job store, or NULL on allocation failure
 */
job_store_t *job_store_create(void);

/**
 * Destroy a job store and free all associated memory.
 * Safe to call with NULL.
 *
 * @param store Pointer to pointer to job store (set to NULL after destruction)
 */
void job_store_destroy(job_store_t **store);

// ============================================================================
// Job Creation
// ============================================================================

/**
 * Create a new job and add it to the store.
 *
 * @param store The job store
 * @param command_line The full command line as typed by user (takes ownership)
 * @param is_background Whether this is a background job
 * @return The newly created job ID, or -1 on failure
 */
int job_store_add(job_store_t *store, string_t *command_line, bool is_background);

/**
 * Add a process to a job.
 *
 * @param store The job store
 * @param job_id The job ID to add the process to
 * @param pid The process ID
 * @param command The command string for this process (takes ownership)
 * @return true on success, false on failure
 */
#ifdef POSIX_API
bool job_store_add_process(job_store_t *store, int job_id, pid_t pid, string_t *command);
#else
bool job_store_add_process(job_store_t *store, int job_id, int pid, string_t *command);
#endif

// ============================================================================
// Job Lookup
// ============================================================================

/**
 * Find a job by job ID.
 *
 * @param store The job store
 * @param job_id The job ID to find
 * @return Pointer to the job, or NULL if not found
 */
job_t *job_store_find(job_store_t *store, int job_id);

/**
 * Get the current job (referenced by %% or %+).
 *
 * @param store The job store
 * @return Pointer to the current job, or NULL if none
 */
job_t *job_store_get_current(const job_store_t *store);

/**
 * Get the previous job (referenced by %-).
 *
 * @param store The job store
 * @return Pointer to the previous job, or NULL if none
 */
job_t *job_store_get_previous(const job_store_t *store);

// ============================================================================
// Job State Management
// ============================================================================

/**
 * Update the state of a job.
 *
 * @param store The job store
 * @param job_id The job ID to update
 * @param new_state The new state
 * @return true on success, false if job not found
 */
bool job_store_set_state(job_store_t *store, int job_id, job_state_t new_state);

/**
 * Update the state of a specific process.
 *
 * @param store The job store
 * @param pid The process ID
 * @param new_state The new state
 * @param exit_status The exit status (if applicable)
 * @return true on success, false if process not found
 */
#ifdef POSIX_API
bool job_store_set_process_state(job_store_t *store, pid_t pid,
                                   job_state_t new_state, int exit_status);
#else
bool job_store_set_process_state(job_store_t *store, int pid,
                                   job_state_t new_state, int exit_status);
#endif

/**
 * Mark a job as notified.
 *
 * @param store The job store
 * @param job_id The job ID
 * @return true on success, false if job not found
 */
bool job_store_mark_notified(job_store_t *store, int job_id);

// ============================================================================
// Job Removal
// ============================================================================

/**
 * Remove a job from the store.
 * The job and all its processes are freed.
 *
 * @param store The job store
 * @param job_id The job ID to remove
 * @return true if job was found and removed, false otherwise
 */
bool job_store_remove(job_store_t *store, int job_id);

/**
 * Remove all completed jobs from the store.
 *
 * @param store The job store
 * @return Number of jobs removed
 */
size_t job_store_remove_completed(job_store_t *store);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Get the number of jobs in the store.
 *
 * @param store The job store
 * @return Number of jobs
 */
size_t job_store_count(const job_store_t *store);

/**
 * Check if a job is still running.
 * A job is running if any of its processes are still running.
 *
 * @param job The job to check
 * @return true if job is running, false otherwise
 */
bool job_is_running(const job_t *job);

/**
 * Check if a job is completed.
 * A job is completed if all processes are done or terminated.
 *
 * @param job The job to check
 * @return true if job is completed, false otherwise
 */
bool job_is_completed(const job_t *job);

#endif // JOB_STORE_H

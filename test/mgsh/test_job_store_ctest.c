/**
 * @file test_job_store_ctest.c
 * @brief Unit tests for job store (job_store.c)
 */

#include <string.h>
#include "ctest.h"
#include "job_store.h"
#include "string_t.h"
#include "xalloc.h"

// ------------------------------------------------------------
// Creation and Destruction Tests
// ------------------------------------------------------------

CTEST(test_job_store_create)
{
    job_store_t *store = job_store_create();
    CTEST_ASSERT_NOT_NULL(ctest, store, "store created");
    CTEST_ASSERT_EQ(ctest, job_store_count(store), 0, "initial count is 0");
    CTEST_ASSERT_NULL(ctest, job_store_get_current(store), "current job is null");
    CTEST_ASSERT_NULL(ctest, job_store_get_previous(store), "previous job is null");
    CTEST_ASSERT_NULL(ctest, job_store_first(store), "first job is null");
    job_store_destroy(&store);
    CTEST_ASSERT_NULL(ctest, store, "store pointer null after destroy");
}

CTEST(test_job_store_destroy_null)
{
    job_store_t *store = NULL;
    job_store_destroy(&store); // Should not crash
    CTEST_ASSERT_NULL(ctest, store, "null pointer handled");
}

// ------------------------------------------------------------
// Job Creation Tests
// ------------------------------------------------------------

CTEST(test_job_store_add_basic)
{
    job_store_t *store = job_store_create();

    string_t *cmd = string_create_from_cstr("sleep 100");
    int job_id = job_store_add(store, cmd, true);

    CTEST_ASSERT_TRUE(ctest, job_id > 0, "valid job_id returned");
    CTEST_ASSERT_EQ(ctest, job_store_count(store), 1, "count is 1");

    job_t *job = job_store_find(store, job_id);
    CTEST_ASSERT_NOT_NULL(ctest, job, "job found");
    CTEST_ASSERT_EQ(ctest, job->job_id, job_id, "job_id matches");
    CTEST_ASSERT_TRUE(ctest, job->is_background, "is_background is true");
    CTEST_ASSERT_EQ(ctest, job->state, JOB_RUNNING, "initial state is RUNNING");
    CTEST_ASSERT_FALSE(ctest, job->is_notified, "is_notified is false");

	string_destroy(&cmd);
    job_store_destroy(&store);
}

CTEST(test_job_store_add_foreground)
{
    job_store_t *store = job_store_create();

    string_t *cmd = string_create_from_cstr("cat file.txt");
    int job_id = job_store_add(store, cmd, false);

    CTEST_ASSERT_TRUE(ctest, job_id > 0, "valid job_id returned");

    job_t *job = job_store_find(store, job_id);
    CTEST_ASSERT_NOT_NULL(ctest, job, "job found");
    CTEST_ASSERT_FALSE(ctest, job->is_background, "is_background is false");

    // Foreground jobs should not update current/previous pointers
    CTEST_ASSERT_NULL(ctest, job_store_get_current(store), "current job is null for foreground");
	string_destroy(&cmd);
    job_store_destroy(&store);
}

CTEST(test_job_store_add_multiple)
{
    job_store_t *store = job_store_create();

    string_t *cmd1 = string_create_from_cstr("sleep 100");
    string_t *cmd2 = string_create_from_cstr("sleep 200");
    string_t *cmd3 = string_create_from_cstr("sleep 300");

    int job1 = job_store_add(store, cmd1, true);
    int job2 = job_store_add(store, cmd2, true);
    int job3 = job_store_add(store, cmd3, true);

    CTEST_ASSERT_EQ(ctest, job_store_count(store), 3, "count is 3");
    CTEST_ASSERT_TRUE(ctest, job1 < job2, "job IDs are sequential");
    CTEST_ASSERT_TRUE(ctest, job2 < job3, "job IDs are sequential");

	string_destroy(&cmd1);
	string_destroy(&cmd2);
	string_destroy(&cmd3);
    job_store_destroy(&store);
}

CTEST(test_job_store_current_previous)
{
    job_store_t *store = job_store_create();

    string_t *cmd1 = string_create_from_cstr("job1");
    string_t *cmd2 = string_create_from_cstr("job2");
    string_t *cmd3 = string_create_from_cstr("job3");

    int job1 = job_store_add(store, cmd1, true);
    job_t *current = job_store_get_current(store);
    CTEST_ASSERT_NOT_NULL(ctest, current, "current set after first background job");
    CTEST_ASSERT_EQ(ctest, current->job_id, job1, "current is job1");
    CTEST_ASSERT_NULL(ctest, job_store_get_previous(store), "previous is null");

    int job2 = job_store_add(store, cmd2, true);
    current = job_store_get_current(store);
    job_t *previous = job_store_get_previous(store);
    CTEST_ASSERT_NOT_NULL(ctest, current, "current updated");
    CTEST_ASSERT_EQ(ctest, current->job_id, job2, "current is job2");
    CTEST_ASSERT_NOT_NULL(ctest, previous, "previous set");
    CTEST_ASSERT_EQ(ctest, previous->job_id, job1, "previous is job1");

    int job3 = job_store_add(store, cmd3, true);
    current = job_store_get_current(store);
    previous = job_store_get_previous(store);
    CTEST_ASSERT_EQ(ctest, current->job_id, job3, "current is job3");
    CTEST_ASSERT_EQ(ctest, previous->job_id, job2, "previous is job2");

	string_destroy(&cmd1);
	string_destroy(&cmd2);
	string_destroy(&cmd3);
    job_store_destroy(&store);
}

// ------------------------------------------------------------
// Process Management Tests
// ------------------------------------------------------------

CTEST(test_job_store_add_process)
{
    job_store_t *store = job_store_create();

    string_t *cmd = string_create_from_cstr("cat file.txt | grep foo | sort");
    int job_id = job_store_add(store, cmd, true);

    string_t *proc1_cmd = string_create_from_cstr("cat file.txt");
    string_t *proc2_cmd = string_create_from_cstr("grep foo");
    string_t *proc3_cmd = string_create_from_cstr("sort");

    bool r1 = job_store_add_process(store, job_id, 1001, proc1_cmd);
    bool r2 = job_store_add_process(store, job_id, 1002, proc2_cmd);
    bool r3 = job_store_add_process(store, job_id, 1003, proc3_cmd);

    CTEST_ASSERT_TRUE(ctest, r1, "first process added");
    CTEST_ASSERT_TRUE(ctest, r2, "second process added");
    CTEST_ASSERT_TRUE(ctest, r3, "third process added");

    job_t *job = job_store_find(store, job_id);
    CTEST_ASSERT_NOT_NULL(ctest, job, "job found");
    CTEST_ASSERT_EQ(ctest, job->pgid, 1001, "pgid set to first process");

    // Count processes
    int count = 0;
    for (process_t *p = job->processes; p; p = p->next)
        count++;
    CTEST_ASSERT_EQ(ctest, count, 3, "three processes in job");

	string_destroy(&cmd);
    job_store_destroy(&store);
}

CTEST(test_job_store_add_process_invalid_job)
{
    job_store_t *store = job_store_create();

    string_t *proc_cmd = string_create_from_cstr("echo test");
    bool result = job_store_add_process(store, 999, 1001, proc_cmd);

    CTEST_ASSERT_FALSE(ctest, result, "add to non-existent job fails");
    string_destroy(&proc_cmd);
    job_store_destroy(&store);
}

// ------------------------------------------------------------
// Job Lookup Tests
// ------------------------------------------------------------

CTEST(test_job_store_find)
{
    job_store_t *store = job_store_create();

    string_t *cmd1 = string_create_from_cstr("job1");
    string_t *cmd2 = string_create_from_cstr("job2");

    int job1 = job_store_add(store, cmd1, true);
    int job2 = job_store_add(store, cmd2, true);

    job_t *found1 = job_store_find(store, job1);
    job_t *found2 = job_store_find(store, job2);
    job_t *not_found = job_store_find(store, 999);

    CTEST_ASSERT_NOT_NULL(ctest, found1, "job1 found");
    CTEST_ASSERT_NOT_NULL(ctest, found2, "job2 found");
    CTEST_ASSERT_NULL(ctest, not_found, "non-existent job not found");

	string_destroy(&cmd1);
	string_destroy(&cmd2);
    job_store_destroy(&store);
}

CTEST(test_job_store_find_by_pgid)
{
    job_store_t *store = job_store_create();

    string_t *cmd = string_create_from_cstr("sleep 100");
    int job_id = job_store_add(store, cmd, true);

    string_t *proc_cmd = string_create_from_cstr("sleep 100");
    job_store_add_process(store, job_id, 5000, proc_cmd);

    job_t *found = job_store_find_by_pgid(store, 5000);
    CTEST_ASSERT_NOT_NULL(ctest, found, "job found by pgid");
    CTEST_ASSERT_EQ(ctest, found->job_id, job_id, "correct job found");

    job_t *not_found = job_store_find_by_pgid(store, 9999);
    CTEST_ASSERT_NULL(ctest, not_found, "non-existent pgid not found");

	string_destroy(&cmd);
    job_store_destroy(&store);
}

CTEST(test_job_store_find_by_prefix)
{
    job_store_t *store = job_store_create();

    string_t *cmd1 = string_create_from_cstr("sleep 100");
    string_t *cmd2 = string_create_from_cstr("grep foo bar.txt");
    string_t *cmd3 = string_create_from_cstr("cat file.txt");

    int job1 = job_store_add(store, cmd1, true);
    int job2 = job_store_add(store, cmd2, true);
    int job3 = job_store_add(store, cmd3, true);

    job_t *found1 = job_store_find_by_prefix(store, "sleep");
    CTEST_ASSERT_NOT_NULL(ctest, found1, "found by prefix 'sleep'");
    CTEST_ASSERT_EQ(ctest, found1->job_id, job1, "correct job found");

    job_t *found2 = job_store_find_by_prefix(store, "grep");
    CTEST_ASSERT_NOT_NULL(ctest, found2, "found by prefix 'grep'");
    CTEST_ASSERT_EQ(ctest, found2->job_id, job2, "correct job found");

    job_t *found3 = job_store_find_by_prefix(store, "cat");
    CTEST_ASSERT_NOT_NULL(ctest, found3, "found by prefix 'cat'");
    CTEST_ASSERT_EQ(ctest, found3->job_id, job3, "correct job found");

    job_t *not_found = job_store_find_by_prefix(store, "zzz");
    CTEST_ASSERT_NULL(ctest, not_found, "non-matching prefix not found");

	string_destroy(&cmd1);
	string_destroy(&cmd2);
	string_destroy(&cmd3);
    job_store_destroy(&store);
}

CTEST(test_job_store_find_by_substring)
{
    job_store_t *store = job_store_create();

    string_t *cmd1 = string_create_from_cstr("cat file.txt | grep pattern");
    string_t *cmd2 = string_create_from_cstr("ls -la /tmp/directory");

    int job1 = job_store_add(store, cmd1, true);
    int job2 = job_store_add(store, cmd2, true);

    job_t *found1 = job_store_find_by_substring(store, "pattern");
    CTEST_ASSERT_NOT_NULL(ctest, found1, "found by substring 'pattern'");
    CTEST_ASSERT_EQ(ctest, found1->job_id, job1, "correct job found");

    job_t *found2 = job_store_find_by_substring(store, "directory");
    CTEST_ASSERT_NOT_NULL(ctest, found2, "found by substring 'directory'");
    CTEST_ASSERT_EQ(ctest, found2->job_id, job2, "correct job found");

    job_t *found3 = job_store_find_by_substring(store, "file.txt");
    CTEST_ASSERT_NOT_NULL(ctest, found3, "found by substring in middle");
    CTEST_ASSERT_EQ(ctest, found3->job_id, job1, "correct job found");

    job_t *not_found = job_store_find_by_substring(store, "notfound");
    CTEST_ASSERT_NULL(ctest, not_found, "non-matching substring not found");

	string_destroy(&cmd1);
	string_destroy(&cmd2);
    job_store_destroy(&store);
}

CTEST(test_job_store_find_by_prefix_most_recent)
{
    job_store_t *store = job_store_create();

    // Add multiple jobs with same prefix
    string_t *cmd1 = string_create_from_cstr("sleep 100");
    string_t *cmd2 = string_create_from_cstr("sleep 200");
    string_t *cmd3 = string_create_from_cstr("sleep 300");

    int job1 = job_store_add(store, cmd1, true);
    int job2 = job_store_add(store, cmd2, true);
    int job3 = job_store_add(store, cmd3, true);

    // Should return most recent (job3)
    job_t *found = job_store_find_by_prefix(store, "sleep");
    CTEST_ASSERT_NOT_NULL(ctest, found, "found by prefix");
    CTEST_ASSERT_EQ(ctest, found->job_id, job3, "most recent job returned");

	string_destroy(&cmd1);
	string_destroy(&cmd2);
	string_destroy(&cmd3);
    job_store_destroy(&store);
}

// ------------------------------------------------------------
// Job State Management Tests
// ------------------------------------------------------------

CTEST(test_job_store_set_state)
{
    job_store_t *store = job_store_create();

    string_t *cmd = string_create_from_cstr("sleep 100");
    int job_id = job_store_add(store, cmd, true);

    job_t *job = job_store_find(store, job_id);
    CTEST_ASSERT_EQ(ctest, job->state, JOB_RUNNING, "initial state is RUNNING");

    bool r1 = job_store_set_state(store, job_id, JOB_STOPPED);
    CTEST_ASSERT_TRUE(ctest, r1, "set_state succeeded");
    CTEST_ASSERT_EQ(ctest, job->state, JOB_STOPPED, "state updated to STOPPED");

    bool r2 = job_store_set_state(store, job_id, JOB_DONE);
    CTEST_ASSERT_TRUE(ctest, r2, "set_state succeeded");
    CTEST_ASSERT_EQ(ctest, job->state, JOB_DONE, "state updated to DONE");

    bool r3 = job_store_set_state(store, 999, JOB_DONE);
    CTEST_ASSERT_FALSE(ctest, r3, "set_state fails for non-existent job");

	string_destroy(&cmd);
    job_store_destroy(&store);
}

CTEST(test_job_store_set_process_state)
{
    job_store_t *store = job_store_create();

    string_t *cmd = string_create_from_cstr("cat | grep | sort");
    int job_id = job_store_add(store, cmd, true);

    string_t *p1 = string_create_from_cstr("cat");
    string_t *p2 = string_create_from_cstr("grep");
    string_t *p3 = string_create_from_cstr("sort");

    job_store_add_process(store, job_id, 1001, p1);
    job_store_add_process(store, job_id, 1002, p2);
    job_store_add_process(store, job_id, 1003, p3);

    job_t *job = job_store_find(store, job_id);
    CTEST_ASSERT_EQ(ctest, job->state, JOB_RUNNING, "job is running");

    // Mark one process as stopped - job should become stopped
    bool r1 = job_store_set_process_state(store, 1002, JOB_STOPPED, 0);
    CTEST_ASSERT_TRUE(ctest, r1, "set process state succeeded");
    CTEST_ASSERT_EQ(ctest, job->state, JOB_STOPPED, "job is stopped when any process stopped");

    // Mark all processes as done
    job_store_set_process_state(store, 1001, JOB_DONE, 0);
    job_store_set_process_state(store, 1002, JOB_DONE, 0);
    job_store_set_process_state(store, 1003, JOB_DONE, 0);
    CTEST_ASSERT_EQ(ctest, job->state, JOB_DONE, "job is done when all processes done");

	string_destroy(&cmd);
	string_destroy(&p1);
	string_destroy(&p2);
	string_destroy(&p3);
    job_store_destroy(&store);
}

CTEST(test_job_store_mark_notified)
{
    job_store_t *store = job_store_create();

    string_t *cmd = string_create_from_cstr("sleep 100");
    int job_id = job_store_add(store, cmd, true);

    job_t *job = job_store_find(store, job_id);
    CTEST_ASSERT_FALSE(ctest, job->is_notified, "initially not notified");

    bool result = job_store_mark_notified(store, job_id);
    CTEST_ASSERT_TRUE(ctest, result, "mark_notified succeeded");
    CTEST_ASSERT_TRUE(ctest, job->is_notified, "is_notified is true");

	string_destroy(&cmd);
    job_store_destroy(&store);
}

// ------------------------------------------------------------
// Job Removal Tests
// ------------------------------------------------------------

CTEST(test_job_store_remove)
{
    job_store_t *store = job_store_create();

    string_t *cmd1 = string_create_from_cstr("job1");
    string_t *cmd2 = string_create_from_cstr("job2");
    string_t *cmd3 = string_create_from_cstr("job3");

    int job1 = job_store_add(store, cmd1, true);
    int job2 = job_store_add(store, cmd2, true);
    int job3 = job_store_add(store, cmd3, true);

    CTEST_ASSERT_EQ(ctest, job_store_count(store), 3, "three jobs added");

    bool r1 = job_store_remove(store, job2);
    CTEST_ASSERT_TRUE(ctest, r1, "remove succeeded");
    CTEST_ASSERT_EQ(ctest, job_store_count(store), 2, "count is 2");
    CTEST_ASSERT_NULL(ctest, job_store_find(store, job2), "job2 not found");
    CTEST_ASSERT_NOT_NULL(ctest, job_store_find(store, job1), "job1 still exists");
    CTEST_ASSERT_NOT_NULL(ctest, job_store_find(store, job3), "job3 still exists");

    bool r2 = job_store_remove(store, 999);
    CTEST_ASSERT_FALSE(ctest, r2, "remove non-existent job fails");

	string_destroy(&cmd1);
	string_destroy(&cmd2);
	string_destroy(&cmd3);
    job_store_destroy(&store);
}

CTEST(test_job_store_remove_current_previous)
{
    job_store_t *store = job_store_create();

    string_t *cmd1 = string_create_from_cstr("job1");
    string_t *cmd2 = string_create_from_cstr("job2");

    int job1 = job_store_add(store, cmd1, true);
    int job2 = job_store_add(store, cmd2, true);

    job_t *current = job_store_get_current(store);
    job_t *previous = job_store_get_previous(store);
    CTEST_ASSERT_EQ(ctest, current->job_id, job2, "current is job2");
    CTEST_ASSERT_EQ(ctest, previous->job_id, job1, "previous is job1");

    // Remove current job
    job_store_remove(store, job2);
    current = job_store_get_current(store);
    CTEST_ASSERT_EQ(ctest, current->job_id, job1, "current becomes previous");

	string_destroy(&cmd1);
	string_destroy(&cmd2);
    job_store_destroy(&store);
}

CTEST(test_job_store_remove_completed)
{
    job_store_t *store = job_store_create();

    string_t *cmd1 = string_create_from_cstr("job1");
    string_t *cmd2 = string_create_from_cstr("job2");
    string_t *cmd3 = string_create_from_cstr("job3");
    string_t *cmd4 = string_create_from_cstr("job4");

    int job1 = job_store_add(store, cmd1, true);
    int job2 = job_store_add(store, cmd2, true);
    int job3 = job_store_add(store, cmd3, true);
    int job4 = job_store_add(store, cmd4, true);

    // Mark job1 and job3 as done and notified
    job_store_set_state(store, job1, JOB_DONE);
    job_store_mark_notified(store, job1);
    job_store_set_state(store, job3, JOB_DONE);
    job_store_mark_notified(store, job3);

    // job2 is done but not notified
    job_store_set_state(store, job2, JOB_DONE);

    // job4 is still running

    size_t removed = job_store_remove_completed(store);
    CTEST_ASSERT_EQ(ctest, removed, 2, "two jobs removed");
    CTEST_ASSERT_EQ(ctest, job_store_count(store), 2, "two jobs remain");
    CTEST_ASSERT_NULL(ctest, job_store_find(store, job1), "job1 removed");
    CTEST_ASSERT_NOT_NULL(ctest, job_store_find(store, job2), "job2 still exists (not notified)");
    CTEST_ASSERT_NULL(ctest, job_store_find(store, job3), "job3 removed");
    CTEST_ASSERT_NOT_NULL(ctest, job_store_find(store, job4), "job4 still exists (running)");

	string_destroy(&cmd1);
	string_destroy(&cmd2);
	string_destroy(&cmd3);
	string_destroy(&cmd4);
    job_store_destroy(&store);
}

// ------------------------------------------------------------
// Utility Function Tests
// ------------------------------------------------------------

CTEST(test_job_is_running)
{
    job_store_t *store = job_store_create();

    string_t *cmd = string_create_from_cstr("cat | grep");
    int job_id = job_store_add(store, cmd, true);

    string_t *p1 = string_create_from_cstr("cat");
    string_t *p2 = string_create_from_cstr("grep");
    job_store_add_process(store, job_id, 1001, p1);
    job_store_add_process(store, job_id, 1002, p2);

    job_t *job = job_store_find(store, job_id);
    CTEST_ASSERT_TRUE(ctest, job_is_running(job), "job is running");

    // Mark one process done, job should still be running
    job_store_set_process_state(store, 1001, JOB_DONE, 0);
    CTEST_ASSERT_TRUE(ctest, job_is_running(job), "job still running with one process");

    // Mark all processes done
    job_store_set_process_state(store, 1002, JOB_DONE, 0);
    CTEST_ASSERT_FALSE(ctest, job_is_running(job), "job not running when all done");

    string_destroy(&cmd);
	string_destroy(&p1);
	string_destroy(&p2);
    job_store_destroy(&store);
}

CTEST(test_job_is_completed)
{
    job_store_t *store = job_store_create();

    string_t *cmd = string_create_from_cstr("cat | grep");
    int job_id = job_store_add(store, cmd, true);

    string_t *p1 = string_create_from_cstr("cat");
    string_t *p2 = string_create_from_cstr("grep");
    job_store_add_process(store, job_id, 1001, p1);
    job_store_add_process(store, job_id, 1002, p2);

    job_t *job = job_store_find(store, job_id);
    CTEST_ASSERT_FALSE(ctest, job_is_completed(job), "job not completed initially");

    // Mark processes done
    job_store_set_process_state(store, 1001, JOB_DONE, 0);
    CTEST_ASSERT_FALSE(ctest, job_is_completed(job), "job not completed with one process");

    job_store_set_process_state(store, 1002, JOB_DONE, 0);
    CTEST_ASSERT_TRUE(ctest, job_is_completed(job), "job completed when all done");

	string_destroy(&cmd);
	string_destroy(&p1);
	string_destroy(&p2);
    job_store_destroy(&store);
}

CTEST(test_job_state_to_string)
{
    CTEST_ASSERT_STR_EQ(ctest, job_state_to_string(JOB_RUNNING), "Running", "JOB_RUNNING");
    CTEST_ASSERT_STR_EQ(ctest, job_state_to_string(JOB_STOPPED), "Stopped", "JOB_STOPPED");
    CTEST_ASSERT_STR_EQ(ctest, job_state_to_string(JOB_DONE), "Done", "JOB_DONE");
    CTEST_ASSERT_STR_EQ(ctest, job_state_to_string(JOB_TERMINATED), "Terminated", "JOB_TERMINATED");
}

CTEST(test_job_store_first_iteration)
{
    job_store_t *store = job_store_create();

    string_t *cmd1 = string_create_from_cstr("job1");
    string_t *cmd2 = string_create_from_cstr("job2");
    string_t *cmd3 = string_create_from_cstr("job3");

    int job1 = job_store_add(store, cmd1, true);
    int job2 = job_store_add(store, cmd2, true);
    int job3 = job_store_add(store, cmd3, true);

    // Iterate through jobs (most recent first)
    int count = 0;
    int ids[3];
    for (job_t *job = job_store_first(store); job; job = job->next)
    {
        ids[count++] = job->job_id;
    }

    CTEST_ASSERT_EQ(ctest, count, 3, "three jobs iterated");
    CTEST_ASSERT_EQ(ctest, ids[0], job3, "first is job3 (most recent)");
    CTEST_ASSERT_EQ(ctest, ids[1], job2, "second is job2");
    CTEST_ASSERT_EQ(ctest, ids[2], job1, "third is job1 (oldest)");

	string_destroy(&cmd1);
	string_destroy(&cmd2);
	string_destroy(&cmd3);
    job_store_destroy(&store);
}

// ------------------------------------------------------------
// Edge Case Tests
// ------------------------------------------------------------

CTEST(test_job_store_null_handling)
{
    CTEST_ASSERT_EQ(ctest, job_store_count(NULL), 0, "count of null store is 0");
    CTEST_ASSERT_NULL(ctest, job_store_get_current(NULL), "current of null store is null");
    CTEST_ASSERT_NULL(ctest, job_store_get_previous(NULL), "previous of null store is null");
    CTEST_ASSERT_NULL(ctest, job_store_find(NULL, 1), "find in null store returns null");
    CTEST_ASSERT_NULL(ctest, job_store_find_by_pgid(NULL, 100), "find_by_pgid in null store returns null");
    CTEST_ASSERT_NULL(ctest, job_store_find_by_prefix(NULL, "test"), "find_by_prefix in null store returns null");
    CTEST_ASSERT_NULL(ctest, job_store_find_by_substring(NULL, "test"), "find_by_substring in null store returns null");
    CTEST_ASSERT_FALSE(ctest, job_is_running(NULL), "null job is not running");
    CTEST_ASSERT_FALSE(ctest, job_is_completed(NULL), "null job is not completed");
}

CTEST(test_job_store_empty_string_lookup)
{
    job_store_t *store = job_store_create();

    string_t *cmd = string_create_from_cstr("test command");
    job_store_add(store, cmd, true);

    job_t *found1 = job_store_find_by_prefix(store, "");
    CTEST_ASSERT_NULL(ctest, found1, "empty prefix returns null");

    job_t *found2 = job_store_find_by_substring(store, "");
    CTEST_ASSERT_NULL(ctest, found2, "empty substring returns null");

	string_destroy(&cmd);
    job_store_destroy(&store);
}

// ------------------------------------------------------------
// Test Suite Main
// ------------------------------------------------------------

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    CTestEntry *suite[] = {
        // Creation and destruction
        CTEST_ENTRY(test_job_store_create),
        CTEST_ENTRY(test_job_store_destroy_null),

        // Job creation
        CTEST_ENTRY(test_job_store_add_basic),
        CTEST_ENTRY(test_job_store_add_foreground),
        CTEST_ENTRY(test_job_store_add_multiple),
        CTEST_ENTRY(test_job_store_current_previous),

        // Process management
        CTEST_ENTRY(test_job_store_add_process),
        CTEST_ENTRY(test_job_store_add_process_invalid_job),

        // Job lookup
        CTEST_ENTRY(test_job_store_find),
        CTEST_ENTRY(test_job_store_find_by_pgid),
        CTEST_ENTRY(test_job_store_find_by_prefix),
        CTEST_ENTRY(test_job_store_find_by_substring),
        CTEST_ENTRY(test_job_store_find_by_prefix_most_recent),

        // Job state management
        CTEST_ENTRY(test_job_store_set_state),
        CTEST_ENTRY(test_job_store_set_process_state),
        CTEST_ENTRY(test_job_store_mark_notified),

        // Job removal
        CTEST_ENTRY(test_job_store_remove),
        CTEST_ENTRY(test_job_store_remove_current_previous),
        CTEST_ENTRY(test_job_store_remove_completed),

        // Utility functions
        CTEST_ENTRY(test_job_is_running),
        CTEST_ENTRY(test_job_is_completed),
        CTEST_ENTRY(test_job_state_to_string),
        CTEST_ENTRY(test_job_store_first_iteration),

        // Edge cases
        CTEST_ENTRY(test_job_store_null_handling),
        CTEST_ENTRY(test_job_store_empty_string_lookup),

        NULL
    };

    int result = ctest_run_suite(suite);

    return result;
}

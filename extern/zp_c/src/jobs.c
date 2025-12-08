
#include "zp_c/jobs.h"
#include "zp_c/fatal.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct job_data_t
{
    pid_t pid;
    job_status_ze status;
    char status_message[256];
    char* command;
    char* working_dir;
} job_data_t;

#define MAX_JOBS 4096

struct jobs_system_zt
{
    job_data_t jobs[MAX_JOBS];
    bool jobs_used[MAX_JOBS];
    uint64_t next_job_id;
    job_id_zt job_ids[MAX_JOBS];
};

// =========================================================================================================================================
// =========================================================================================================================================
// find_job_index_by_id_z: Find the index of a job in the system by its job_id. Returns -1 if not found or if job_id is zero.
// =========================================================================================================================================
// =========================================================================================================================================
static int find_job_index_by_id_z(jobs_system_zh system, job_id_zt job_id)
{
    fatal_check_z(system, "system is null");

    if (job_id.value == 0)
    {
        return -1;
    }

    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (system->jobs_used[i] && system->job_ids[i].value == job_id.value)
        {
            return i;
        }
    }
    return -1;
}

// =========================================================================================================================================
// =========================================================================================================================================
// allocate_job_z: Allocate a free job slot in the system. Returns the index of the allocated slot or -1 if no slots available.
// =========================================================================================================================================
// =========================================================================================================================================
static int allocate_job_z(jobs_system_zh system)
{
    fatal_check_z(system, "system is null");

    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (!system->jobs_used[i])
        {
            system->jobs_used[i] = true;
            memset(&system->jobs[i], 0, sizeof(job_data_t));
            return i;
        }
    }
    return -1;
}

// =========================================================================================================================================
// =========================================================================================================================================
// free_job_z: Free a job slot by deallocating its command and working_dir strings and marking the slot as unused.
// =========================================================================================================================================
// =========================================================================================================================================
static void free_job_z(jobs_system_zh system, int index)
{
    fatal_check_z(system, "system is null");
    fatal_check_bool_z(index >= 0 && index < MAX_JOBS, "index out of range");

    if (system->jobs[index].command)
    {
        free(system->jobs[index].command);
        system->jobs[index].command = nullptr;
    }
    if (system->jobs[index].working_dir)
    {
        free(system->jobs[index].working_dir);
        system->jobs[index].working_dir = nullptr;
    }

    system->jobs_used[index]     = false;
    system->job_ids[index].value = 0;
}

// =========================================================================================================================================
// =========================================================================================================================================
// jobs_system_init_z: Initialize a new jobs system. Allocates and initializes the system structure with empty job slots.
// =========================================================================================================================================
// =========================================================================================================================================
jobs_system_zh jobs_system_init_z(void)
{
    // =============================================================================================
    // =============================================================================================
    // Allocate jobs system structure.
    // =============================================================================================
    // =============================================================================================
    jobs_system_zh system;
    {
        system = (jobs_system_zh)fatal_alloc_z(sizeof(struct jobs_system_zt), "failed to allocate jobs system");
    }

    // =============================================================================================
    // =============================================================================================
    // Initialize job arrays and next job ID.
    // =============================================================================================
    // =============================================================================================
    {
        memset(system->jobs_used, 0, sizeof(system->jobs_used));
        memset(system->job_ids, 0, sizeof(system->job_ids));
        system->next_job_id = 1;
    }

    return system;
}

// =========================================================================================================================================
// =========================================================================================================================================
// jobs_system_update_z: Update job statuses by checking for completed processes. Non-blocking check using waitpid with WNOHANG.
// =========================================================================================================================================
// =========================================================================================================================================
void jobs_system_update_z(jobs_system_zh system)
{
    fatal_check_z(system, "system is null");

    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (!system->jobs_used[i])
        {
            continue;
        }

        job_data_t* job = &system->jobs[i];
        if (job->pid > 0)
        {
            int status;
            pid_t result = waitpid(job->pid, &status, WNOHANG);
            if (result > 0)
            {
                job->pid = 0;

                if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
                {
                    job->status = JOB_STATUS_COMPLETED;
                    strncpy(job->status_message, "Completed successfully", sizeof(job->status_message) - 1);
                    job->status_message[sizeof(job->status_message) - 1] = '\0';
                }
                else
                {
                    job->status = JOB_STATUS_FAILED;
                    strncpy(job->status_message, "Failed", sizeof(job->status_message) - 1);
                    job->status_message[sizeof(job->status_message) - 1] = '\0';
                }
            }
        }
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// jobs_system_cleanup_z: Cleanup the jobs system by terminating all running jobs and freeing all resources.
// =========================================================================================================================================
// =========================================================================================================================================
void jobs_system_cleanup_z(jobs_system_zh system)
{
    // =============================================================================================
    // =============================================================================================
    // Validate system pointer.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(system, "system is null");
    }

    // =============================================================================================
    // =============================================================================================
    // Terminate all running jobs and free job resources.
    // =============================================================================================
    // =============================================================================================
    {
        for (int i = 0; i < MAX_JOBS; i++)
        {
            if (!system->jobs_used[i])
            {
                continue;
            }

            job_data_t* job = &system->jobs[i];
            if (job->pid > 0)
            {
                kill(job->pid, SIGTERM);
                waitpid(job->pid, nullptr, 0);
                job->pid = 0;
            }
            job->status = JOB_STATUS_IDLE;
            strncpy(job->status_message, "Ready", sizeof(job->status_message) - 1);
            job->status_message[sizeof(job->status_message) - 1] = '\0';

            free_job_z(system, i);
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Free system structure.
    // =============================================================================================
    // =============================================================================================
    {
        free(system);
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// jobs_start_z: Start a new job by forking a process to execute the command. Returns false if no job slots available, true on success.
// =========================================================================================================================================
// =========================================================================================================================================
bool jobs_start_z(jobs_system_zh system, const char* command, const char* working_dir, job_id_zt* p_out_job_id)
{
    fatal_check_z(system, "system is null");
    fatal_check_z(command, "command is null");
    fatal_check_z(p_out_job_id, "p_out_job_id is null");

    // =============================================================================================
    // =============================================================================================
    // Allocate a job slot.
    // =============================================================================================
    // =============================================================================================
    int index;
    {
        index = allocate_job_z(system);
        if (index < 0)
        {
            return false;
        }
    }

    job_data_t* job = &system->jobs[index];

    // =============================================================================================
    // =============================================================================================
    // Allocate and copy command string.
    // =============================================================================================
    // =============================================================================================
    {
        size_t cmd_len = strlen(command) + 1;
        job->command   = (char*)fatal_alloc_z(cmd_len, "failed to allocate command string");
        strncpy(job->command, command, cmd_len);
    }

    // =============================================================================================
    // =============================================================================================
    // Allocate and copy working directory string if provided.
    // =============================================================================================
    // =============================================================================================
    {
        if (working_dir && working_dir[0] != '\0')
        {
            size_t dir_len   = strlen(working_dir) + 1;
            job->working_dir = (char*)fatal_alloc_z(dir_len, "failed to allocate working directory string");
            strncpy(job->working_dir, working_dir, dir_len);
        }
        else
        {
            job->working_dir = nullptr;
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Assign job ID and set initial status.
    // =============================================================================================
    // =============================================================================================
    job_id_zt job_id;
    {
        job_id.value           = system->next_job_id++;
        system->job_ids[index] = job_id;
        job->status            = JOB_STATUS_RUNNING;
        strncpy(job->status_message, "Running...", sizeof(job->status_message) - 1);
        job->status_message[sizeof(job->status_message) - 1] = '\0';
    }

    // =============================================================================================
    // =============================================================================================
    // Fork process and execute command.
    // =============================================================================================
    // =============================================================================================
    {
        pid_t pid = fork();

        if (pid == 0)
        {
            if (job->working_dir && job->working_dir[0] != '\0')
            {
                chdir(job->working_dir);
            }

            char* argv[] = {(char*)"sh", (char*)"-c", job->command, nullptr};
            execvp("sh", argv);
            exit(1);
        }
        else if (pid > 0)
        {
            job->pid = pid;
        }
        else
        {
            free_job_z(system, index);
            fatal_z("failed to fork process");
        }
    }

    *p_out_job_id = job_id;
    return true;
}

// =========================================================================================================================================
// =========================================================================================================================================
// jobs_stop_z: Stop a running job by sending SIGTERM and waiting for it to terminate. No-op if job not found or not running.
// =========================================================================================================================================
// =========================================================================================================================================
void jobs_stop_z(jobs_system_zh system, job_id_zt job_id)
{
    fatal_check_z(system, "system is null");

    if (job_id.value == 0)
    {
        return;
    }

    int index = find_job_index_by_id_z(system, job_id);
    if (index < 0)
    {
        return;
    }

    job_data_t* job = &system->jobs[index];
    if (job->pid > 0)
    {
        kill(job->pid, SIGTERM);
        waitpid(job->pid, nullptr, 0);
        job->pid    = 0;
        job->status = JOB_STATUS_IDLE;
        strncpy(job->status_message, "Ready", sizeof(job->status_message) - 1);
        job->status_message[sizeof(job->status_message) - 1] = '\0';
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// jobs_is_running_z: Check if a job is currently running. Returns true if job has active PID or status is RUNNING.
// =========================================================================================================================================
// =========================================================================================================================================
bool jobs_is_running_z(jobs_system_zh system, job_id_zt job_id)
{
    fatal_check_z(system, "system is null");

    if (job_id.value == 0)
    {
        return false;
    }

    int index = find_job_index_by_id_z(system, job_id);
    if (index < 0)
    {
        return false;
    }

    job_data_t* job = &system->jobs[index];
    return job->pid > 0 || job->status == JOB_STATUS_RUNNING;
}

// =========================================================================================================================================
// =========================================================================================================================================
// jobs_get_status_z: Get the status of a job. Returns JOB_STATUS_IDLE if job not found or job_id is zero.
// =========================================================================================================================================
// =========================================================================================================================================
job_status_ze jobs_get_status_z(jobs_system_zh system, job_id_zt job_id)
{
    fatal_check_z(system, "system is null");

    if (job_id.value == 0)
    {
        return JOB_STATUS_IDLE;
    }

    int index = find_job_index_by_id_z(system, job_id);
    if (index < 0)
    {
        return JOB_STATUS_IDLE;
    }

    return system->jobs[index].status;
}

// =========================================================================================================================================
// =========================================================================================================================================
// jobs_get_status_message_z: Get the status message of a job. Copies message to buffer with null termination.
// =========================================================================================================================================
// =========================================================================================================================================
void jobs_get_status_message_z(jobs_system_zh system, job_id_zt job_id, char* buffer, size_t buffer_size)
{
    fatal_check_z(system, "system is null");
    fatal_check_z(buffer, "buffer is null");
    fatal_check_bool_z(buffer_size > 0, "buffer_size is zero");

    if (job_id.value == 0)
    {
        strncpy(buffer, "Ready", buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return;
    }

    int index = find_job_index_by_id_z(system, job_id);
    if (index < 0)
    {
        strncpy(buffer, "Ready", buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return;
    }

    strncpy(buffer, system->jobs[index].status_message, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
}

// =========================================================================================================================================
// =========================================================================================================================================
// jobs_get_pid_z: Get the process ID of a job. Returns 0 if job not found or job_id is zero.
// =========================================================================================================================================
// =========================================================================================================================================
pid_t jobs_get_pid_z(jobs_system_zh system, job_id_zt job_id)
{
    fatal_check_z(system, "system is null");

    if (job_id.value == 0)
    {
        return 0;
    }

    int index = find_job_index_by_id_z(system, job_id);
    if (index < 0)
    {
        return 0;
    }

    return system->jobs[index].pid;
}

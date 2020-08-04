#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

#include "debug.h"
#include "polya.h"
#include "master.h"

/*
 * master
 * (See polya.h for specification.)
 */
int write_pipes[MAX_WORKERS][2];
int read_pipes[MAX_WORKERS][2];
int worker_status[MAX_WORKERS];
pid_t workerpids[MAX_WORKERS];
int worker_count;
int numworkers;
struct problem *worker_problems[MAX_WORKERS];

volatile sig_atomic_t sigchild;

int master(int workers)
{
    sigset_t sigchld_mask;
    sigemptyset(&sigchld_mask);
    sigaddset(&sigchld_mask, SIGCHLD);

    sf_start();
    install_signal();

    init(workers);
    // init_fds(workers);


    for(int i = 0; i < workers; i++)
    {
        int write_pipe_status = pipe(write_pipes[i]);
        int read_pipe_status = pipe(read_pipes[i]);

        if(write_pipe_status<0 || read_pipe_status<0)
        {
            perror("Error creating pipes");
            exit(EXIT_FAILURE);
        }

        pid_t pid = fork();
        add_workerpid(pid);

        // Forked successfully
        if(pid>=0)
        {
            // Child process
            if(pid==0)
            {
                dup2(write_pipes[i][0], STDIN_FILENO);
                dup2(read_pipes[i][1], STDOUT_FILENO);
                close(write_pipes[i][0]);
                close(write_pipes[i][1]);
                close(read_pipes[i][0]);
                close(read_pipes[i][1]);
                char *args[] = {"bin/polya_worker", NULL};
                int exec_result = execv(args[0], args);
                if(exec_result<0)
                {
                    perror("Error replacing the current process image with a new process image");
                    exit(EXIT_FAILURE);
                }
            }
            else
            {
                debug("[%d:Master] Starting worker %d (pid = %d)", getpid(), i, pid);

                close(write_pipes[i][0]);
                close(read_pipes[i][1]);

                update_worker_state(pid, WORKER_STARTED);
                debug("[%d:Master] Set State of worker %d (pid = %d): init -> started", getpid(), i, pid);

                debug("[%d:Master] Workers alive: %d", getpid(), worker_count);
                debug("[%d:Master] Started worker %d (pid = %d, in = %d, out = %d)", getpid(), i, pid, read_pipes[i][0], write_pipes[i][1]);
            }
        }
        // Fork failed
        else
        {
            perror("Error creating child process");
            exit(EXIT_FAILURE);
        }

    }

    int stopped_workerid;
    int running_workerid;
    // Main loop
    while(1)
    {
        // sigset_t empty_mask;
        // sigemptyset(&empty_mask);
        // sigsuspend(&empty_mask);

        if(sigchild)
        {

            int wait_status;
            int pid;
            while((pid = waitpid(-1, &wait_status, WNOHANG|WUNTRACED|WCONTINUED))>0)
            {
                sigprocmask(SIG_BLOCK, &sigchld_mask, NULL);
                if(WIFSTOPPED(wait_status))
                {
                    debug("[%d:Master] Worker %d (pid = %d, prev state = %d) has stopped", getpid(), get_workerid(pid), pid, worker_status[get_workerid(pid)]);
                    if(worker_status[get_workerid(pid)]==WORKER_STARTED)
                    {
                        //send problem to worker
                        send_problem(pid);
                    }
                    else if(worker_status[get_workerid(pid)]==WORKER_RUNNING)
                    {
                        //read result
                        stopped_workerid = get_workerid(pid);
                        update_worker_state(workerpids[stopped_workerid], WORKER_STOPPED);
                        debug("[%d:Master] Set state of worker %d (pid = %d): running -> stopped", getpid(), stopped_workerid, workerpids[stopped_workerid]);

                        struct result *result_header = malloc(sizeof(struct result));
                        debug("[%d:Master] Reading the result header", getpid());
                        ssize_t read_result = read(read_pipes[stopped_workerid][0], result_header, sizeof(struct result));
                        if(read_result<0)
                        {
                            perror("Error while reading");
                            exit(EXIT_FAILURE);
                        }
                        debug("[%d:Master] Read %ld bytes", getpid(), read_result);
                        debug("[%d:Master] Got Result: size = %ld, failed = %d", getpid(), result_header->size, result_header->failed);

                        // Copying header to problem
                        struct result *result = malloc(result_header->size);
                        memcpy(result, result_header, result_header->size);

                        // Read the remaining data
                        read_result = read(read_pipes[stopped_workerid][0], result->data, result_header->size - sizeof(struct result));
                        if(read_result<0)
                        {
                            perror("Error while reading");
                            exit(EXIT_FAILURE);
                        }
                        // print_result(result,stopped_workerid);

                        update_worker_state(workerpids[stopped_workerid], WORKER_IDLE);
                        debug("[%d:Master] Set state of worker %d (pid = %d): stopped -> idle", getpid(), stopped_workerid, workerpids[stopped_workerid]);

                        debug("[%d:Master] Retriving worker %d's problem", getpid(), stopped_workerid);
                        int result_valid = post_result(result, worker_problems[stopped_workerid]);
                        // debug("Result Valid: %d", result_valid);
                        if(result_valid==0)
                        {
                            for (int i = 0; i < workers; ++i)
                            {
                                if(i==stopped_workerid)
                                {
                                    send_problem(workerpids[stopped_workerid]);
                                    continue;
                                }
                                int cancel_pid = workerpids[i];
                                sf_cancel(cancel_pid);
                                kill(cancel_pid, SIGHUP);
                                // kill(workerpids[i], SIGCONT);
                            }
                        }
                        else
                        {
                            send_problem(workerpids[stopped_workerid]);
                        }
                        sf_recv_result(workerpids[stopped_workerid], result);
                        // free(result_header);
                        // free(result);
                    }
                    else //if(worker_status[get_workerid(pid)]==WORKER_STOPPED)
                    {
                        fprintf(stderr, "The status here was %d\n", worker_status[get_workerid(pid)]);
                        fflush(stderr);
                        update_worker_state(workerpids[stopped_workerid], WORKER_EXITED);
                        debug("[%d:Master] Here", getpid());
                    }
                }
                else if(WIFCONTINUED(wait_status))
                {
                    debug("[%d:Master] Worker %d (pid = %d, prev state = %d) is running", getpid(), get_workerid(pid), pid, worker_status[get_workerid(pid)]);
                    running_workerid = get_workerid(pid);
                    update_worker_state(workerpids[running_workerid], WORKER_RUNNING);
                    debug("[%d:Master] Set state of worker %d (pid = %d): stopped -> running", getpid(), stopped_workerid, workerpids[stopped_workerid]);
                }
                else if(WIFEXITED(wait_status))
                {
                    if(WEXITSTATUS(wait_status)==EXIT_SUCCESS)
                    {
                        update_worker_state(workerpids[stopped_workerid], WORKER_EXITED);
                        // debug("[%d:Master] Set state of worker %d (pid = %d): idle -> exited", getpid(), stopped_workerid, workerpids[stopped_workerid]);
                    };

                }
                sigprocmask(SIG_UNBLOCK, &sigchld_mask, NULL);

            }
        }
    }

    sf_end();
    return EXIT_FAILURE;
}

void install_signal()
{
    debug("[%d:Master] Installing signal handler", getpid());
    signal(SIGCHLD, signal_handler);
}

void signal_handler(int sig)
{
    sigchild = 1;
}

// void init_fds(int workers)
// {
//     write_pipes = malloc(sizeof(int *)*workers);
//     read_pipes = malloc(sizeof(int *)*workers);

//     for(int  i = 0; i < workers; i++)
//     {
//         write_pipes[i] = malloc(2*sizeof(int));
//         read_pipes[i] = malloc(2*sizeof(int));
//     }
// }

void init(int workers)
{
    // Init numworkers
    numworkers = workers;

    // // Init worker status array
    // worker_status = malloc(workers *  sizeof(int));
    for (int i = 0; i < workers; ++i)
    {
        *(workerpids+i) = 0;
    }

    // Init worker_count
    worker_count = 0;

    // // Init workerpids
    // workerpids = malloc(workers * sizeof(pid_t));
    for (int i = 0; i < workers; ++i)
    {
        *(workerpids+i) = -1;
    }

    // // Init worker_problems
    // worker_problems = malloc(workers * sizeof(struct problem));
}

void update_worker_state(pid_t workerpid, int new_state)
{
    int id = get_workerid(workerpid);
    sf_change_state(workerpid, worker_status[id], new_state);
    worker_status[id] = new_state;
}

int get_workerid(pid_t workerpid)
{
    int i;
    for (i = 0; i < numworkers; ++i)
    {
        if(*(workerpids+i)==workerpid)
        {
            break;
        }
    }
    return i;
}

void add_workerpid(pid_t workerpid)
{
    worker_count++;
    for (int i = 0; i < numworkers; ++i)
    {
        if(*(workerpids+i)==-1)
        {
            *(workerpids+i) = workerpid;
            break;
        }
    }
}

void send_problem(pid_t pid)
{
    int stopped_workerid = get_workerid(pid);
    if(worker_status[get_workerid(pid)]==WORKER_STARTED)
    {

    }
    struct problem *problem = get_problem_variant(numworkers, stopped_workerid);
    // print_problem(problem, stopped_workerid);
    if(problem==NULL)
    {
        sigset_t sigchld_mask;
        sigemptyset(&sigchld_mask);
        sigaddset(&sigchld_mask, SIGCHLD);
        sigprocmask(SIG_UNBLOCK, &sigchld_mask, NULL);
        for (int i = 0; i < numworkers; ++i)
        {
            debug("[%d:Master] Worker %d has status %d", getpid(), i, worker_status[i]);
            // update_worker_state(workerpids[i], WORKER_EXITED);
            kill(workerpids[i], SIGTERM);
            // update_worker_state(workerpids[stopped_workerid], WORKER_EXITED);
            // debug("[%d:Master] Set state of worker %d (pid = %d): started -> idle", getpid(), stopped_workerid, workerpids[stopped_workerid]);
        }

        int all_exited = 1;
        for (int i = 0; i < numworkers; ++i)
        {
            if(worker_status[i]!=WORKER_EXITED)
                all_exited = 0;
        }
        // destructor();
        sf_end();
        if(all_exited)
        {
            debug("MASTER: ALL EXITED");
            exit(EXIT_SUCCESS);
        }
        else
        {
            debug("MASTER: NOT ALL EXITED");
            exit(EXIT_FAILURE);
        }
    }
    write(write_pipes[stopped_workerid][1], problem, problem->size);
    sf_send_problem(workerpids[stopped_workerid], problem);
    debug("[%d:Master] Storing worker %d's problem", getpid(), stopped_workerid);
    worker_problems[stopped_workerid] = problem;
    kill(workerpids[stopped_workerid], SIGCONT);
    update_worker_state(workerpids[stopped_workerid], WORKER_CONTINUED);
    debug("[%d:Master] Set state of worker %d (pid = %d): idle -> continued", getpid(), stopped_workerid, workerpids[stopped_workerid]);
}

void destructor()
{
    for (int i = 0; i < numworkers; ++i)
    {
        free(worker_problems[i]);
    }
}
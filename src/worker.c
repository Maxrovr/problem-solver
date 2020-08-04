#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "debug.h"
#include "polya.h"
#include "worker.h"

/*
 * worker
 * (See polya.h for specification.)
 */
volatile sig_atomic_t canceledp;
volatile sig_atomic_t exitp;

int worker(void) {

    install_handlers();

    while(1)
    {
        raise_sigstop();

        if(exitp)
        {
            debug("[%d:Worker] Aborting", getpid());
            exit(EXIT_SUCCESS);
        }

        // Read the problem header
        struct problem *problem_header = malloc(sizeof(struct problem));
        debug("[%d:Worker] Reading the problem header", getpid());
        ssize_t read_result = read(STDIN_FILENO, problem_header, sizeof(struct problem));
        if(read_result<0)
        {
            perror("Error while reading");
            exit(EXIT_FAILURE);
        }
        debug("[%d:Worker] Read %ld bytes", getpid(), read_result);
        debug("[%d:Worker] Got problem: size = %ld, type = %d, variants = %d, variant = %d, id = %d", getpid(), problem_header->size, problem_header->type, problem_header->nvars, problem_header->var, problem_header->id);

        // Copying header to problem
        struct problem *problem = malloc(problem_header->size);
        memcpy(problem, problem_header, problem_header->size);

        // Read the remaining data
        read_result = read(STDIN_FILENO, problem->data, problem_header->size - sizeof(struct problem));
        if(read_result<0)
        {
            perror("Error while reading");
            exit(EXIT_FAILURE);
        }
        // print_problem(problem);

        // Solving the problem
        debug("[%d:Worker] Solving the problem", getpid());
        struct result *result = solvers[problem->type].solve(problem, &canceledp);
        // debug("[%d:Worker] Result = %p", getpid(), result);
        if(result != NULL)
        {
            debug("[%d:Worker] Got Result: size = %ld, failed = %d", getpid(), result->size, result->failed);
            int check_res = solvers[problem->type].check(result, problem);
            if(check_res==0)
                debug("[%d:Worker] Solution is successfully validated", getpid());
        }
        else
        {
            result = malloc(sizeof(struct result));
            result->size = sizeof(struct result);
            result->id = problem->id;
            result->failed = 1;
        }
        // print_result(result);
        //Writing the result
        debug("[%d:Worker] Writing result size = %ld, failed = %d", getpid(), result->size, result->failed);
        write(STDOUT_FILENO, result, result->size);
        free(result);
        free(problem_header);
        free(problem);
        // raise_sigstop();
    }




    return EXIT_SUCCESS;
}

void signal_handler(int sig)
{
    if(sig==SIGHUP)
    {
        canceledp = 1;
        debug("[%d:Worker] Recieved signal SIGHUP(%d)", getpid(), sig);
    }
    if(sig==SIGTERM)
    {
        debug("[%d:Worker] Recieved signal SIGTERM(%d)", getpid(), sig);
        // canceledp = 1;
        exitp = 1;
    }
}

void install_handlers()
{
    debug("[%d:Worker] Installing handlers", getpid());
    signal(SIGHUP, signal_handler);
    signal(SIGTERM, signal_handler);
}

void raise_sigstop()
{
    debug("[%d:Worker] Idling (sending SIGSTOP to self)", getpid());
    raise(SIGSTOP);
}
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <criterion/criterion.h>
#include <criterion/logging.h>
#include "polya.h"

#define TEST_TIMEOUT 12

extern int read_data(FILE *in, char *resultp, int nbytes);
extern int write_data(FILE *out, char *datap, int nbytes);

/*
 * Finalization function to try to ensure no stray processes are left over
 * after a test finishes.
 */
static void killall() {
  system("killall -s KILL polya_worker");
  system("killall -s KILL bin/polya");
}

/*
 * Write a "problem" to the standard output.
 * Return 0 if successful, EOF if error.
 * Taken from solution's master.c
 */
static int write_problem(FILE *out, struct problem *prob) {

    // Write the header.
    if(write_data(out, (char *)prob, sizeof(*prob)) == EOF)
	return EOF;

    // Write the data.
    for(size_t s = 0; s < prob->size - sizeof(*prob); s++) {
	if(fputc(prob->data[s], out) == EOF)
	    return EOF;
    }

    // Flush the stream, to be sure that the worker process can
    // immediately see what we've written.
    fflush(out);
    return 0;
}

/*
 * Read a "result" from an input stream.
 * The result is returned as a pointer to a malloc'ed "struct result",
 * which the caller is responsible for freeing.
 * If there is any error reading the problem, NULL is returned.
 * Taken from solution's master.c
 */
static struct result *read_result(FILE *in) {
    struct result *result, rhdr;
    int c;

    // Read the result header.
    if(read_data(in, (char *)&rhdr, sizeof(*result)) == EOF)
	return NULL;

    // Now that we know how big the result is, allocate space for it
    // and read the rest.
    result = malloc(rhdr.size);
    if(result == NULL)
	return NULL;
    *result = rhdr;  // Store original result header.
    for(size_t s = 0; s < result->size - sizeof(*result); s++) {
	if((c = fgetc(in)) == EOF) {
	    free(result);
	    return NULL;
	}
	result->data[s] = c & 0xff;
    }
    return result;
}

/*
 * Wrapper for kill(2).
 * Will print a message and fail the current test in case of error. 
 */
int Kill(pid_t pid, int sig) {
    if(pid <= 0)
      cr_assert_fail("pid (%d) being killed is not positive.\n", pid);
    int kill_retval = kill(pid, sig);
    if (-1 == kill_retval) {
        perror("kill");
        cr_assert_fail("kill(2) failed sending signal %d to process %d.\n", sig, pid);
    }
    return kill_retval;
}

/*
 * Wrapper for waitpid(2).
 * Will print a message and fail the current test in case of error. 
 */
pid_t Waitpid(pid_t pid, int *wstatus, int options) {
    pid_t wpid = waitpid(pid, wstatus, options);
    if (-1 == wpid) {
        perror("waitpid");
        cr_assert_fail("waitpid(2) failed waiting on process %d.\n", pid);
    }
    return wpid;
}

/*
 * Wrapper for pipe(2).
 * Will print a message and fail the current test in case of error. 
 */
int Pipe (int pipefd[2]) {
    int retval = pipe(pipefd);
    if (-1 == retval) {
        perror("pipe");
        cr_assert_fail("pipe(2) failed.\n");
    }
    return retval;
}

/*
 * Wrapper for dup2(2).
 * Will print a message and fail the current test in case of error. 
 */
int Dup2 (int oldfd, int newfd) {
    int retval = dup2(oldfd, newfd);
    if (-1 == retval) {
        perror("dup2");
        cr_assert_fail("dup2(2) failed.\n");
    }
    return retval;
}

/*
 * Wrapper for close(2).
 * Will print a message and fail the current test in case of error. 
 */
int Close (int fd) {
    int retval = close(fd);
    if (-1 == retval) {
        perror("close");
        cr_assert_fail("close(2) failed.\n");
    }
    return retval;
}

/*
 * Wrapper for fdopen(3).
 * Will print a message and fail the current test in case of error. 
 */
FILE *Fdopen (int fd, const char *mode) {
    FILE *fp = fdopen(fd, mode);
    if (NULL == fp) {
        perror("fdopen");
        cr_assert_fail("fdopen(3) failed.\n");
    }
    return fp;
}

/* 
 * Forks and execs our worker program after performing necessary redirection.
 * Also updates the FILE pointers at from_worker and to_worker if they are not
 * NULL.
 */
pid_t launch_worker (FILE **from_worker, FILE **to_worker) {
    /* File descriptors for pipes */
    int master_to_worker[2];
    int worker_to_master[2];

    /* Set up pipes */
    Pipe(master_to_worker);
    Pipe(worker_to_master);
    
    /* Fork and execute the worker program */
    pid_t pid = fork();
    if (0 == pid) {
        // Create a new process group so that the test driver is not affected
        // by erroneous kills in student code.
        setpgid(0, 0);
        Dup2(master_to_worker[0], STDIN_FILENO);
        Dup2(worker_to_master[1], STDOUT_FILENO);
	    Close(master_to_worker[1]);
        Close(master_to_worker[0]);
	    Close(worker_to_master[0]);
	    Close(worker_to_master[1]);
        execl("bin/polya_worker", "polya_worker", NULL);
        perror("exec");
        cr_assert_fail("execl(3) failed.\n");
    }

    /* Give back relevant file pointers */
    if (NULL != from_worker) {
        *from_worker = Fdopen(worker_to_master[0], "r");
    }
    if (NULL != to_worker) {
        *to_worker = Fdopen(master_to_worker[1], "w");
    }

    return pid;
}

/*
 * worker_test_1
 * Basic test - see if worker SIGSTOPs itself after being resumed
 */
Test(worker_suite, worker_test_1, .timeout=TEST_TIMEOUT, .fini = killall) {
    /* Launch the worker program (we don't need any file pointers) */
    pid_t pid = launch_worker(NULL, NULL);

    /* Check the worker's state and resume it to validate correct behavior */
    int status = 0;
    Waitpid(pid, &status, WUNTRACED);  // Wait for stop
    cr_assert(WIFSTOPPED(status), "Worker didn't SIGSTOP itself.\n");
    Kill(pid, SIGCONT);  // Trigger continue
    Waitpid(pid, &status, WCONTINUED);  // Wait for continue
    cr_assert(WIFCONTINUED(status), "Worker didn't continue after SIGCONT was sent.\n");
}

/*
 * worker_test_2
 * See if sending SIGTERM to worker causes it to exit(3) with status EXIT_SUCCESS
 */
Test(worker_suite, worker_test_2, .timeout=TEST_TIMEOUT, .fini = killall) {
    /* Launch the worker program (we don't need any file pointers) */
    pid_t pid = launch_worker(NULL, NULL);

    /* Wait for the worker to SIGSTOP itself */
    int status = 0;
    Waitpid(pid, &status, WUNTRACED);
    cr_assert(WIFSTOPPED(status), "Worker didn't SIGSTOP itself.\n");

    /* Signal the worker to continue and immediately terminate */
    Kill(pid, SIGTERM);
    Kill(pid, SIGCONT);

    /* Wait for process to resume after receipt of SIGCONT */
    Waitpid(pid, &status, WCONTINUED);
    cr_assert(WIFCONTINUED(status), "Worker didn't continue after SIGCONT was sent.\n");

    /* Wait for process to exit after receipt of SIGTERM */
    Waitpid(pid, &status, 0);
    cr_assert(WIFEXITED(status), "Worker didn't exit after SIGTERM was sent.\n");

    /* The worker process should have exited using exit(3) with status EXIT_SUCCESS */
    int exit_status = WEXITSTATUS(status);
    cr_assert_eq(exit_status, EXIT_SUCCESS, "Worker process did not exit with code EXIT_SUCCESS (found: %d).\n", exit_status);
    cr_assert(WIFEXITED(status), "Worker process did not exit correctly (waitpid(2) yielded status %x).\n", status);
}

/*
 * worker_test_3
 * Ensure a worker process can correctly solve a trivial problem type
 */
Test(worker_suite, worker_test_3, .timeout=TEST_TIMEOUT, .fini = killall) {
    FILE *from_worker = NULL, *to_worker = NULL;
    pid_t pid = launch_worker(&from_worker, &to_worker);

    /* Wait for the worker to SIGSTOP itself */
    int status = 0;
    Waitpid(pid, &status, WUNTRACED);

    /* A trivial problem for the worker to solve */
    struct problem prob = {
        (size_t) 16,
        (short) TRIVIAL_PROBLEM_TYPE,
        (short) 1,
        (short) 1,
        (short) 0,
    };

    /* Signal the worker to continue and wait until this happens */
    Kill(pid, SIGCONT);
    Waitpid(pid, &status, WCONTINUED);

    /* Send the problem to the worker */
    write_problem(to_worker, &prob);

    /* Wait for the worker to SIGSTOP itself again */
    Waitpid(pid, &status, WUNTRACED);

    /* Read and validate the problem's result, if one was given */
    struct result *result = read_result(from_worker);
    cr_assert_not_null(result, "Failed reading result from worker.\n");
    cr_assert_eq(result->failed, 0, "Worker returned a 'failed' result for a trivial problem.\n");
}

/*
 * worker_test_4
 * Ensure a SIGHUP successfully cancels the current problem and yields a 'failed' result
 */
Test(worker_suite, worker_test_4, .timeout=TEST_TIMEOUT, .fini = killall) {
    FILE *from_worker = NULL, *to_worker = NULL;
    pid_t pid = launch_worker(&from_worker, &to_worker);

    /* Wait for the worker to SIGSTOP itself */
    int status = 0;
    Waitpid(pid, &status, WUNTRACED);

    /* A fake crypto miner problem for the worker to solve */
    struct problem prob = {
        (size_t) 16,
        (short) CRYPTO_MINER_PROBLEM_TYPE,
        (short) 1,
        (short) 1,
        (short) 0,
    };

    /* Signal the worker to continue and wait until this happens */
    Kill(pid, SIGCONT);
    Kill(pid, SIGHUP);
    Waitpid(pid, &status, WCONTINUED);

    /* Send the problem to the worker and cancel it via SIGHUP */
    write_problem(to_worker, &prob);

    /* Wait for the worker to SIGSTOP itself again */
    Waitpid(pid, &status, WUNTRACED);

    /* Read and validate the problem's result, if one was given */
    struct result *result = read_result(from_worker);
    cr_assert_not_null(result, "Failed reading result from worker.\n");
    cr_assert_eq(result->failed, 1, "Worker didn't return a 'failed' result for a trivial problem.\n");
}

struct crypto_miner_problem {
    size_t size;        // Total length in bytes, including size and type.
    short type;         // Problem type.
    short id;           // Problem ID.
    short nvars;        // Number of possible variant forms of the problem.
    short var;          // This variant of the problem.
    char padding[0];    // To align the subsequent data on a 16-byte boundary.
    int bsize;          // Size of the block, in bytes.
    int nsize;          // Size of a nonce, in bytes.
    short diff;         // Difficulty level to be satisfied.
    char data[0];       // Data: block, followed by starting nonce.
};

/*
 * worker_test_5
 * Ensure a worker process can correctly solve a trivial problem type
 */
Test(worker_suite, worker_test_5, .timeout=TEST_TIMEOUT, .fini = killall) {
    FILE *from_worker = NULL, *to_worker = NULL;
    pid_t pid = launch_worker(&from_worker, &to_worker);

    /* Wait for the worker to SIGSTOP itself */
    int status = 0;
    Waitpid(pid, &status, WUNTRACED);

    /* A crypto miner problem for the worker to solve */
    // Extracted from a valid, but arbitrarily chosen problem. Is 40 bytes.
    unsigned char data[] = {0xc2, 0xbc, 0xc2, 0x92, 0x75, 0x38, 0x1a, 0xc3, 0x8d, 0xc3, 0x81, 0xc3, 0x82, 0xc3, 0x9e, 0x74, 0xc2, 0x95, 0x13, 0x6e, 0xc2, 0xb5, 0xc2, 0x9d, 0xc3, 0xa1, 0x27, 0x50, 0xc2, 0x8f, 0x58, 0xc3, 0xb9, 0x18, 0xc3, 0xbf, 0xc2, 0xb3, 0xc3, 0x98};
    size_t bsize = 32, nsize = 8;
    size_t size = sizeof(struct crypto_miner_problem) + bsize + nsize;
    struct crypto_miner_problem *prob = malloc(size);
    prob->size = size;
    prob->type = CRYPTO_MINER_PROBLEM_TYPE;
    prob->id = 5;
    prob->nvars = 1;
    prob->var = 0;
    prob->bsize = bsize;
    prob->nsize = nsize;
    prob->diff = 25;
    memcpy(prob->data, data, bsize+nsize);

    /* Signal the worker to continue and wait until this happens */
    Kill(pid, SIGCONT);
    Waitpid(pid, &status, WCONTINUED);

    /* Send the problem to the worker */
    write_problem(to_worker, (struct problem *) prob);

    /* Wait for the worker to SIGSTOP itself again */
    Waitpid(pid, &status, WUNTRACED);

    /* Read and validate the problem's result, if one was given */
    struct result *result = read_result(from_worker);
    cr_assert_not_null(result, "Failed reading result from worker.\n");
    cr_assert_eq(result->failed, 0, "Worker returned a 'failed' result for a trivial problem.\n");

    /* Validate that the returned result struct is byte-for-byte the one we expect */
    unsigned char expected_result[] = {0x20, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x5, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x8, 0x0, 0x0, 0x0, 0x8, 0xef, 0xa9, 0xc0, 0xc2, 0xb3, 0xc3, 0x98, 0x0, 0x0, 0x0, 0x0};
    cr_assert_eq(result->size, sizeof(expected_result), "Returned struct was of size %d (expected %d)\n", result->size, sizeof(expected_result));
    unsigned char *current_byte = (unsigned char *) result;
    for (int i=0; i<sizeof(expected_result); i++) {
        cr_assert_eq(*current_byte, expected_result[i], "Returned result differed from expected result (expected %x, but found %x at byte %d out of %d)!", expected_result[i], *current_byte, i, sizeof(expected_result));
        current_byte++;
    }
}

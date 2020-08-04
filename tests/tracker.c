#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

#include "polya.h"
#include "event.h"
#include "tracker.h"

typedef struct worker {
    int pid;                            /* Process ID of worker. */
    int state;                          /* Worker state. */
    int problem_solved;                 /* Has current problem been solved? */
    int solving;                        /* Is worker currently solving a problem? */
    int exited;                         /* Has worker exited? */
    int problem_id;                     /* ID of current problem. */
    struct problem *prob;               /* Current problem header + data. */
    struct result *result;              /* Current result header + data. */
    struct timeval last_change_time;    /* Time of last state change for the worker. */
    struct timeval last_event_time;     /* Time of last event for the worker. */
    struct timeval next_event_timeout;  /* Timeout for next event (0 for no timeout). */
} WORKER;

int worker_state(WORKER *worker) { return worker->state; }

WORKER worker_table[MAX_WORKERS];
int num_workers = 0;

/* Defines valid worker state changes */
#define WORKER_STATE_MAX (WORKER_ABORTED+1)
static int valid_transitions[WORKER_STATE_MAX][WORKER_STATE_MAX] = {
    /*                    INIT, STARTED,   IDLE,    CONT.,  RUN,     STOP, 	EXIT,	  ABORTED */
    /* INIT */		{ 0,       1,       0,       0,      0,        0,         0,         0 },
    /* STARTED */ 	{ 0,       0,       1,       0,      0,        0,         0,         0 },
    /* IDLE */  	{ 0,       0,       0,       1,      1,        0,         1,         0 },  // NOTE: No event for sending SIGTERM
    /* CONT. */ 	{ 0,       0,       0,       0,      1,        1,         1,         0 },
    /* RUN */   	{ 0,       0,       0,       0,      0,        1,         1,         0 },  // NOTE: No event for sending SIGTERM
    /* STOP */  	{ 0,       0,       1,       0,      0,        0,         0,         0 },
    /* EXIT */  	{ 0,       0,       0,       0,      0,        0,         0,         0 },
    /* ABORT */  	{ 0,       0,       0,       0,      0,        0,         0,         0 }
};

/*
 * Predefined timeout values.
 */
#define ZERO_SEC { 0, 0 }
#define ONE_USEC { 0, 1 }
#define ONE_MSEC { 0, 1000 }
#define TEN_MSEC { 0, 10000 }
#define HND_MSEC { 0, 100000 }
#define ONE_SEC  { 1, 0 }

static struct timeval transition_timeouts[WORKER_STATE_MAX][WORKER_STATE_MAX] = {
    /*                    INIT,      STARTED,   IDLE,      CONT.,     RUN,       STOP,      EXIT,      ABORTED */
    /* INIT */		{ ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC },
    /* STARTED */ 	{ ZERO_SEC,  ZERO_SEC,  HND_MSEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC },
    /* IDLE */  	{ ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  HND_MSEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC },
    /* CONT. */ 	{ ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  HND_MSEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC },
    /* RUN */   	{ ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC },
    /* STOP */  	{ ZERO_SEC,  ZERO_SEC,  HND_MSEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC },
    /* EXIT */  	{ ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC },
    /* ABORT */  	{ ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC }
};

static int master_started = 0;

static void show_worker(WORKER *worker) {
    fprintf(stderr, "[%d: %s, %d]",
	    worker->pid, worker_state_names[worker->state], worker->problem_id);
}

void tracker_show_workers(void) {
    fprintf(stderr, "WORKERS: ");
    for(int i = 0; i < MAX_WORKERS; i++) {
	WORKER *worker = &worker_table[i];
	if(worker->pid) {
	    show_worker(worker);
	}
    }
    fprintf(stderr, "\n");
}

/**
 * Checks for a valid transition, within any specified time limit.
 * @param worker the worker changing state
 * @param old the old status
 * @param new the new status
 * @return nonzero if transition is valid, 0 otherwise
 */
static int valid_transition(WORKER *worker, int old, int new, struct timeval *when) {
    if(old < 0 || old >= WORKER_STATE_MAX || new < 0 || new >= WORKER_STATE_MAX)
	return 0;
    if(!valid_transitions[old][new]) {
	fprintf(stderr, "Worker %d transition is invalid (old %s, new %s)\n",
		worker->pid, worker_state_names[old], worker_state_names[new]);
	return 0;
    }
    struct timeval timeout = transition_timeouts[old][new];
    if(timeout.tv_sec == 0 && timeout.tv_usec == 0)
	return 1;
    struct timeval exp_time = worker->last_change_time;
    exp_time.tv_usec += timeout.tv_usec;
    while(exp_time.tv_usec >= 1000000) {
	exp_time.tv_sec++;
	exp_time.tv_usec -= 1000000;
    }
    exp_time.tv_sec += timeout.tv_sec;
    if(when->tv_sec > exp_time.tv_sec ||
       (when->tv_sec == exp_time.tv_sec && when->tv_usec > exp_time.tv_usec)) {
	fprintf(stderr, "Worker %d transition (old %s, new %s) is too late "
		"(exp = %ld.%06ld, sent = %ld.%06ld)\n",
		worker->pid, worker_state_names[old], worker_state_names[new],
		exp_time.tv_sec, exp_time.tv_usec, when->tv_sec, when->tv_usec);
	return 0;
    }
#if 0
    fprintf(stderr, "Worker %d transition (old %s, new %s) is timely "
	    "(exp = %ld.%06ld, sent = %ld.%06ld)\n",
	    worker->pid, worker_state_names[old], worker_state_names[new],
	    exp_time.tv_sec, exp_time.tv_usec, when->tv_sec, when->tv_usec);
#endif
    return 1;
}

/**
 * Function is to be run when a solved result is received. Iterates through
 * workers and marks workers with same problem as already satisfied for bookkeeping.
 * This is done to allow for checking cancel requests sent to a worker with a problem
 * that has not been solved yet.
 * @param prob the problem that has been solved.
 * @param exception the worker that solved the problem, we dont want to mark this
 */
static void mark_workers_cancellable(struct problem *prob, WORKER *exception) {
    for (int i = 0; i < MAX_WORKERS; i++) {
	WORKER *worker = &worker_table[i];
	if (worker->prob == prob && worker != exception) {
	    worker->problem_solved = 1;
#if 0
	    fprintf(stderr, "Worker %d is now cancellable.\n", worker->pid);
	    if (worker->status == WORKER_RUNNING) {
		worker->next_event_expected = CANCEL_EVENT;
	    }
#endif
	}
    }
}

/**
 * Finds the index of the worker in the worker table.
 * @param pid The pid of the worker to find.
 * @return the worker corresponding to pid, NULL else.
 */
WORKER *find_worker(int pid) {
    for(int i = 0; i < MAX_WORKERS; i++) {
	if(worker_table[i].pid == pid)
	    return &worker_table[i];
    }
    return NULL;
}

static int time_increases(struct timeval *tv1, struct timeval *tv2) {
    return (tv1->tv_sec < tv2->tv_sec ||
	    (tv1->tv_sec == tv2->tv_sec && tv1->tv_usec < tv2->tv_usec));
}

int track_event(EVENT *ep) {
    if (!master_started) {
	if (ep->type == START_EVENT)
	    master_started = 1;
	else
	    fprintf(stderr, "First event is not START_EVENT.\n");
	return 0;
    }
    WORKER *worker = find_worker(ep->pid);

    switch(ep->type) {
    case START_EVENT:
	fprintf(stderr, "Redundant START_EVENT received.\n");
	return -1;
	break;
    case CHANGE_EVENT:
	if(!worker) {
	    if (ep->newstate == WORKER_STARTED) {
		if (num_workers >= MAX_WORKERS) {
		    fprintf(stderr, "Exceeded max workers count (%d)\n", MAX_WORKERS);
		    return -1;
		}
		worker = &worker_table[num_workers++];
		worker->pid = ep->pid;
		worker->state = 0;  // INIT
		worker->exited = 0;
	    } else {
		fprintf(stderr, "Worker %d changing state does not exist\n", ep->pid);
		return -1;
	    }
	}
	if (worker->exited) {
	    fprintf(stderr, "Worker %d changing state has exited before.\n", ep->pid);
	    return -1;
	}
	if (ep->oldstate != worker->state) {
	    fprintf(stderr, "Worker %d old state doesn't match (exp: %s, got: %s)\n", ep->pid,
		    worker_state_names[worker->state], worker_state_names[ep->oldstate]);
	    return -1;
	}
	if (!time_increases(&worker->last_event_time, &ep->time)) {
	    fprintf(stderr, "Worker %d time of last event (%ld.%06ld) "
                    "is greater than time of this event (%ld.%06ld)\n",
		    worker->pid, worker->last_event_time.tv_sec, worker->last_event_time.tv_usec,
		    ep->time.tv_sec, ep->time.tv_usec);
	    return -1;
	}
	if (!valid_transition(worker, ep->oldstate, ep->newstate, &ep->time)) {
	    // Error message is printed by valid_transition.
	    return -1;
	}
	fprintf(stderr, "                   [%d: %s -> %s]\n", worker->pid,
		worker_state_names[worker->state], worker_state_names[ep->newstate]);
	worker->state = ep->newstate;
	worker->last_event_time = ep->time;
	gettimeofday(&worker->last_change_time, NULL);

	// Handle worker terminating
	if (worker->state == WORKER_ABORTED || worker->state == WORKER_EXITED) {
	    if (worker->exited) {
		fprintf(stderr, "Worker %d exited multiple times\n", worker->pid);
		return -1;
	    }
	    worker->exited = 1;
	}
	break;
    case SEND_EVENT:
	if (!worker) {
	    fprintf(stderr, "Worker %d being sent problem %p does not exist.\n", ep->pid, ep->prob);
	    return -1;
	}
	if (worker->exited) {
	    fprintf(stderr, "Worker %d being sent problem %p has already exited.\n", ep->pid, ep->prob);
	    return -1;
	}
	if (worker->solving) {
	    fprintf(stderr, "Worker %d being sent problem is already solving.\n", ep->pid);
	    return -1;
	}
	if (worker->state != WORKER_IDLE && worker->state != WORKER_CONTINUED 
	    && worker->state != WORKER_RUNNING) {
	    fprintf(stderr, "Worker %d being sent a problem while %s\n", ep->pid,
		    worker_state_names[worker->state]);
	    return -1;
	}
	worker->prob = ep->prob;
	worker->result = NULL;
	worker->problem_id = ep->msg_id;
	worker->problem_solved = 0;
	worker->solving = 1;
	break;
    case RECV_EVENT:
	if (!worker) {
	    fprintf(stderr, "Worker %d sending result %p does not exist\n", ep->pid, ep->result);
	    return -1;
	}
	if (worker->exited) {
	    fprintf(stderr, "Worker %d sending result %p has already exited.\n", ep->pid, ep->result);
	    return -1;
	}
	// The reference worker does not match problem IDs to result IDs 
	// if (ep->msg_id != worker->problem_id) {
	// 	fprintf(stderr, "Worker %d sent result mismatch (assigned: %hu, returned: %hu)\n", ep->pid,
	// 		worker->problem_id, ep->msg_id);
	// 	return -1;
	// }
	if (!ep->result_failed)
	    mark_workers_cancellable(worker->prob, worker);
	worker->result = ep->result;
	worker->solving = 0;
	break;
    case CANCEL_EVENT:
	if (!worker) {
	    fprintf(stderr, "Worker %d being canceled does not exist\n", ep->pid);
	    return -1;
	}
	if (worker->exited) {
	    fprintf(stderr, "Worker %d being canceled has already exited.\n", ep->pid);
	    return -1;
	}
	if (!worker->solving) {
	    fprintf(stderr, "Worker %d being canceled is not solving.\n", ep->pid);
	    return -1;
	}
	if (!worker->problem_solved) {
	    fprintf(stderr, "Worker %d being canceled but problem has not been solved.\n", ep->pid);
	    return -1;
	}
	worker->solving = 0;
	break;
    case END_EVENT:
	// TODO: Need a way to check if all problems solved.
	// This can't be done by querying the problem module, because that is in another process.
	for (int i = 0; i < MAX_WORKERS; i++) {
	    WORKER temp_worker = worker_table[i];
	    if (temp_worker.pid)
		if (!temp_worker.exited) {
		    fprintf(stderr, "Polya ending while worker with pid %d is not exited yet.\n", temp_worker.pid);
		    return -1;
		}
	}
	break;
    default:
	fprintf(stderr, "Attempt to track unknown event type %d\n", ep->type);
	return -1;
    }
    //tracker_show_workers();
    return 0;
}


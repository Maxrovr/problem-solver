typedef struct worker WORKER;

extern int num_workers;
extern char *worker_state_names[];

WORKER *find_worker(int pid);
int worker_state(WORKER *worker);
int track_event(EVENT *ep);

void init(int workers);
void signal_handler(int sig);
void install_signal();
void init_fds(int workers);
void update_worker_state(pid_t workerpid, int new_state);
int get_workerid(pid_t workerpid);
void add_workerpid(pid_t workerpid);
void send_problem(pid_t pid);
void destructor();
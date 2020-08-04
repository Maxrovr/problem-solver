
#define EVENT_SOCKET "event_socket"

typedef enum event_type {
    NO_EVENT, ANY_EVENT, EOF_EVENT, START_EVENT, END_EVENT, CHANGE_EVENT,
    SEND_EVENT, RECV_EVENT, CANCEL_EVENT
} EVENT_TYPE;

typedef struct event {
    int type;
    int pid;
    int oldstate;
    int newstate;
    int msg_id;
    int result_failed;
    struct problem *prob;
    struct result *result;
    struct timeval time;
} EVENT;

extern char *event_type_names[];

#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include "map.h"
#include "util.h"
#include "list.h"
#include "ringbuf.h"
#include "protocol.h"


#define EPOLL_WORKERS 4
#define MAX_EVENTS	  64

#define ERR_UNKNOWN   0x64
#define ERR_MISS_CHAN 0x65
#define ERR_MISS_MEX  0x66
#define ERR_MISS_ID   0x67

#define OK          "OK\n"
#define E_UNKNOWN   "ERR: Unknown command\n"
#define E_MISS_CHAN "ERR: Missing channel name\n"
#define E_MISS_MEX  "ERR: Missing message to publish\n"
#define E_MISS_ID   "ERR: Missing ID and clean session set to True\n"

#define TIMEOUT         60


enum REPLY_TYPE { NO_REPLY, ACK_REPLY, NACK_REPLY, JACK_REPLY, DATA_REPLY, PING_REPLY };

enum STATUS { ONLINE, OFFLINE };


typedef struct client client_t;

typedef struct reply reply_t;

struct client {
    uint8_t type;
    uint8_t status;
    int fd;
    int (*ctx_handler)(int, client_t *);
    char *id;
    reply_t *reply;
    list_t *subscriptions;
    union {
        request_t *req;
        response_t *res;
    };
};


struct socks {
    int epollfd;
    int serversock;
};


struct reply {
    uint8_t type;
    uint8_t qos;
    int fd;
    char *data;
    char *channel;
};


struct command {
    int ctype;
    int (*handler)(client_t *);
};


struct global {
    /* Eventfd to break the epoll_wait loop in case of signals */
    uint8_t run;
    /* Logging level, to be set by reading configuration */
    uint8_t loglevel;
    /* Atomic auto-increment unsigned long long int to get the next message ID */
    atomic_t *next_id;
    /* Channels mapping */
    map_t *channels;
    /* ACK awaiting mapping fds (Unused) */
    map_t *ack_waiting;
    /* Tracking clients */
    map_t *clients;
    /* Peers connected */
    list_t *peers;
    /* Global lock to avoid race conditions on critical shared parts */
    pthread_mutex_t lock;
    /* Approximation of the load */
    atomic_t *throughput;
    /* Throttler utility */
    throttler_t *throttler;
    /* Epoll workers count */
    int workers;
};


extern struct global global;


int parse_header(ringbuf_t *, char *, uint8_t *);
int start_server(const char *, char *, int);

#endif

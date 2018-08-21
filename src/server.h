#ifndef NETWORKING_H
#define NETWORKING_H

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include "map.h"
#include "util.h"

#define EPOLL_WORKERS 4
#define MAX_EVENTS	  64

#define OK          "OK\n"
#define E_UNKNOWN   "ERR: Unknown command\n"
#define E_MISS_CHAN "ERR: Missing channel name\n"
#define E_MISS_MEX  "ERR: Missing message to publish\n"

#define TIMEOUT      60

enum REPLY_TYPE { NO_REPLY, ACK_REPLY, NACK_REPLY, DATA_REPLY, PING_REPLY };


struct socks {
    int epollfd;
    int serversock;
};


typedef struct {
    uint8_t type;
    uint8_t qos;
    int fd;
    char *data;
    char *channel;
} reply_t;


struct global {
    /* Eventfd to break the epoll_wait loop in case of signals */
    uint8_t run;
    /* Logging level, to be set by reading configuration */
    uint8_t loglevel;
    /* Atomic auto-increment unsigned long long int to get the next message ID */
    counter_t *next_id;
    /* Channels mapping */
    map_t *channels;
    /* ACK awaiting mapping fds (Unused) */
    map_t *ack_waiting;
    /* Global lock to avoid race conditions on critical shared parts */
    pthread_mutex_t lock;
};


extern struct global global;


int start_server();

#endif

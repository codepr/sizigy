#ifndef NETWORKING_H
#define NETWORKING_H

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include "map.h"
#include "util.h"
#include "ringbuf.h"

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
    uint8_t run;
    uint8_t loglevel;
    counter_t *next_id;
    map_t *channels;
    map_t *ack_waiting;
    pthread_mutex_t lock;
};


extern struct global global;


int start_server();

#endif

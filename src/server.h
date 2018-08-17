#ifndef NETWORKING_H
#define NETWORKING_H

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include "map.h"
#include "util.h"

#define EPOLL_WORKERS 4
#define MAX_EVENTS	  64
#define BUFSIZE		  2048

#define OK          "OK\n"
#define E_UNKNOWN   "ERR: Unknown command\n"
#define E_MISS_CHAN "ERR: Missing channel name\n"
#define E_MISS_MEX  "ERR: Missing message to publish\n"


enum REPLY_TYPE { NO_REPLY, ACK_REPLY, NACK_REPLY, DATA_REPLY, PING_REPLY };


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


struct global {
    counter_t next_id;
    map *channels;
    map *ack_waiting;
    pthread_mutex_t lock;
};


extern struct global global;


int start_server();
int sendall(int , char *, ssize_t *);
int recvall(int, char *);

#endif

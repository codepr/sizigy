/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2018, Andrea Giacomo Baldan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include "map.h"
#include "util.h"
#include "list.h"
#include "ringbuf.h"
#include "channel.h"
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


enum REPLY_TYPE {
    NO_REPLY,
    CONNACK_REPLY,
    SUBACK_REPLY,
    PUBACK_REPLY,
    JACK_REPLY,
    DATA_REPLY
};

enum STATUS { ONLINE, OFFLINE };


typedef struct client Client;

typedef struct reply Reply;

struct client {
    uint8_t type;
    uint8_t status;
    const char *addr;
    int fd;
    int (*ctx_handler)(int, Client *);
    char *id;
    Reply *reply;
    List *subscriptions;
    union {
        Request *req;
        Response *res;
    };
};


struct socks {
    const int epollfd;
    const int serversock;
};


struct reply {
    uint8_t type;
    int fd;
    union {
        /* Publish reply */
        struct {
            uint8_t qos;
            uint8_t retain;
            char *data;
            char *channel;
        };
        /* Suback reply */
        Message *retained;
        /* Common ack reply */
        uint8_t rc;
    };
};


struct command {
    const int ctype;
    int (*handler)(Client *);
};


struct global {
    /* Eventfd to break the epoll_wait loop in case of signals */
    uint8_t run;
    /* Logging level, to be set by reading configuration */
    uint8_t loglevel;
    /* Bus listening port, for peers connections */
    int bus_port;
    /* Bus epoll fd */
    int bepollfd;
    /* Atomic auto-increment unsigned long long int to get the next message ID */
    Atomic *next_id;
    /* Channels mapping */
    Map *channels;
    /* ACK awaiting mapping fds (Unused) */
    Map *ack_waiting;
    /* Tracking clients */
    Map *clients;
    /* Peers connected */
    List *peers;
    /* Global lock to avoid race conditions on critical shared parts */
    pthread_mutex_t lock;
    /* Approximation of the load */
    Atomic *throughput;
    /* Throttler utility */
    Throttler *throttler;
    /* Epoll workers count */
    int workers;
};


extern struct global global;


uint8_t *recv_packet(const int, Ringbuffer *, uint8_t *);
int start_server(const char *, char *, int);

#endif

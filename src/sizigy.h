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

#ifndef SIZIGY_H
#define SIZIGY_H

#include <stdint.h>
#include "list.h"
#include "util.h"
#include "queue.h"
#include "hashmap.h"
#include "protocol.h"


typedef struct client Client;
typedef struct reply Reply;
typedef struct sizigydb SizigyDB;


struct client {
    uint8_t status;
    uint16_t keepalive;
    // Atomic *last_action_time;
    uint64_t last_action_time;
    const char *addr;
    int fd;
    int (*ctx_handler)(SizigyDB *, Client *);
    uint8_t *id;
    Reply *reply;
    List *subscriptions;
    void *ptr;
};


struct sizigydb {
    /* Eventfd to break the epoll_wait loop in case of signals */
    uint8_t run;
    /* Bus listening port, for peers connections */
    int bus_port;
    /* Main epoll loop fd */
    int epollfd;
    /* Bus epoll fd */
    int bepollfd;
    /* topics hashmapping */
    Hashmap *topics;
    /* ACK awaiting hashmapping fds (Unused) */
    Hashmap *ack_waiting;
    /* Tracking clients */
    Hashmap *clients;
    /* Peers connected */
    List *peers;
    /* Global lock to avoid race conditions on critical shared parts */
    pthread_mutex_t lock;
};


typedef struct {
    uint64_t id;
    uint8_t qos;
    uint8_t dup;
    uint8_t retained;
    time_t creation_time;
    const uint8_t *sender;
    const uint8_t *topic;
    const uint8_t *payload;
} Message;


typedef struct {
    const char *name;
    List *subscribers;
    Queue *messages;
    Message *retained;
} Topic;


typedef struct {
    Client *client;
    const uint8_t *topic;
    uint8_t qos;
} Subscription;


// Message ops
Message *create_message(Publish *, const uint8_t *);
void destroy_message(Message *);


// Client ops
void add_client(SizigyDB *, Client *);
void del_client(SizigyDB *, Client *);
void update_client_last_action(Client *);
uint64_t get_client_last_action(Client *);


// Subscriber ops
Subscription *create_subscription(Client *, const char *, uint8_t);
void destroy_subscription(Subscription *);


// topic ops
Topic *create_topic(char *);
void destroy_topic(Topic *);
void add_topic(SizigyDB *, char *);
void add_subscriber(SizigyDB *, Subscription *);
void del_subscriber(SizigyDB *, Subscription *);

#endif

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

#ifndef CHANNEL_H
#define CHANNEL_H

#include <stdint.h>
#include "list.h"
#include "queue.h"


#define ADD_DELAY       0
#define PUB_DELAY       500
#define MAX_PUB_RETRY   10
#define PUB_RETRY_DELAY 300


typedef struct {
    uint64_t id;
    uint8_t qos;
    uint8_t dup;
    uint8_t retained;
    time_t creation_time;
    char *channel;
    char *payload;
} Message;


typedef struct {
    char *name;
    List *subscribers;
    Queue *messages;
    Message *retained;
} Channel;


struct subscriber {
    int fd;
    uint8_t qos;
    char *name;
};


Channel *create_channel(char *);
void add_subscriber(Channel *, struct subscriber *);
void del_subscriber(Channel *, struct subscriber *);
int publish_message(Channel *, uint8_t, uint8_t, void *, int);
void store_message(Channel *, const uint64_t, uint8_t, uint8_t, const char *, int);
void retain_message(Channel *, const uint64_t, uint8_t, uint8_t, const char *);
void destroy_channel(Channel *);

#endif

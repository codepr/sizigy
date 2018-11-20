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

#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/epoll.h>
#include "server.h"
#include "network.h"
#include "protocol.h"


static int handle_request(const int epollfd, const int fd) {

    /* Buffer to initialize the ring buffer, used to handle input from client */
    uint8_t buffer[ONEMB * 2];

    /* Ringbuffer pointer struct, helpful to handle different and unknown
       size of chunks of data which can result in partially formed packets or
       overlapping as well */
    Ringbuffer *rbuf = ringbuf_init(buffer, ONEMB * 2);

    /* Placeholders structures, at this point we still don't know if we got a
       request or a response */
    Response res;
    uint8_t type = 0;

    /* We must read all incoming bytes till an entire packet is received. This
       is achieved by using a standardized protocol, which send the size of the
       complete packet as the first 5 bytes. By knowing it we know if the packet is
       ready to be deserialized and used.*/
    Buffer *bytes = recv_packet(fd, rbuf, &type);

    uint8_t read_all = unpack_response(bytes, &res);

    free(bytes);

    /* Free ring buffer as we alredy have all needed informations in memory */
    ringbuf_free(rbuf);

    if (read_all == 1) {
        return -1;
    }

    if (res.header->opcode == PUBLISH)
        printf("%s\n", res.message);

    mod_epoll(epollfd, fd, EPOLLIN, NULL);

    return 0;
}


void *response_thread(void *ptr) {
    struct socks *pair = (struct socks *) ptr;
    struct epoll_event *events = malloc(sizeof(*events) * MAX_EVENTS);
    int events_cnt;
    while ((events_cnt = epoll_wait(pair->epollfd, events, MAX_EVENTS, -1)) > 0) {
        for (int i = 0; i < events_cnt; i++) {
            if ((events[i].events & EPOLLERR) ||
                    (events[i].events & EPOLLHUP) ||
                    (!(events[i].events & EPOLLIN) && !(events[i].events & EPOLLOUT))) {
                /* An error has occured on this fd, or the socket is not
                   ready for reading */
                perror ("epoll_wait(2)");
                close(events[i].data.fd);
                continue;
            } else if (events[i].events & EPOLLIN) {
                if (handle_request(pair->epollfd, events[i].data.fd) == -1) {
                    perror("Error handling request");
                    close(events[i].data.fd);
                }
            }
        }
    }

    free(events);

    return NULL;
}


int main(int argc, char **argv) {
    int epollfd;
    /* Initialize epollfd */
    if ((epollfd = epoll_create1(0)) == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }
    /* Connect to the broker*/
    int connfd = make_connection("127.0.0.1", 9090);
    set_nonblocking(connfd);
    add_epoll(epollfd, connfd, NULL);
    ssize_t n;
    const uint8_t *topic = (const uint8_t *) "test01";
    /* Create a protocol formatted packet to subscribe to a topic */
    Request *sub_r = build_subscribe_request(REQUEST, SUBSCRIBE, AT_MOST_ONCE, topic, (const uint8_t *) "");
    /* Pack it in order to be sent in binary format */
    Buffer *sp = pack_request(sub_r);
    /* Subscribe to the topic */
    if ((n = sendall(connfd, sp->data, sp->size, &(ssize_t) { 0 })) < 0)
        return -1;

    free(sp);

    struct socks pair = { epollfd, connfd };
    response_thread(&pair);
    return 0;
}

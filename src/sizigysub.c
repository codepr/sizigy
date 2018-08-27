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


int handle_request(int epollfd, int fd) {
    int ret = 0;
    /* Buffer to initialize the ring buffer, used to handle input from client */
    uint8_t buffer[ONEMB * 2];

    /* Ringbuffer pointer struct, helpful to handle different and unknown
       size of chunks of data which can result in partially formed packets or
       overlapping as well */
    ringbuf_t *rbuf = ringbuf_init(buffer, ONEMB * 2);

    /* Read all data to form a packet flag */
    int8_t read_all = -1;
    ssize_t n;
    protocol_packet_t *p = malloc(sizeof(protocol_packet_t));

    time_t start = time(NULL);
    while (read_all != 0) {
        if ((n = recvall(fd, rbuf, read_all)) < 0) {
            ret = -1;
            goto cleanup;
        }
        if (n == 0) {
            ret = 0;
            goto cleanup;
        }

        /* Unpack incoming bytes */
        char bytes[ringbuf_size(rbuf)];
        /* Check the header, returning -1 in case of insufficient informations
           about the total packet length and the subsequent payload bytes */
        read_all = parse_header(rbuf, bytes);

        if (read_all == 0)
            read_all = unpack((uint8_t *) bytes, p);

        if (time(NULL) - start > 60)
            read_all = 1;
    }

    if (read_all == 1) {
        ret = -1;
        goto cleanup;
    }

    if (p->opcode == 0x05)
        printf("%s\n", p->pub_packet->data);

    ringbuf_free(rbuf);

    mod_epoll(epollfd, fd, EPOLLIN, NULL);

cleanup:
    free(p);
    return ret;
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
    /* Create a protocol formatted packet to subscribe to a channel */
    protocol_packet_t *sub_packet = build_request_subscribe("test01", 0);
    /* Pack it in order to be sent in binary format */
    packed_t *sp = pack(sub_packet);
    /* Subscribe to the channel */
    if ((n = sendall(connfd, sp->data, sp->size, &(ssize_t) { 0 })) < 0)
        return -1;

    free(sp);

    struct socks pair = { epollfd, connfd };
    response_thread(&pair);
    return 0;
}

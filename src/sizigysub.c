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
    /* Buffer to initialize the ring buffer, used to handle input from client */
    uint8_t buffer[BUFSIZE * 3];

    /* Ringbuffer pointer struct, helpful to handle different and unknown
       size of chunks of data which can result in partially formed packets or
       overlapping as well */
    ringbuf_t *rbuf = ringbuf_init(buffer, BUFSIZE * 3);

    /* Read all data to form a packet flag */
    int8_t read_all = -1;
    ssize_t n;
    protocol_packet_t *p = malloc(sizeof(protocol_packet_t));

    time_t start = time(NULL);
    while (read_all == -1) {
        if ((n = recvall(fd, rbuf)) < 0) {
            return -1;
        }
        if (n == 0) {
            return 0;
        }

        /* Unpack incoming bytes */
        read_all = unpack(rbuf, p);

        if (time(NULL) - start > 60)
            read_all = 1;

    }

    if (read_all == 1)
        return -1;

    if (p->opcode == 0x05)
        printf("%s\n", p->payload.sys_pubpacket->data);

    ringbuf_free(rbuf);
    free(p);

    set_epollin(epollfd, fd);

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
    add_epollin(epollfd, connfd);
    ssize_t n;
    /* Create a protocol formatted packet to subscribe to a channel */
    protocol_packet_t *sub_packet = create_sys_subpacket(SUBSCRIBE_CHANNEL, 0, 0, "test01");
    /* Pack it in order to be sent in binary format */
    packed_t *sp = pack(sub_packet);
    /* Subscribe to the channel */
    if ((n = sendall(connfd, sp->data, &sp->size)) < 0)
        return -1;

    free(sp);

    struct socks pair = { epollfd, connfd };
    response_thread(&pair);
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/eventfd.h>
#include <arpa/inet.h>
#include "util.h"
#include "server.h"
#include "parser.h"
#include "channel.h"
#include "protocol.h"


struct global global;


static void add_epollin(int efd, int fd) {
    struct epoll_event ev;
    ev.data.fd = fd;
    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;

    if (epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        perror("epoll_ctl(2): add epollin");
    }
}


static void set_epollout(int efd, int fd, void *data) {
    struct epoll_event ev;
    if (data)
        ev.data.ptr = data;
    ev.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;

    if (epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev) < 0) {
        perror("epoll_ctl(2): set epollout");
    }
}


static void set_epollin(int efd, int fd) {
    struct epoll_event ev;
    ev.data.fd = fd;
    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;

    if (epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev) < 0) {
        perror("epoll_ctl(2): set epollin");
    }
}


static void free_reply(reply_t *reply) {
    if (reply->data)
        free(reply->data);
    if (reply->channel)
        free(reply->channel);
}

/* Set non-blocking socket */
static int set_nonblocking(int fd) {
    int flags, result;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        return -1;
    }
    result = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (result == -1) {
        perror("fcntl");
        return -1;
    }
    return 0;
}


/* Auxiliary function for creating epoll server */
static int create_and_bind(const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int sfd;

    memset(&hints, 0, sizeof (struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;     /* 0.0.0.0 all interfaces */

    if (getaddrinfo(host, port, &hints, &result) != 0) {
        perror("getaddrinfo error");
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (sfd == -1) continue;

        /* set SO_REUSEADDR so the socket will be reusable after process kill */
        if (setsockopt(sfd, SOL_SOCKET, (SO_REUSEPORT | SO_REUSEADDR),
                    &(int) { 1 }, sizeof(int)) < 0)
            perror("SO_REUSEADDR");

        if ((bind(sfd, rp->ai_addr, rp->ai_addrlen)) == 0) {
            /* Succesful bind */
            break;
        }
        close(sfd);
    }

    if (rp == NULL) {
        perror("Could not bind");
        return -1;
    }

    freeaddrinfo(result);
    return sfd;
}


/*
 * Create a non-blocking socket and make it listen on the specfied address and
 * port
 */
static int make_listen(const char *host, const char *port) {
    int sfd;

    if ((sfd = create_and_bind(host, port)) == -1)
        abort();

    if ((set_nonblocking(sfd)) == -1)
        abort();

    if ((listen(sfd, SOMAXCONN)) == -1) {
        perror("listen");
        abort();
    }

    return sfd;
}


static int accept_connection(int epollfd, int serversock) {

    int clientsock;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if ((clientsock = accept(serversock, (struct sockaddr *) &addr, &addrlen)) < 0) {
        return -1;
    }

    set_nonblocking(clientsock);

    char ip_buff[INET_ADDRSTRLEN+1];
    if (inet_ntop(AF_INET, &addr.sin_addr, ip_buff, sizeof(ip_buff)) == NULL) {
        close(clientsock);
        return -1;
    }

    DEBUG("*** Client connection from %s\n", ip_buff);

    // add to EPOLLIN client sock
    add_epollin(epollfd, clientsock);

    return 0;
}


static int send_data(void *arg1, int fd) {
    queue_item *item = (queue_item *) arg1;
    protocol_packet_t *pp = (protocol_packet_t *) item->data;
    packed_t *p = pack(pp);
    if (sendall(fd, p->data, &p->size) < 0) {
        perror("send(2): error sending\n");
        return -1;
    }
    free(pp->payload.sys_pubpacket->data);
    free(p->data);
    free(p);
    return 0;
}


static int close_socket(void *arg1, void *arg2) {
    int fd = *(int *) arg1;
    map_entry *kv = (map_entry *) arg2;
    struct subscriber *sub = (struct subscriber *) kv->val;
    if (sub->fd == fd)
        close(sub->fd);
    return 0;
}


static int handle_request(int epollfd, int clientfd) {

    /* Buffer to initialize the ring buffer, used to handle input from client */
    uint8_t buffer[BUFSIZE * 3];

    /* Ringbuffer pointer struct, helpful to handle different and unknown
       size of chunks of data which can result in partially formed packets or
       overlapping as well */
    ringbuf_t *rbuf = ringbuf_init(buffer, BUFSIZE * 3);

    /* Read all data to form a packet flag */
    int8_t read_all = -1;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    ssize_t n;
    protocol_packet_t *p = malloc(sizeof(protocol_packet_t));

    pthread_mutex_lock(&(global.lock));

    /* We must read all incoming bytes till an entire packet is received. This
       is achieved by using a standardized protocol, which send the size of the
       complete packet as the first 4 bytes. By knowing it we know if the packet is
       ready to be deserialized and used.*/
    while (read_all == -1) {
        if ((n = recvall(clientfd, rbuf)) < 0) {
            return -1;
        }
        if (n == 0) {
            return 0;
        }
        /* Unpack incoming bytes */
        read_all = unpack(rbuf, p);
    }

    if (getpeername(clientfd, (struct sockaddr *) &addr, &addrlen) < 0) {
        return -1;
    }

    char ip_buff[INET_ADDRSTRLEN + 1];
    if (inet_ntop(AF_INET, &addr.sin_addr, ip_buff, sizeof(ip_buff)) == NULL) {
        return -1;
    }

    /* Parse command according to the communication protocol */
    command_t *comm = parse_command(p);

    reply_t *reply = malloc(sizeof(reply_t));

    if (!reply) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }

    reply->fd = clientfd;
    reply->data = OK;       // placeholder reply
    reply->qos = comm->qos;
    void *raw_chan = NULL;  // placeholder for map_get

    switch(comm->opcode) {
        case CREATE_CHANNEL:
            DEBUG("*** CREATE %s\n", comm->cmd.b->channel_name);
            channel_t *channel = create_channel(comm->cmd.b->channel_name);
            map_put(global.channels, comm->cmd.b->channel_name, channel);
            reply->type = ACK_REPLY;
            break;
        case DELETE_CHANNEL:
            DEBUG("*** DELETE %s\n", comm->cmd.b->channel_name);
            raw_chan = map_get(global.channels, comm->cmd.b->channel_name);
            if (raw_chan) {
                channel_t *chan = (channel_t *) raw_chan;
                destroy_channel(chan);
            }
            map_del(global.channels, comm->cmd.b->channel_name);
            reply->type = ACK_REPLY;
            break;
        case SUBSCRIBE_CHANNEL:
            DEBUG("*** SUBSCRIBE %s\n", comm->cmd.b->channel_name);
            void *raw = map_get(global.channels, comm->cmd.b->channel_name);
            if (!raw) {
                channel_t *channel = create_channel(comm->cmd.b->channel_name);
                map_put(global.channels, strdup(comm->cmd.b->channel_name), channel);
            }
            channel_t *chan = (channel_t *) map_get(global.channels, comm->cmd.b->channel_name);
            struct subscriber *sub = malloc(sizeof(struct subscriber));
            if (!sub) {
                perror("malloc(3) failed");
                exit(EXIT_FAILURE);
            }
            sub->fd = clientfd;
            sub->name = "sub";
            sub->qos = comm->qos;
            add_subscriber(chan, sub);
            send_queue(chan->messages, clientfd, send_data);
            reply->type = ACK_REPLY;
            break;
        case UNSUBSCRIBE_CHANNEL:
            DEBUG("*** UNSUBSCRIBE %s\n", comm->cmd.b->channel_name);
            raw_chan = map_get(global.channels, comm->cmd.b->channel_name);
            if (raw_chan) {
                channel_t *chan = (channel_t *) raw_chan;
                // XXX basic placeholder subscriber
                struct subscriber sub = { clientfd, AT_MOST_ONCE, "sub" };
                del_subscriber(chan, &sub);
            }
            reply->type = ACK_REPLY;
            break;
        case PUBLISH_MESSAGE:
            reply->data = strdup(comm->cmd.a->message);
            reply->channel = strdup(comm->cmd.a->channel_name);
            reply->type = DATA_REPLY;
            break;
        case PING:
            reply->data = "PONG\n";
            reply->type = PING_REPLY;
            DEBUG("*** PING from %s\n", ip_buff);
            break;
        case ACK:
            reply->type = NO_REPLY;
            uint64_t id = 0;
            /* Extract uint64_t ID from the payload */
            char *id_str = comm->cmd.b->channel_name;
            while (*id_str != '\0') {
                id = (id * 10) + (*id_str - '0');
                id_str++;
            }
            DEBUG("*** ACK from %s for packet id %ld\n", ip_buff, id);
            /* Remove packet from ACK waiting map_t */
            map_del(global.ack_waiting, &id);
            break;
        case ERR_UNKNOWN:
            DEBUG("*** %s", E_UNKNOWN);
            reply->type = NACK_REPLY;
            reply->data = E_UNKNOWN;
            break;
        case ERR_MISS_CHAN:
            DEBUG("*** %s", E_MISS_CHAN);
            reply->type = NACK_REPLY;
            reply->data = E_MISS_CHAN;
            break;
        case ERR_MISS_MEX:
            DEBUG("*** %s", E_MISS_MEX);
            reply->type = NACK_REPLY;
            reply->data = E_MISS_MEX;
            break;
        case QUIT:
            DEBUG("*** QUIT\n");
            shutdown(clientfd, 0);
            close(clientfd);
            break;
        default:
            DEBUG("*** %s", E_UNKNOWN);
            break;
    }

    if (reply->type != NO_REPLY)
        set_epollout(epollfd, clientfd, reply);
    else
        set_epollin(epollfd, clientfd);

    pthread_mutex_unlock(&global.lock);

    if (p->opcode == PUBLISH_MESSAGE) {
        if (p->type == SYSTEM_PACKET)
            free(p->payload.sys_pubpacket->data);
        else
            free(p->payload.cli_pubpacket->data);
        free(comm->cmd.a->channel_name);
        free(comm->cmd.a->message);
        free(comm->cmd.a);
    } else if (p->opcode == SUBSCRIBE_CHANNEL
            || p->opcode == UNSUBSCRIBE_CHANNEL
            || p->opcode == ACK) {
        /* free(p.payload.sub_packet.channel_name); */
        free(comm->cmd.b->channel_name);
        free(comm->cmd.b);
    } else {
        free(p->payload.data);
    }
    free(comm);
    free(p);

    return 0;
}


static void *worker(void *args) {
    struct socks *fds = (struct socks *) args;
    struct epoll_event *events = malloc(sizeof(*events) * MAX_EVENTS);

    if (events == NULL) {
        perror("malloc(3) failed");
        pthread_exit(NULL);
    }

    int events_cnt;
    while ((events_cnt = epoll_wait(fds->epollfd, events, MAX_EVENTS, -1)) > 0) {
        for (int i = 0; i < events_cnt; i++) {
            if ((events[i].events & EPOLLERR) ||
                    (events[i].events & EPOLLHUP) ||
                    (!(events[i].events & EPOLLIN) && !(events[i].events & EPOLLOUT))) {
                /* An error has occured on this fd, or the socket is not
                   ready for reading */
                perror ("epoll_wait(2)");
                close(events[i].data.fd);
                continue;
            }
            if (events[i].data.fd == global.run) {
                eventfd_t val;
                eventfd_read(global.run, &val);
                DEBUG("Exiting..\n");
                break;
            } else if (events[i].data.fd == fds->serversock) {
                if (accept_connection(fds->epollfd, events[i].data.fd) == -1) {
                    fprintf(stderr, "Error accepting new client: %s\n",
                            strerror(errno));
                }
                // Rearm descriptor server socket on EPOLLIN
                set_epollin(fds->epollfd, fds->serversock);
            } else if (events[i].events & EPOLLIN) {
                if (handle_request(fds->epollfd, events[i].data.fd) == -1) {
                    fprintf(stderr, "Error handling request: %s\n",
                            strerror(errno));
                    /* Clean up all channels if the client was subscribed */
                    map_iterate2(global.channels, close_socket, NULL);
                    close(events[i].data.fd);
                }
            } else if (events[i].events & EPOLLOUT) {

                reply_t *reply = (reply_t *) events[i].data.ptr;
                ssize_t sent;
                protocol_packet_t *pp = create_data_packet(ACK, (uint8_t *) OK);
                packed_t *p = pack(pp);

                if (reply->type == ACK_REPLY) {
                    if ((sent = sendall(reply->fd, p->data, &p->size)) < 0) {
                        perror("send(2): can't write on socket descriptor");
                        goto cleanup;
                    }
                } else if (reply->type == NACK_REPLY) {
                    pp->opcode = NACK;
                    pp->payload.data = (uint8_t *) reply->data;
                    free(p->data);
                    p = pack(pp);
                    if ((sent = sendall(reply->fd, p->data, &p->size)) < 0) {
                        perror("send(2): can't write on socket descriptor");
                        goto cleanup;
                    }
                } else if (reply->type == PING_REPLY) {
                    pp->opcode = PING;
                    pp->payload.data = (uint8_t *) reply->data;
                    free(p->data);
                    p = pack(pp);
                    if ((sent = sendall(reply->fd, p->data, &p->size)) < 0) {
                        perror("send(2): can't write on socket descriptor");
                        goto cleanup;
                    }
                } else if (reply->type == NO_REPLY) {
                    // Ignore
                } else {
                    // reply to original sender
                    if ((sent = sendall(reply->fd, p->data, &p->size)) < 0) {
                        perror("send(2): can't write on socket descriptor");
                        goto cleanup;
                    }
                    void *raw_subs = map_get(global.channels, reply->channel);
                    if (!raw_subs) {
                        channel_t *channel = create_channel(strdup(reply->channel));
                        map_put(global.channels, strdup(reply->channel), channel);
                    }
                    channel_t *chan = (channel_t *) map_get(global.channels, reply->channel);
                    publish_message(chan, reply->qos, strdup(reply->data));
                }
cleanup:
                free(p->data);
                free(p);
                // Rearm descriptor on EPOLLIN
                set_epollin(fds->epollfd, reply->fd);
                // Clean up reply
                if (reply->type == DATA_REPLY)
                    free_reply(reply);
                free(reply);
            }
        }
    }

    if (events_cnt == 0 && global.run == 0)
        perror("epoll_wait(2) error");

    free(events);

    return NULL;
}


/* static int destroy_queue_data(void *t1, void *t2) { */
/*     map_entry *kv = (map_entry *) t2; */
/*     if (kv) { */
/*         // free value field */
/*         if (kv->val) { */
/*             channel_t *c = (channel_t *) kv->val; */
/*             queue_item *item = c->messages->front; */
/*             while (item) { */
/*                 protocol_packet_t *p = (protocol_packet_t *) item->data; */
/*                 if (p->payload.sys_pubpacket->data) */
/*                     free(p->payload.sys_pubpacket->data); */
/*                 free(p); */
/*                 item = item->next; */
/*             } */
/*             #<{(| release_queue(c->messages); |)}># */
/*         } */
/*     } else return MAP_ERR; */
/*     return MAP_OK; */
/* } */


static int destroy_channels(void *t1, void *t2) {
    map_entry *kv = (map_entry *) t2;
    if (kv) {
        // free value field
        if (kv->val) {
            channel_t *c = (channel_t *) kv->val;
            destroy_channel(c);
        }
    } else return MAP_ERR;
    return MAP_OK;
}


/*
 * Main entry point for start listening on a socket and running an epoll event
 * loop his main responsibility is to pass incoming client connections
 * descriptor to workers thread.
 */
int start_server(void) {

    /* Initialize global server object */
    global.loglevel = DEBUG;
    global.run = eventfd(0, EFD_NONBLOCK);
    global.channels = map_create();
    global.ack_waiting = map_create();
    global.next_id = init_counter();  // counter to get message id, should be enclosed inside locks
    /* init_counter(global.next_id);  // counter to get message id, should be enclosed inside locks */
    pthread_mutex_init(&(global.lock), NULL);
    int epollfd;

    /* Initialize epollfd */
    if ((epollfd = epoll_create1(0)) == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    /* Initialize the socket */
    int fd = make_listen("127.0.0.1", "9090");

    /* Add eventfd to the loop */
    add_epollin(epollfd, global.run);

    /* Set socket in EPOLLIN flag mode, ready to read data */
    add_epollin(epollfd, fd);

    /* Worker thread pool */
    pthread_t workers[EPOLL_WORKERS];

    /* I/0 thread pool initialization, passing a the pair {epollfd, fd} sockets
       for each one. Every worker handle input from clients, accepting
       connections and sending out data when a socket is ready to write */
    struct socks fds = { epollfd, fd };

    for (int i = 0; i < EPOLL_WORKERS; ++i)
        pthread_create(&workers[i], NULL, worker, (void *) &fds);

    INFO("*** Sizigy v0.1.0\n");
    INFO("*** Starting server on 127.0.0.1:9090\n");

    /* Use main thread as a worker too */
    worker(&fds);

    free(global.next_id);

    /* for (int i = 0; i < EPOLL_WORKERS; ++i) */
    /*     pthread_join(&workers[i], NULL); */

    /* Free all resources allocated */
    /* map_iterate2(global.channels, destroy_queue_data, NULL); */
    map_iterate2(global.channels, destroy_channels, NULL);
    /* map_release(global.channels); */
    return 0;
}


int sendall(int sfd, uint8_t *buf, ssize_t *len) {
    int total = 0;
    ssize_t bytesleft = *len;
    int n;
    while (total < *len) {
        n = send(sfd, buf + total, bytesleft, MSG_NOSIGNAL);
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            else {
                perror("send(2): error sending data\n");
                break;
            }
        }
        total += n;
        bytesleft -= n;
    }
    *len = total;
    return n == -1 ? -1 : 0;
}


int recvall(int sfd, ringbuf_t *ringbuf) {
    int n = 0;
    int total = 0;
    uint8_t buf[BUFSIZE];
    for (;;) {
        if ((n = recv(sfd, buf, BUFSIZE - 1, 0)) < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                perror("recv(2): error reading data\n");
                return -1;
            }
        }
        if (n == 0) {
            return 0;
        }

        ringbuf_bulk_put(ringbuf, buf, n);

        total += n;
    }
    return total;
}

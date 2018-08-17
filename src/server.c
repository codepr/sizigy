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


static void free_reply(struct reply *reply) {
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

    printf("*** [%p] Client connection from %s\n", (void *) pthread_self(), ip_buff);

    // add to EPOLLIN client sock
    add_epollin(epollfd, clientsock);

    return 0;
}


static int send_data(void *arg1, int fd) {
    queue_item *item = (queue_item *) arg1;
    struct message *m = (struct message *) item->data;
    struct protocol_packet pp = create_sys_pubpacket(PUBLISH_MESSAGE, m->qos, 0, m->channel, m->payload, 0);
    pp.payload.sys_pubpacket.id = m->id;
    struct packed p = pack(pp);
    if (sendall(fd, p.data, &p.size) < 0) {
        perror("send(2): error sending\n");
        return -1;
    }
    free(pp.payload.sys_pubpacket.data);
    free(p.data);
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

    char readbuff[BUFSIZE];
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    ssize_t n;

    memset(readbuff, 0x00, BUFSIZE);

    pthread_mutex_lock(&(global.lock));

    if ((n = recvall(clientfd, readbuff)) < 0) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }

    readbuff[n] = '\0';

    if (getpeername(clientfd, (struct sockaddr *) &addr, &addrlen) < 0) {
        return -1;
    }

    char ip_buff[INET_ADDRSTRLEN + 1];
    if (inet_ntop(AF_INET, &addr.sin_addr, ip_buff, sizeof(ip_buff)) == NULL) {
        return -1;
    }

    /* Unpack incoming bytes */
    struct protocol_packet p = unpack(readbuff);

    /* Parse command according to the communication protocol */
    struct command comm = parse_protocol_command(p);
    /* free(p.data);   //FIXME release memory */
    struct reply *reply = malloc(sizeof(struct reply));
    reply->fd = clientfd;
    reply->data = OK;       // placeholder reply
    reply->qos = comm.qos;
    void *raw_chan = NULL; // placeholder for map_get

    switch(comm.opcode) {
        case CREATE_CHANNEL:
            printf("*** [%p] CREATE %s\n", (void *) pthread_self(), comm.cmd.b.channel_name);
            struct channel *channel = create_channel(comm.cmd.b.channel_name);
            map_put(global.channels, comm.cmd.b.channel_name, channel);
            reply->type = ACK_REPLY;
            break;
        case DELETE_CHANNEL:
            printf("*** [%p] DELETE %s\n", (void *) pthread_self(), comm.cmd.b.channel_name);
            raw_chan = map_get(global.channels, comm.cmd.b.channel_name);
            if (raw_chan) {
                struct channel *chan = (struct channel *) raw_chan;
                destroy_channel(chan);
            }
            map_del(global.channels, comm.cmd.b.channel_name);
            reply->type = ACK_REPLY;
            break;
        case SUBSCRIBE_CHANNEL:
            printf("*** [%p] SUBSCRIBE %s\n", (void *) pthread_self(), comm.cmd.b.channel_name);
            void *raw = map_get(global.channels, comm.cmd.b.channel_name);
            if (!raw) {
                struct channel *channel = create_channel(comm.cmd.b.channel_name);
                map_put(global.channels, comm.cmd.b.channel_name, channel);
            }
            struct channel *chan = (struct channel *) map_get(global.channels, comm.cmd.b.channel_name);
            struct subscriber *sub = malloc(sizeof(struct subscriber));
            sub->fd = clientfd;
            sub->name = "sub";
            sub->qos = comm.qos;
            add_subscriber(chan, sub);
            send_queue(chan->messages, clientfd, send_data);
            reply->type = ACK_REPLY;
            break;
        case UNSUBSCRIBE_CHANNEL:
            printf("*** [%p] UNSUBSCRIBE %s\n", (void *) pthread_self(), comm.cmd.b.channel_name);
            raw_chan = map_get(global.channels, comm.cmd.b.channel_name);
            if (raw_chan) {
                struct channel *chan = (struct channel *) raw_chan;
                // XXX basic placeholder subscriber
                struct subscriber sub = { clientfd, AT_MOST_ONCE, "sub" };
                del_subscriber(chan, &sub);
            }
            reply->type = ACK_REPLY;
            break;
        case PUBLISH_MESSAGE:
            reply->data = comm.cmd.a.message;
            reply->channel = comm.cmd.a.channel_name;
            reply->type = DATA_REPLY;
            break;
        case PING:
            reply->data = "PONG\n";
            reply->type = PING_REPLY;
            printf("*** [%p] PING from %s\n", (void *) pthread_self(), ip_buff);
            break;
        case ACK:
            reply->type = NO_REPLY;
            uint64_t id = 0;
            /* Extract uint64_t ID from the payload */
            while (*comm.cmd.b.channel_name != '\0') {
                id = (id * 10) + (*comm.cmd.b.channel_name - '0');
                comm.cmd.b.channel_name++;
            }
            printf("*** [%p] ACK from %s for packet id %ld\n", (void *) pthread_self(), ip_buff, id);
            /* Remove packet from ACK waiting map */
            map_del(global.ack_waiting, &id);
            break;
        case ERR_UNKNOWN:
            printf("*** [%p] %s", (void *) pthread_self(), E_UNKNOWN);
            reply->type = NACK_REPLY;
            reply->data = E_UNKNOWN;
            break;
        case ERR_MISS_CHAN:
            printf("*** [%p] %s", (void *) pthread_self(), E_MISS_CHAN);
            reply->type = NACK_REPLY;
            reply->data = E_MISS_CHAN;
            break;
        case ERR_MISS_MEX:
            printf("*** [%p] %s", (void *) pthread_self(), E_MISS_MEX);
            reply->type = NACK_REPLY;
            reply->data = E_MISS_MEX;
            break;
        case QUIT:
            printf("*** [%p] QUIT\n", (void *) pthread_self());
            shutdown(clientfd, 0);
            close(clientfd);
            break;
        default:
            printf("*** [%p] %s", (void *) pthread_self(), E_UNKNOWN);
            break;
    }

    set_epollout(epollfd, clientfd, reply);

    pthread_mutex_unlock(&global.lock);

    if (p.opcode == PUBLISH_MESSAGE) {
        if (p.type == SYSTEM_PACKET)
            free(p.payload.sys_pubpacket.data);
        else
            free(p.payload.cli_pubpacket.data);
    } else if (p.opcode == SUBSCRIBE_CHANNEL
            || p.opcode == UNSUBSCRIBE_CHANNEL
            || p.opcode == ACK) {
        free(p.payload.sub_packet.channel_name);
    } else {
        free(p.payload.data);
    }

    return 0;
}


static void *worker(void *args) {
    struct socks *fds = (struct socks *) args;
    struct epoll_event *events = malloc(sizeof(*events) * MAX_EVENTS);

    if (events == NULL) {
        perror("malloc(3) failed when attempting to allocate events buffer");
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
            if (events[i].data.fd == fds->serversock) {
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
                struct reply *reply = (struct reply *) events[i].data.ptr;
                ssize_t sent;
                struct protocol_packet pp = create_data_packet(ACK, OK);
                struct packed p = pack(pp);
                if (reply->type == ACK_REPLY) {
                    if ((sent = sendall(reply->fd, p.data, &p.size)) < 0) {
                        perror("send(2): can't write on socket descriptor");
                        return NULL;
                    }
                } else if (reply->type == NACK_REPLY) {
                    pp.opcode = NACK;
                    pp.payload.data = reply->data;
                    free(p.data);
                    p = pack(pp);
                    if ((sent = sendall(reply->fd, p.data, &p.size)) < 0) {
                        perror("send(2): can't write on socket descriptor");
                        return NULL;
                    }
                } else if (reply->type == PING_REPLY) {
                    pp.opcode = PING;
                    pp.payload.data = reply->data;
                    free(p.data);
                    p = pack(pp);
                    if ((sent = sendall(reply->fd, p.data, &p.size)) < 0) {
                        perror("send(2): can't write on socket descriptor");
                        return NULL;
                    }
                } else if (reply->type == NO_REPLY) {
                    // Ignore
                } else {
                    // Reply to original sender
                    if ((sent = sendall(reply->fd, p.data, &p.size)) < 0) {
                            perror("send(2): can't write on socket descriptor");
                            return NULL;
                    }
                    void *raw_subs = map_get(global.channels, reply->channel);
                    if (!raw_subs) {
                        struct channel *channel = create_channel(reply->channel);
                        map_put(global.channels, reply->channel, channel);
                    }
                    struct channel *chan = (struct channel *) map_get(global.channels, reply->channel);
                    publish_message(chan, reply->qos, strdup(reply->data));
                }
                free(p.data);
                // Rearm descriptor on EPOLLIN
                set_epollin(fds->epollfd, reply->fd);
                // Clean up reply
                if (reply->type == DATA_REPLY)
                    free_reply(reply);
                free(reply);
            }
        }
    }

    if (events_cnt == 0) {
        fprintf(stderr, "epoll_wait(2) returned 0, but timeout was not specified...?");
    } else {
        perror("epoll_wait(2) error");
    }

    free(events);

    return NULL;
}


/*
 * Main entry point for start listening on a socket and running an epoll event
 * loop his main responsibility is to pass incoming client connections
 * descriptor to workers thread.
 */
int start_server(void) {

    /* Initialize global server object */
    global.channels = map_create();
    global.ack_waiting = map_create();
    init_counter(&global.next_id);  // counter to get message id, should be enclosed inside locks
    pthread_mutex_init(&(global.lock), NULL);
    int epollfd;

    /* Initialize epollfd */
    if ((epollfd = epoll_create1(0)) == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    /* Initialize the socket */
    int fd = make_listen("127.0.0.1", "9090");

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

    printf("*** [%p] Sizigy v0.1.0\n", (void *) pthread_self());
    printf("*** [%p] Starting server on 127.0.0.1:9090\n", (void *) pthread_self());

    /* Use main thread as a worker too */
    worker(&fds);
    return 0;
}


int sendall(int sfd, char *buf, ssize_t *len) {
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
                return -1;
            }
        }
        total += n;
        bytesleft -= n;
    }
    *len = total;
    return n == -1 ? -1 : 0;
}


int recvall(int sfd, char *buf) {
    int n = 0;
    int total = 0;
    for (;;) {
        if ((n = recv(sfd, buf+total, sizeof(buf) - 1, 0)) < 0) {
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
        total += n;
    }
    return total;
}

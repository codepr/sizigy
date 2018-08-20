#include <time.h>
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
#include "network.h"
#include "protocol.h"


struct global global;


static void free_reply(reply_t *reply) {
    if (reply->data)
        free(reply->data);
    if (reply->channel)
        free(reply->channel);
}


static int send_data(void *arg1, void *ptr) {
    struct subscriber *sub = (struct subscriber *) ptr;
    queue_item *item = (queue_item *) arg1;
    protocol_packet_t *pp = (protocol_packet_t *) item->data;
    if (sub->qos > 0)
        pp->payload.sys_pubpacket->qos = 1;
    packed_t *p = pack(pp);
    if (sendall(sub->fd, p->data, &p->size) < 0) {
        perror("send(2): error sending\n");
        return -1;
    }
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
    time_t start = time(NULL);
    while (read_all == -1) {
        if ((n = recvall(clientfd, rbuf)) < 0) {
            return -1;
        }
        if (n == 0) {
            return 0;
        }

        /* #<{(| Drain ringbuffer if it contains more than one complete packet |)}># */
        /* while (read_all != -1 || ringbuf_size(rbuf) > 0) { */
        /* Unpack incoming bytes */
        read_all = unpack(rbuf, p);

        if (time(NULL) - start > TIMEOUT)
            read_all = 1;
    }

    if (read_all == 1)
        return -1;

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

    reply->type = ACK_REPLY;
    reply->fd = clientfd;
    reply->data = OK;       // placeholder reply
    reply->qos = comm->qos;
    void *raw_chan = NULL;  // placeholder for map_get

    switch (comm->opcode) {
        case CREATE_CHANNEL:
            DEBUG("*** CREATE %s\n", comm->cmd.b->channel_name);
            channel_t *channel = create_channel(comm->cmd.b->channel_name);
            map_put(global.channels, comm->cmd.b->channel_name, channel);
            break;
        case DELETE_CHANNEL:
            DEBUG("*** DELETE %s\n", comm->cmd.b->channel_name);
            raw_chan = map_get(global.channels, comm->cmd.b->channel_name);
            if (raw_chan) {
                channel_t *chan = (channel_t *) raw_chan;
                destroy_channel(chan);
            }
            map_del(global.channels, comm->cmd.b->channel_name);
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
            sub->offset = comm->cmd.b->offset;
            add_subscriber(chan, sub);
            send_queue(chan->messages, sub, send_data);
            break;
        case UNSUBSCRIBE_CHANNEL:
            DEBUG("*** UNSUBSCRIBE %s\n", comm->cmd.b->channel_name);
            raw_chan = map_get(global.channels, comm->cmd.b->channel_name);
            if (raw_chan) {
                channel_t *chan = (channel_t *) raw_chan;
                // XXX basic placeholder subscriber
                struct subscriber sub = { clientfd, AT_MOST_ONCE, 0, "sub" };
                del_subscriber(chan, &sub);
            }
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
            reply->type = NACK_REPLY;
            reply->data = E_UNKNOWN;
            DEBUG("*** %s", E_UNKNOWN);
            break;
    }

    if (reply->type != NO_REPLY)
        set_epollout(epollfd, clientfd, reply);
    else {
        set_epollin(epollfd, clientfd);
        free(reply);
    }

    pthread_mutex_unlock(&global.lock);

    if (p->opcode == PUBLISH_MESSAGE) {
        if (p->type == SYSTEM_PACKET) {
            free(p->payload.sys_pubpacket->data);
            free(p->payload.sys_pubpacket);
        }
        else {
            free(p->payload.cli_pubpacket->data);
            free(p->payload.cli_pubpacket);
        }
        free(comm->cmd.a->channel_name);
        free(comm->cmd.a->message);
        free(comm->cmd.a);
    } else if (p->opcode == SUBSCRIBE_CHANNEL
            || p->opcode == UNSUBSCRIBE_CHANNEL
            || p->opcode == ACK
            || p->opcode == DATA) {
        /* free(p.payload.sub_packet.channel_name); */
        free(comm->cmd.b->channel_name);
        free(comm->cmd.b);
    } else {
        free(p->payload.data);
    }
    free(comm);
    free(p);
    ringbuf_free(rbuf);

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
                DEBUG("Stopping epoll loop. Exiting.\n\n");
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
                free(pp);
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


static int destroy_queue_data(void *t1, void *t2) {
    map_entry *kv = (map_entry *) t2;
    if (kv) {
        // free value field
        if (kv->val) {
            channel_t *c = (channel_t *) kv->val;
            queue_item *item = c->messages->front;
            while (item) {
                protocol_packet_t *p = (protocol_packet_t *) item->data;
                if (p->payload.sys_pubpacket->data)
                    free(p->payload.sys_pubpacket->data);
                free(p->payload.sys_pubpacket);
                free(p);
                item = item->next;
            }
        }
    } else return MAP_ERR;
    return MAP_OK;
}


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
    map_iterate2(global.channels, destroy_queue_data, NULL);
    map_iterate2(global.channels, destroy_channels, NULL);
    /* map_release(global.channels); */
    return 0;
}

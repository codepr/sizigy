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

static int reply_handler(const int, client_t *);
static int request_handler(const int, client_t *);


static int send_data(void *arg1, void *ptr) {
    struct subscriber *sub = (struct subscriber *) ptr;
    queue_item *item = (queue_item *) arg1;
    message_t *m = (message_t *) item->data;
    char *channel = append_string(m->channel, " ");
    protocol_packet_t *pp = create_sys_pubpacket(PUBLISH_MESSAGE,
            m->qos, m->redelivered, channel, m->payload, 0);
    if (sub->qos > 0)
        pp->payload.sys_pubpacket->qos = 1;
    pp->payload.sys_pubpacket->id = m->id;
    packed_t *p = pack(pp);
    if (sendall(sub->fd, p->data, p->size, &(ssize_t) { 0 }) < 0) {
        perror("send(2): error sending\n");
        return -1;
    }
    free(channel);
    free(p->data);
    free(p);
    free(pp->payload.sys_pubpacket->data);
    free(pp->payload.sys_pubpacket);
    free(pp);
    return 0;
}


static void add_reply(client_t *c, uint8_t type, uint8_t qos, const int fd, char *data, char *channel) {
    reply_t *r = malloc(sizeof(reply_t));
    if (!r) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    r->type = type;
    r->qos = qos;
    r->fd = fd;
    r->data = data;
    r->channel = channel;
    c->reply = r;
}


static int err_handler(client_t *c, const uint8_t errcode) {
    if (!c->reply) {
        c->reply = malloc(sizeof(reply_t));
    }
    c->reply->type = NACK_REPLY;
    if (errcode == ERR_UNKNOWN) {
        DEBUG("%s", E_UNKNOWN);
        c->reply->data = E_UNKNOWN;
    }
    else if (errcode == ERR_MISS_CHAN) {
        DEBUG("%s", E_MISS_CHAN);
        c->reply->data = E_MISS_CHAN;
    }
    else if (errcode == ERR_MISS_MEX) {
        DEBUG("%s", E_MISS_MEX);
        c->reply->data = E_MISS_MEX;
    }
    else if (errcode == ERR_MISS_ID) {
        DEBUG("%s", E_MISS_ID);
        c->reply->data = E_MISS_ID;
    }
    else {
        DEBUG("%s", E_UNKNOWN);
        c->reply->data = E_UNKNOWN;
    }
    return -1;
}


static int ack_handler(client_t *c, command_t *cmd) {
    c->reply = NULL;
    uint64_t id = 0;
    /* Extract uint64_t ID from the payload */
    char *id_str = cmd->cmd.b->channel_name;
    while (*id_str != '\0') {
        id = (id * 10) + (*id_str - '0');
        id_str++;
    }
    DEBUG("ACK from %s (id=%ld)", c->id, id);
    /* Remove packet from ACK waiting map_t */
    map_del(global.ack_waiting, &id);

    return -1;
}


static int quit_handler(client_t *c, command_t *cmd) {
    DEBUG("QUIT");
    shutdown(c->fd, 0);
    close(c->fd);
    return -1;
}


static int ping_handler(client_t *c, command_t *cmd) {
    add_reply(c, PING_REPLY, cmd->qos, c->fd, "PONG", NULL);
    DEBUG("PING from %s", c->id);
    return 0;
}


static int join_handler(client_t *c, command_t *cmd) {
    add_reply(c, JACK_REPLY, cmd->qos, c->fd, OK, NULL);
    // XXX for now just insert pointer to client struct
    global.peers = list_head_insert(global.peers, c);
    DEBUG("JOIN request accepted");
    return 0;
}


static int join_ack_handler(client_t *c, command_t *cmd) {
    add_reply(c, ACK_REPLY, cmd->qos, c->fd, OK, NULL);
    // XXX for now just insert pointer to client struct
    global.peers = list_head_insert(global.peers, c);
    DEBUG("JOINED cluster");
    return 0;
}


static int handshake_handler(client_t *c, command_t *cmd) {
    add_reply(c, ACK_REPLY, cmd->qos, c->fd, OK, NULL);
    char *id = append_string("C:", cmd->cmd.h->id);
    if (cmd->cmd.h->clean_session == 1) {
        map_del(global.clients, c->id);
        map_put(global.clients, id, c);
        free(c->id);
        c->id = id;
    } else {
        /* Should check if global map contains an entry and return an
           error code in case of failure, for now we just add the new
           client */
        map_put(global.clients, id, c);
    }

    return 0;
}


static int create_channel_handler(client_t *c, command_t *cmd) {
    add_reply(c, ACK_REPLY, cmd->qos, c->fd, OK, NULL);
    DEBUG("CREATE %s", cmd->cmd.b->channel_name);
    channel_t *channel = create_channel(cmd->cmd.b->channel_name);
    map_put(global.channels, cmd->cmd.b->channel_name, channel);

    return 0;
}


static int delete_channel_handler(client_t *c, command_t *cmd) {
    add_reply(c, ACK_REPLY, cmd->qos, c->fd, OK, NULL);
    DEBUG("DELETE %s", cmd->cmd.b->channel_name);
    void *raw_chan = map_get(global.channels, cmd->cmd.b->channel_name);
    if (raw_chan) {
        channel_t *chan = (channel_t *) raw_chan;
        destroy_channel(chan);
    }
    map_del(global.channels, cmd->cmd.b->channel_name);

    return 0;
}


static int replica_handler(client_t *c, command_t *cmd) {
    add_reply(c, ACK_REPLY, cmd->qos, c->fd, OK, NULL);
    void *raw = map_get(global.channels, cmd->cmd.a->channel_name);
    if (!raw) {
        channel_t *channel = create_channel(cmd->cmd.a->channel_name);
        map_put(global.channels, strdup(cmd->cmd.a->channel_name), channel);
    }
    channel_t *chan = (channel_t *) map_get(global.channels, cmd->cmd.a->channel_name);
    /* Add message to the channel */
    // XXX require new command packet for replica (e.g. save ID etc)
    add_message(chan, 0, cmd->qos, cmd->cmd.a->redelivered, cmd->cmd.a->message, 0);
    DEBUG("REPLICA received");
    return 1;
}


static int publish_message_handler(client_t *c, command_t *cmd) {
    add_reply(c, DATA_REPLY, cmd->qos, c->fd,
            strdup(cmd->cmd.a->message), strdup(cmd->cmd.a->channel_name));
    return 1;
}


static int subscribe_channel_handler(client_t *c, command_t *cmd) {
    add_reply(c, ACK_REPLY, cmd->qos, c->fd, OK, NULL);
    DEBUG("SUBSCRIBE %s", cmd->cmd.b->channel_name);
    void *raw = map_get(global.channels, cmd->cmd.b->channel_name);
    if (!raw) {
        channel_t *channel = create_channel(cmd->cmd.b->channel_name);
        map_put(global.channels, strdup(cmd->cmd.b->channel_name), channel);
    }
    channel_t *chan = (channel_t *) map_get(global.channels, cmd->cmd.b->channel_name);
    struct subscriber *sub = malloc(sizeof(struct subscriber));
    if (!sub) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    sub->fd = c->fd;
    sub->name = c->id;
    sub->qos = cmd->qos;
    sub->offset = cmd->cmd.b->offset;
    add_subscriber(chan, sub);
    send_queue(chan->messages, sub, send_data);

    list_head_insert(c->subscriptions, chan->name);

    return 0;
}


static int unsubscribe_channel_handler(client_t *c, command_t *cmd) {
    add_reply(c, ACK_REPLY, cmd->qos, c->fd, OK, NULL);
    DEBUG("UNSUBSCRIBE %s", cmd->cmd.b->channel_name);
    void *raw_chan = map_get(global.channels, cmd->cmd.b->channel_name);
    if (raw_chan) {
        channel_t *chan = (channel_t *) raw_chan;
        // XXX basic placeholder subscriber
        struct subscriber sub = { c->fd, AT_MOST_ONCE, 0, c->id };
        del_subscriber(chan, &sub);
    }

    // TODO remove subscriptions from client

    return 0;
}


static struct command commands[] = {
    {ACK, ack_handler},
    {QUIT, quit_handler},
    {PING, ping_handler},
    {JOIN, join_handler},
    {JOIN_ACK, join_ack_handler},
    {REPLICA, replica_handler},
    {HANDSHAKE, handshake_handler},
    {CREATE_CHANNEL, create_channel_handler},
    {DELETE_CHANNEL, delete_channel_handler},
    {PUBLISH_MESSAGE, publish_message_handler},
    {SUBSCRIBE_CHANNEL, subscribe_channel_handler},
    {UNSUBSCRIBE_CHANNEL, unsubscribe_channel_handler}
};


int commands_len(void) {
    return sizeof(commands) / sizeof(struct command);
}


static void free_reply(reply_t *reply) {
    if (reply->data)
        free(reply->data);
    if (reply->channel)
        free(reply->channel);
}


/* static int close_socket(void *arg1, void *arg2) { */
/*     int fd = *(int *) arg1; */
/*     map_entry *kv = (map_entry *) arg2; */
/*     struct subscriber *sub = (struct subscriber *) kv->val; */
/*     if (sub->fd == fd) */
/*         close(sub->fd); */
/*     return 0; */
/* } */


static int request_handler(const int epollfd, client_t *client) {
    const int clientfd = client->fd;

    /* Buffer to initialize the ring buffer, used to handle input from client */
    uint8_t buffer[ONEMB * 2];

    /* Ringbuffer pointer struct, helpful to handle different and unknown
       size of chunks of data which can result in partially formed packets or
       overlapping as well */
    ringbuf_t *rbuf = ringbuf_init(buffer, ONEMB * 2);

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    if (getpeername(clientfd, (struct sockaddr *) &addr, &addrlen) < 0) {
        return -1;
    }

    char ip_buff[INET_ADDRSTRLEN + 1];
    if (inet_ntop(AF_INET, &addr.sin_addr, ip_buff, sizeof(ip_buff)) == NULL) {
        return -1;
    }

    /* Read all data to form a packet flag */
    int read_all = -1;
    ssize_t n;
    protocol_packet_t *p = malloc(sizeof(protocol_packet_t));

    /* We must read all incoming bytes till an entire packet is received. This
       is achieved by using a standardized protocol, which send the size of the
       complete packet as the first 4 bytes. By knowing it we know if the packet is
       ready to be deserialized and used.*/
    time_t start = time(NULL);
    while (read_all != 0) {
        /* Read till EAGAIN or EWOULDBLOCK, passing an optional parameter len
           which define the remaining bytes to be read */
        if ((n = recvall(clientfd, rbuf, read_all)) < 0) {
            free(p);
            return -1;
        }
        if (n == 0) {
            free(p);
            return 0;
        }

        char bytes[ringbuf_size(rbuf)];
        /* Check the header, returning -1 in case of insufficient informations
           about the total packet length and the subsequent payload bytes */
        read_all = parse_header(rbuf, bytes);

        if (read_all == 0)
            read_all = unpack((uint8_t *) bytes, p);

        if ((time(NULL) - start) > TIMEOUT)
            read_all = 1;
    }

    /* Free ring buffer as we alredy have all needed informations in memory */
    ringbuf_free(rbuf);

    if (read_all == 1) {
        free(p);
        return -1;
    }

    pthread_mutex_lock(&(global.lock));

    /* Parse command according to the communication protocol */
    command_t *comm = parse_command(p);

    pthread_mutex_unlock(&(global.lock));


    int free_reply = -1;
    int executed = 0;

    // Loop through commands array to find the correct handler
    for (int i = 0; i < commands_len(); i++) {
        if (commands[i].ctype == comm->opcode) {
            free_reply = commands[i].handler(client, comm);
            executed = 1;
        }
    }
    // If no handler is found, it must be an error case
    if (executed == 0) {
        free_reply = err_handler(client, comm->opcode);
    }

    pthread_mutex_lock(&(global.lock));

    // Set reply handler as the current context handler
    client->ctx_handler = reply_handler;

    // Clean up garbage
    if (comm->opcode != ACK) {
        mod_epoll(epollfd, clientfd, EPOLLOUT, client);
    } else {
        client->ctx_handler = request_handler;
        mod_epoll(epollfd, clientfd, EPOLLIN, client);
        if (free_reply > -1 && client->reply) {
            free(client->reply);
            client->reply = NULL;
        }
    }

    pthread_mutex_unlock(&(global.lock));

    if (p->opcode == PUBLISH_MESSAGE || p->opcode == REPLICA) {
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
            || p->opcode == JOIN
            || p->opcode == JOIN_ACK
            || p->opcode == DATA) {
        free(comm->cmd.b->channel_name);
        free(comm->cmd.b);
    } else if (p->opcode == HANDSHAKE) {
        free(comm->cmd.h->id);
        free(comm->cmd.h);
    } else {
        free(p->payload.data);
    }
    free(comm);
    free(p);

    return 0;
}


static int reply_handler(const int epollfd, client_t *client) {
    reply_t *reply = client->reply;
    int ret = 0;
    ssize_t sent;
    protocol_packet_t *pp = create_data_packet(ACK, (uint8_t *) OK);
    packed_t *p = pack(pp);

    if (reply->type == ACK_REPLY) {
        if ((sent = sendall(reply->fd, p->data, p->size, &(ssize_t) { 0 })) < 0) {
            perror("send(2): can't write on socket descriptor");
            ret = -1;
        }
    } else if (reply->type == JACK_REPLY) {
        pp->opcode = JOIN_ACK;
        pp->payload.data = (uint8_t *) reply->data;
        free(p->data);
        free(p);
        p = pack(pp);
        if ((sent = sendall(reply->fd, p->data, p->size, &(ssize_t) { 0 })) < 0) {
            perror("send(2): can't write on socket descriptor");
            ret = -1;
        }
    } else if (reply->type == NACK_REPLY) {
        pp->opcode = NACK;
        pp->payload.data = (uint8_t *) reply->data;
        free(p->data);
        free(p);
        p = pack(pp);
        if ((sent = sendall(reply->fd, p->data, p->size, &(ssize_t) { 0 })) < 0) {
            perror("send(2): can't write on socket descriptor");
            ret = -1;
        }
    } else if (reply->type == PING_REPLY) {
        pp->opcode = PING;
        pp->payload.data = (uint8_t *) reply->data;
        free(p->data);
        free(p);
        p = pack(pp);
        if ((sent = sendall(reply->fd, p->data, p->size, &(ssize_t) { 0 })) < 0) {
            perror("send(2): can't write on socket descriptor");
            ret = -1;
        }
    } else if (reply->type == NO_REPLY) {
        // Ignore
    } else {
        // reply to original sender
        if ((sent = sendall(reply->fd, p->data, p->size, &(ssize_t) { 0 })) < 0) {
            perror("send(2): can't write on socket descriptor");
            ret = -1;
        }
        void *raw_subs = map_get(global.channels, reply->channel);
        if (!raw_subs) {
            channel_t *channel = create_channel(reply->channel);
            map_put(global.channels, strdup(reply->channel), channel);
        }
        channel_t *chan = (channel_t *) map_get(global.channels, reply->channel);
        double tic = clock();
        sent = publish_message(chan, reply->qos, strdup(reply->data), 0);
        double elapsed = (clock() - tic) /CLOCKS_PER_SEC;
        int load = (sent / elapsed);
        set_value(global.throughput, load);
    }

    free(p->data);
    free(p);
    free(pp);

    if (reply->type == DATA_REPLY)
        free_reply(reply);
    free(reply);

    client->reply = NULL;
    client->ctx_handler = request_handler;
    mod_epoll(epollfd, client->fd, EPOLLIN, client);
    return ret;
}


static int accept_handler(const int epollfd, client_t *server) {
    const int fd = server->fd;
    /* Accept the connection */
    int clientsock = accept_connection(fd);
    /* Abort if not accepted */
    if (clientsock == -1)
        return -1;
    /* Create a server structure to handle his context connection */
    client_t *client = malloc(sizeof(client_t));
    client->status = ONLINE;
    client->fd = clientsock;
    client->ctx_handler = request_handler;
    const char *id = random_name(16);
    char *name = append_string("C:", id);  // C states that it is a client (could be another sizigy instance)
    free((void *) id);
    client->id = name;
    client->reply = NULL;
    client->subscriptions = list_create();
    /* Add new accepted server to the global map */
    map_put(global.clients, name, client);
    /* Add it to the epoll loop */
    add_epoll(epollfd, clientsock, client);
    /* Rearm server fd to accept new connections */
    mod_epoll(epollfd, fd, EPOLLIN, server);
    return 0;
}


static void *worker(void *args) {
    struct socks *fds = (struct socks *) args;
    struct epoll_event *events = malloc(sizeof(*events) * MAX_EVENTS);

    if (!events) {
        perror("malloc(3) failed");
        pthread_exit(NULL);
    }

    int events_cnt;
    while ((events_cnt = epoll_wait(fds->epollfd, events, MAX_EVENTS, -1)) > 0) {
        for (int i = 0; i < events_cnt; i++) {
            /* Check for errors first */
            if ((events[i].events & EPOLLERR) ||
                    (events[i].events & EPOLLHUP) ||
                    (!(events[i].events & EPOLLIN) && !(events[i].events & EPOLLOUT))) {
                /* An error has occured on this fd, or the socket is not
                   ready for reading */
                perror ("epoll_wait(2)");
                close(events[i].data.fd);
                continue;
            } else if (events[i].data.fd == global.run) {
                /* And quit event after that */
                eventfd_t val;
                eventfd_read(global.run, &val);
                DEBUG("Stopping epoll loop. Thread %p exiting.", (void *) pthread_self());
                goto exit;
            } else {
                /* Finally handle the request according to its type */
                ((client_t *) events[i].data.ptr)->ctx_handler(fds->epollfd, events[i].data.ptr);
            }
        }
    }

exit:
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
                message_t *m = (message_t *) item->data;
                if (m->payload)
                    free(m->payload);
                free(m);
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


static int destroy_clients(void *t1, void *t2) {
    map_entry *kv = (map_entry *) t2;
    if (kv) {
        if (kv->val) {
            client_t *c = (client_t *) kv->val;
            if (c->id)
                free(c->id);
            if (c->reply)
                free(c->reply);
            if (c->subscriptions)
                free(c->subscriptions);
        }
    } else return MAP_ERR;
    return MAP_OK;
}


int parse_header(ringbuf_t *rbuf, char *bytearray) {
    /* Check the size of the ring buffer, we need at least the first 4 bytes in
       order to get the total length of the packet */
    if (ringbuf_empty(rbuf) || ringbuf_size(rbuf) < sizeof(uint32_t))
        return -1;

    uint8_t *tmp = (uint8_t *) bytearray;

    /* Try to read at least length of the packet */
    for (uint8_t i = 0; i < sizeof(uint32_t); i++)
        ringbuf_pop(rbuf, tmp++);

    uint8_t *tot = (uint8_t *) bytearray;
    uint32_t tlen = *((uint32_t *) tot);

    /* If there's no bytes nr equal to the total size of the packet abort and
       read again */
    if (ringbuf_size(rbuf) < tlen - sizeof(uint32_t))
        return tlen - sizeof(uint32_t) - ringbuf_size(rbuf);

    /* Empty the rest of the ring buffer */
    while ((tlen - sizeof(uint32_t)) > 0) {
        ringbuf_pop(rbuf, tmp++);
        --tlen;
    }

    return 0;
}



/*
 * Main entry point for start listening on a socket and running an epoll event
 * loop his main responsibility is to pass incoming client connections
 * descriptor to workers thread.
 */
int start_server(const char *addr, char *port, int node_fd) {

    /* Initialize global server object */
    global.loglevel = DEBUG;
    global.run = eventfd(0, EFD_NONBLOCK);
    global.channels = map_create();
    global.ack_waiting = map_create();
    global.clients = map_create();
    global.peers = list_create();
    global.next_id = init_counter();  // counter to get message id, should be enclosed inside locks
    global.throughput = init_atomic();
    global.throttler = init_throttler();
    pthread_mutex_init(&(global.lock), NULL);

    /* Initialize epollfd for server component */
    const int epollfd = epoll_create1(0);

    if (epollfd == -1) {
        perror("epoll_create1");
        goto cleanup;
    }

    /* Another one to handle bus events */
    const int bepollfd = epoll_create1(0);

    if (bepollfd == -1) {
        perror("epoll_create1");
        goto cleanup;
    }

    /* Initialize the sockets, first the server one */
    const int fd = make_listen(addr, port);

    char bus_port[6];

    bus_port[0] = '1';
    strncpy(bus_port+1, port, sizeof(bus_port) - 2);
    bus_port[5] = '\0';

    /* The bus one for distribution */
    const int bfd = make_listen(addr, bus_port);

    /* Add eventfd to the loop, this time only in LT in order to wake up all threads */
    struct epoll_event ev;
    ev.data.fd = global.run;
    ev.events = EPOLLIN;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, global.run, &ev) < 0) {
        perror("epoll_ctl(2): add epollin");
    }

    if (epoll_ctl(bepollfd, EPOLL_CTL_ADD, global.run, &ev) < 0) {
        perror("epoll_ctl(2): add epollin");
    }

    /* Client structure for the server component */
    client_t server = { ONLINE, fd, accept_handler, "server", NULL, list_create() };

    /* And another one for the bus */
    client_t bus = { ONLINE, bfd, accept_handler, "bus", NULL, list_create() };

    /* Set socket in EPOLLIN flag mode, ready to read data */
    add_epoll(epollfd, fd, &server);

    /* Set bus socket in EPOLLIN too */
    add_epoll(bepollfd, bfd, &bus);

    /* Bus dedicated thread */
    pthread_t bus_worker;
    struct socks bus_fds = { bepollfd, bfd };
    pthread_create(&bus_worker, NULL, worker, (void *) &bus_fds);

    /* Worker thread pool */
    pthread_t workers[EPOLL_WORKERS];

    /* I/0 thread pool initialization, passing a the pair {epollfd, fd} sockets
       for each one. Every worker handle input from clients, accepting
       connections and sending out data when a socket is ready to write */
    struct socks fds = { epollfd, fd };

    for (int i = 0; i < EPOLL_WORKERS; ++i)
        pthread_create(&workers[i], NULL, worker, (void *) &fds);

    INFO("Sizigy v0.1.0");
    INFO("Starting server on %s:%s", addr, port);

    client_t node = { ONLINE, node_fd, request_handler, "node", NULL, list_create() };
    /* Add eventual connected node */
    if (node_fd > 0) {
        add_epoll(bepollfd, node_fd, &node);
        /* Ask for joining the cluster */
        protocol_packet_t *join_req_packet = create_data_packet(JOIN, (uint8_t *) OK);
        packed_t *p = pack(join_req_packet);
        int rc = sendall(node_fd, p->data, p->size, &(ssize_t) { 0 });
        if (rc < 0)
            printf("Failed join\n");
        free(join_req_packet);
        free(p);
    }

    /* Use main thread as a worker too */
    worker(&fds);

    for (int i = 0; i < EPOLL_WORKERS; ++i)
        pthread_join(workers[i], NULL);

    pthread_join(bus_worker, NULL);

cleanup:
    /* Free all resources allocated */
    map_iterate2(global.channels, destroy_queue_data, NULL);
    map_iterate2(global.channels, destroy_channels, NULL);
    map_iterate2(global.clients, destroy_clients, NULL);
    free(server.subscriptions);
    free(bus.subscriptions);
    free(node.subscriptions);
    free(global.peers);
    free(global.next_id);
    free(global.throughput);
    free(global.throttler);
    pthread_mutex_destroy(&(global.lock));
    /* map_release(global.channels); */
    DEBUG("Bye\n");
    return 0;
}

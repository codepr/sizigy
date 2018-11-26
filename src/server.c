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
#include "network.h"


#define reply_connack(c, rc) \
    (set_ack_reply((c), CONNACK, (rc)));

#define reply_suback(c, rc) \
    (set_ack_reply((c), SUBACK, (rc)));

#define reply_unsuback(c, rc) \
    (set_ack_reply((c), UNSUBACK, (rc)));

#define reply_puback(c, rc) \
    (set_ack_reply((c), PUBACK, (rc)));

#define reply_pingresp(c, rc) \
    (set_ack_reply((c), PINGRESP, (rc)));

// Reference to the config structure, could be refactored lately to a more
// structured configuration
struct config config;


static int reply_handler(SizigyDB *, Client *);
static int request_handler(SizigyDB *, Client *);
static int accept_handler(SizigyDB *, Client *);
static int close_socket(void *, void *);
static int connect_handler(SizigyDB *, Client *);
static int disconnect_handler(SizigyDB *, Client *);
static int publish_handler(SizigyDB *, Client *);
static int pingreq_handler(SizigyDB *, Client *);
static int subscribe_handler(SizigyDB *, Client *);
static int unsubscribe_handler(SizigyDB *, Client *);
static int destroy_clients(void *, void *);
static int destroy_topics(void *, void *);
static int destroy_queue_data(void *, void *);
static void *keepalive(void *);
static void *worker(void *);
static void set_reply(Client *, const uint8_t, Buffer *);
static void set_ack_reply(Client *, const uint8_t, const uint8_t);

// Fixed size of the header of each packet, consists of essentially the first
// 5 bytes containing respectively the type of packet (REQUEST OR RESPONSE)
// and the total length in bytes of the packet
static const int HEADLEN = sizeof(uint8_t) + sizeof(uint32_t);

/* Parse header, require at least the first 5 bytes in order to read packet
   type and total length that we need to recv to complete the packet */
Buffer *recv_packet(const int clientfd, Ringbuffer *rbuf, uint8_t *opcode) {

    size_t n = 0;
    uint8_t read_all = 0;

    while (n < HEADLEN) {
        /* Read first 5 bytes to get the total len of the packet */
        n += recvbytes(clientfd, rbuf, read_all, HEADLEN);
        if (n < 0) {
            shutdown(clientfd, 0);
            close(clientfd);
            // TODO: remove client from config
            return NULL;
        } else if (n == 0) {
            return NULL;
        }
    }

    uint8_t tmp[ringbuf_size(rbuf)];
    uint8_t *bytearray = tmp;

    /* Try to read at least length of the packet */
    for (uint8_t i = 0; i < HEADLEN; i++)
        ringbuf_pop(rbuf, bytearray++);

    uint8_t *opc = (uint8_t *) tmp;
    uint32_t tlen = ntohl(*((uint32_t *) (tmp + sizeof(uint8_t))));

    /* Read remaining bytes to complete the packet */
    while (ringbuf_size(rbuf) < tlen - HEADLEN) {
        if ((n = recvbytes(clientfd, rbuf, read_all, tlen - HEADLEN)) < 0) {
            shutdown(clientfd, 0);
            close(clientfd);
            // TODO: remove client from config
            return NULL;
        }
    }

    /* Allocate a buffer to fit the entire packet */
    Buffer *b = buffer_init(tlen);

    /* Copy previous read part of the header (first 5 bytes) */
    memcpy(b->data, tmp, HEADLEN);

    /* Move forward pointer after HEADLEN bytes */
    bytearray = b->data + HEADLEN;

    /* Empty the rest of the ring buffer */
    while ((tlen - HEADLEN) > 0) {
        ringbuf_pop(rbuf, bytearray++);
        --tlen;
    }

    *opcode = *opc;

    return b;
}


/* Used to clean up disconnected clients for whatever reason from the
   subscriptions or config connected peer */
static int close_socket(void *arg1, void *arg2) {
    if (!arg1 || !arg2)
        return 0;
    int fd = *(int *) arg1;
    hashmap_entry *kv = (hashmap_entry *) arg2;
    if (!kv || !kv->val)
        return 0;
    Client *sub = (Client *) kv->val;
    if (sub->fd == fd) {
        shutdown(sub->fd, 0);
        close(sub->fd);
    }
    return 0;
}


/* Build a reply object and link it to the Client pointer */
static void set_reply(Client *c, const uint8_t opcode, Buffer *payload) {
    Reply *r = malloc(sizeof(Reply));
    if (!r) oom("setting reply");

    r->opcode = opcode;
    r->fd = c->fd;
    r->payload = payload;

    c->reply = r;
}


static void set_ack_reply(Client *c, const uint8_t opcode, const uint8_t rc) {
    Ack *ack = ack_pkt(opcode, 0, rc);
    Buffer *b = buffer_init(ack->header->size);
    pack_ack(b, ack);
    set_reply(c, opcode, b);
    free_ack(ack);
}


static int disconnect_handler(SizigyDB *db, Client *c) {

    shutdown(c->fd, 0);
    close(c->fd);

    /* Close all fds passed around the structures */
    hashmap_iterate2(db->topics, close_socket, NULL);
    DEBUG("Received DISCONNECT from %s", c->id);
    return -1;
}


static int connect_handler(SizigyDB *db, Client *c) {

    uint8_t rc = 0x00;
    Connect *pkt = (Connect *) c->ptr;
    DEBUG("Received CONNECT from %s clean_session=%d", pkt->id, pkt->clean_session);

    if (!pkt->id || pkt->idlen == 0) {
        rc = 0x01;
    } else {
        if (pkt->clean_session == 1) {
            del_client(db, c);
            free(c->id);
            c->id = (uint8_t *) strdup((const char *) pkt->id);
            add_client(db, c);
        } else {
            add_client(db, c);
        }
    }

    DEBUG("Sending CONNACK to %s rc=%d", c->id, rc);
    reply_connack(c, rc);
    update_client_last_action(c);

    free_connect(pkt);

    return 0;
}


static int publish_handler(SizigyDB *db, Client *c) {

    Publish *pkt = (Publish *) c->ptr;

    if (!pkt->topic || pkt->topic_len == 0) {
        ERROR("Error: missing topic");
    } else {
        Reply *r = malloc(sizeof(Reply));
        if (!r) oom("setting reply");

        r->opcode = PUBLISH;
        r->fd = c->fd;
        r->publish = pkt;

        c->reply = r;
    }

    return 1;
}


static int pingreq_handler(SizigyDB *db, Client *c) {
    /* Update last activity timestamp for the client */
    DEBUG("Received PINGREQ from %s", c->id);
    reply_pingresp(c, 0x00);
    update_client_last_action(c);
    DEBUG("Sending PINGRESP to %s", c->id);
    return 1;
}


static int subscribe_handler(SizigyDB *db, Client *c) {

    uint8_t rc = 0x00;
    Subscribe *pkt = (Subscribe *) c->ptr;

    DEBUG("Received SUBSCRIBE from %s t=%s q=%d", c->id, pkt->topic, pkt->qos);

    if (!pkt->topic || pkt->topic_len == 0) {
        rc = 0x01;
        reply_suback(c, rc);
    } else {
        Topic *t;
        void *raw;
        if ((raw = hashmap_get(db->topics, pkt->topic))) {
            t = (Topic *) raw;
        } else {
            t = create_topic(strdup((char *) pkt->topic));
            hashmap_put(db->topics, strdup((char *) pkt->topic), t);
        }

        reply_suback(c, rc);
        Subscription *s = create_subscription(c, t->name, pkt->qos);
        add_subscriber(db, s);
        c->reply->retained = t->retained;
        // Update subscriptions for the client
        c->subscriptions = list_head_insert(c->subscriptions, (void *) t->name);
    }

    DEBUG("Sending SUBACK to %s r=%d", c->id, rc);

    return 0;
}


static int unsubscribe_handler(SizigyDB *db, Client *c) {

    uint8_t rc = 0x00;
    Unsubscribe *pkt = (Unsubscribe *) c->ptr;

    DEBUG("Received UNSUBSCRIBE from %s t=%s", c->id, pkt->topic);

    if (!pkt->topic || pkt->topic_len == 0) {
        rc = 0x01;
        reply_unsuback(c, rc);
    } else {
        reply_unsuback(c, rc);
        Subscription s = {
            .client = c,
            .topic = pkt->topic,
            .qos = 0  // No matter value to remove it
        };
        // XXX basic placeholder subscriber
        del_subscriber(db, &s);
    }
    // TODO remove subscriptions from client

    DEBUG("Sending UNSUBACK to %s r=%d", c->id, rc);

    return 0;
}

/* Static command hashmap */
static struct command commands_hashmap[] = {
    {CONNECT, connect_handler},
    {PUBLISH, publish_handler},
    {PINGREQ, pingreq_handler},
    {SUBSCRIBE, subscribe_handler},
    {UNSUBSCRIBE, unsubscribe_handler},
    {DISCONNECT, disconnect_handler}
};


static int commands_hashmap_len(void) {
    return sizeof(commands_hashmap) / sizeof(struct command);
}


static void *unpack_packet(const uint8_t opcode, Buffer *b) {
    if (opcode == CONNECT) {
        Connect *conn = malloc(sizeof(Connect));
        unpack_connect(b, conn);
        return conn;
    } else if (opcode == SUBSCRIBE) {
        Subscribe *sub = malloc(sizeof(Subscribe));
        unpack_subscribe(b, sub);
        return sub;
    } else if (opcode == PUBLISH) {
        Publish *pub = malloc(sizeof(Publish));
        unpack_publish(b, pub);
        return pub;
    } else if (opcode == SUBACK || opcode == PUBACK) {
        Ack *ack = malloc(sizeof(Ack));
        unpack_ack(b, ack);
        return ack;
    }

    return NULL;
}


/* Handle incoming requests, after being accepted or after a reply */
static int request_handler(SizigyDB *db, Client *client) {

    const int clientfd = client->fd;

    /* Buffer to initialize the ring buffer, used to handle input from client */
    uint8_t buffer[ONEMB * 2];

    /* Ringbuffer pointer struct, helpful to handle different and unknown
       size of chunks of data which can result in partially formed packets or
       overlapping as well */
    Ringbuffer *rbuf = ringbuf_init(buffer, ONEMB * 2);

    /* Read all data to form a packet flag */
    int read_all = -1;

    /* Placeholders structures, at this point we still don't know if we got a
       request or a response */
    uint8_t opcode = 0;

    /* We must read all incoming bytes till an entire packet is received. This
       is achieved by using a standardized protocol, which send the size of the
       complete packet as the first 4 bytes. By knowing it we know if the packet is
       ready to be deserialized and used.*/
    Buffer *b = recv_packet(clientfd, rbuf, &opcode);

    if (!b) {
        client->ctx_handler = request_handler;
        mod_epoll(db->epollfd, clientfd, EPOLLIN, client);
        ringbuf_free(rbuf);
        return 0;
    }

    void *pkt = unpack_packet(opcode, b);

    if (!pkt) read_all = 1;

    buffer_destroy(b);

    /* Free ring buffer as we alredy have all needed informations in memory */
    ringbuf_free(rbuf);

    if (read_all == 1) {
        return -1;
    }

    update_client_last_action(client);

    /* Link the correct structure to the client, according to the packet type
       received */
    client->ptr = pkt;

    int free_reply = -1;
    int executed = 0;

    // Loop through commands_hashmap array to find the correct handler
    for (int i = 0; i < commands_hashmap_len(); i++) {
        if (commands_hashmap[i].ctype == opcode) {
            free_reply = commands_hashmap[i].handler(db, client);
            executed = 1;
        }
    }

    // If no handler is found, it must be an error case
    if (executed == 0)
        ERROR("Unknown command");

    // Set reply handler as the current context handler
    client->ctx_handler = reply_handler;

    // Set up epoll events
    if (opcode != PUBREC || executed == 0) {
        mod_epoll(db->epollfd, clientfd, EPOLLOUT, client);
    } else {
        client->ctx_handler = request_handler;
        mod_epoll(db->epollfd, clientfd, EPOLLIN, client);
        if (executed != 0 && free_reply > -1 && client->reply) {
            free(client->reply);
            client->reply = NULL;
        }
    }

    return 0;
}


/* Handle reply state, after a request/response has been processed in
   request_handler routine */
static int reply_handler(SizigyDB *db, Client *client) {

    int ret = 0;
    if (!client->reply)
        return ret;

    Reply *reply = client->reply;
    ssize_t sent;

    if (reply->opcode == PUBLISH) {

        Publish *p = reply->publish;
        Topic *t;
        void *raw;

        if ((raw = hashmap_get(db->topics, (void *) p->topic))) {
            t = (Topic *) raw;
        } else {
            t = create_topic(strdup((char *) p->topic));
            hashmap_put(db->topics, strdup((const char *) p->topic), t);
        }

        DEBUG("Received PUBLISH from %s t=%s q=%d r=%d (%ld bytes)",
                client->id, p->topic, p->qos, p->retain, p->header->size);

        double tic = clock();
        sent = publish_message(t, p, (const uint8_t *) strdup((char *) client->id));
        double elapsed = (clock() - tic) / CLOCKS_PER_SEC;
        int load = (sent / elapsed);

        if (sent > -1) update_client_last_action(client);

        // Check for QOS level
        if (p->qos == 1) {
            Ack *ack = ack_pkt(PUBACK, 0, 0x00);
            // reply to original sender
            Buffer *res = buffer_init(ack->header->size);
            pack_ack(res, ack);
            if ((sendall(reply->fd, res->data, res->size, &sent)) < 0) {
                perror("send(2): can't write on socket descriptor");
                ret = -1;
            }
            free_ack(ack);
            buffer_destroy(res);
        }
        free(p->header);
        free(p);
        /* free_publish(reply->publish); */
    } else {

        if ((sendall(reply->fd, reply->payload->data,
                        reply->payload->size, &sent)) < 0) {
            perror("send(2): can't write on socket descriptor");
            ret = -1;
        }

        buffer_destroy(reply->payload);
    }

    free(client->reply);
    client->reply = NULL;

    /* Set up EPOLL event for read fds */
    client->ctx_handler = request_handler;
    mod_epoll(db->epollfd, client->fd, EPOLLIN, client);
    return ret;
}

/* Handle new connection, create a a fresh new Client structure and link it
   to the fd, ready to be set in EPOLLIN event */
static int accept_handler(SizigyDB *db, Client *server) {
    const int fd = server->fd;

    /* Accept the connection */
    int clientsock = accept_connection(fd);

    /* Abort if not accepted */
    if (clientsock == -1)
        return -1;

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    if (getpeername(clientsock, (struct sockaddr *) &addr, &addrlen) < 0)
        return -1;

    char ip_buff[INET_ADDRSTRLEN + 1];
    if (inet_ntop(AF_INET, &addr.sin_addr, ip_buff, sizeof(ip_buff)) == NULL)
        return -1;

    struct sockaddr_in sin;
    socklen_t sinlen = sizeof(sin);

    if (getsockname(fd, (struct sockaddr *) &sin, &sinlen) < 0)
        return -1;

    /* Create a server structure to handle his context connection */
    Client *client = malloc(sizeof(Client));
    if (!client) oom("creating client during accept");

    client->status = ONLINE;
    client->addr = strdup(ip_buff);
    client->fd = clientsock;
    client->keepalive = config.keepalive,  // FIXME: Placeholder
    /* client->last_action_time = init_atomic(); */
    client->ctx_handler = request_handler;

    update_client_last_action(client);

    const char *id = random_name(16);

    client->id = (uint8_t *) id;
    client->reply = NULL;
    client->subscriptions = list_create();

    /* If the socket is listening in the bus port, add the connection to the peers */
    if (sin.sin_port == db->bus_port)
        db->peers = list_head_insert(db->peers, client);

    /* Add new accepted server to the config hashmap */
    add_client(db, client);

    /* Add it to the epoll loop */
    add_epoll(db->epollfd, clientsock, client);

    /* Rearm server fd to accept new connections */
    mod_epoll(db->epollfd, fd, EPOLLIN, server);

    return 0;
}


static int check_client_status(void *t1, void *t2) {
    if (!t2)
        return HASHMAP_ERR;
    hashmap_entry *kv = (hashmap_entry *) t2;
    if (!kv || kv->val)
        return HASHMAP_OK;
    uint64_t now = (uint64_t) time(NULL);
    // free value field
    Client *c = (Client *) kv->val;
    /* const uint64_t last_action = read_atomic(c->last_action_time); */
    const uint64_t last_action = c->last_action_time;
    if (c->status == ONLINE && (now - last_action) >= c->keepalive) {
        DEBUG("Disconnecting client %s", c->id);
        // TODO: Remove fd from epoll with an mod_epoll EPOLL_CTL_DEL
        // call
        shutdown(c->fd, 0);
        close(c->fd);
        c->status = OFFLINE;
        return HASHMAP_OK;
    }
    return HASHMAP_ERR;
}


static void *keepalive(void *args) {
    SizigyDB *db = (SizigyDB *) args;
    /* Iterate through all clients in the config hashmap every second in order to
       check last activity timestamp */
    eventfd_t val;
    while (1) {
        eventfd_read(config.run, &val);
        if (val == 1)
            break;
        hashmap_iterate2(db->clients, check_client_status, NULL);
        sleep(1);
    }
    return NULL;
}

/* Main worker function, his responsibility is to wait on events on a shared
   EPOLL fd, use the same way for clients or peer to distribute messages */
static void *worker(void *args) {

    SizigyDB *db = (SizigyDB *) args;

    /* struct socks *fds = (struct socks *) args; */
    struct epoll_event *evs = malloc(sizeof(*evs) * MAX_EVENTS);

    if (!evs) {
        perror("malloc(3) failed");
        pthread_exit(NULL);
    }

    int events_cnt;
    while ((events_cnt = epoll_wait(db->epollfd, evs, MAX_EVENTS, -1)) > 0) {
        for (int i = 0; i < events_cnt; i++) {

            /* Check for errors first */
            if ((evs[i].events & EPOLLERR) ||
                    (evs[i].events & EPOLLHUP) ||
                    (!(evs[i].events & EPOLLIN) && !(evs[i].events & EPOLLOUT))) {

                /* An error has occured on this fd, or the socket is not
                   ready for reading */
                perror ("epoll_wait(2)");
                hashmap_iterate2(db->topics, close_socket, NULL);

                close(evs[i].data.fd);
                continue;
            } else if (evs[i].data.fd == config.run) {

                /* And quit event after that */
                eventfd_t val;
                eventfd_read(config.run, &val);

                DEBUG("Stopping epoll loop. Thread %p exiting.",
                        (void *) pthread_self());

                goto exit;
            } else {
                /* Finally handle the request according to its type */
                ((Client *) evs[i].data.ptr)->ctx_handler(db, evs[i].data.ptr);
            }
        }
    }

exit:
    if (events_cnt == 0 && config.run == 0)
        perror("epoll_wait(2) error");

    free(evs);

    return NULL;
}


static int destroy_queue_data(void *t1, void *t2) {
    if (!t2)
        return HASHMAP_ERR;
    hashmap_entry *kv = (hashmap_entry *) t2;
    if (!kv || !kv->val)
        return HASHMAP_OK;
    // free value field
    Topic *t = (Topic *) kv->val;
    QueueItem *item = t->messages->front;
    while (item) {
        Message *m = (Message *) item->data;
        destroy_message(m);
        item = item->next;
    }
    return HASHMAP_OK;
}


static int destroy_topics(void *t1, void *t2) {
    if (!t2)
        return HASHMAP_ERR;
    hashmap_entry *kv = (hashmap_entry *) t2;
    if (!kv || !kv->val)
        return HASHMAP_OK;
    // free value field
    Topic *t = (Topic *) kv->val;
    destroy_topic(t);
    if (kv->key)
        free(kv->key);
    return HASHMAP_OK;
}


static int destroy_clients(void *t1, void *t2) {
    if (!t2)
        return HASHMAP_ERR;
    hashmap_entry *kv = (hashmap_entry *) t2;
    if (!kv || !kv->val)
        return HASHMAP_OK;
    Client *c = (Client *) kv->val;
    if (c->id) {
        free(c->id);
        c->id = NULL;
    }
    if (c->reply) {
        free(c->reply);
        c->reply = NULL;
    }
    if (c->subscriptions)
        list_release(c->subscriptions, 0);
    if (c->addr) {
        free((char *) c->addr);
        c->addr = NULL;
    }
    free(c);
    c = NULL;
    return HASHMAP_OK;
}

/*
 * Main entry point for start listening on a socket and running an epoll event
 * loop his main responsibility is to pass incoming client connections
 * descriptor to workers thread.
 */
int start_server(const char *addr, char *port, int node_fd) {

    /* Initialize config server object */
    config.loglevel = DEBUG;
    config.run = eventfd(0, EFD_NONBLOCK);
    pthread_mutex_init(&(config.lock), NULL);
    config.keepalive = 60;

    SizigyDB sizigydb;

    /* Initialize SizigyDB server object */
    sizigydb.run = eventfd(0, EFD_NONBLOCK);
    sizigydb.topics = hashmap_create();
    sizigydb.ack_waiting = hashmap_create();
    sizigydb.clients = hashmap_create();
    sizigydb.peers = list_create();
    pthread_mutex_init(&(config.lock), NULL);

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

    /* Add 10k to the listening server port */
    int bport = atoi(port) + 10000;
    char bus_port[12];
    snprintf(bus_port, sizeof(bus_port), "%d", bport);

    /* Add the bus port to the config shared structure */
    sizigydb.bus_port = bport;

    /* The bus one for distribution */
    const int bfd = make_listen(addr, bus_port);

    /* Add eventfd to the loop, this time only in LT in order to wake up all threads */
    struct epoll_event ev;
    ev.data.fd = config.run;
    ev.events = EPOLLIN;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, config.run, &ev) < 0) {
        perror("epoll_ctl(2): add epollin");
    }

    if (epoll_ctl(bepollfd, EPOLL_CTL_ADD, config.run, &ev) < 0) {
        perror("epoll_ctl(2): add epollin");
    }

    /* Client structure for the server component */
    Client server = {
        .status = ONLINE,
        .addr = addr,
        .keepalive = config.keepalive,  // FIXME: Placeholder
        .fd = fd,
        /* .last_action_time = init_atomic(), */
        .last_action_time = 0,
        .ctx_handler = accept_handler,
        .id = (uint8_t *) "server",
        .reply = NULL,
        .subscriptions = list_create(),
        .ptr = NULL
    };

    /* And another one for the bus */
    Client bus = {
        .status = ONLINE,
        .addr = addr,
        .keepalive = config.keepalive,  // FIXME: Placeholder
        .fd = bfd,
        /* .last_action_time = init_atomic(), */
        .last_action_time = 0,
        .ctx_handler = accept_handler,
        .id = (uint8_t *) "bus",
        .reply = NULL,
        .subscriptions = list_create(),
        .ptr = NULL
    };

    /* Set socket in EPOLLIN flag mode, ready to read data */
    add_epoll(epollfd, fd, &server);

    /* Set bus socket in EPOLLIN too */
    add_epoll(bepollfd, bfd, &bus);

    sizigydb.bepollfd = bepollfd;
    sizigydb.epollfd = epollfd;

    /* Bus dedicated thread */
    pthread_t bus_worker;
    /* struct socks bus_fds = { bepollfd, bfd }; */
    pthread_create(&bus_worker, NULL, worker, (void *) &sizigydb);

    /* Worker thread pool */
    pthread_t workers[EPOLL_WORKERS];

    /* I/0 thread pool initialization, passing a the pair {epollfd, fd} sockets
       for each one. Every worker handle input from clients, accepting
       connections and sending out data when a socket is ready to write */

    for (int i = 0; i < EPOLL_WORKERS; ++i)
        pthread_create(&workers[i], NULL, worker, (void *) &sizigydb);

    INFO("Sizigy v0.6.0");
    INFO("Starting server on %s:%s", addr, port);

    Client node = {
        .status = ONLINE,
        .keepalive = config.keepalive,  // FIXME: Placeholder
        .addr = addr,
        .fd = node_fd,
        .ctx_handler = request_handler,
        /* .last_action_time = init_atomic(), */
        .last_action_time = 0,
        .id = (uint8_t *) "node",
        .reply = NULL,
        .subscriptions = list_create(),
        .ptr = NULL
    };

    pthread_t keepalive_thread;

    pthread_create(&keepalive_thread, NULL, keepalive, &sizigydb);

    /* Use main thread as a worker too */
    worker(&sizigydb);

    for (int i = 0; i < EPOLL_WORKERS; ++i)
        pthread_join(workers[i], NULL);

    /* Dedicated thread to bus communications, e.g. distribution */
    pthread_join(bus_worker, NULL);

cleanup:
    /* Free all resources allocated */
    /* hashmap_iterate2(sizigydb.topics, destroy_queue_data, NULL); */
    /* hashmap_iterate2(sizigydb.topics, destroy_topics, NULL); */
    /* hashmap_iterate2(sizigydb.clients, destroy_clients, NULL); */
    /* hashmap_release(sizigydb.topics); */
    /* hashmap_release(sizigydb.clients); */
    hashmap_release_map(sizigydb.topics, destroy_topics);
    hashmap_release_map(sizigydb.clients, destroy_clients);
    hashmap_release(sizigydb.ack_waiting);
    /* free_atomic(server.last_action_time); */
    /* free_atomic(node.last_action_time); */
    /* free_atomic(bus.last_action_time); */
    free(server.subscriptions);
    free(bus.subscriptions);
    free(node.subscriptions);
    list_release(sizigydb.peers, 1);
    pthread_mutex_destroy(&(config.lock));
    pthread_mutex_destroy(&(sizigydb.lock));
    DEBUG("Bye\n");
    return 0;
}


void retain_message(Topic *t, Publish *msg, const uint8_t *sender) {
    Message *m = create_message(msg, sender);
    m->retained = 1;
    t->retained = m;
}


int publish_message(Topic *t, Publish *msg, const uint8_t *sender) {
    int total_bytes_sent = 0;
    /* Sent bytes sentinel */
    ssize_t sent = 0;
    int send_rc = 0;

    Buffer *b = buffer_init(msg->header->size);
    pack_publish(b, msg);

    if (msg->retain)
        retain_message(t, msg, sender);

    /* Iterate through all the subscribers to send them the message */
    list_node *cursor = t->subscribers->head;
    while (cursor) {
        Subscription *sub = (Subscription *) cursor->data;
        send_rc = sendall(sub->client->fd, b->data, b->size, &sent);
        if (send_rc < 0)
            perror("Can't publish");
        update_client_last_action(sub->client);
        total_bytes_sent += sent;
        DEBUG("Sending PUBLISH to %s c=%s q=%d r=%d (%ld bytes)",
                sub->client->id, t->name, msg->qos, msg->retain, b->size);

        cursor = cursor->next;
    }

    buffer_destroy(b);

    return total_bytes_sent;
}

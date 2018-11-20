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
/* #include "topic.h" */
#include "network.h"


#define reply_connack(c, fd, status) \
    (add_ack_reply((c), CONNACK_REPLY, (fd), (status)));

#define reply_suback(c, fd, status) \
    (add_ack_reply((c), SUBACK_REPLY, (fd), (status)));

#define reply_pingresp(c, fd) \
    (add_ack_reply((c), PINGRESP_REPLY, (fd), 0x00));

// Reference to the global structure, could be refactored lately to a more
// structured configuration
struct global global;


static int reply_handler(SizigyDB *, Client *);
static int request_handler(SizigyDB *, Client *);
static int accept_handler(SizigyDB *, Client *);
static int close_socket(void *, void *);
static void add_reply(Client *, uint8_t,
        uint8_t, uint8_t, const int, const uint8_t *, const uint8_t *);
static void free_reply(Reply *);
static void add_ack_reply(Client *, uint8_t, const int, uint8_t);
static int quit_handler(SizigyDB *, Client *);
static int join_handler(SizigyDB *, Client *);
static int join_ack_handler(SizigyDB *, Client *);
static int connect_handler(SizigyDB *, Client *);
static int replica_handler(SizigyDB *, Client *);
static int publish_handler(SizigyDB *, Client *);
static int pingreq_handler(SizigyDB *, Client *);
static int subscribe_handler(SizigyDB *, Client *);
static int unsubscribe_handler(SizigyDB *, Client *);
static int destroy_clients(void *, void *);
static int destroy_topics(void *, void *);
static int destroy_queue_data(void *, void *);
static void *keepalive(void *);
static void *worker(void *);
static Buffer *craft_response(Reply *, Response *, uint8_t );

// Fixed size of the header of each packet, consists of essentially the first
// 5 bytes containing respectively the type of packet (REQUEST OR RESPONSE)
// and the total length in bytes of the packet
static const int HEADLEN = sizeof(uint8_t) + sizeof(uint32_t);

/* Parse header, require at least the first 5 bytes in order to read packet
   type and total length that we need to recv to complete the packet */
Buffer *recv_packet(const int clientfd, Ringbuffer *rbuf, uint8_t *type) {

    size_t n = 0;
    uint8_t read_all = 0;

    while (n < HEADLEN) {
        /* Read first 5 bytes to get the total len of the packet */
        n += recvbytes(clientfd, rbuf, read_all, HEADLEN);
        if (n < 0) {
            shutdown(clientfd, 0);
            close(clientfd);
            // TODO: remove client from global
            return NULL;
        } else if (n == 0) {
            return NULL;
        }
    }

    /* DEBUG("RINGBUFSIZE %d", ringbuf_size(rbuf)); */

    uint8_t tmp[ringbuf_size(rbuf)];
    uint8_t *bytearray = tmp;

    /* Try to read at least length of the packet */
    for (uint8_t i = 0; i < HEADLEN; i++)
        ringbuf_pop(rbuf, bytearray++);

    uint8_t *typ = (uint8_t *) tmp;
    uint32_t tlen = ntohl(*((uint32_t *) (tmp + sizeof(uint8_t))));

    /* DEBUG("TLEN %d", tlen); */

    /* Read remaining bytes to complete the packet */
    while (ringbuf_size(rbuf) < tlen - HEADLEN) {
        if ((n = recvbytes(clientfd, rbuf, read_all, tlen - HEADLEN)) < 0) {
            shutdown(clientfd, 0);
            close(clientfd);
            // TODO: remove client from global
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

    *type = *typ;

    return b;
}


/* Used to clean up disconnected clients for whatever reason from the
   subscriptions or global connected peer */
static int close_socket(void *arg1, void *arg2) {
    int fd = *(int *) arg1;
    hashmap_entry *kv = (hashmap_entry *) arg2;
    Client *sub = (Client *) kv->val;
    if (sub->fd == fd) {
        shutdown(sub->fd, 0);
        close(sub->fd);
    }
    return 0;
}


/* Build a reply object and link it to the Client pointer */
static void add_reply(Client *c, uint8_t type, uint8_t qos,
        uint8_t retain, const int fd, const uint8_t *topic, const uint8_t *message) {

    Reply *r = malloc(sizeof(Reply));
    if (!r) oom("adding reply");

    r->type = type;
    r->qos = qos;
    r->fd = fd;
    r->retain = retain;
    r->data = message;
    r->topic = topic;
    c->reply = r;
}


static void add_ack_reply(Client *c, uint8_t type, const int fd, uint8_t rc) {
    Reply *r = malloc(sizeof(Reply));
    if (!r) oom("adding ack reply");

    r->type = type;
    r->fd = fd;
    r->rc = rc;
    c->reply = r;
}


static int quit_handler(SizigyDB *db, Client *c) {

    shutdown(c->fd, 0);
    close(c->fd);

    /* Close all fds passed around the structures */
    hashmap_iterate2(db->topics, close_socket, NULL);
    DEBUG("QUIT %s", c->id);
    return -1;
}


static int join_handler(SizigyDB *db, Client *c) {
    if (c->type == REQUEST) {

        /* XXX Dirty hack: Request of client contains the client bus listening port */
        add_reply(c, JACK_REPLY, 0, 0, c->fd,
                c->req->ack_data, (const uint8_t *) c->id);

        // XXX for now just insert pointer to client struct
        db->peers = list_head_insert(db->peers, c);
        DEBUG("CLUSTER_JOIN from %s at %s:%s request accepted", c->id, c->addr, c->req->ack_data);
    }
    return 0;
}

/* TODO Should be removed, or at least incorporated with join_handler as a response */
static int join_ack_handler(SizigyDB *db, Client *c) {
    if (c->type == REQUEST) {
        // XXX for now just insert pointer to client struct
        db->peers = list_head_insert(db->peers, c);
        DEBUG("Joined cluster, new member at: %s", c->req->ack_data);

        /* Extract hostname and port from the ack payload in order to connect
           to the new member */
        char *member = (char *) c->req->ack_data;
        char *mhost = strtok(member, ":");
        char *mport = strtok(NULL, "\0");

        /* Connect to the new member */
        int fd =  make_connection(mhost, atoi(mport));
        set_nonblocking(fd);

        /* Add it to a client structure */
        Client *client = malloc(sizeof(Client));
        client->status = ONLINE;
        client->addr = strdup(mhost);
        client->fd = fd;
        client->ctx_handler = request_handler;

        const char *id = random_name(16);
        char *name = append_string("C:", id);  // C states that it is a client (could be another sizigy instance)
        free((void *) id);

        client->id = name;
        client->reply = NULL;
        client->subscriptions = list_create();

        db->peers = list_head_insert(db->peers, client);

        /* Add new accepted server to the global hashmap */
        hashmap_put(db->clients, name, client);
        /* Add it to the epoll loop */
        add_epoll(db->bepollfd, fd, client);

        free(mhost);
    }
    return 0;
}


static int connect_handler(SizigyDB *db, Client *c) {

    if (c->type == REQUEST) {
        if (!c->req->sub_id || c->req->sub_id_len == 0) {
            reply_connack(c, c->fd, 0x01);
            DEBUG("Sending CONNACK to %s rc=1", c->req->sub_id);
        } else {

            char *id = append_string("C:", (const char *) c->req->sub_id);
            if (c->req->clean_session == 1) {
                del_client(db, c);
                free(c->id);
                c->id = id;
                add_client(db, c);
            } else {
                /* Should check if global hashmap contains an entry and return an
                   error code in case of failure, for now we just add the new
                   client */
                add_client(db, c);
            }

            DEBUG("Received CONNECT id=%s clean_session=%d", c->req->sub_id, c->req->clean_session);
            reply_connack(c, c->fd, 0x00);
            DEBUG("Sending CONNACK to %s rc=0", c->req->sub_id);
        }

        update_client_last_action(c);
    }

    return 0;
}


static int replica_handler(SizigyDB *db, Client *c) {

    if (c->type == REQUEST) {

        Topic *t;
        void *raw;
        if ((raw = hashmap_get(db->topics, c->req->topic))) {
            t = (Topic *) raw;
        } else {
            t = create_topic((char *) c->req->topic);
            hashmap_put(db->topics, strdup((char *) c->req->topic), t);
        }

        /* Add message to the topic */
        // XXX require new command packet for replica (e.g. save ID etc)
        store_message(t, 0, c->req->qos, 0, c->req->message, 0);

        DEBUG("REPLICA t=%s i=%d q=%d m=%s", c->req->topic,
                c->req->id, c->req->qos, c->req->message);
    }
    return 1;
}


static int publish_handler(SizigyDB *db, Client *c) {

    if (c->type == REQUEST) {
        if (!c->req->topic || c->req->topic_len == 0) {
            ERROR("Error: missing topic");
        } else {
            add_reply(c, DATA_REPLY, c->req->qos, c->req->retain, c->fd,
                    (const uint8_t *) strdup((char *) c->req->topic),
                    (const uint8_t *) strdup((char *) c->req->message));
        }
    }
    return 1;
}


static int pingreq_handler(SizigyDB *db, Client *c) {
    /* Update last activity timestamp for the client */
    if (c->type == REQUEST) {
        DEBUG("Received PINGREQ from %s", c->id);
        reply_pingresp(c, c->fd);
        update_client_last_action(c);
        DEBUG("Sending PINGRESP to %s", c->id);
    }
    return 1;
}


static int subscribe_handler(SizigyDB *db, Client *c) {

    if (c->type == REQUEST) {
        if (!c->req->topic || c->req->topic_len == 0) {

            reply_suback(c, c->fd, 0x01);
            DEBUG("Sending SUBACK i=%s r=1", c->id);

        } else {

            DEBUG("Received SUBSCRIBE i=%s t=%s q=%d",
                    c->id, c->req->topic, c->req->qos);

            reply_suback(c, c->fd, 0x00);

            DEBUG("Sending SUBACK i=%s r=0", c->id);

            Topic *t;
            void *raw;
            if ((raw = hashmap_get(db->topics, c->req->topic))) {
                t = (Topic *) raw;
            } else {
                t = create_topic((char *) c->req->topic);
                hashmap_put(db->topics, strdup((char *) c->req->topic), t);
            }

            Subscription *s = create_subscription(c, t->name, c->req->qos);

            add_subscriber(db, s);

            c->reply->retained = t->retained;

            // Update subscriptions for the client
            c->subscriptions = list_head_insert(c->subscriptions, (void *) t->name);
        }
    }

    return 0;
}


static int unsubscribe_handler(SizigyDB *db, Client *c) {

    if (c->type == REQUEST) {
        if (!c->req->topic || c->req->topic_len == 0) {
            reply_suback(c, c->fd, 0x01);
            DEBUG("Sending SUBACK to %s r=1", c->id);
        } else {
            DEBUG("Received UNSUBSCRIBE from %s t=%s", c->id, c->req->topic);
            reply_suback(c, c->fd, 0x00);
            DEBUG("Sending SUBACK to %s r=1", c->id);
            Subscription s = {
                .client = c,
                .topic = c->req->topic,
                .qos = 0  // No matter value to remove it
            };
            // XXX basic placeholder subscriber
            del_subscriber(db, &s);
        }
    }
    // TODO remove subscriptions from client

    return 0;
}

/* Static command hashmap */
static struct command commands_hashmap[] = {
    {QUIT, quit_handler},
    {CLUSTER_JOIN, join_handler},
    {CLUSTER_JOIN_ACK, join_ack_handler},
    {REPLICA, replica_handler},
    {CONNECT, connect_handler},
    {PUBLISH, publish_handler},
    {PINGREQ, pingreq_handler},
    {SUBSCRIBE, subscribe_handler},
    {UNSUBSCRIBE, unsubscribe_handler}
};


static int commands_hashmap_len(void) {
    return sizeof(commands_hashmap) / sizeof(struct command);
}


static void free_reply(Reply *reply) {
    if (reply->data)
        free((uint8_t *) reply->data);
    if (reply->topic)
        free((uint8_t *) reply->topic);
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
    Response res;
    Request req;
    uint8_t type = 0;

    /* We must read all incoming bytes till an entire packet is received. This
       is achieved by using a standardized protocol, which send the size of the
       complete packet as the first 4 bytes. By knowing it we know if the packet is
       ready to be deserialized and used.*/
    Buffer *b = recv_packet(clientfd, rbuf, &type);

    if (!b) {
        client->ctx_handler = request_handler;
        mod_epoll(db->epollfd, clientfd, EPOLLIN, client);
        ringbuf_free(rbuf);
        return 0;
    }

    if (type == REQUEST)
        read_all = unpack_request(b, &req);
    else if (type == RESPONSE)
        read_all = unpack_response(b, &res);
    else
        read_all = 1;

    buffer_destroy(b);

    /* Free ring buffer as we alredy have all needed informations in memory */
    ringbuf_free(rbuf);

    if (read_all == 1) {
        return -1;
    }

    uint8_t opcode;
    client->type = type;
    update_client_last_action(client);

    /* Link the correct structure to the client, according to the packet type
       received */
    if (type == REQUEST) {
        client->req = &req;
        opcode = req.header->opcode;
        // Update KEEPALIVE value for the new connected client
        if (opcode == CONNECT) {
            client->keepalive = req.keepalive;
        }
    } else {
        client->res = &res;
        opcode = res.header->opcode;
    }

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
    if (executed == 0) {
        ERROR("Unknown command");
    }

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

    if (type == REQUEST) free(req.header);
    else free(res.header);

    /* Clean up heap memory */
    switch (opcode) {
        case CONNECT:
            if (type == REQUEST) free(req.sub_id);
            break;
        case UNSUBSCRIBE:
            if (type == REQUEST) free(req.data);
            break;
        case REPLICA:
        case PUBLISH:
        case SUBSCRIBE:
            if (type == REQUEST) {
                free(req.topic);
                free(req.message);
            } else {
                free(res.topic);
                free(res.message);
            }
            break;
        case CONNACK:
        case SUBACK:
        case PUBACK:
            if (type == REQUEST) free(req.ack_data);
            break;
    }

    return 0;
}


static Buffer *craft_response(Reply *r, Response *ack, uint8_t rc) {
    switch (r->type) {
        case CONNACK_REPLY:
           ack->header->opcode = CONNACK;
           ack->rc = rc;
           break;
        case SUBACK_REPLY:
           ack->header->opcode = SUBACK;
           // In case of retained message we assume rc == 0
           if (!r->retained)
               ack->rc = rc;
           else ack->rc = 0x00;
           break;
        case PUBACK_REPLY:
           ack->header->opcode = PUBACK;
           ack->rc = rc;
           break;
        case PINGRESP_REPLY:
           ack->header->opcode = PINGRESP;
           ack->rc = rc;
           break;
    }

    return pack_response(ack);
}


/* Handle reply state, after a request/response has been processed in
   request_handler routine */
static int reply_handler(SizigyDB *db, Client *client) {

    int ret = 0;
    if (!client->reply) return ret;

    Reply *reply = client->reply;
    ssize_t sent;

    /* Alloc on the heap a ACK response, will be manipulated according to the
       case of use */
    Buffer *p_ack = NULL;

    if (reply->type >= CONNACK_REPLY && reply->type <= PINGRESP_REPLY) {

        Response *ack = build_ack_res(CONNACK, 0x00);
        p_ack = craft_response(reply, ack, 0x00);

        if ((sendall(reply->fd, p_ack->data, p_ack->size, &sent)) < 0) {
            perror("send(2): can't write on socket descriptor");
            ret = -1;
        }

        free(ack->header);
        free(ack);
        buffer_destroy(p_ack);

        /* In case of reply->retained we assume that the rc == 0 */
        if (reply->type == SUBACK_REPLY && reply->retained) {
            Message *m = reply->retained;
            char *topic = append_string((const char *) m->topic, " ");
            Response *r =
                build_pub_res(m->qos, (const uint8_t *) topic, m->payload, 0);

            r->id = m->id;

            Buffer *p = pack_response(r);
            if (sendall(reply->fd, p->data, p->size, &(ssize_t) { 0 }) < 0) {
                perror("send(2): error sending\n");
            }

            free(topic);
            buffer_destroy(p);
            free(r);
        }

    } else if (reply->type == JACK_REPLY) {

        /* Dirty hack: we should now send host:port pairs in order to update
           all the cluster with the new member, excluding the self and himself */
        /* So first we retrive the id of the newly joined member */
        const char *new_member_id = (const char *) reply->data;
        const char *new_member_port = (const char *) reply->topic;

        /* Next we must populate the string with the host:port pairs */
        list_node *cur = db->peers->head;

        while (cur) {

            Client *cli = (Client *) cur->data;
            if (strcasecmp(cli->id, new_member_id) != 0) {

                /* Make string host:pair, this one will be sent to all matching peers, e.g. all
                   other that are no aware of the new joined member */
                char *hostcolon = append_string(client->addr, ":");
                char *pair = append_string(hostcolon, new_member_port);

                /* Pack request and send it to other peers so they can connect to the new member */
                Request *join_ack =
                    build_ack_req(CLUSTER_JOIN_ACK, (const uint8_t *) pair);
                p_ack = pack_request(join_ack);

                if ((sendall(cli->fd, p_ack->data, p_ack->size, &sent)) < 0) {
                    perror("send(2): can't write on socket descriptor");
                    ret = -1;
                }

                free(pair);
                free(hostcolon);
                free(join_ack);
            }
            cur = cur->next;
        }

        /* Send an OK ACK to the original sender */
        Response *join_ack = build_ack_res(CLUSTER_JOIN_ACK, 0x00);
        p_ack = pack_response(join_ack);
        if ((sendall(reply->fd, p_ack->data, p_ack->size, &sent)) < 0) {
            perror("send(2): can't write on socket descriptor");
            ret = -1;
        }
        free(join_ack);
        buffer_destroy(p_ack);

    } else {

        Topic *t;
        void *raw;
        if ((raw = hashmap_get(db->topics, (void *) reply->topic))) {
            t = (Topic *) raw;
        } else {
            t = create_topic((char *) reply->topic);
            hashmap_put(db->topics, strdup((const char *) reply->topic), t);
        }

        double tic = clock();
        /* sent = publish_message(t, reply->qos, reply->retain, */
        /*         (const uint8_t *) strdup((char *) reply->data), 0, (const uint8_t *) client->id); */
        Message *m = create_message(reply->qos, reply->retain, 0,
                (const uint8_t *) strdup(t->name), reply->data, (const uint8_t *) client->id);
        sent = publish_message2(t, m, 0);
        double elapsed = (clock() - tic) / CLOCKS_PER_SEC;
        int load = (sent / elapsed);

        if (sent > -1) update_client_last_action(client);

        // Check for QOS level
        if (reply->qos == 1) {
            Response *puback = build_ack_res(PUBACK, 0x00);
            // reply to original sender
            Buffer *res = pack_response(puback);
            if ((sendall(reply->fd, res->data, res->size, &sent)) < 0) {
                perror("send(2): can't write on socket descriptor");
                ret = -1;
            }
            free(puback->header);
            free(puback);
            buffer_destroy(res);
        }
        write_atomic(global.throughput, load);
    }

    if (reply->type == DATA_REPLY)
        free_reply(reply);
    free(reply);

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

    if (getpeername(clientsock, (struct sockaddr *) &addr, &addrlen) < 0) {
        return -1;
    }

    char ip_buff[INET_ADDRSTRLEN + 1];
    if (inet_ntop(AF_INET, &addr.sin_addr, ip_buff, sizeof(ip_buff)) == NULL) {
        return -1;
    }

    struct sockaddr_in sin;
    socklen_t sinlen = sizeof(sin);

    if (getsockname(fd, (struct sockaddr *) &sin, &sinlen) < 0) {
        return -1;
    }

    /* Create a server structure to handle his context connection */
    Client *client = malloc(sizeof(Client));
    if (!client) oom("creating client during accept");

    client->status = ONLINE;
    client->addr = strdup(ip_buff);
    client->fd = clientsock;
    client->keepalive = global.keepalive,  // FIXME: Placeholder
    client->last_action_time = init_atomic();
    client->ctx_handler = request_handler;

    write_atomic(client->last_action_time, (const uint64_t ) time(NULL));

    const char *id = random_name(16);
    char *name = append_string("C:", id);  // C states that it is a client (could be another sizigy instance)
    free((void *) id);

    client->id = name;
    client->reply = NULL;
    client->subscriptions = list_create();

    /* If the socket is listening in the bus port, add the connection to the peers */
    if (sin.sin_port == global.bus_port) {
        db->peers = list_head_insert(db->peers, client);
    }

    /* Add new accepted server to the global hashmap */
    add_client(db, client);

    /* Add it to the epoll loop */
    add_epoll(db->epollfd, clientsock, client);

    /* Rearm server fd to accept new connections */
    mod_epoll(db->epollfd, fd, EPOLLIN, server);

    return 0;
}


static int check_client_status(void *t1, void *t2) {
    hashmap_entry *kv = (hashmap_entry *) t2;
    uint64_t now = (uint64_t) time(NULL);
    if (kv) {
        // free value field
        if (kv->val) {
            Client *c = (Client *) kv->val;
            const uint64_t last_action = read_atomic(c->last_action_time);
            if (c->status == ONLINE && (now - last_action) >= c->keepalive) {
                DEBUG("Disconnecting client %s", c->id);
                // TODO: Remove fd from epoll with an mod_epoll EPOLL_CTL_DEL
                // call
                shutdown(c->fd, 0);
                close(c->fd);
                c->status = OFFLINE;
            }
        }
        return HASHMAP_OK;
    }
    return HASHMAP_ERR;
}


static void *keepalive(void *args) {
    SizigyDB *db = (SizigyDB *) args;
    /* Iterate through all clients in the global hashmap every second in order to
       check last activity timestamp */
    eventfd_t val;
    while (1) {
        eventfd_read(global.run, &val);
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
            } else if (evs[i].data.fd == global.run) {

                /* And quit event after that */
                eventfd_t val;
                eventfd_read(global.run, &val);

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
    if (events_cnt == 0 && global.run == 0)
        perror("epoll_wait(2) error");

    free(evs);

    return NULL;
}


static int destroy_queue_data(void *t1, void *t2) {
    hashmap_entry *kv = (hashmap_entry *) t2;
    if (kv) {
        // free value field
        if (kv->val) {
            Topic *t = (Topic *) kv->val;
            QueueItem *item = t->messages->front;
            while (item) {
                Message *m = (Message *) item->data;
                destroy_message(m);
                item = item->next;
            }
        }
    } else return HASHMAP_ERR;
    return HASHMAP_OK;
}


static int destroy_topics(void *t1, void *t2) {
    hashmap_entry *kv = (hashmap_entry *) t2;
    if (kv) {
        // free value field
        if (kv->val) {
            Topic *t = (Topic *) kv->val;
            destroy_topic(t);
        }
    } else return HASHMAP_ERR;
    return HASHMAP_OK;
}


static int destroy_clients(void *t1, void *t2) {
    hashmap_entry *kv = (hashmap_entry *) t2;
    if (kv) {
        if (kv->val) {
            Client *c = (Client *) kv->val;
            if (c->id)
                free(c->id);
            if (c->reply)
                free(c->reply);
            if (c->subscriptions)
                list_release(c->subscriptions, 0);
            free(c->last_action_time);
        }
    } else return HASHMAP_ERR;
    return HASHMAP_OK;
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
    global.topics = hashmap_create();
    global.ack_waiting = hashmap_create();
    global.clients = hashmap_create();
    global.peers = list_create();
    global.next_id = init_atomic();  // counter to get message id, should be enclosed inside locks
    global.throughput = init_atomic();
    global.throttler = init_throttler();
    pthread_mutex_init(&(global.lock), NULL);
    global.keepalive = 60;

    SizigyDB sizigydb;
    /* Initialize SizigyDB server object */
    sizigydb.run = eventfd(0, EFD_NONBLOCK);
    sizigydb.topics = hashmap_create();
    sizigydb.ack_waiting = hashmap_create();
    sizigydb.clients = hashmap_create();
    sizigydb.peers = list_create();
    sizigydb.next_id = init_atomic();  // counter to get message id, should be enclosed inside locks
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

    /* Add 10k to the listening server port */
    int bport = atoi(port) + 10000;
    char bus_port[12];
    snprintf(bus_port, sizeof(bus_port), "%d", bport);

    /* Add the bus port to the global shared structure */
    global.bus_port = bport;
    sizigydb.bus_port = bport;

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
    Client server = {
        .type = REQUEST,
        .status = ONLINE,
        .addr = addr,
        .keepalive = global.keepalive,  // FIXME: Placeholder
        .fd = fd,
        .last_action_time = init_atomic(),
        .ctx_handler = accept_handler,
        .id = "server",
        .reply = NULL,
        .subscriptions = list_create(),
        { NULL }
    };

    /* And another one for the bus */
    Client bus = {
        .type = REQUEST,
        .status = ONLINE,
        .addr = addr,
        .keepalive = global.keepalive,  // FIXME: Placeholder
        .fd = bfd,
        .last_action_time = init_atomic(),
        .ctx_handler = accept_handler,
        .id = "bus",
        .reply = NULL,
        .subscriptions = list_create(),
        { NULL }
    };

    /* Set socket in EPOLLIN flag mode, ready to read data */
    add_epoll(epollfd, fd, &server);

    /* Set bus socket in EPOLLIN too */
    add_epoll(bepollfd, bfd, &bus);

    global.bepollfd = bepollfd;
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
    /* struct socks fds = { epollfd, fd }; */

    for (int i = 0; i < EPOLL_WORKERS; ++i)
        pthread_create(&workers[i], NULL, worker, (void *) &sizigydb);

    INFO("Sizigy v0.5.5");
    INFO("Starting server on %s:%s", addr, port);

    Client node = {
        .type = REQUEST,
        .status = ONLINE,
        .keepalive = global.keepalive,  // FIXME: Placeholder
        .addr = addr,
        .fd = node_fd,
        .ctx_handler = request_handler,
        .last_action_time = init_atomic(),
        .id = "node",
        .reply = NULL,
        .subscriptions = list_create(),
        { NULL }
    };

    /* Add eventual connected node */
    if (node_fd > 0) {
        add_epoll(bepollfd, node_fd, &node);
        /* Ask for joining the cluster */
        Request *join_req_packet =
            build_ack_req(CLUSTER_JOIN, (uint8_t *) bus_port);
        Buffer *p = pack_request(join_req_packet);
        int rc = sendall(node_fd, p->data, p->size, &(ssize_t) { 0 });
        if (rc < 0)
            printf("Failed join\n");
        free(join_req_packet);
        buffer_destroy(p);
    }

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
    hashmap_iterate2(global.topics, destroy_queue_data, NULL);
    hashmap_iterate2(global.topics, destroy_topics, NULL);
    hashmap_iterate2(global.clients, destroy_clients, NULL);
    hashmap_iterate2(sizigydb.topics, destroy_queue_data, NULL);
    hashmap_iterate2(sizigydb.topics, destroy_topics, NULL);
    hashmap_iterate2(sizigydb.clients, destroy_clients, NULL);
    free(server.subscriptions);
    free(bus.subscriptions);
    free(node.subscriptions);
    list_release(global.peers, 1);
    list_release(sizigydb.peers, 1);
    free(global.next_id);
    free(global.throughput);
    free(global.throttler);
    pthread_mutex_destroy(&(global.lock));
    pthread_mutex_destroy(&(sizigydb.lock));
    DEBUG("Bye\n");
    return 0;
}


void retain_message(Topic *t, const uint64_t id,
        uint8_t qos, uint8_t dup, const uint8_t *payload) {

    Message *m = malloc(sizeof(Message));
    if (!m) oom("creating message to be stored");

    m->creation_time = time(NULL);
    m->id = id;
    m->qos = qos;
    m->dup = dup;
    m->payload = (uint8_t *) strdup((const char *) payload);
    m->topic = (uint8_t *) t->name;
    m->retained = 1;
    t->retained = m;

}


void retain_message2(Topic *t, Message *m) {
    m->retained = 1;
    t->retained = m;
}




void store_message(Topic *t, const uint64_t id,
        uint8_t qos, uint8_t dup, const uint8_t *payload, int check_peers) {

    Message *m = malloc(sizeof(Message));
    if (!m) oom("creating message to be stored");

    m->creation_time = (uint64_t) time(NULL);
    m->id = id;
    m->qos = qos;
    m->dup = dup;
    m->payload = (uint8_t *) strdup((const char *) payload);
    m->topic = (uint8_t *) t->name;
    enqueue(t->messages, m);

    // Check if cluster has members and spread the replicas
    if (check_peers == 1 && global.peers->len > 0) {
        const char *m = append_string(t->name, " "); // TODO: check
        Request *replica_r = build_rep_req(qos, (const uint8_t *) m, payload);
        Buffer *p = pack_request(replica_r);
        list_node *cur = global.peers->head;

        while (cur) {
            Client *c = (Client *) cur->data;
            sendall(c->fd, p->data, p->size, &(ssize_t) { 0 });
            cur = cur->next;
        }

        free((char *) m);
        free(replica_r->header);
        free(replica_r);
        buffer_destroy(p);
    }
}


int publish_message(Topic *t, uint8_t qos, uint8_t retain,
        const uint8_t *message, int incr, const uint8_t *sender) {
    int total_bytes_sent = 0;
    uint8_t qos_mod = 0;
    int increment = incr < 0 ? 0 : 1;
    const char *topic = append_string(t->name, " ");
    uint8_t duplicate = 0;
    Response *response =
        build_pub_res(qos, (const uint8_t *) topic, message, increment);
    uint64_t id = response->id;
    Buffer *p = pack_response(response);
    int send_rc = 0;

    /* Prepare packet for AT_LEAST_ONCE subscribers */
    if (response->qos == AT_MOST_ONCE) {
        response->qos = AT_LEAST_ONCE;
        qos_mod = 1;
    }

    Buffer *p_ack = pack_response(response);

    /* Restore original qos */
    if (qos_mod)
        response->qos = AT_MOST_ONCE;

    DEBUG("[%p] Received PUBLISH from %s s=%ld c=%s i=%ld q=%d r=%d d=%d p=%s",
            (void *) pthread_self(), sender, p->size, t->name, id, qos, retain, duplicate, (char *) message);

    /* Add message to the Queue associated to the topic */
    /* store_message(chan, response->id, response->qos, response->sent_count, message, 1); */
    if (retain == 1)
        retain_message(t, response->id, response->qos, response->sent_count, message);

    /* Sent bytes sentinel */
    ssize_t sent = 0;

    /* Iterate through all the subscribers to send them the message */
    list_node *cursor = t->subscribers->head;
    while (cursor) {
        Subscription *sub = (Subscription *) cursor->data;
        /* Check if subscriber has a qos != qos param */
        if (sub->qos > qos) {
            send_rc = sendall(sub->client->fd, p_ack->data, p_ack->size, &sent);
            if (send_rc < 0)
                perror("Can't publish");
        } else {
            send_rc = sendall(sub->client->fd, p->data, p->size, &sent);
            if (send_rc < 0)
                perror("Can't publish");
        }
        update_client_last_action(sub->client);
        total_bytes_sent += sent;
        DEBUG("[%p] Sending PUBLISH to %s p=%s", (void *) pthread_self(), sub->client->id, message);
        cursor = cursor->next;
    }

    buffer_destroy(p);
    buffer_destroy(p_ack);
    free(response->header);
    free(response);
    free((uint8_t *) message);
    free((uint8_t *) topic);

    if (send_rc < 0) return -1;

    return total_bytes_sent;
}


int publish_message2(Topic *t, Message *m, int incr) {
    int total_bytes_sent = 0;
    uint8_t qos_mod = 0;
    int increment = incr < 0 ? 0 : 1;
    const char *topic = append_string(t->name, " ");
    uint8_t duplicate = 0;
    Response *response =
        build_pub_res(m->qos, m->topic, m->payload, increment);
    uint64_t id = response->id;
    Buffer *p = pack_response(response);
    int send_rc = 0;

    /* Prepare packet for AT_LEAST_ONCE subscribers */
    if (response->qos == AT_MOST_ONCE) {
        response->qos = AT_LEAST_ONCE;
        qos_mod = 1;
    }

    Buffer *p_ack = pack_response(response);

    /* Restore original qos */
    if (qos_mod)
        response->qos = AT_MOST_ONCE;

    DEBUG("Received PUBLISH from %s s=%ld c=%s i=%ld q=%d r=%d d=%d",
            m->sender, p->size, t->name, id, m->qos, m->retained, duplicate);

    /* Add message to the Queue associated to the topic */
    /* store_message(chan, response->id, response->qos, response->sent_count, message, 1); */
    if (m->retained == 1)
        retain_message2(t, m);

    /* Sent bytes sentinel */
    ssize_t sent = 0;

    /* Iterate through all the subscribers to send them the message */
    list_node *cursor = t->subscribers->head;
    while (cursor) {
        Subscription *sub = (Subscription *) cursor->data;
        /* Check if subscriber has a qos != qos param */
        if (sub->qos > m->qos) {
            send_rc = sendall(sub->client->fd, p_ack->data, p_ack->size, &sent);
            if (send_rc < 0)
                perror("Can't publish");
        } else {
            send_rc = sendall(sub->client->fd, p->data, p->size, &sent);
            if (send_rc < 0)
                perror("Can't publish");
        }
        update_client_last_action(sub->client);
        total_bytes_sent += sent;
        DEBUG("Sending PUBLISH from %s s=%ld c=%s i=%ld q=%d r=%d d=%d",
                m->sender, p->size, t->name, id, m->qos, m->retained, duplicate);

        cursor = cursor->next;
    }

    buffer_destroy(p);
    buffer_destroy(p_ack);
    free(response->header);
    free(response);
    free((uint8_t *) topic);

    if (send_rc < 0) return -1;

    return total_bytes_sent;
}



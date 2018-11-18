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
#include "channel.h"
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


static int reply_handler(const int, Client *);
static int request_handler(const int, Client *);
static int accept_handler(const int , Client *);
static int close_socket(void *, void *);
static void add_reply(Client *, uint8_t,
        uint8_t, uint8_t, const int, char *, char *);
static void free_reply(Reply *);
static void add_ack_reply(Client *, uint8_t, const int, uint8_t);
static int quit_handler(Client *);
static int join_handler(Client *);
static int join_ack_handler(Client *);
static int connect_handler(Client *);
static int replica_handler(Client *);
static int publish_handler(Client *);
static int pingreq_handler(Client *);
static int subscribe_handler(Client *);
static int unsubscribe_handler(Client *);
static int destroy_clients(void *, void *);
static int destroy_channels(void *, void *);
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
        if ((n += recvbytes(clientfd, rbuf, read_all, HEADLEN)) < 0) {
            shutdown(clientfd, 0);
            close(clientfd);
            // TODO: remove client from global
            return NULL;
        }
    }

    DEBUG("RINGBUFSIZE %d", ringbuf_size(rbuf));

    uint8_t tmp[ringbuf_size(rbuf)];
    uint8_t *bytearray = tmp;

    /* Try to read at least length of the packet */
    for (uint8_t i = 0; i < HEADLEN; i++)
        ringbuf_pop(rbuf, bytearray++);

    uint8_t *typ = (uint8_t *) tmp;
    uint32_t tlen = ntohl(*((uint32_t *) (tmp + sizeof(uint8_t))));

    DEBUG("TLEN %d", tlen);

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
    struct subscriber *sub = (struct subscriber *) kv->val;
    if (sub->fd == fd)
        close(sub->fd);
    return 0;
}


/* Build a reply object and link it to the Client pointer */
static void add_reply(Client *c, uint8_t type, uint8_t qos,
        uint8_t retain, const int fd, char *channel, char *message) {

    Reply *r = malloc(sizeof(Reply));
    if (!r) oom("adding reply");

    r->type = type;
    r->qos = qos;
    r->fd = fd;
    r->retain = retain;
    r->data = message;
    r->channel = channel;
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


static int quit_handler(Client *c) {

    shutdown(c->fd, 0);
    close(c->fd);

    /* Close all fds passed around the structures */
    hashmap_iterate2(global.channels, close_socket, NULL);
    DEBUG("QUIT %s", c->id);
    return -1;
}


static int join_handler(Client *c) {
    if (c->type == REQUEST) {

        /* XXX Dirty hack: Request of client contains the client bus listening port */
        add_reply(c, JACK_REPLY, 0, 0, c->fd, (char *) c->req->ack_data, c->id);

        // XXX for now just insert pointer to client struct
        global.peers = list_head_insert(global.peers, c);
        DEBUG("CLUSTER_JOIN from %s at %s:%s request accepted", c->id, c->addr, c->req->ack_data);
    }
    return 0;
}

/* TODO Should be removed, or at least incorporated with join_handler as a response */
static int join_ack_handler(Client *c) {
    if (c->type == REQUEST) {
        // XXX for now just insert pointer to client struct
        global.peers = list_head_insert(global.peers, c);
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

        global.peers = list_head_insert(global.peers, client);

        /* Add new accepted server to the global hashmap */
        hashmap_put(global.clients, name, client);
        /* Add it to the epoll loop */
        add_epoll(global.bepollfd, fd, client);

        free(mhost);
    }
    return 0;
}


static int connect_handler(Client *c) {

    if (c->type == REQUEST) {
        if (!c->req->sub_id || c->req->sub_id_len == 0) {
            reply_connack(c, c->fd, 0x01);
            DEBUG("Sending CONNACK to %s rc=1", c->req->sub_id);
        } else {

            char *id = append_string("C:", (const char *) c->req->sub_id);
            if (c->req->clean_session == 1) {
                hashmap_del(global.clients, c->id);
                hashmap_put(global.clients, id, c);
                free(c->id);
                c->id = id;
            } else {
                /* Should check if global hashmap contains an entry and return an
                   error code in case of failure, for now we just add the new
                   client */
                hashmap_put(global.clients, id, c);
            }

            DEBUG("Received CONNECT id=%s clean_session=%d", c->req->sub_id, c->req->clean_session);
            reply_connack(c, c->fd, 0x00);
            DEBUG("Sending CONNACK to %s rc=0", c->req->sub_id);
        }

        c->last_action_time = (uint64_t) time(NULL);
    }

    return 0;
}


static int replica_handler(Client *c) {
    if (c->type == REQUEST) {

        void *raw = hashmap_get(global.channels, c->req->channel);
        if (!raw) {
            Channel *channel = create_channel((char *) c->req->channel);
            hashmap_put(global.channels, strdup((char *) c->req->channel), channel);
        }
        Channel *chan = (Channel *) hashmap_get(global.channels, c->req->channel);

        /* Add message to the channel */
        // XXX require new command packet for replica (e.g. save ID etc)
        store_message(chan, 0, c->req->qos, 0, (char *) c->req->message, 0);

        DEBUG("REPLICA channel=%s id=%d qos=%d message=%s",
                c->req->channel, c->req->id, c->req->qos, c->req->message);
    }
    return 1;
}


static int publish_handler(Client *c) {

    if (c->type == REQUEST) {
        if (!c->req->channel || c->req->channel_len == 0) {
            ERROR("Error: missing channel");
        } else {
            add_reply(c, DATA_REPLY, c->req->qos, c->req->retain, c->fd,
                    strdup((char *) c->req->channel), strdup((char *) c->req->message));
        }
    }
    return 1;
}


static int pingreq_handler(Client *c) {
    /* Update last activity timestamp for the client */
    if (c->type == REQUEST) {
        reply_pingresp(c, c->fd);
        c->last_action_time = (uint64_t) time(NULL);
        DEBUG("Sending PINGRESP to %s", c->id);
    }
    return 1;
}


static int subscribe_handler(Client *c) {

    if (c->type == REQUEST) {
        if (!c->req->channel || c->req->channel_len == 0) {

            reply_suback(c, c->fd, 0x01);
            DEBUG("Sending SUBACK id=%s rc=1", c->id);

        } else {

            DEBUG("Received SUBSCRIBE id=%s channel=%s qos=%d", c->id, c->req->channel, c->req->qos);

            reply_suback(c, c->fd, 0x00);

            DEBUG("Sending SUBACK id=%s rc=0", c->id);

            void *raw = hashmap_get(global.channels, c->req->channel);
            if (!raw) {
                Channel *channel = create_channel((char *) c->req->channel);
                hashmap_put(global.channels, strdup((char *) c->req->channel), channel);
            }
            Channel *chan = (Channel *) hashmap_get(global.channels, c->req->channel);

            struct subscriber *sub = malloc(sizeof(struct subscriber));
            if (!sub) oom("creating subscriber");

            sub->fd = c->fd;
            sub->name = c->id;
            sub->qos = c->req->qos;
            add_subscriber(chan, sub);

            /* Send retained messages, if any */
            if (chan->retained) {
                c->reply->retained = chan->retained;
            } else {
                c->reply->retained = NULL;
            }

            // Update subscriptions for the client
            list_head_insert(c->subscriptions, chan->name);
        }
    }

    return 0;
}


static int unsubscribe_handler(Client *c) {

    if (c->type == REQUEST) {
        if (!c->req->channel || c->req->channel_len == 0) {
            /* return err_handler(c, ERR_MISS_CHAN); */
            reply_suback(c, c->fd, 0x01);
            DEBUG("Sending SUBACK id=%s rc=1", c->id);
        } else {

            DEBUG("Received UNSUBSCRIBE id=%s channel=%s", c->id, c->req->channel);

            reply_suback(c, c->fd, 0x00);

            DEBUG("Sending SUBACK id=%s rc=1", c->id);

            void *raw_chan = hashmap_get(global.channels, c->req->channel);

            if (raw_chan) {
                Channel *chan = (Channel *) raw_chan;

                // XXX basic placeholder subscriber
                struct subscriber sub = { c->fd, AT_MOST_ONCE, c->id };
                del_subscriber(chan, &sub);
            }
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
        free(reply->data);
    if (reply->channel)
        free(reply->channel);
}

/* Handle incoming requests, after being accepted or after a reply */
static int request_handler(const int epollfd, Client *client) {

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
        mod_epoll(epollfd, clientfd, EPOLLIN, client);
        ringbuf_free(rbuf);
        return 0;
    }

    if (type == REQUEST)
        read_all = unpack_request(b, &req);
    else if (type == RESPONSE)
        read_all = unpack_response(b, &res);

    buffer_destroy(b);

    /* Free ring buffer as we alredy have all needed informations in memory */
    ringbuf_free(rbuf);

    if (read_all == 1) {
        return -1;
    }

    uint8_t opcode;
    client->type = type;
    client->last_action_time = (uint64_t) time(NULL);

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
            free_reply = commands_hashmap[i].handler(client);
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
        mod_epoll(epollfd, clientfd, EPOLLOUT, client);
    } else {
        client->ctx_handler = request_handler;
        mod_epoll(epollfd, clientfd, EPOLLIN, client);
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
                free(req.channel);
                free(req.message);
            } else {
                free(res.channel);
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
static int reply_handler(const int epollfd, Client *client) {

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
            char *channel = append_string(m->channel, " ");
            Response *r = build_pub_res(m->qos, channel, m->payload, 0);

            r->id = m->id;

            Buffer *p = pack_response(r);
            if (sendall(reply->fd, p->data, p->size, &(ssize_t) { 0 }) < 0) {
                perror("send(2): error sending\n");
            }

            free(channel);
            buffer_destroy(p);
            free(r);
        }

    } else if (reply->type == JACK_REPLY) {

        /* Dirty hack: we should now send host:port pairs in order to update
           all the cluster with the new member, excluding the self and himself */
        /* So first we retrive the id of the newly joined member */
        char *new_member_id = reply->data;
        char *new_member_port = reply->channel;

        /* Next we must populate the string with the host:port pairs */
        list_node *cur = global.peers->head;

        while (cur) {

            Client *cli = (Client *) cur->data;
            if (strcasecmp(cli->id, new_member_id) != 0) {

                /* Make string host:pair, this one will be sent to all matching peers, e.g. all
                   other that are no aware of the new joined member */
                char *hostcolon = append_string(client->addr, ":");
                char *pair = append_string(hostcolon, new_member_port);

                /* Pack request and send it to other peers so they can connect to the new member */
                Request *join_ack = build_ack_req(CLUSTER_JOIN_ACK, pair);
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

        void *raw_subs = hashmap_get(global.channels, reply->channel);
        /* If not channel is found, we create it and store in the global hashmap */
        if (!raw_subs) {
            Channel *channel = create_channel(reply->channel);
            hashmap_put(global.channels, strdup(reply->channel), channel);
        }
        /* Retrieve the channel to publish data to by name */
        Channel *chan = (Channel *) hashmap_get(global.channels, reply->channel);

        double tic = clock();
        sent = publish_message(chan, reply->qos, reply->retain, strdup(reply->data), 0);
        double elapsed = (clock() - tic) / CLOCKS_PER_SEC;
        int load = (sent / elapsed);

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
    mod_epoll(epollfd, client->fd, EPOLLIN, client);
    return ret;
}

/* Handle new connection, create a a fresh new Client structure and link it
   to the fd, ready to be set in EPOLLIN event */
static int accept_handler(const int epollfd, Client *server) {
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
    client->last_action_time = (uint64_t) time(NULL);
    client->ctx_handler = request_handler;

    const char *id = random_name(16);
    char *name = append_string("C:", id);  // C states that it is a client (could be another sizigy instance)
    free((void *) id);

    client->id = name;
    client->reply = NULL;
    client->subscriptions = list_create();

    /* If the socket is listening in the bus port, add the connection to the peers */
    if (sin.sin_port == global.bus_port) {
        global.peers = list_head_insert(global.peers, client);
    }

    /* Add new accepted server to the global hashmap */
    hashmap_put(global.clients, name, client);

    /* Add it to the epoll loop */
    add_epoll(epollfd, clientsock, client);

    /* Rearm server fd to accept new connections */
    mod_epoll(epollfd, fd, EPOLLIN, server);

    return 0;
}


static int check_client_status(void *t1, void *t2) {
    hashmap_entry *kv = (hashmap_entry *) t2;
    uint64_t now = (uint64_t) time(NULL);
    if (kv) {
        // free value field
        if (kv->val) {
            Client *c = (Client *) kv->val;
            if (c->status == ONLINE && (now - c->last_action_time) >= c->keepalive) {
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
    /* Iterate through all clients in the global hashmap every second in order to
       check last activity timestamp */
    while (1) {
        hashmap_iterate2(global.clients, check_client_status, NULL);
        sleep(1);
    }
    return NULL;
}

/* Main worker function, his responsibility is to wait on events on a shared
   EPOLL fd, use the same way for clients or peer to distribute messages */
static void *worker(void *args) {
    struct socks *fds = (struct socks *) args;
    struct epoll_event *evs = malloc(sizeof(*evs) * MAX_EVENTS);

    if (!evs) {
        perror("malloc(3) failed");
        pthread_exit(NULL);
    }

    int events_cnt;
    while ((events_cnt = epoll_wait(fds->epollfd, evs, MAX_EVENTS, -1)) > 0) {
        for (int i = 0; i < events_cnt; i++) {

            /* Check for errors first */
            if ((evs[i].events & EPOLLERR) ||
                    (evs[i].events & EPOLLHUP) ||
                    (!(evs[i].events & EPOLLIN) && !(evs[i].events & EPOLLOUT))) {

                /* An error has occured on this fd, or the socket is not
                   ready for reading */
                perror ("epoll_wait(2)");
                hashmap_iterate2(global.channels, close_socket, NULL);

                close(evs[i].data.fd);
                continue;
            } else if (evs[i].data.fd == global.run) {

                /* And quit event after that */
                eventfd_t val;
                eventfd_read(global.run, &val);

                DEBUG("Stopping epoll loop. Thread %p exiting.", (void *) pthread_self());

                goto exit;
            } else {
                /* Finally handle the request according to its type */
                ((Client *) evs[i].data.ptr)->ctx_handler(fds->epollfd, evs[i].data.ptr);
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
            Channel *c = (Channel *) kv->val;
            QueueItem *item = c->messages->front;
            while (item) {
                Message *m = (Message *) item->data;
                if (m->payload)
                    free(m->payload);
                free(m);
                item = item->next;
            }
        }
    } else return HASHMAP_ERR;
    return HASHMAP_OK;
}


static int destroy_channels(void *t1, void *t2) {
    hashmap_entry *kv = (hashmap_entry *) t2;
    if (kv) {
        // free value field
        if (kv->val) {
            Channel *c = (Channel *) kv->val;
            destroy_channel(c);
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
    global.channels = hashmap_create();
    global.ack_waiting = hashmap_create();
    global.clients = hashmap_create();
    global.peers = list_create();
    global.next_id = init_atomic();  // counter to get message id, should be enclosed inside locks
    global.throughput = init_atomic();
    global.throttler = init_throttler();
    pthread_mutex_init(&(global.lock), NULL);
    global.keepalive = 60;

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
        .last_action_time = (uint64_t) time(NULL),
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
        .last_action_time = (uint64_t) time(NULL),
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

    INFO("Sizigy v0.5.1");
    INFO("Starting server on %s:%s", addr, port);

    Client node = {
        .type = REQUEST,
        .status = ONLINE,
        .keepalive = global.keepalive,  // FIXME: Placeholder
        .addr = addr,
        .fd = node_fd,
        .ctx_handler = request_handler,
        .last_action_time = (uint64_t) time(NULL),
        .id = "node",
        .reply = NULL,
        .subscriptions = list_create(),
        { NULL }
    };

    /* Add eventual connected node */
    if (node_fd > 0) {
        add_epoll(bepollfd, node_fd, &node);
        /* Ask for joining the cluster */
        Request *join_req_packet = build_ack_req(CLUSTER_JOIN, bus_port);
        Buffer *p = pack_request(join_req_packet);
        int rc = sendall(node_fd, p->data, p->size, &(ssize_t) { 0 });
        if (rc < 0)
            printf("Failed join\n");
        free(join_req_packet);
        buffer_destroy(p);
    }

    pthread_t keepalive_thread;

    pthread_create(&keepalive_thread, NULL, keepalive, NULL);

    /* Use main thread as a worker too */
    worker(&fds);

    for (int i = 0; i < EPOLL_WORKERS; ++i)
        pthread_join(workers[i], NULL);

    /* Dedicated thread to bus communications, e.g. distribution */
    pthread_join(bus_worker, NULL);

cleanup:
    /* Free all resources allocated */
    hashmap_iterate2(global.channels, destroy_queue_data, NULL);
    hashmap_iterate2(global.channels, destroy_channels, NULL);
    hashmap_iterate2(global.clients, destroy_clients, NULL);
    free(server.subscriptions);
    free(bus.subscriptions);
    free(node.subscriptions);
    list_release(global.peers, 1);
    free(global.next_id);
    free(global.throughput);
    free(global.throttler);
    pthread_mutex_destroy(&(global.lock));
    DEBUG("Bye\n");
    return 0;
}

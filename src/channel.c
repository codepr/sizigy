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
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include "map.h"
#include "util.h"
#include "server.h"
#include "network.h"
#include "channel.h"


static int sub_compare(void *arg1, void *arg2) {
    list_node *node1 = (list_node *) arg1;
    list_node *node2 = (list_node *) arg2;
    struct subscriber *sub1 = (struct subscriber *) node1->data;
    struct subscriber *sub2 = (struct subscriber *) node2->data;
    // FIXME should be && in place of ||
    if (sub1->fd == sub2->fd)
        return 0;
    return 1;
}


Channel *create_channel(char *name) {

    Channel *channel = malloc(sizeof(Channel));
    if (!channel) oom("creating channel");

    channel->name = strdup(name);
    channel->subscribers = list_create();
    channel->messages = create_queue();
    channel->retained = NULL;

    return channel;
}


void add_subscriber(Channel *chan, struct subscriber *subscriber) {
    chan->subscribers = list_head_insert(chan->subscribers, subscriber);
}


void del_subscriber(Channel *chan, struct subscriber *subscriber) {
    list_node node = { subscriber, NULL };
    chan->subscribers->head = list_remove_node(chan->subscribers->head, &node, sub_compare);
}


void retain_message(Channel *channel, const uint64_t id,
        uint8_t qos, uint8_t dup, const char *payload) {

    Message *m = malloc(sizeof(Message));
    if (!m) oom("creating message to be stored");

    m->creation_time = time(NULL);
    m->id = id;
    m->qos = qos;
    m->dup = dup;
    m->payload = strdup(payload);
    m->channel = channel->name;
    m->retained = 1;
    channel->retained = m;

}


void store_message(Channel *channel, const uint64_t id,
        uint8_t qos, uint8_t dup, const char *payload, int check_peers) {

    Message *m = malloc(sizeof(Message));
    if (!m) oom("creating message to be stored");

    m->creation_time = time(NULL);
    m->id = id;
    m->qos = qos;
    m->dup = dup;
    m->payload = strdup(payload);
    m->channel = channel->name;
    enqueue(channel->messages, m);

    // Check if cluster has members and spread the replicas
    if (check_peers == 1 && global.peers->len > 0) {
        char *m = append_string(channel->name, " ");
        Request *replica_r = build_rep_req(qos, m, (char *) payload);
        Buffer *p = pack_request(replica_r);
        list_node *cur = global.peers->head;

        while (cur) {
            Client *c = (Client *) cur->data;
            sendall(c->fd, p->data, p->size, &(ssize_t) { 0 });
            cur = cur->next;
        }

        free(m);
        free(replica_r->header);
        free(replica_r);
        free_packed(p);
    }
}


int publish_message(Channel *chan, uint8_t qos, uint8_t retain, void *message, int incr) {
    int total_bytes_sent = 0;
    uint8_t qos_mod = 0;
    int increment = incr < 0 ? 0 : 1;
    char *channel = append_string(chan->name, " ");
    uint8_t duplicate = 0;
    Response *response = build_pub_res(qos, channel, message, increment);
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

    DEBUG("Received PUBLISH size=%ld ch=%s id=%ld qos=%d rt=%d dup=%d p=%s",
            p->size, chan->name, id, qos, retain, duplicate, (char *) message);

    /* Add message to the Queue associated to the channel */
    /* store_message(chan, response->id, response->qos, response->sent_count, message, 1); */
    if (retain == 1)
        retain_message(chan, response->id, response->qos, response->sent_count, message);

    /* Sent bytes sentinel */
    ssize_t sent = 0;

    /* Iterate through all the subscribers to send them the message */
    list_node *cursor = chan->subscribers->head;
    while (cursor) {
        struct subscriber *sub = (struct subscriber *) cursor->data;
        /* Check if subscriber has a qos != qos param */
        if (sub->qos > qos) {
            send_rc = sendall(sub->fd, p_ack->data, p_ack->size, &sent);
            if (send_rc < 0)
                perror("Can't publish");
            total_bytes_sent += sent;
        } else {
            send_rc = sendall(sub->fd, p->data, p->size, &sent);
            if (send_rc < 0)
                perror("Can't publish");
            total_bytes_sent += sent;
        }
        DEBUG("Sending PUBLISH to %s p=%s", sub->name, message);
        cursor = cursor->next;
    }
    free_packed(p);
    free_packed(p_ack);
    free(response->header);
    free(response);
    free(channel);
    free(message);
    return total_bytes_sent;
}


void destroy_channel(Channel *chan) {
    if (chan->name)
        free(chan->name);
    if (chan->subscribers)
        list_release(chan->subscribers, 1);
    if (chan->messages)
        release_queue(chan->messages);
    free(chan);
}

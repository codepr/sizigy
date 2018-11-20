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

#include <string.h>
#include "sizigy.h"


void add_client(SizigyDB *db, Client *c) {
    hashmap_put(db->clients, c->id, c);
}


void del_client(SizigyDB *db, Client *c) {
    hashmap_del(db->clients, c->id);
}


void update_client_last_action(Client *c) {
    write_atomic(c->last_action_time, (const uint64_t) time(NULL));
}


uint64_t get_client_last_action(Client *c) {
    return read_atomic(c->last_action_time);
}

/* TOPIC */

Topic *create_topic(char *name) {

    Topic *topic = malloc(sizeof(Topic));
    if (!topic) oom("creating topic");

    topic->name = strdup(name);
    topic->subscribers = list_create();
    topic->messages = create_queue();
    topic->retained = NULL;

    return topic;
}


void destroy_topic(Topic *t) {
    if (t->name)
        free((char *) t->name);
    if (t->subscribers)
        list_release(t->subscribers, 1);
    if (t->messages)
        release_queue(t->messages);
    if (t->retained)
        free(t->retained);
    free(t);
}


static int sub_compare(void *arg1, void *arg2) {
    list_node *node1 = (list_node *) arg1;
    list_node *node2 = (list_node *) arg2;
    Subscription *sub1 = (Subscription *) node1->data;
    Subscription *sub2 = (Subscription *) node2->data;
    // FIXME should be && in place of ||
    if (sub1->client->fd == sub2->client->fd)
        return 0;
    return 1;
}


void add_topic(SizigyDB *db, char *name) {
    Topic *t = create_topic(name);
    hashmap_put(db->topics, name, t);
}


void add_subscriber(SizigyDB *db, Subscription *s) {
    Topic *topic = (Topic *) hashmap_get(db->topics, (void *) s->topic);
    pthread_mutex_lock(&db->lock);
    topic->subscribers = list_head_insert(topic->subscribers, s);
    pthread_mutex_unlock(&db->lock);
}


void del_subscriber(SizigyDB *db, Subscription *s) {
    Topic *topic = (Topic *) hashmap_get(db->topics, (void *) s->topic);
    pthread_mutex_lock(&db->lock);
    list_node node = { s, NULL };
    topic->subscribers->head =
        list_remove_node(topic->subscribers->head, &node, sub_compare);
    pthread_mutex_unlock(&db->lock);
}


Subscription *create_subscription(Client *c, const char *topic, uint8_t qos) {
    Subscription *s = malloc(sizeof(Subscription));
    s->client = c;
    s->topic = (const uint8_t *) topic;
    s->qos = qos;
    return s;
}


void destroy_subscription(Subscription *s) {
    if (s->topic) free((void *) s->topic);
    free(s);
}


Message *create_message(uint8_t qos, uint8_t retain, uint8_t dup, const uint8_t *topic, const uint8_t *payload, const uint8_t *sender) {

    Message *m = malloc(sizeof(Message));
    m->qos = qos;
    m->dup = dup;
    m->sender = sender;
    m->topic = topic;
    m->payload = payload;
    m->retained = retain;
    m->creation_time = time(NULL);

    return m;
}


void destroy_message(Message *m) {
    free((uint8_t *) m->sender);
    free((uint8_t *) m->topic);
    free((uint8_t *) m->payload);
    free(m);
}

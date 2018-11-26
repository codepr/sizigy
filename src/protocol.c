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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include "util.h"
#include "server.h"
#include "network.h"
#include "protocol.h"


static void pack_header(Header *, Buffer *);
static void unpack_header(Buffer *, Header *);


/* Init Buffer data structure, to ease byte arrays handling */
Buffer *buffer_init(const size_t len) {
    Buffer *b = malloc(sizeof(Buffer));
    b->data = malloc(len);
    if (!b || !b->data) oom("allocating memory for new buffer");
    b->size = len;
    b->pos = 0;
    return b;
}


/* Destroy a previously allocated Buffer structure */
void buffer_destroy(Buffer *b) {
    assert(b);
    assert(b->data);
    b->size = b->pos = 0;
    free(b->data);
    free(b);
}


// Reading data
uint8_t read_uint8(Buffer *b) {
    uint8_t val = *(b->data + b->pos);
    b->pos += sizeof(uint8_t);
    return val;
}


uint16_t read_uint16(Buffer *b) {
    uint16_t val = ntohs(*((uint16_t *) (b->data + b->pos)));
    b->pos += sizeof(uint16_t);
    return val;
}


uint32_t read_uint32(Buffer *b) {
    uint32_t val = ntohl(*((uint32_t *) (b->data + b->pos)));
    b->pos += sizeof(uint32_t);
    return val;
}


uint64_t read_uint64(Buffer *b) {
    uint64_t val = ntohll(b->data + b->pos);
    b->pos += sizeof(uint64_t);
    return val;
}


uint8_t *read_string(Buffer *b, size_t len) {
    uint8_t *str = malloc(len + 1);
    memcpy(str, b->data + b->pos, len);
    str[len] = '\0';
    b->pos += len;
    return str;
}


// Write data
void write_uint8(Buffer *b, uint8_t val) {
    *(b->data + b->pos) = val;
    b->pos += sizeof(uint8_t);
}


void write_uint16(Buffer *b, uint16_t val) {
    *((uint16_t *) (b->data + b->pos)) = htons(val);
    b->pos += sizeof(uint16_t);
}


void write_uint32(Buffer *b, uint32_t val) {
    *((uint32_t *) (b->data + b->pos)) = htonl(val);
    b->pos += sizeof(uint32_t);
}


void write_uint64(Buffer *b, uint64_t val) {
    htonll(b->data + b->pos, val);
    b->pos += sizeof(uint64_t);
}


void write_string(Buffer *b, uint8_t *str) {
    size_t len = strlen((char *) str);
    memcpy(b->data + b->pos, str, len);
    b->pos += len;
}


/* Advance internal position index of a Buffer by `offset` steps */
void advance_pos(Buffer *b, size_t offset) {

    assert(b);
    assert(b->pos + offset <= b->size);

    b->pos += offset;
}

// REFACTOR

static void pack_header(Header *h, Buffer *b) {

    assert(b);
    assert(h);

    write_uint8(b, h->opcode);
    write_uint32(b, b->size);
}


static void unpack_header(Buffer *b, Header *h) {

    assert(b);
    assert(h);

    h->opcode = read_uint8(b);
    h->size = read_uint32(b);
}


int8_t unpack_connect(Buffer *b, Connect *pkt) {

    assert(b);
    assert(pkt);

    /* Start unpacking bytes into the Request structure */

    pkt->header = malloc(sizeof(Header));
    if (!pkt->header)
        return -EOOM;

    unpack_header(b, pkt->header);

    pkt->idlen = read_uint16(b);
    pkt->clean_session = read_uint8(b);
    pkt->keepalive = read_uint16(b);
    pkt->id = read_string(b, pkt->idlen);

    return OK;
}


int8_t unpack_subscribe(Buffer *b, Subscribe *s) {

    assert(b);
    assert(s);

    /* Start unpacking bytes into the Request structure */

    s->header = malloc(sizeof(Header));
    if (!s->header)
        return -EOOM;

    unpack_header(b, s->header);

    s->topic_len = read_uint16(b);
    s->qos = read_uint8(b);
    s->topic = read_string(b, s->topic_len);

    return OK;
}


int8_t unpack_unsubscribe(Buffer *b, Unsubscribe *u) {

    assert(b);
    assert(u);

    u->header = malloc(sizeof(*u->header));
    if (!u->header)
        return -EOOM;

    unpack_header(b, u->header);

    u->topic_len = read_uint16(b);
    u->topic = read_string(b, u->topic_len);

    return OK;
}


int8_t unpack_publish(Buffer *b, Publish *p) {

    assert(b);
    assert(p);

    /* Start unpacking bytes into the Request structure */

    p->header = malloc(sizeof(Header));
    if (!p->header)
        return -EOOM;

    unpack_header(b, p->header);

    p->topic_len = read_uint16(b);
    p->message_len = read_uint32(b);
    p->qos = read_uint8(b);
    p->retain = read_uint8(b);
    p->dup = read_uint8(b);
    p->topic = read_string(b, p->topic_len);
    p->message = read_string(b, p->message_len);

    return OK;
}


int8_t unpack_ack(Buffer *b, Ack *a) {

    assert(b);
    assert(a);

    /* Start unpacking bytes into the Request structure */

    a->header = malloc(sizeof(Header));
    if (!a->header)
        return -EOOM;

    unpack_header(b, a->header);

    a->id = read_uint16(b);
    a->rc = read_uint8(b);

    return OK;
}


Connect *connect_pkt(const uint8_t *id,
        const uint8_t keepalive, const uint8_t clean_session) {

    assert(id);

    Connect *pkt = malloc(sizeof(Connect));
    if (!pkt) oom("building ack request");

    pkt->header = malloc(sizeof(Header));
    if (!pkt->header) oom ("building header for ack request");

    pkt->header->opcode = CONNECT;
    pkt->header->size = HEADERLEN + strlen((char *) id) +
        (2 * sizeof(uint16_t)) + (2 * sizeof(uint8_t));
    pkt->idlen = strlen((char *) id);
    pkt->id = (uint8_t *) id;
    pkt->keepalive = keepalive;
    pkt->clean_session = clean_session;

    return pkt;
}


Subscribe *subscribe_pkt(const uint8_t *topic, const uint8_t qos) {

    assert(topic);

    Subscribe *pkt = malloc(sizeof(Subscribe));
    if (!pkt) oom("building connect request");

    pkt->header = malloc(sizeof(Header));
    if (!pkt->header) oom("building connect header");

    pkt->header->opcode = SUBSCRIBE;
    pkt->header->size = HEADERLEN + strlen((char *) topic) +
        sizeof(uint16_t) + (3 * sizeof(uint8_t));
    pkt->topic_len = strlen((char *) topic);
    pkt->qos = qos;
    pkt->topic = (uint8_t *) topic;

    return pkt;
}


Unsubscribe *unsubscribe_pkt(const uint8_t *topic) {

    assert(topic);

    Unsubscribe *pkt = malloc(sizeof(*pkt));
    if (!pkt) oom("building connect request");

    pkt->header = malloc(sizeof(Header));
    if (!pkt->header) oom("building connect header");

    pkt->header->opcode = UNSUBSCRIBE;
    pkt->header->size = HEADERLEN + strlen((char *) topic) +
        sizeof(uint16_t) + sizeof(uint8_t);
    pkt->topic_len = strlen((char *) topic);
    pkt->topic = (uint8_t *) topic;

    return pkt;
}


Publish *publish_pkt(const uint8_t *topic, const uint8_t *message,
        const uint8_t qos, const uint8_t retain, const uint8_t dup) {

    assert(topic);
    assert(message);

    Publish *pkt = malloc(sizeof(Publish));
    if (!pkt) oom("building unsubscribe request");

    pkt->header = malloc(sizeof(Header));
    if (!pkt->header) oom("building unsubscribe header");

    pkt->header->opcode = PUBLISH;
    pkt->header->size = HEADERLEN + strlen((char *) topic) +
        strlen((char *) message) + sizeof(uint16_t) + sizeof(uint32_t) + (5 * sizeof(uint8_t));
    pkt->topic_len = strlen((char *) topic);
    pkt->message_len = strlen((char *) message);
    pkt->qos = qos;
    pkt->retain = retain;
    pkt->dup = dup;
    pkt->topic = (uint8_t *) topic;
    pkt->message = (uint8_t *) message;

    return pkt;
}


Ack *ack_pkt(const uint8_t opcode, const uint16_t id, const uint8_t rc) {

    Ack *pkt = malloc(sizeof(Ack));
    if (!pkt) oom("building subscribe request");

    pkt->header = malloc(sizeof(Header));
    if (!pkt->header) oom("building header of subscribe request");

    pkt->header->opcode = opcode;
    pkt->header->size = HEADERLEN + sizeof(uint16_t) + sizeof(uint8_t);
    pkt->id = id;
    pkt->rc = rc;

    return pkt;
}


void pack_connect(Buffer *b, Connect *pkt) {

    assert(b);
    assert(pkt);

    pack_header(pkt->header, b);

    write_uint16(b, strlen((char *) pkt->id));
    write_string(b, pkt->id);
    write_uint16(b, pkt->keepalive);  // FIXME: Placeholder
    write_uint8(b, pkt->clean_session);
}


void pack_subscribe(Buffer *b, Subscribe *pkt) {

    assert(b);
    assert(pkt);

    pack_header(pkt->header, b);

    write_uint16(b, strlen((char *) pkt->topic));
    write_uint8(b, pkt->qos);
    write_string(b, pkt->topic);
}


void pack_unsubscribe(Buffer *b, Unsubscribe *u) {

    assert(b);
    assert(u);

    pack_header(u->header, b);

    write_uint16(b, u->topic_len);
    write_string(b, u->topic);
}


void pack_publish(Buffer *b, Publish *pkt) {

    assert(b);
    assert(pkt);

    pack_header(pkt->header, b);

    write_uint16(b, strlen((char *) pkt->topic));
    write_uint32(b, strlen((char *) pkt->message));
    write_uint8(b, pkt->qos);
    write_uint8(b, pkt->retain);
    write_uint8(b, pkt->dup);
    write_string(b, pkt->topic);
    write_string(b, pkt->message);
}


void pack_ack(Buffer *b, Ack *pkt) {

    assert(b);
    assert(pkt);

    pack_header(pkt->header, b);

    write_uint16(b, pkt->id);
    write_uint8(b, pkt->rc);
}


void free_ack(Ack *a) {
    if (!a)
        return;
    if (a->header) {
        free(a->header);
        a->header = NULL;
    }
    free(a);
    a = NULL;
}


void free_connect(Connect *c) {
    if (!c)
        return;
    if (c->header) {
        free(c->header);
        c->header = NULL;
    }
    if (c->id) {
        free(c->id);
        c->id = NULL;
    }
    free(c);
    c = NULL;
}


void free_publish(Publish *p) {
    if (!p)
        return;
    if (p->header) {
        free(p->header);
        p->header = NULL;
    }
    if (p->topic) {
        free(p->topic);
        p->topic = NULL;
    }
    if (p->message) {
        free(p->message);
        p->message = NULL;
    }
    free(p);
    p = NULL;
}


void free_subscribe(Subscribe *s) {
    if (!s)
        return;
    if (s->header) {
        free(s->header);
        s->header = NULL;
    }
    if (s->topic) {
        free(s->topic);
        s->topic = NULL;
    }
    free(s);
    s = NULL;
}


void free_unsubscribe(Unsubscribe *u) {
    if (!u)
        return;
    if (u->header) {
        free(u->header);
        u->header = NULL;
    }
    if (u->topic) {
        free(u->topic);
        u->topic = NULL;
    }
    free(u);
    u = NULL;
}

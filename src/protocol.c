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
static void advance_pos(Buffer *, size_t);


static void pack_header(Header *h, Buffer *b) {

    assert(h);

    write_uint8(b, h->type);
    write_uint32(b, b->size);
    write_uint8(b, h->opcode);
    write_uint32(b, h->data_len);
}


static void pack_connect_packet(Buffer *b,
        uint8_t clean_session, uint8_t *sub_id) {

    assert(b);

    write_uint16(b, strlen((char *) sub_id));
    write_uint8(b, clean_session);
    write_uint16(b, 60);  // FIXME: Placeholder
    write_string(b, sub_id);
}


static void pack_subscribe_packet(Buffer *b, uint8_t qos,
        uint8_t retain, uint8_t *channel, uint8_t *message) {

    assert(b);

    write_uint16(b, strlen((char *) channel));
    write_uint32(b, strlen((char *) message));
    write_uint8(b, retain);
    write_uint8(b, qos);
    write_string(b, channel);
    write_string(b, message);
}


static void pack_ack_packet(Buffer *b, uint64_t id, uint8_t *data) {

    assert(b);

    write_uint16(b, strlen((char *) data));
    write_uint64(b, id);
    write_string(b, data);
}


Buffer *pack_request(Request *request) {

    assert(request);

    uint32_t tlen = 0;
    Buffer *b = NULL;

    switch (request->header->opcode) {
        case CONNECT:
            // move index after data size value, where opcode start
            tlen = HEADERLEN + sizeof(uint16_t) +
                sizeof(uint8_t) + request->header->data_len;
            b = buffer_init(tlen);
            pack_header(request->header, b);
            pack_connect_packet(b, request->clean_session, request->sub_id);
            break;
        case REPLICA:
        case PUBLISH:
        case SUBSCRIBE:
            tlen = HEADERLEN + sizeof(uint16_t) +
                (2 * sizeof(uint8_t)) + sizeof(uint32_t) +
                request->channel_len + request->message_len;
            b = buffer_init(tlen);
            pack_header(request->header, b);
            pack_subscribe_packet(b, request->qos,
                    request->retain, request->channel, request->message);
            break;
        case QUIT:
        case PINGREQ:
        case CLUSTER_JOIN:
        case CLUSTER_JOIN_ACK:
            tlen = HEADERLEN + sizeof(uint16_t) +
                sizeof(uint64_t) + request->ack_len;
            b = buffer_init(tlen);
            pack_header(request->header, b);
            pack_ack_packet(b, request->id, request->ack_data);
            break;
        case UNSUBSCRIBE:
            tlen = HEADERLEN + request->header->data_len;
            b = buffer_init(tlen);
            pack_header(request->header, b);
            write_string(b, request->data);
            break;
    }

    return b;
}


int8_t unpack_request(Buffer *b, Request *r) {

    assert(b);
    assert(r);

    /* Start unpacking bytes into the Request structure */

    r->header = malloc(sizeof(Header));
    if (!r->header) oom("unpacking request header");

    r->header->type = read_uint8(b);
    advance_pos(b, sizeof(uint32_t));
    r->header->opcode = read_uint8(b);
    r->header->data_len = read_uint32(b);

    switch (r->header->opcode) {
        case CONNECT:
            r->sub_id_len = read_uint16(b);
            r->clean_session = read_uint8(b);
            r->keepalive = read_uint16(b);
            r->sub_id = read_string(b, r->sub_id_len);
            break;
        case REPLICA:
        case PUBLISH:
        case SUBSCRIBE:
            r->channel_len = read_uint16(b);
            r->message_len = read_uint32(b);
            r->qos = read_uint8(b);
            r->retain = read_uint8(b);
            r->channel = read_string(b, r->channel_len);
            r->message = read_string(b, r->message_len);
            break;
        case CLUSTER_JOIN:
        case CLUSTER_JOIN_ACK:
            r->ack_len = read_uint16(b);
            r->id = read_uint64(b);
            r->ack_data = read_string(b, r->ack_len);
            break;
        case UNSUBSCRIBE:
            r->data = read_string(b, r->header->data_len);
            break;
    }

    return 0;
}


Buffer *pack_response(Response *response) {

    assert(response);

    Buffer *b = NULL;
    size_t tlen = 0;

    switch (response->header->opcode) {
        case REPLICA:
        case PUBLISH:
            tlen = HEADERLEN + sizeof(uint32_t) + response->channel_len +
                response->message_len + sizeof(uint16_t) +
                (2 * sizeof(uint8_t)) + sizeof(uint64_t);
            b = buffer_init(tlen);
            pack_header(response->header, b);
            write_uint16(b, response->channel_len);
            write_uint32(b, response->message_len);
            write_uint8(b, response->qos);
            write_uint8(b, response->sent_count);
            write_uint64(b, response->id);
            write_string(b, response->channel);
            write_string(b, response->message);
            break;
        case CONNACK:
        case SUBACK:
        case PUBACK:
        case PINGRESP:
        case CLUSTER_JOIN:
        case CLUSTER_JOIN_ACK:
            tlen = HEADERLEN + response->header->data_len + sizeof(uint8_t);
            b = buffer_init(tlen);
            pack_header(response->header, b);
            write_uint8(b, response->rc);
            break;
    }

    return b;
}


int8_t unpack_response(Buffer *b, Response *r) {

    assert(r);

    /* Start unpacking bytes into the protocol_packet_t structure */

    r->header = malloc(sizeof(Header));
    if (!r->header) oom("unpacking response");

    r->header->type = read_uint8(b);
    advance_pos(b, sizeof(uint32_t));
    r->header->opcode = read_uint8(b);
    r->header->data_len = read_uint32(b);

    switch (r->header->opcode) {
        case PUBLISH:
            r->channel_len = read_uint16(b);
            r->message_len = read_uint32(b);
            r->qos = read_uint8(b);
            r->sent_count = read_uint8(b);
            r->id = read_uint64(b);
            r->channel = read_string(b, r->channel_len);
            r->message = read_string(b, r->message_len);
            break;
        case SUBACK:
        case PINGRESP:
        case CLUSTER_JOIN:
        case CLUSTER_JOIN_ACK:
            r->rc = read_uint16(b);
            break;
    }

    return 0;
}


Request *build_ack_request(uint8_t type,
        uint8_t opcode, uint64_t id, char *data) {

    Request *r = malloc(sizeof(Request));
    if (!r) oom("building ack request");

    r->header = malloc(sizeof(Header));
    if (!r->header) oom ("building header for ack request");

    r->header->type = type;
    r->header->opcode = opcode;
    r->header->data_len = r->ack_len = strlen(data);
    r->id = id;
    r->ack_data = (uint8_t *) data;

    return r;
}


Request *build_connect_request(uint8_t type,
        uint8_t opcode, uint8_t clean_session, char *sub_id) {

    Request *r = malloc(sizeof(Request));
    if (!r) oom("building connect request");

    r->header = malloc(sizeof(Header));
    if (!r->header) oom("building connect header");

    r->header->type = type;
    r->header->opcode = opcode;
    r->clean_session = clean_session;
    r->header->data_len = r->sub_id_len = strlen(sub_id);
    r->sub_id = (uint8_t *) sub_id;
    r->keepalive = 60;  // FIXME: Placeholder

    return r;
}


Request *build_unsubscribe_request(uint8_t type, uint8_t opcode, char *data) {

    Request *r = malloc(sizeof(Request));
    if (!r) oom("building unsubscribe request");

    r->header = malloc(sizeof(Header));
    if (!r->header) oom("building unsubscribe header");

    r->header->type = type;
    r->header->opcode = opcode;
    r->header->data_len = strlen(data);
    r->data = (uint8_t *) data;

    return r;
}


Request *build_subscribe_request(uint8_t type, uint8_t opcode,
        uint8_t qos, char *channel_name, char *message) {

    Request *r = malloc(sizeof(Request));
    if (!r) oom("building subscribe request");

    r->header = malloc(sizeof(Header));
    if (!r->header) oom("building header of subscribe request");

    uint16_t clen = strlen(channel_name);
    uint32_t mlen = strlen(message);

    r->header->type = type;
    r->header->opcode = opcode;
    r->header->data_len = clen + mlen;
    r->qos = qos;
    r->retain = 0;
    r->channel_len = clen;
    r->message_len = mlen;
    r->channel = (uint8_t *) channel_name;
    r->message = (uint8_t *) message;

    return r;
}


Response *build_publish_response(uint8_t type, uint8_t opcode,
        uint8_t qos, char *channel_name, char *message, uint8_t incr) {

    uint64_t id = read_atomic(global.next_id);
    if (incr == 1) {
        id = incr_read_atomic(global.next_id);
    }

    Response *r = malloc(sizeof(Response));
    if (!r) oom("building publish request");

    r->header = malloc(sizeof(Header));
    if (!r->header) oom("building header of publish request");

    uint16_t clen = strlen(channel_name);
    uint32_t mlen = strlen(message);

    r->header->type = type;
    r->header->opcode = opcode;
    r->header->data_len = clen + mlen;
    r->qos = qos;
    r->id = id;
    r->sent_count = 0;
    r->channel_len = clen;
    r->message_len = mlen;
    r->channel = (uint8_t *) channel_name;
    r->message = (uint8_t *) message;

    return r;
}


Response *build_ack_response(uint8_t type, uint8_t opcode, uint8_t rc) {

    Response *r = malloc(sizeof(Response));
    if (!r) oom("building ack request");

    r->header = malloc(sizeof(Header));
    if (!r->header) oom("building header of ack request");

    r->header->type = type;
    r->header->opcode = opcode;
    r->header->data_len = 0;
    r->rc = rc;

    return r;
}


/* Init Buffer data structure, to ease byte arrays handling */
Buffer *buffer_init(size_t len) {
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

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


static uint8_t *pack_header(Header *h, uint32_t total_len) {

    // bytes to be allocated + size of the data field
    uint8_t *raw = malloc(total_len);
    if (!raw) oom("packing header");

    uint8_t *type = raw;
    uint8_t *tot = raw + sizeof(uint8_t);
    uint8_t *opcode = tot + sizeof(uint32_t);

    // fix index just after size of the data part
    uint8_t *datalen = opcode + sizeof(uint8_t);

    // pack the whole structure
    *type = h->type;
    *((uint32_t *) tot) = htonl(total_len);
    *opcode = h->opcode;
    *((uint32_t *) datalen) = htonl(h->data_len);

    return raw;
}


static void pack_connect_packet(uint8_t *ptr,
        uint16_t id_len, uint8_t clean_session, uint8_t *sub_id) {

    assert(ptr);

    uint8_t *sublen = ptr;
    uint8_t *cs = ptr + sizeof(uint16_t);
    uint8_t *data = cs + sizeof(uint8_t);

    *((uint16_t *) sublen) = htons(id_len);
    *cs = clean_session;
    memcpy(data, sub_id, id_len);
}


static void pack_subscribe_packet(uint8_t *ptr, uint8_t qos, uint8_t retain,
        uint16_t clen, uint32_t mlen, uint8_t *channel, uint8_t *message) {

    assert(ptr);

    /* Set position pointers first */
    uint8_t *channel_len = ptr;
    uint8_t *message_len = ptr + sizeof(uint16_t);
    uint8_t *q = message_len + sizeof(uint32_t);
    uint8_t *ret = q + sizeof(uint8_t);
    uint8_t *chan = ret + sizeof(uint8_t);
    uint8_t *mex = chan + clen;

    /* Assign values to them */
    *((uint16_t *) channel_len) = htons(clen);
    *((uint32_t *) message_len) = htonl(mlen);
    *ret = retain;
    *q = qos;

    memcpy(chan, channel, clen);
    if (mlen > 0)
        memcpy(mex, message, mlen);
}


static void pack_ack_packet(uint8_t *ptr,
        uint16_t data_len, uint64_t id, uint8_t *data) {

    assert(ptr);

    uint8_t *dlen = ptr;
    uint8_t *mid = ptr + sizeof(uint16_t);
    uint8_t *payload = mid + sizeof(uint64_t);

    *((uint16_t *) dlen) = htons(data_len);
    htonll(mid, id);

    memcpy(payload, data, data_len);
}


Buffer *pack_request(Request *request) {

    assert(request);

    Buffer *packed = malloc(sizeof(Buffer));
    if (!packed) oom("packing protocol_packet");

    /* 2 unsigned char fields and 1 unsigned integer for len + another 1 for total len */
    uint32_t hdrlen = (2 * sizeof(uint8_t)) + (2 * sizeof(uint32_t));

    uint8_t *hdr = NULL;
    uint32_t tlen = 0;

    switch (request->header->opcode) {
        case CONNECT:
            // move index after data size value, where opcode start
            tlen = hdrlen + sizeof(uint16_t) + sizeof(uint8_t) + request->header->data_len;
            hdr = pack_header(request->header, tlen);
            pack_connect_packet(hdr + hdrlen, request->sub_id_len, request->clean_session, request->sub_id);
            break;
        case REPLICA:
        case PUBLISH:
        case SUBSCRIBE:
            tlen = hdrlen + sizeof(uint16_t) + (2 * sizeof(uint8_t)) + \
                   sizeof(uint32_t) + request->channel_len + \
                   request->message_len;
            hdr = pack_header(request->header, tlen);
            pack_subscribe_packet(hdr + hdrlen, request->qos, request->retain,
                    request->channel_len, request->message_len, request->channel, request->message);
            break;
        case QUIT:
        case CLUSTER_JOIN:
        case CLUSTER_JOIN_ACK:
            tlen = hdrlen + sizeof(uint16_t) + sizeof(uint64_t) + request->ack_len;
            hdr = pack_header(request->header, tlen);
            pack_ack_packet(hdr + hdrlen, request->ack_len, request->id, request->ack_data);
            break;
        case UNSUBSCRIBE:
            tlen = hdrlen + request->header->data_len;
            hdr = pack_header(request->header, tlen);
            memcpy(hdr + hdrlen, request->data, request->header->data_len);
            break;
    }

    packed->size = tlen;
    packed->data = hdr;

    return packed;
}


int8_t unpack_request(uint8_t *bytes, Request *r) {

    assert(bytes);
    assert(r);

    /* Start unpacking bytes into the protocol_packet_t structure */
    uint8_t *type = bytes;
    uint8_t *tlen = type + sizeof(uint8_t);
    uint8_t *opcode = tlen + sizeof(uint32_t);
    uint8_t *dlen = opcode + sizeof(uint8_t);
    size_t retain_offset = (2 * sizeof(uint32_t)) + sizeof(uint16_t) + sizeof(uint8_t);
    size_t channel_offset = retain_offset + sizeof(uint8_t);

    r->header = malloc(sizeof(Header));
    if (!r->header) oom("unpacking request header");

    r->header->type = *type;
    r->header->opcode = *opcode;
    r->header->data_len = ntohl(*((uint32_t *) dlen));

    switch (r->header->opcode) {
        case CONNECT:
            r->sub_id_len = ntohs(*((uint16_t *) (dlen + sizeof(uint32_t))));
            r->clean_session = *(dlen + sizeof(uint32_t) + sizeof(uint16_t));
            r->sub_id = malloc(r->sub_id_len + 1);
            memcpy(r->sub_id, dlen + sizeof(uint32_t) +
                    sizeof(uint16_t) + sizeof(uint8_t), r->header->data_len);
            r->sub_id[r->header->data_len] = '\0';
            break;
        case REPLICA:
        case PUBLISH:
        case SUBSCRIBE:
            r->channel_len = ntohs(*((uint16_t *) (dlen + sizeof(uint32_t))));
            r->message_len = ntohl(*((uint32_t *) (dlen + sizeof(uint32_t) + sizeof(uint16_t))));
            r->qos = *(dlen + (2 * sizeof(uint32_t)) + sizeof(uint16_t));
            r->retain = *(dlen + retain_offset);
            r->channel = malloc(r->channel_len + 1);
            memcpy(r->channel, dlen + channel_offset, r->channel_len);
            r->channel[r->channel_len] = '\0';
            r->message = malloc(r->message_len + 1);
            memcpy(r->message, dlen + channel_offset + r->channel_len, r->message_len);
            r->message[r->message_len] = '\0';
            break;
        case CLUSTER_JOIN:
        case CLUSTER_JOIN_ACK:
            r->ack_len = ntohs(*((uint16_t *) (dlen + sizeof(uint32_t))));
            r->id = ntohll(dlen + sizeof(uint32_t) + sizeof(uint16_t));
            r->ack_data = malloc(r->ack_len + 1);
            memcpy(r->ack_data, dlen + sizeof(uint32_t) +
                    sizeof(uint16_t) + sizeof(uint64_t), r->ack_len);
            r->ack_data[r->ack_len] = '\0';
            break;
        case UNSUBSCRIBE:
            r->data = malloc(r->header->data_len + 1);
            memcpy(r->data, dlen + sizeof(uint32_t), r->header->data_len);
            r->data[r->header->data_len] = '\0';
            break;
    }

    return 0;
}


Buffer *pack_response(Response *response) {

    assert(response);

    Buffer *packed = malloc(sizeof(Buffer));
    if (!packed) oom("packing protocol_packet");

    /* 2 unsigned char fields and 1 unsigned integer for len + another 1 for total len */
    uint32_t headerlen = (2 * sizeof(uint8_t)) + (2 * sizeof(uint32_t));

    uint8_t *hdr = NULL;
    uint8_t *data = NULL;
    uint16_t clen = response->channel_len;
    uint32_t mlen = response->message_len;
    uint32_t tlen = headerlen + response->header->data_len;

    switch (response->header->opcode) {
        case REPLICA:
        case PUBLISH:
            tlen = headerlen + sizeof(uint32_t) + clen + mlen +
                sizeof(uint16_t) + (2 * sizeof(uint8_t)) + sizeof(uint64_t);
            hdr = pack_header(response->header, tlen);
            data = hdr + headerlen;
            uint8_t *channel_len = data;
            uint8_t *message_len = data + sizeof(uint16_t);
            uint8_t *q = message_len + sizeof(uint32_t);
            uint8_t *sc = q + sizeof(uint8_t);
            uint8_t *mid = sc + sizeof(uint8_t);
            uint8_t *chan = mid + sizeof(uint64_t);
            uint8_t *mex = chan + clen;
            *((uint16_t *) channel_len) = htons(clen);
            *((uint32_t *) message_len) = htonl(mlen);
            *q = response->qos;
            *sc = response->sent_count;
            htonll(mid, response->id);
            memcpy(chan, response->channel, clen);
            memcpy(mex, response->message, mlen);
            break;
        case CONNACK:
        case SUBACK:
        case PUBACK:
        case CLUSTER_JOIN:
        case CLUSTER_JOIN_ACK:
            tlen += sizeof(uint8_t);
            hdr = pack_header(response->header, tlen);
            data = hdr + headerlen;
            *data = response->rc;
            break;
    }

    packed->size = tlen;
    packed->data = hdr;

    return packed;
}


int8_t unpack_response(uint8_t *bytes, Response *r) {

    assert(r);

    /* Start unpacking bytes into the protocol_packet_t structure */
    uint8_t *type = bytes;
    uint8_t *tlen = bytes + sizeof(uint8_t);
    uint8_t *opcode = tlen + sizeof(uint32_t);
    uint8_t *dlen = opcode + sizeof(uint8_t);
    size_t channel_pos = (2 * sizeof(uint32_t)) + sizeof(uint16_t) \
                         + (2 * sizeof(uint8_t)) + sizeof(uint64_t);
    size_t id_pos = (2 * sizeof(uint32_t)) + sizeof(uint16_t) + sizeof(uint8_t);

    r->header = malloc(sizeof(Header));
    if (!r->header) oom("unpacking response");

    r->header->type = *type;
    r->header->opcode = *opcode;
    r->header->data_len = ntohl(*((uint32_t *) dlen));

    switch (r->header->opcode) {
        case PUBLISH:
            r->channel_len = ntohs(*((uint16_t *) (dlen + sizeof(uint32_t))));
            r->message_len = ntohl(*((uint32_t *) (dlen \
                            + sizeof(uint32_t) + sizeof(uint16_t))));
            r->qos = *(dlen + (2 * sizeof(uint32_t)) + sizeof(uint16_t));
            r->sent_count = *(dlen + id_pos);
            r->id = ntohll(dlen + (2 * sizeof(uint32_t)) \
                    + sizeof(uint16_t) + (2 * sizeof(uint8_t)));
            r->channel = malloc(r->channel_len + 1);
            memcpy(r->channel, dlen + channel_pos, r->channel_len);
            r->channel[r->channel_len] = '\0';
            r->message = malloc(r->message_len + 1);
            memcpy(r->message, dlen + channel_pos + r->channel_len, r->message_len);
            r->message[r->message_len] = '\0';
            break;
        case SUBACK:
        case CLUSTER_JOIN:
        case CLUSTER_JOIN_ACK:
            r->rc = ntohs(*(dlen + sizeof(uint32_t)));
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


void free_packed(Buffer *p) {
    assert(p);
    free(p->data);
    free(p);
}

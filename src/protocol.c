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


static uint8_t *pack_header(uint8_t type,
        uint8_t opcode, uint32_t data_len, uint32_t total_len) {

    // bytes to be allocated + size of the data field
    uint8_t *raw = malloc(total_len);
    if (!raw) oom("packing header");

    uint8_t *typ = raw;
    uint8_t *tot = raw + sizeof(uint8_t);
    uint8_t *opcod = tot + sizeof(uint32_t);

    // fix index just after size of the data part
    uint8_t *datal = opcod + sizeof(uint8_t);

    // pack the whole structure
    *typ = type;
    *((uint32_t *) tot) = htonl(total_len);
    *opcod = opcode;
    *((uint32_t *) datal) = htonl(data_len);

    return raw;
}


static void pack_handshake_packet(uint8_t *ptr,
        uint16_t id_len, uint8_t clean_session, uint8_t *sub_id) {

    assert(ptr);

    uint8_t *sublen = ptr;
    uint8_t *cs = ptr + sizeof(uint16_t);
    uint8_t *data = cs + sizeof(uint8_t);

    *((uint16_t *) sublen) = htons(id_len);
    *cs = clean_session;
    memcpy(data, sub_id, id_len);
}


static void pack_subscribe_packet(uint8_t *ptr, uint8_t qos,
        uint64_t offset, uint16_t clen, uint32_t mlen, uint8_t *channel, uint8_t *message) {

    assert(ptr);

    /* Set position pointers first */
    uint8_t *channel_len = ptr;
    uint8_t *message_len = ptr + sizeof(uint16_t);
    uint8_t *q = message_len + sizeof(uint32_t);
    uint8_t *off = q + sizeof(uint8_t);
    uint8_t *chan = off + sizeof(uint64_t);
    uint8_t *mex = chan + clen;

    /* Assign values to them */
    *((uint16_t *) channel_len) = htons(clen);
    *((uint32_t *) message_len) = htonl(mlen);
    *q = qos;
    htonll(off, offset);

    memcpy(chan, channel, clen);
    memcpy(mex, message, mlen);
}


static void pack_ack_packet(uint8_t *ptr, uint16_t data_len, uint64_t id, uint8_t *data) {

    assert(ptr);

    uint8_t *dlen = ptr;
    uint8_t *mid = ptr + sizeof(uint16_t);
    uint8_t *payload = mid + sizeof(uint64_t);

    *((uint16_t *) dlen) = htons(data_len);
    htonll(mid, id);

    memcpy(payload, data, data_len);
}


packed_t *pack_request(request_t *request) {

    assert(request);

    packed_t *packed = malloc(sizeof(packed_t));
    if (!packed) oom("packing protocol_packet");

    /* 2 unsigned char fields and 1 unsigned integer for len + another 1 for total len */
    uint32_t hdrlen = (2 * sizeof(uint8_t)) + (2 * sizeof(uint32_t));

    uint8_t *hdr = NULL;
    uint32_t tlen = 0;

    switch (request->opcode) {
        case HANDSHAKE:
            // move index after data size value, where opcode start
            tlen = hdrlen + sizeof(uint16_t) + sizeof(uint8_t) + request->data_len;
            hdr = pack_header(request->type, request->opcode, request->data_len, tlen);
            pack_handshake_packet(hdr + hdrlen, request->sub_id_len, request->clean_session, request->sub_id);
            break;
        case REPLICA:
        case PUBLISH:
        case SUBSCRIBE:
            tlen = hdrlen + sizeof(uint16_t) + sizeof(uint8_t) + \
                   sizeof(uint32_t) + sizeof(uint64_t) + \
                   request->channel_len + request->message_len;
            hdr = pack_header(request->type, request->opcode, request->data_len, tlen);
            pack_subscribe_packet(hdr + hdrlen, request->qos, request->offset,
                    request->channel_len, request->message_len, request->channel, request->message);
            break;
        case ACK:
        case CLUSTER_JOIN:
        case CLUSTER_JOIN_ACK:
            tlen = hdrlen + sizeof(uint16_t) + sizeof(uint64_t) + request->ack_len;
            hdr = pack_header(request->type, request->opcode, request->data_len, tlen);
            pack_ack_packet(hdr + hdrlen, request->ack_len, request->id, request->ack_data);
            break;
        case UNSUBSCRIBE:
            tlen = hdrlen + request->data_len;
            hdr = pack_header(request->type, request->opcode, request->data_len, tlen);
            memcpy(hdr + hdrlen, request->data, request->data_len);
            break;
    }

    packed->size = tlen;
    packed->data = hdr;

    return packed;
}


int8_t unpack_request(uint8_t *bytes, request_t *r) {

    assert(bytes);
    assert(r);

    /* Start unpacking bytes into the protocol_packet_t structure */
    uint8_t *type = bytes;
    uint8_t *tlen = type + sizeof(uint8_t);
    uint8_t *opcode = tlen + sizeof(uint32_t);
    uint8_t *dlen = opcode + sizeof(uint8_t);

    r->type = *type;
    r->opcode = *opcode;
    r->data_len = ntohl(*((uint32_t *) dlen));

    switch (r->opcode) {
        case HANDSHAKE:
            r->sub_id_len = ntohs(*((uint16_t *) (dlen + sizeof(uint32_t))));
            r->clean_session = *(dlen + sizeof(uint32_t) + sizeof(uint16_t));
            r->sub_id = malloc(r->sub_id_len + 1);
            memcpy(r->sub_id, dlen + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint8_t), r->data_len);
            r->sub_id[r->data_len] = '\0';
            break;
        case REPLICA:
        case PUBLISH:
        case SUBSCRIBE:
            r->channel_len = ntohs(*((uint16_t *) (dlen + sizeof(uint32_t))));
            r->message_len = ntohl(*((uint32_t *) (dlen + sizeof(uint32_t) + sizeof(uint16_t))));
            r->qos = *(dlen + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint32_t));
            r->offset = ntohll(dlen + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint8_t));
            r->channel = malloc(r->channel_len + 1);
            memcpy(r->channel, dlen + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint64_t), r->channel_len);
            r->channel[r->channel_len] = '\0';
            r->message = malloc(r->message_len + 1);
            memcpy(r->message, dlen + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint64_t) + r->channel_len, r->message_len);
            r->message[r->message_len] = '\0';
            break;
        case ACK:
        case NACK:
        case CLUSTER_JOIN:
        case CLUSTER_JOIN_ACK:
            r->ack_len = ntohs(*((uint16_t *) (dlen + sizeof(uint32_t))));
            r->id = ntohll(dlen + sizeof(uint32_t) + sizeof(uint16_t));
            r->ack_data = malloc(r->ack_len + 1);
            memcpy(r->ack_data, dlen + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint64_t), r->ack_len);
            r->ack_data[r->ack_len] = '\0';
            break;
        case UNSUBSCRIBE:
            r->data = malloc(r->data_len + 1);
            memcpy(r->data, dlen + sizeof(uint32_t), r->data_len);
            r->data[r->data_len] = '\0';
            break;
    }

    return 0;
}


packed_t *pack_response(response_t *response) {

    assert(response);

    packed_t *packed = malloc(sizeof(packed_t));
    if (!packed) oom("packing protocol_packet");

    /* 2 unsigned char fields and 1 unsigned integer for len + another 1 for total len */
    uint32_t headerlen = (2 * sizeof(uint8_t)) + (2 * sizeof(uint32_t));

    uint8_t *hdr = NULL;
    uint8_t *data = NULL;
    uint16_t clen = response->channel_len;
    uint32_t mlen = response->message_len;
    uint32_t tlen = headerlen + response->data_len;

    switch (response->opcode) {
        case REPLICA:
        case PUBLISH:
            tlen = headerlen + sizeof(uint32_t) + clen + mlen + sizeof(uint16_t) + (2 * sizeof(uint8_t)) + sizeof(uint64_t);
            hdr = pack_header(response->type, response->opcode, response->data_len, tlen);
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
        case ACK:
        case CLUSTER_JOIN:
        case CLUSTER_JOIN_ACK:
            hdr = pack_header(response->type, response->opcode, response->data_len, tlen);
            data = hdr + headerlen;
            memcpy(data, response->data, response->data_len);
            break;
    }

    packed->size = tlen;
    packed->data = hdr;

    return packed;
}


int8_t unpack_response(uint8_t *bytes, response_t *r) {

    assert(r);

    /* Start unpacking bytes into the protocol_packet_t structure */
    uint8_t *type = bytes;
    uint8_t *tlen = bytes + sizeof(uint8_t);
    uint8_t *opcode = tlen + sizeof(uint32_t);
    uint8_t *dlen = opcode + sizeof(uint8_t);

    r->type = *type;
    r->opcode = *opcode;
    r->data_len = ntohl(*((uint32_t *) dlen));

    switch (r->opcode) {
        case PUBLISH:
            r->channel_len = ntohs(*((uint16_t *) (dlen + sizeof(uint32_t))));
            r->message_len = ntohl(*((uint32_t *) (dlen + sizeof(uint32_t) + sizeof(uint16_t))));
            r->qos = *(dlen + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint32_t));
            r->sent_count = *(dlen + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint8_t));
            r->id = ntohll(dlen + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint8_t));
            r->channel = malloc(r->channel_len + 1);
            memcpy(r->channel, dlen + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint64_t), r->channel_len + 1);
            r->channel[r->channel_len] = '\0';
            r->message = malloc(r->message_len + 1);
            memcpy(r->message, dlen + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint64_t) + r->channel_len + 1, r->message_len + 1);
            r->message[r->message_len] = '\0';
            break;
        case ACK:
        case NACK:
        case CLUSTER_JOIN:
        case CLUSTER_JOIN_ACK:
            r->data = malloc(r->data_len + 1);
            memcpy(r->data, dlen + sizeof(uint32_t), r->data_len);
            r->data[r->data_len] = '\0';
            break;
    }

    return 0;
}


request_t *build_ack_request(uint8_t type,
        uint8_t opcode, uint64_t id, char *data) {

    request_t *r = malloc(sizeof(request_t));
    if (!r) oom("building handshake request");

    r->type = type;
    r->opcode = opcode;
    r->data_len = r->ack_len = strlen(data);
    r->id = id;
    r->ack_data = (uint8_t *) data;

    return r;
}


request_t *build_handshake_request(uint8_t type,
        uint8_t opcode, uint8_t clean_session, char *sub_id) {

    request_t *r = malloc(sizeof(request_t));
    if (!r) oom("building handshake request");

    r->type = type;
    r->opcode = opcode;
    r->clean_session = clean_session;
    r->data_len = r->sub_id_len = strlen(sub_id);
    r->sub_id = (uint8_t *) sub_id;

    return r;
}


request_t *build_unsubscribe_request(uint8_t type, uint8_t opcode, char *data) {

    request_t *r = malloc(sizeof(request_t));
    if (!r) oom("building handshake request");

    r->type = type;
    r->opcode = opcode;
    r->data_len = strlen(data);
    r->data = (uint8_t *) data;

    return r;
}


request_t *build_subscribe_request(uint8_t type, uint8_t opcode,
        uint8_t qos, char *channel_name, char *message, int64_t offset) {

    request_t *r = malloc(sizeof(request_t));
    if (!r) oom("building subscribe request");

    uint16_t clen = strlen(channel_name);
    uint32_t mlen = strlen(message);

    r->type = type;
    r->opcode = opcode;
    r->data_len = clen + mlen;
    r->qos = qos;
    r->offset = offset;
    r->channel_len = clen;
    r->message_len = mlen;
    r->channel = (uint8_t *) channel_name;
    r->message = (uint8_t *) message;

    return r;
}


response_t *build_publish_response(uint8_t type, uint8_t opcode,
        uint8_t qos, char *channel_name, char *message, uint8_t incr) {

    uint64_t id = read_atomic(global.next_id);
    if (incr == 1) {
        id = incr_read_atomic(global.next_id);
    }

    response_t *r = malloc(sizeof(response_t));
    if (!r) oom("building subscribe request");

    uint16_t clen = strlen(channel_name);
    uint32_t mlen = strlen(message);

    r->type = type;
    r->opcode = opcode;
    r->data_len = clen + mlen;
    r->qos = qos;
    r->id = id;
    r->sent_count = 0;
    r->channel_len = clen;
    r->message_len = mlen;
    r->channel = (uint8_t *) channel_name;
    r->message = (uint8_t *) message;

    return r;
}


response_t *build_ack_response(uint8_t type, uint8_t opcode, uint8_t *data) {

    response_t *r = malloc(sizeof(response_t));
    if (!r) oom("building subscribe request");

    r->type = type;
    r->opcode = opcode;
    r->data_len = strlen((char *) data);
    r->data = data;

    return r;
}


void free_packed(packed_t *p) {
    assert(p);
    free(p->data);
    free(p);
}

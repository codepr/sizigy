#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "server.h"
#include "protocol.h"
#include "util.h"


static struct handshake_packet *unpack_handshake_packet(uint8_t *bytes) {
    uint8_t *meta = bytes;
    uint8_t *clean_session = meta + sizeof(uint32_t);
    // Move index after data field length and type (for now) to obtain operation code position
    uint8_t *data = clean_session + sizeof(uint8_t);
    // build up the protocol packet
    struct handshake_packet *packet = malloc(sizeof(struct handshake_packet));
    if (!packet) oom("creating handshake packet");
    // unpack all bytes into the structure
    ssize_t data_len = *((uint32_t *) meta);
    packet->clean_session = *clean_session;

    packet->id = malloc((data_len + 1));

    if (!packet->id) oom("allocating space for ID in handshake packet");

    memcpy(packet->id, data, data_len);
    packet->id[data_len] = '\0';
    return packet;
}


static packed_t *pack_sub_packet(struct sub_packet *packet) {
    ssize_t dlen = strlen((char *) packet->channel_name);
    uint32_t tlen = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(int64_t) + dlen;
    uint8_t *raw = malloc(tlen);
    if (!raw) oom("packing subscribe packet");
    uint8_t *meta = raw;
    uint8_t *qos = raw + sizeof(uint32_t);
    uint8_t *offset = qos + sizeof(uint8_t);
    uint8_t *channel_name = offset + sizeof(int64_t);

    *((uint32_t *) meta) = dlen;
    *qos = packet->qos;
    *((int64_t *) offset) = packet->offset;
    memcpy(channel_name, packet->channel_name, dlen);
    packed_t *packed = malloc(sizeof(packed_t));
    if (!packed) oom("packing subscribe packet");
    packed->size = tlen;
    packed->data = raw;

    return packed;
}


static struct sub_packet *unpack_sub_packet(uint8_t *bytes) {
    uint8_t *meta = bytes;
    uint8_t *qos = meta + sizeof(uint32_t);
    uint8_t *offset = qos + sizeof(uint8_t);
    // Move index after data field length and type (for now) to obtain operation code position
    uint8_t *data = offset + sizeof(int64_t);
    // build up the protocol packet
    struct sub_packet *packet = malloc(sizeof(struct sub_packet));
    if (!packet) oom("unpacking subscription packet");
    // unpack all bytes into the structure
    ssize_t data_len = *((uint32_t *) meta);
    packet->qos = *qos;
    packet->offset = *((int64_t *) offset);

    packet->channel_name = malloc((data_len + 1));
    if (!packet->channel_name) oom("unpacking subscription packet");

    memcpy(packet->channel_name, data, data_len);
    packet->channel_name[data_len] = '\0';
    return packet;
}


static packed_t *pack_pub_packet(struct pub_packet *packet, uint8_t type) {
    packed_t *packed = malloc(sizeof(packed_t));
    if (!packed) oom("packing publish packet");

    if (type == SYSTEM_PACKET) {
        ssize_t dlen = strlen((char *) packet->payload);
        uint32_t tlen = sizeof(uint32_t) + (2 * sizeof(uint8_t)) + sizeof(uint64_t) + dlen;

        uint8_t *raw = malloc(tlen);
        if (!raw) oom("packing publish packet");

        uint8_t *meta = raw;
        uint8_t *qos = raw + sizeof(uint32_t);
        uint8_t *redelivered = qos + sizeof(uint8_t);
        uint8_t *id = redelivered + sizeof(uint8_t);
        uint8_t *data = id + sizeof(uint64_t);

        *((uint32_t *) meta) = dlen;
        *qos = packet->qos;
        *redelivered = packet->redelivered;
        *((uint64_t *) id) = packet->id;
        memcpy(data, packet->payload, dlen);
        packed->size = tlen;
        packed->data = raw;

    } else if (type == CLIENT_PACKET) {
        ssize_t dlen = strlen((char *) packet->data);
        uint32_t tlen = sizeof(uint32_t) + (2 * sizeof(uint8_t)) + dlen;

        uint8_t *raw = malloc(tlen);
        if (!raw) oom("packing client publish packet");

        uint8_t *meta = raw;
        uint8_t *qos = raw + sizeof(uint32_t);
        uint8_t *redelivered = qos + sizeof(uint8_t);
        uint8_t *data = redelivered + sizeof(uint8_t);

        *((uint32_t *) meta) = dlen;
        *qos = packet->qos;
        *redelivered = packet->redelivered;
        memcpy(data, packet->data, dlen);
        packed->size = tlen;
        packed->data = raw;
    } else {
        free(packed);
        packed = NULL;
    }
    return packed;
}


static struct pub_packet *unpack_pub_packet(uint8_t *bytes, uint8_t type) {
    // build up the protocol packet
    struct pub_packet *packet = malloc(sizeof(struct pub_packet));
    if (!packet) oom("unpacking publish packet");

    if (type == SYSTEM_PACKET) {
        uint8_t *meta = bytes;
        uint8_t *qos = meta + sizeof(uint32_t);
        uint8_t *redelivered = qos + sizeof(uint8_t);
        uint8_t *id = redelivered + sizeof(uint8_t);
        // Move index after data field length and type (for now) to obtain operation code position
        uint8_t *data = id + sizeof(uint64_t);

        // unpack all bytes into the structure
        ssize_t data_len = *((uint32_t *) meta);
        packet->qos = *qos;
        packet->redelivered = *redelivered;
        packet->id = *((uint64_t *) id);

        packet->payload = malloc((data_len + 1));
        if (!packet->payload) oom("unpacking publish packet");

        memcpy(packet->payload, data, data_len);
        packet->payload[data_len] = '\0';
    } else if (type == CLIENT_PACKET) {
        uint8_t *meta = bytes;
        uint8_t *qos = meta + sizeof(uint32_t);
        uint8_t *redelivered = qos + sizeof(uint8_t);
        // Move index after data field length and type (for now) to obtain operation code position
        uint8_t *data = redelivered + sizeof(uint8_t);
        // unpack all bytes into the structure
        ssize_t data_len = *((uint32_t *) meta);
        packet->qos = *qos;
        packet->redelivered = *redelivered;

        packet->data = malloc(data_len + 1);
        if (!packet->data) oom("unpacking client publish packet");

        memcpy(packet->data, data, data_len);
        packet->data[data_len] = '\0';

    } else {
        free(packet);
        packet = NULL;
    }
    return packet;
}


packed_t *pack(protocol_packet_t *packet) {
    packed_t *packed = malloc(sizeof(packed_t));
    if (!packed) oom("packing protocol_packet");

    ssize_t dlen = 0;
    uint32_t tlen = 0;
    uint8_t *raw = NULL;
    uint8_t *meta = NULL;
    uint8_t *type = NULL;
    uint8_t *opcode = NULL;
    uint8_t *data = NULL;
    packed_t *p = NULL;

    switch (packet->opcode) {
        case ACK:
        case NACK:
        case DATA:
        case QUIT:
        case PING:
        case CLUSTER_JOIN:
        case CLUSTER_JOIN_ACK:
        case UNSUBSCRIBE_CHANNEL:
            // Data byte length
            dlen = strlen((char *) packet->data);
            // structure whole length to be allocated
            tlen = sizeof(uint32_t) + (2 * sizeof(uint8_t)) + dlen;
            // bytes to be allocated + size of the data field
            raw = malloc(tlen + sizeof(uint32_t));
            if (!raw) {
                perror("malloc(3) failed: packing protocol_packet");
                goto clean_and_exit;
            }

            /* Packet total len */
            uint8_t *tot = raw;

            opcode = raw + sizeof(uint32_t);
            // fix index just after size of the data part
            meta = opcode + sizeof(uint8_t);
            // move index after data size value, where opcode start
            type = meta + sizeof(uint32_t);
            // move after opcode field size to reach data field
            data = type + sizeof(uint8_t);
            // pack the whole structure
            *((uint32_t *) tot) = tlen + sizeof(uint32_t);
            *((uint32_t *) meta) = dlen;
            *type = packet->type;
            *opcode = packet->opcode;
            memcpy(data, packet->data, dlen);
            packed->size = tlen + sizeof(uint32_t);
            packed->data = raw;
            break;
        case SUBSCRIBE_CHANNEL:
            p = pack_sub_packet(packet->sub_packet);
            // Data byte length
            dlen = p->size;
            // structure whole length to be allocated
            tlen = (2 * sizeof(uint8_t)) + dlen;
            // bytes to be allocated + size of the data field
            raw = malloc(tlen + sizeof(uint32_t));
            if (!raw) {
                perror("malloc(3) failed: packing protocol_packet");
                goto clean_and_exit;
            }

            meta = raw;
            // fix index just after size of the data part
            opcode = raw + sizeof(uint32_t);
            // move index after data size value, where opcode start
            type = opcode + sizeof(uint8_t);
            data = type + sizeof(uint8_t);
            // pack the whole structure
            *((uint32_t *) meta) = tlen +  sizeof(uint32_t);
            *type = packet->type;
            *opcode = packet->opcode;
            memcpy(data, p->data, p->size);
            free(p->data);
            packed->size = tlen + sizeof(uint32_t);
            packed->data = raw;
            break;
        case REPLICA:
        case PUBLISH_MESSAGE:
            p = pack_pub_packet(packet->pub_packet, packet->type);
            // Data byte length
            dlen = p->size;
            // structure whole length to be allocated
            tlen = (2 * sizeof(uint8_t)) + dlen;
            // bytes to be allocated + size of the data field
            raw = malloc(tlen + sizeof(uint32_t));
            if (!raw) {
                perror("malloc(3) failed: packing protocol_packet");
                goto clean_and_exit;
            }

            meta = raw;
            // fix index just after size of the data part
            opcode = meta + sizeof(uint32_t);
            // move index after data size value, where opcode start
            type = opcode + sizeof(uint8_t);
            data = type + sizeof(uint8_t);
            // pack the whole structure
            *((uint32_t *) meta) = tlen + sizeof(uint32_t);
            *type = packet->type;
            *opcode = packet->opcode;
            memcpy(data, p->data, p->size);
            free(p->data);
            packed->size = tlen + sizeof(uint32_t);
            packed->data = raw;
            break;
    }
    free(p);
    return packed;

clean_and_exit:
    free(p->data);
    free(p);
    exit(EXIT_FAILURE);
}


int8_t unpack(uint8_t *bytes, protocol_packet_t *packet) {
    /* Start unpacking bytes into the protocol_packet_t structure */
    uint8_t *opcode = bytes + sizeof(uint32_t);
    uint8_t *meta = NULL;
    uint8_t *type = NULL;
    uint8_t *data = NULL;
    packet->opcode = *((uint8_t *) opcode);

    switch (packet->opcode) {
        case UNSUBSCRIBE_CHANNEL:
        case PING:
        case CLUSTER_JOIN:
        case CLUSTER_JOIN_ACK:
        case QUIT:
        case ACK:
        case NACK:
        case DATA:
            meta = bytes + sizeof(uint32_t) + sizeof(uint8_t);
            type = meta + sizeof(uint32_t);
            data = type + sizeof(uint8_t);
            ssize_t data_len = *((uint32_t *) meta);
            packet->type = *type;

            packet->data = malloc((data_len + 1));
            if (!packet->data) oom("unpacking protocol_packet");

            memcpy(packet->data, data, data_len);
            packet->data[data_len] = '\0';
            break;
        case HANDSHAKE:
            type = bytes + sizeof(uint32_t) + sizeof(uint8_t);
            data = type + sizeof(uint8_t);
            packet->type = *type;
            packet->handshake_packet = unpack_handshake_packet(data);
            break;
        case SUBSCRIBE_CHANNEL:
            type = bytes + sizeof(uint32_t) + sizeof(uint8_t);
            data = type + sizeof(uint8_t);
            packet->type = *type;
            packet->sub_packet = unpack_sub_packet(data);
            break;
        case REPLICA:
        case PUBLISH_MESSAGE:
            type = bytes + sizeof(uint32_t) + sizeof(uint8_t);
            data = type + sizeof(uint8_t);
            packet->type = *type;
            packet->pub_packet = unpack_pub_packet(data, packet->type);
            break;
        default:
            return -1;
    }
    return 0;
}


protocol_packet_t *build_packet(uint8_t type, uint8_t opcode, uint8_t qos,
        uint8_t redelivered, char *channel_name, char *message, int64_t offset, uint8_t incr) {

    protocol_packet_t *packet = malloc(sizeof(protocol_packet_t));
    if (!packet) oom("building packet");

    packet->type = type;
    packet->opcode = opcode;

    if (opcode != PUBLISH_MESSAGE && opcode != SUBSCRIBE_CHANNEL && opcode != REPLICA) {
        packet->data = (uint8_t *) message;
    } else if (opcode == SUBSCRIBE_CHANNEL) {
        struct sub_packet *subpacket = malloc(sizeof(struct sub_packet));
        if (!subpacket) oom("building packet");

        subpacket->qos = qos;
        subpacket->offset = offset;
        subpacket->channel_name = (uint8_t *) channel_name;
        packet->sub_packet = subpacket;
    } else {
        char *data = append_string(channel_name, message);

        struct pub_packet *pub_packet = malloc(sizeof(struct pub_packet));
        if (!pub_packet) oom("building packet");

        pub_packet->qos = qos;
        pub_packet->redelivered = redelivered;
        if (type == SYSTEM_PACKET) {
            uint64_t id = read_atomic(global.next_id);
            if ((opcode == DATA || opcode == PUBLISH_MESSAGE) && incr == 1) {
                id = incr_read_atomic(global.next_id);
            }
            pub_packet->id = id;
            pub_packet->payload = (uint8_t *) data;
        } else {
            pub_packet->data = (uint8_t *) data;
        }
        packet->pub_packet = pub_packet;
    }
    return packet;
}

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "server.h"
#include "protocol.h"
#include "util.h"


static struct packed pack_sub_packet(struct sub_packet packet) {
    ssize_t dlen = strlen(packet.channel_name);
    uint32_t tlen = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint64_t) + dlen;
    char *raw = malloc(sizeof(char) * tlen);
    char *meta = raw;
    char *qos = raw + sizeof(uint32_t);
    char *offset = raw + sizeof(uint8_t);
    char *channel_name = offset + sizeof(uint64_t);

    *((uint32_t *) meta) = dlen;
    *((uint8_t *) qos) = packet.qos;
    *((uint64_t *) offset) = packet.offset;
    memcpy(channel_name, packet.channel_name, dlen);
    struct packed packed = { tlen, raw };
    return packed;
}


static struct sub_packet unpack_sub_packet(char *bytes) {
    char *meta = bytes;
    char *qos = meta + sizeof(uint32_t);
    char *offset = qos + sizeof(uint8_t);
    // Move index after data field length and type (for now) to obtain operation code position
    char *data = offset + sizeof(uint64_t);
    // build up the protocol packet
    struct sub_packet packet;
    // unpack all bytes into the structure
    ssize_t data_len = *((uint32_t *) meta);
    packet.qos = *((uint8_t *) qos);
    packet.offset = *((uint64_t *) offset);
    packet.channel_name = malloc((data_len + 1) * sizeof(char));
    memcpy(packet.channel_name, data, data_len);
    packet.channel_name[data_len] = '\0';
    return packet;
}


static struct packed pack_syspubpacket(struct sys_pubpacket packet) {
    ssize_t dlen = strlen(packet.data);
    uint32_t tlen = sizeof(uint32_t) + (2 * sizeof(uint8_t)) + sizeof(uint64_t) + dlen;
    char *raw = malloc(sizeof(char) * tlen);
    char *meta = raw;
    char *qos = raw + sizeof(uint32_t);
    char *redelivered = qos + sizeof(uint8_t);
    char *id = redelivered + sizeof(uint8_t);
    char *data = id + sizeof(uint64_t);

    *((uint32_t *) meta) = dlen;
    *((uint8_t *) qos) = packet.qos;
    *((uint8_t *) redelivered) = packet.redelivered;
    *((uint64_t *) id) = packet.id;
    memcpy(data, packet.data, dlen);
    struct packed packed = { tlen, raw };
    return packed;
}


static struct sys_pubpacket unpack_sys_pubpacket(char *bytes) {
    char *meta = bytes;
    char *qos = meta + sizeof(uint32_t);
    char *redelivered = qos + sizeof(uint8_t);
    char *id = redelivered + sizeof(uint8_t);
    // Move index after data field length and type (for now) to obtain operation code position
    char *data = id + sizeof(uint64_t);
    // build up the protocol packet
    struct sys_pubpacket packet;
    // unpack all bytes into the structure
    ssize_t data_len = *((uint32_t *) meta);
    packet.qos = *((uint8_t *) qos);
    packet.redelivered = *((uint8_t *) redelivered);
    packet.id = *((uint64_t *) id);
    packet.data = malloc((data_len + 1) * sizeof(char));
    memcpy(packet.data, data, data_len);
    packet.data[data_len] = '\0';
    return packet;
}


static struct cli_pubpacket unpack_cli_pubpacket(char *bytes) {
    char *meta = bytes;
    char *qos = meta + sizeof(uint32_t);
    char *redelivered = qos + sizeof(uint8_t);
    // Move index after data field length and type (for now) to obtain operation code position
    char *data = redelivered + sizeof(uint8_t);
    // build up the protocol packet
    struct cli_pubpacket packet;
    // unpack all bytes into the structure
    ssize_t data_len = *((uint32_t *) meta);
    packet.qos = *((uint8_t *) qos);
    packet.redelivered = *((uint8_t *) redelivered);
    packet.data = malloc((data_len + 1) * sizeof(char));
    memcpy(packet.data, data, data_len);
    packet.data[data_len] = '\0';
    return packet;
}


struct packed pack(struct protocol_packet packet) {
    struct packed packed;
    ssize_t dlen = 0;
    uint32_t tlen = 0;
    char *raw;
    char *meta;
    char *type;
    char *opcode;
    char *data;
    struct packed p;

    switch (packet.opcode) {
        case ACK:
        case NACK:
        case DATA:
        case QUIT:
        case PING:
        case CREATE_CHANNEL:
        case DELETE_CHANNEL:
        case UNSUBSCRIBE_CHANNEL:
            // Data byte length
            dlen = strlen(packet.payload.data);
            // structure whole length to be allocated
            tlen = sizeof(uint32_t) + (2 * sizeof(uint8_t)) + dlen;
            // bytes to be allocated + size of the data field
            raw = malloc(sizeof(char) * tlen);
            opcode = raw;
            // fix index just after size of the data part
            meta = raw + sizeof(uint8_t);
            // move index after data size value, where opcode start
            type = meta + sizeof(uint32_t);
            // move after opcode field size to reach data field
            data = type + sizeof(uint8_t);
            // pack the whole structure
            *((uint32_t *) meta) = dlen;
            *((uint8_t *) type) = packet.type;
            *((uint8_t *) opcode) = packet.opcode;
            memcpy(data, packet.payload.data, dlen);
            packed.size = tlen;
            packed.data = raw;
            break;
        case SUBSCRIBE_CHANNEL:
            p = pack_sub_packet(packet.payload.sub_packet);
            // Data byte length
            dlen = p.size;
            // structure whole length to be allocated
            tlen = (2 * sizeof(uint8_t)) + sizeof(uint64_t) + dlen;
            // bytes to be allocated + size of the data field
            raw = malloc(sizeof(char) * tlen);
            // fix index just after size of the data part
            opcode = raw;
            // move index after data size value, where opcode start
            type = opcode + sizeof(uint8_t);
            data = type + sizeof(uint8_t);
            // pack the whole structure
            *((uint8_t *) type) = packet.type;
            *((uint8_t *) opcode) = packet.opcode;
            memcpy(data, p.data, p.size);
            packed.size = tlen;
            packed.data = raw;
            break;
        case PUBLISH_MESSAGE:
            p = pack_syspubpacket(packet.payload.sys_pubpacket);
            // Data byte length
            dlen = p.size;
            // structure whole length to be allocated
            /* tlen = (4 * sizeof(uint8_t)) + sizeof(uint64_t) + dlen; */
            tlen = (2 * sizeof(uint8_t)) + dlen;
            // bytes to be allocated + size of the data field
            raw = malloc(sizeof(char) * tlen);
            // fix index just after size of the data part
            opcode = raw;
            // move index after data size value, where opcode start
            type = opcode + sizeof(uint8_t);
            data = type + sizeof(uint8_t);
            // pack the whole structure
            *((uint8_t *) type) = packet.type;
            *((uint8_t *) opcode) = packet.opcode;
            memcpy(data, p.data, p.size);
            packed.size = tlen;
            packed.data = raw;
            break;
    }
    return packed;
}


struct protocol_packet unpack(char *bytes) {
    struct protocol_packet packet;
    char *opcode = bytes;
    char *meta;
    char *type;
    char *data;
    packet.opcode = *((uint8_t *) opcode);

    switch (packet.opcode) {
        case CREATE_CHANNEL:
        case DELETE_CHANNEL:
        case UNSUBSCRIBE_CHANNEL:
        case PING:
        case QUIT:
        case ACK:
        case NACK:
        case DATA:
            meta = bytes + sizeof(uint8_t);
            type = meta + sizeof(uint32_t);
            data = type + sizeof(uint8_t);
            ssize_t data_len = *((uint32_t *) meta);
            packet.type = *((uint8_t *) type);
            packet.payload.data = malloc((data_len + 1) * sizeof(char));
            memcpy(packet.payload.data, data, data_len);
            packet.payload.data[data_len] = '\0';
            break;
        case SUBSCRIBE_CHANNEL:
            type = bytes + sizeof(uint8_t);
            data = type + sizeof(uint8_t);
            packet.type = *((uint8_t *) type);
            packet.payload.sub_packet = unpack_sub_packet(data);
            break;
        case PUBLISH_MESSAGE:
            type = bytes + sizeof(uint8_t);
            data = type + sizeof(uint8_t);
            packet.type = *((uint8_t *) type);
            if (packet.type == SYSTEM_PACKET)
                packet.payload.sys_pubpacket = unpack_sys_pubpacket(data);
            else
                packet.payload.cli_pubpacket = unpack_cli_pubpacket(data);
            break;
    }
    return packet;
}


struct protocol_packet create_data_packet(uint8_t opcode, char *data) {
    struct protocol_packet packet;
    packet.type = SYSTEM_PACKET;
    packet.opcode = opcode;
    packet.payload.data = data;
    return packet;
}


struct protocol_packet create_sys_pubpacket(uint8_t opcode, uint8_t qos, uint8_t redelivered, char *channel_name, char *message, uint8_t incr) {
    uint64_t id = 0;
    if ((opcode == DATA || opcode == PUBLISH_MESSAGE) && incr == 1) {
        id = incr_read(&global.next_id);
    }
    char *data = append_string(channel_name, message);
    struct sys_pubpacket sys_pubpacket = { qos, redelivered, id, data };
    struct protocol_packet packet;
    packet.type = SYSTEM_PACKET;
    packet.opcode = opcode;
    packet.payload.sys_pubpacket = sys_pubpacket;
    return packet;
}

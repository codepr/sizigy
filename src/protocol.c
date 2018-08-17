#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "server.h"
#include "protocol.h"
#include "util.h"


static packed_t pack_sub_packet(struct sub_packet packet) {
    ssize_t dlen = strlen(packet.channel_name);
    uint32_t tlen = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint64_t) + dlen;
    char *raw = malloc(sizeof(char) * tlen);
    if (!raw) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    char *meta = raw;
    char *qos = raw + sizeof(uint32_t);
    char *offset = raw + sizeof(uint8_t);
    char *channel_name = offset + sizeof(uint64_t);

    *((uint32_t *) meta) = dlen;
    *((uint8_t *) qos) = packet.qos;
    *((uint64_t *) offset) = packet.offset;
    memcpy(channel_name, packet.channel_name, dlen);
    packed_t packed = { tlen, raw };
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

    if (!packet.channel_name) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }

    memcpy(packet.channel_name, data, data_len);
    packet.channel_name[data_len] = '\0';
    return packet;
}


static packed_t pack_syspubpacket(struct sys_pubpacket packet) {
    ssize_t dlen = strlen(packet.data);
    uint32_t tlen = sizeof(uint32_t) + (2 * sizeof(uint8_t)) + sizeof(uint64_t) + dlen;
    char *raw = malloc(sizeof(char) * tlen);

    if (!raw) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }

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
    packed_t packed = { tlen, raw };
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

    if (!packet.data) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }

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

    if (!packet.data) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }

    memcpy(packet.data, data, data_len);
    packet.data[data_len] = '\0';
    return packet;
}


packed_t pack(protocol_packet_t packet) {
    packed_t packed;
    ssize_t dlen = 0;
    uint32_t tlen = 0;
    char *raw = NULL;
    char *meta = NULL;
    char *type = NULL;
    char *opcode = NULL;
    char *data = NULL;
    packed_t p;

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

            if (!raw) {
                perror("malloc(3) failed");
                exit(EXIT_FAILURE);
            }

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

            if (!raw) {
                perror("malloc(3) failed");
                exit(EXIT_FAILURE);
            }

            // fix index just after size of the data part
            opcode = raw;
            // move index after data size value, where opcode start
            type = opcode + sizeof(uint8_t);
            data = type + sizeof(uint8_t);
            // pack the whole structure
            *((uint8_t *) type) = packet.type;
            *((uint8_t *) opcode) = packet.opcode;
            memcpy(data, p.data, p.size);
            free(p.data);
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

            if (!raw) {
                perror("malloc(3) failed");
                exit(EXIT_FAILURE);
            }

            // fix index just after size of the data part
            opcode = raw;
            // move index after data size value, where opcode start
            type = opcode + sizeof(uint8_t);
            data = type + sizeof(uint8_t);
            // pack the whole structure
            *((uint8_t *) type) = packet.type;
            *((uint8_t *) opcode) = packet.opcode;
            memcpy(data, p.data, p.size);
            free(p.data);
            packed.size = tlen;
            packed.data = raw;
            break;
    }
    return packed;
}


protocol_packet_t unpack(char *bytes) {
    protocol_packet_t packet;
    char *opcode = bytes;
    char *meta = NULL;
    char *type = NULL;
    char *data = NULL;
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

            if (!packet.payload.data) {
                perror("malloc(3) failed");
                exit(EXIT_FAILURE);
            }

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


protocol_packet_t create_data_packet(uint8_t opcode, char *data) {
    protocol_packet_t packet;
    packet.type = SYSTEM_PACKET;
    packet.opcode = opcode;
    packet.payload.data = data;
    return packet;
}


protocol_packet_t create_sys_pubpacket(uint8_t opcode, uint8_t qos, uint8_t redelivered, char *channel_name, char *message, uint8_t incr) {
    uint64_t id = read_counter(&global.next_id);
    if ((opcode == DATA || opcode == PUBLISH_MESSAGE) && incr == 1) {
        id = incr_read(&global.next_id);
    }
    char *data = append_string(channel_name, message);
    struct sys_pubpacket sys_pubpacket = { qos, redelivered, id, data };
    protocol_packet_t packet;
    packet.type = SYSTEM_PACKET;
    packet.opcode = opcode;
    packet.payload.sys_pubpacket = sys_pubpacket;
    return packet;
}


protocol_packet_t *create_sys_pubpacket_p(uint8_t opcode, uint8_t qos, uint8_t redelivered, char *channel_name, char *message, uint8_t incr) {
    uint64_t id = read_counter(&global.next_id);
    if ((opcode == DATA || opcode == PUBLISH_MESSAGE) && incr == 1) {
        id = incr_read(&global.next_id);
    }
    char *data = append_string(channel_name, message);
    struct sys_pubpacket sys_pubpacket = { qos, redelivered, id, data };
    protocol_packet_t *packet = malloc(sizeof(protocol_packet_t));
    if (!packet) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    packet->type = SYSTEM_PACKET;
    packet->opcode = opcode;
    packet->payload.sys_pubpacket = sys_pubpacket;
    return packet;
}


packed_t pack_sys_pubpacket(uint8_t opcode, uint8_t qos, uint8_t redelivered, char *channel_name, char *message, uint8_t incr) {
    protocol_packet_t p = create_sys_pubpacket(opcode, qos, redelivered, channel_name, message, incr);
    packed_t packed = pack(p);
    free(p.payload.sys_pubpacket.data);
    return packed;
}


packed_t pack_data_packet(uint8_t opcode, char *data) {
    protocol_packet_t p = create_data_packet(opcode, data);
    packed_t packed = pack(p);
    free(p.payload.data);
    return packed;
}

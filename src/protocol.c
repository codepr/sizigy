#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "server.h"
#include "protocol.h"
#include "util.h"


static packed_t *pack_sub_packet(struct sub_packet *packet) {
    ssize_t dlen = strlen((char *) packet->channel_name);
    uint32_t tlen = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(int64_t) + dlen;
    uint8_t *raw = malloc(tlen);
    if (!raw) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    uint8_t *meta = raw;
    uint8_t *qos = raw + sizeof(uint32_t);
    uint8_t *offset = qos + sizeof(uint8_t);
    uint8_t *channel_name = offset + sizeof(int64_t);

    *((uint32_t *) meta) = dlen;
    *qos = packet->qos;
    *((int64_t *) offset) = packet->offset;
    memcpy(channel_name, packet->channel_name, dlen);
    packed_t *packed = malloc(sizeof(packed_t));
    if (!packed) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
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
    if (!packet) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    // unpack all bytes into the structure
    ssize_t data_len = *((uint32_t *) meta);
    packet->qos = *qos;
    packet->offset = *((int64_t *) offset);
    packet->channel_name = malloc((data_len + 1));

    if (!packet->channel_name) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }

    memcpy(packet->channel_name, data, data_len);
    packet->channel_name[data_len] = '\0';
    return packet;
}


static packed_t *pack_syspubpacket(struct sys_pubpacket *packet) {
    ssize_t dlen = strlen((char *) packet->data);
    uint32_t tlen = sizeof(uint32_t) + (2 * sizeof(uint8_t)) + sizeof(uint64_t) + dlen;
    uint8_t *raw = malloc(tlen);

    if (!raw) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }

    uint8_t *meta = raw;
    uint8_t *qos = raw + sizeof(uint32_t);
    uint8_t *redelivered = qos + sizeof(uint8_t);
    uint8_t *id = redelivered + sizeof(uint8_t);
    uint8_t *data = id + sizeof(uint64_t);

    *((uint32_t *) meta) = dlen;
    *qos = packet->qos;
    *redelivered = packet->redelivered;
    *((uint64_t *) id) = packet->id;
    memcpy(data, packet->data, dlen);
    packed_t *packed = malloc(sizeof(packed_t));
    if (!packed) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    packed->size = tlen;
    packed->data = raw;
    return packed;
}


static struct sys_pubpacket *unpack_sys_pubpacket(uint8_t *bytes) {
    uint8_t *meta = bytes;
    uint8_t *qos = meta + sizeof(uint32_t);
    uint8_t *redelivered = qos + sizeof(uint8_t);
    uint8_t *id = redelivered + sizeof(uint8_t);
    // Move index after data field length and type (for now) to obtain operation code position
    uint8_t *data = id + sizeof(uint64_t);
    // build up the protocol packet
    struct sys_pubpacket *packet = malloc(sizeof(struct sys_pubpacket));

    if (!packet) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }

    // unpack all bytes into the structure
    ssize_t data_len = *((uint32_t *) meta);
    packet->qos = *qos;
    packet->redelivered = *redelivered;
    packet->id = *((uint64_t *) id);
    packet->data = malloc((data_len + 1));

    if (!packet->data) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }

    memcpy(packet->data, data, data_len);
    packet->data[data_len] = '\0';
    return packet;
}


static packed_t *pack_clipubpacket(struct cli_pubpacket *packet) {
    ssize_t dlen = strlen((char *) packet->data);
    uint32_t tlen = sizeof(uint32_t) + (2 * sizeof(uint8_t)) + dlen;
    uint8_t *raw = malloc(tlen);

    if (!raw) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }

    uint8_t *meta = raw;
    uint8_t *qos = raw + sizeof(uint32_t);
    uint8_t *redelivered = qos + sizeof(uint8_t);
    uint8_t *data = redelivered + sizeof(uint8_t);

    *((uint32_t *) meta) = dlen;
    *qos = packet->qos;
    *redelivered = packet->redelivered;
    memcpy(data, packet->data, dlen);
    packed_t *packed = malloc(sizeof(packed_t));
    if (!packed) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    packed->size = tlen;
    packed->data = raw;
    return packed;
}


static struct cli_pubpacket *unpack_cli_pubpacket(uint8_t *bytes) {
    uint8_t *meta = bytes;
    uint8_t *qos = meta + sizeof(uint32_t);
    uint8_t *redelivered = qos + sizeof(uint8_t);
    // Move index after data field length and type (for now) to obtain operation code position
    uint8_t *data = redelivered + sizeof(uint8_t);
    // build up the protocol packet
    struct cli_pubpacket *packet = malloc(sizeof(struct cli_pubpacket));
    if (!packet) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    // unpack all bytes into the structure
    ssize_t data_len = *((uint32_t *) meta);
    packet->qos = *qos;
    packet->redelivered = *redelivered;
    packet->data = malloc((data_len + 1));

    if (!packet->data) {
        perror("malloc(3) failed");
        goto clean_and_exit;
    }

    memcpy(packet->data, data, data_len);
    packet->data[data_len] = '\0';
    return packet;

clean_and_exit:
    free(packet);
    exit(EXIT_FAILURE);
}


packed_t *pack(protocol_packet_t *packet) {
    packed_t *packed = malloc(sizeof(packed_t));
    if (!packed) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
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
        case CREATE_CHANNEL:
        case DELETE_CHANNEL:
        case UNSUBSCRIBE_CHANNEL:
            // Data byte length
            dlen = strlen((char *) packet->payload.data);
            // structure whole length to be allocated
            tlen = sizeof(uint32_t) + (2 * sizeof(uint8_t)) + dlen;
            // bytes to be allocated + size of the data field
            raw = malloc(tlen + sizeof(uint32_t));

            if (!raw) {
                perror("malloc(3) failed");
                exit(EXIT_FAILURE);
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
            memcpy(data, packet->payload.data, dlen);
            packed->size = tlen + sizeof(uint32_t);
            packed->data = raw;
            break;
        case SUBSCRIBE_CHANNEL:
            p = pack_sub_packet(packet->payload.sub_packet);
            // Data byte length
            dlen = p->size;
            // structure whole length to be allocated
            /* tlen = (2 * sizeof(uint8_t)) + sizeof(int64_t) + dlen; */
            tlen = (2 * sizeof(uint8_t)) + dlen;
            // bytes to be allocated + size of the data field
            raw = malloc(tlen + sizeof(uint32_t));

            if (!raw) {
                perror("malloc(3) failed");
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
        case PUBLISH_MESSAGE:
            if (packet->type == SYSTEM_PACKET)
                p = pack_syspubpacket(packet->payload.sys_pubpacket);
            else
                p = pack_clipubpacket(packet->payload.cli_pubpacket);
            // Data byte length
            dlen = p->size;
            // structure whole length to be allocated
            tlen = (2 * sizeof(uint8_t)) + dlen;
            // bytes to be allocated + size of the data field
            raw = malloc(tlen + sizeof(uint32_t));

            if (!raw) {
                perror("malloc(3) failed");
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


int8_t unpack(ringbuf_t *rbuf, protocol_packet_t *packet) {

    /* Check the size of the ring buffer, we need at least the first 4 bytes in
       order to get the total length of the packet */
    if (ringbuf_empty(rbuf) || ringbuf_size(rbuf) < sizeof(uint32_t))
        return -1;

    uint8_t bytes[ringbuf_capacity(rbuf)];
    uint8_t *tmp = bytes;

    /* Try to read at least length of the packet */
    for (uint8_t i = 0; i < sizeof(uint32_t); i++)
        ringbuf_get(rbuf, tmp++);

    uint8_t *tot = bytes;
    uint32_t tlen = *((uint32_t *) tot);

    /* If there's no bytes nr equal to the total size of the packet abort and read again */
    if (ringbuf_size(rbuf) < tlen - sizeof(uint32_t))
        return -1;

    /* Empty the rest of the ring buffer */
    while ((tlen - sizeof(uint32_t)) > 0) {
        ringbuf_get(rbuf, tmp++);
        --tlen;
    }

    /* Start unpacking bytes into the protocol_packet_t structure */
    uint8_t *opcode = bytes + sizeof(uint32_t);
    uint8_t *meta = NULL;
    uint8_t *type = NULL;
    uint8_t *data = NULL;
    packet->opcode = *((uint8_t *) opcode);

    switch (packet->opcode) {
        case CREATE_CHANNEL:
        case DELETE_CHANNEL:
        case UNSUBSCRIBE_CHANNEL:
        case PING:
        case QUIT:
        case ACK:
        case NACK:
        case DATA:
            meta = bytes + sizeof(uint32_t) + sizeof(uint8_t);
            type = meta + sizeof(uint32_t);
            data = type + sizeof(uint8_t);
            ssize_t data_len = *((uint32_t *) meta);
            packet->type = *type;
            packet->payload.data = malloc((data_len + 1));

            if (!packet->payload.data) {
                perror("malloc(3) failed");
                exit(EXIT_FAILURE);
            }

            memcpy(packet->payload.data, data, data_len);
            packet->payload.data[data_len] = '\0';
            break;
        case SUBSCRIBE_CHANNEL:
            type = bytes + sizeof(uint32_t) + sizeof(uint8_t);
            data = type + sizeof(uint8_t);
            packet->type = *type;
            packet->payload.sub_packet = unpack_sub_packet(data);
            break;
        case PUBLISH_MESSAGE:
            type = bytes + sizeof(uint32_t) + sizeof(uint8_t);
            data = type + sizeof(uint8_t);
            packet->type = *type;
            if (packet->type == SYSTEM_PACKET)
                packet->payload.sys_pubpacket = unpack_sys_pubpacket(data);
            else
                packet->payload.cli_pubpacket = unpack_cli_pubpacket(data);
            break;
        default:
            return -1;
    }
    return 0;
}


protocol_packet_t *create_data_packet(uint8_t opcode, uint8_t *data) {
    protocol_packet_t *packet = malloc(sizeof(protocol_packet_t));
    if (!packet) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    packet->type = SYSTEM_PACKET;
    packet->opcode = opcode;
    packet->payload.data = data;
    return packet;
}


protocol_packet_t *create_sys_subpacket(uint8_t opcode, uint8_t qos, int64_t offset, char *channel) {
    struct sub_packet *subpacket = malloc(sizeof(struct sub_packet));
    if (!subpacket) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    subpacket->qos = qos;
    subpacket->offset = offset;
    subpacket->channel_name = (uint8_t *) channel;

    protocol_packet_t *packet = malloc(sizeof(protocol_packet_t));
    if (!packet) {
        perror("malloc(3) failed");
        goto clean_and_exit;
    }
    packet->type = SYSTEM_PACKET;
    packet->opcode = opcode;
    packet->payload.sub_packet = subpacket;

    return packet;

clean_and_exit:
    free(subpacket);
    exit(EXIT_FAILURE);
}


protocol_packet_t *create_sys_pubpacket(uint8_t opcode, uint8_t qos,
        uint8_t redelivered, char *channel_name, char *message, uint8_t incr) {

    uint64_t id = read_counter(global.next_id);
    if ((opcode == DATA || opcode == PUBLISH_MESSAGE) && incr == 1) {
        id = incr_read(global.next_id);
    }
    char *data = append_string(channel_name, message);
    struct sys_pubpacket *sys_pubpacket = malloc(sizeof(struct sys_pubpacket));
    if (!sys_pubpacket) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    sys_pubpacket->qos = qos;
    sys_pubpacket->redelivered = redelivered;
    sys_pubpacket->id = id;
    sys_pubpacket->data = (uint8_t *) data;

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


protocol_packet_t *create_cli_pubpacket(uint8_t opcode, uint8_t qos, uint8_t redelivered, char *channel, char *message) {
    char *data = append_string(channel, message);
    struct cli_pubpacket *cp = malloc(sizeof(struct cli_pubpacket));
    if (!cp) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    cp->qos = qos;
    cp->redelivered = redelivered;
    cp->data = (uint8_t *) data;

    protocol_packet_t *packet = malloc(sizeof(protocol_packet_t));
    if (!packet) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    packet->type = CLIENT_PACKET;
    packet->opcode = opcode;
    packet->payload.cli_pubpacket = cp;

    return packet;
}


packed_t *pack_sys_pubpacket(uint8_t opcode, uint8_t qos,
        uint8_t redelivered, char *channel_name, char *message, uint8_t incr) {
    protocol_packet_t *p = create_sys_pubpacket(opcode, qos, redelivered, channel_name, message, incr);
    packed_t *packed = pack(p);
    free(p->payload.sys_pubpacket->data);
    free(p);
    return packed;
}


packed_t *pack_data_packet(uint8_t opcode, uint8_t *data) {
    protocol_packet_t *p = create_data_packet(opcode, data);
    packed_t *packed = pack(p);
    free(p->payload.data);
    free(p);
    return packed;
}

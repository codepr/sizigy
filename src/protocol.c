#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "protocol.h"
#include "util.h"


struct packed pack(struct protocol_packet packet) {
    // Data byte length
    ssize_t dlen = strlen(packet.data);
    // structure whole length to be allocated
    uint32_t tlen = sizeof(uint32_t) + (2* sizeof(uint8_t)) + (2 * sizeof(uint16_t)) + dlen;
    // bytes to be allocated + size of the data field
    char *raw = malloc(sizeof(char) * tlen);
    char *metadata = raw;
    // fix index just after size of the data part
    char *type = raw + sizeof(uint32_t);
    // move index after data size value, where opcode start
    char *opcode = type + sizeof(uint8_t);
    // going forward to reach deliver_level index
    char *deliver_level = opcode + sizeof(uint16_t);
    // move by 2 bytes forward for redelivedered pointer
    char *redelivered = deliver_level + sizeof(uint16_t);
    // move after opcode field size to reach data field
    char *data = redelivered + sizeof(uint8_t);
    // pack the whole structure
    *((uint32_t *) metadata) = dlen;
    *((uint8_t *) type) = packet.type;
    *((uint16_t *) opcode) = packet.opcode;
    *((uint16_t *) deliver_level) = packet.deliver_level;
    *((uint8_t *) redelivered) = packet.redelivered;
    strcpy(data, packet.data);
    struct packed packed = { tlen, raw };
    return packed;
}


struct protocol_packet unpack(char *bytes) {
    char *metadata = bytes;
    char *type = metadata + sizeof(uint32_t);
    // Move index after data field length and type (for now) to obtain operation code position
    char *opcode = type + sizeof(uint8_t);
    char *deliver_level = opcode + sizeof(uint16_t);
    char *redelivered = deliver_level + sizeof(uint16_t);
    // move index after opcode to obtain data position
    char *data = redelivered + sizeof(uint8_t);
    // build up the protocol packet
    struct protocol_packet packet;
    // unpack all bytes into the structure
    ssize_t data_len = *((uint32_t *) metadata);
    packet.type = *((uint8_t *) type);
    packet.opcode = *((uint16_t *) opcode);
    packet.deliver_level = *((uint16_t *) deliver_level);
    packet.redelivered = *((uint8_t *) redelivered);
    packet.data = malloc((data_len + 1) * sizeof(char));
    strcpy(packet.data, data);
    return packet;
}



struct protocol_packet create_single_packet(uint8_t type, uint16_t dlevel, uint8_t redelivered, char *channel_name) {
    struct protocol_packet packet = { CLIENT_PACKET, type, dlevel, redelivered, channel_name };
    return packet;
}


struct protocol_packet create_double_packet(uint8_t type, uint16_t dlevel, uint8_t redelivered, char *channel_name, char *message) {
    char *data = append_string(channel_name, message);
    struct protocol_packet packet = { CLIENT_PACKET, type, dlevel, redelivered, data };
    return packet;
}

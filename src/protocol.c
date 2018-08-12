#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "protocol.h"
#include "util.h"


struct packed pack(struct protocol_packet packet) {
    // Data byte length
    ssize_t data_len = strlen(packet.data);
    // structure whole length to be allocated
    uint32_t total_len = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint16_t) + data_len;
    // bytes to be allocated + size of the data field
    char *raw = malloc(sizeof(char) * total_len);
    char *metadata = raw;
    // fix index just after size of the data part
    char *type = raw + sizeof(uint32_t);
    // move index after data size value, where opcode start
    char *opcode = type + sizeof(uint8_t);
    // move after opcode field size to reach data field
    char *data = opcode + sizeof(uint16_t);
    // pack the whole structure
    *((uint32_t *) metadata) = data_len;
    *((uint8_t *) type) = packet.type;
    *((uint16_t *) opcode) = packet.opcode;
    strcpy(data, packet.data);
    struct packed packed = { total_len, raw };
    return packed;
}


struct protocol_packet unpack(char *bytes, ssize_t len) {
    char *metadata = bytes;
    char *type = metadata + sizeof(uint32_t);
    // Move index after data field length and type (for now) to obtain operation code position
    char *opcode = type + sizeof(uint8_t);
    // move index after opcode to obtain data position
    char *data = opcode + sizeof(uint16_t);
    // build up the protocol packet
    struct protocol_packet packet;
    // unpack all bytes into the structure
    /* ssize_t data_len = *((uint32_t *) metadata); */
    ssize_t data_len = len;
    packet.type = *((uint8_t *) type);
    packet.opcode = *((uint16_t *) opcode);
    packet.data = malloc((data_len + 1) * sizeof(char));
    strcpy(packet.data, data);
    return packet;
}


struct protocol_packet create_packet(void) {
    struct protocol_packet packet = { 0x80, 0x01, "Hello world" };
    return packet;
}


struct protocol_packet create_single_packet(uint8_t type, char *channel_name) {
    struct protocol_packet packet = { CLIENT_PACKET, type, channel_name };
    return packet;
}


struct protocol_packet create_double_packet(uint8_t type, char *channel_name, char *message) {
    char *data = append_string(channel_name, message);
    struct protocol_packet packet = { CLIENT_PACKET, type, data };
    return packet;
}

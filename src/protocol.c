#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "protocol.h"

/*
 * Opcode :
 * 0x01 : Handshake
 * 0x02 : Subscribe
 * 0x03 : Publish
 * 0x04 : Bye
 * 0x05 : Ack
 * 0x06 : Nack
 */


char *pack(struct protocol_packet packet) {
    // Data byte length
    ssize_t data_len = strlen(packet.data);
    // structure whole length to be allocated
    uint32_t total_len = sizeof(uint16_t) + sizeof(ssize_t) + data_len;
    // bytes to be allocated + size of the data field
    char *packed = malloc(sizeof(char) * (total_len + sizeof(uint32_t)));
    char *metadata = packed;
    // move index after data size value, where opcode start
    char *opcode = packed + sizeof(uint32_t);
    // move after opcode field size to reach data field
    char *data = opcode + sizeof(uint16_t);
    // pack the whole structure
    *((uint32_t *) metadata) = data_len;
    *((uint32_t *) opcode) = packet.opcode;
    strcpy(data, packet.data);
    return packed;
}


struct protocol_packet unpack(char *bytes) {
    char *metadata = bytes;
    char *opcode = metadata + sizeof(uint32_t);
    char *data = opcode + sizeof(uint16_t);
    struct protocol_packet packet;
    uint32_t data_len = *((uint32_t *) metadata);
    packet.opcode = *((uint32_t *) opcode);
    packet.data = malloc((data_len + 1) * sizeof(char));
    strcpy(packet.data, data);
    return packet;
}


struct protocol_packet create_packet(void) {
    struct protocol_packet packet = { 0x01, "Hello world" };
    return packet;
}

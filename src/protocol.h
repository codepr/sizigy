#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include <stdint.h>


struct protocol_packet {
    uint16_t opcode;
    char *data;
};


char *pack(struct protocol_packet);
struct protocol_packet unpack(char *);
struct protocol_packet create_subscribe_packet(char *);
struct protocol_packet create_unsubscribe_packet(char *);
struct protocol_packet create_publish_packet(char *);

#endif

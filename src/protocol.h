#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include <stdint.h>

/* Packet type definition */
#define CLIENT_PACKET 0x80
#define SYSTEM_PACKET 0x81

/* Operation codes */
#define CONNECT             0x00
#define CREATE_CHANNEL      0x01
#define DELETE_CHANNEL      0x02
#define SUBSCRIBE_CHANNEL   0x03
#define UNSUBSCRIBE_CHANNEL 0x04
#define PUBLISH_MESSAGE     0x05
#define QUIT                0x06
#define ACK                 0x07
#define NACK                0x08
#define DATA                0x09

/* Deliverance guarantee */
#define AT_MOST_ONCE  0x100
#define AT_LEAST_ONCE 0x101


struct protocol_packet {
    uint8_t type;
    uint16_t opcode;
    uint16_t deliver_level;
    uint8_t redelivered;
    char *data;
};


struct packed {
    ssize_t size;
    char *data;
};


struct packed pack(struct protocol_packet);
struct protocol_packet unpack(char *);
struct protocol_packet create_single_packet(uint8_t, uint16_t, uint8_t, char *);
struct protocol_packet create_double_packet(uint8_t, uint16_t, uint8_t, char *, char *);


#endif

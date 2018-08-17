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
#define PING                0x0a

/* Deliverance guarantee */
#define AT_MOST_ONCE  0x00
#define AT_LEAST_ONCE 0x01


struct sub_packet {
    uint8_t qos;
    uint64_t offset;
    char *channel_name;
};


struct sys_pubpacket {
    uint8_t qos;
    uint8_t redelivered;
    uint64_t id;
    char *data;
};


struct cli_pubpacket {
    uint8_t qos;
    uint8_t redelivered;
    char *data;
};


typedef struct {
    uint8_t type;
    uint8_t opcode;
    union {
        struct sub_packet sub_packet;
        struct sys_pubpacket sys_pubpacket;
        struct cli_pubpacket cli_pubpacket;
        char *data;
    } payload;
} protocol_packet_t;


typedef struct {
    ssize_t size;
    char *data;
} packed_t;


packed_t pack(protocol_packet_t);
protocol_packet_t unpack(char *);
protocol_packet_t create_data_packet(uint8_t, char *);
protocol_packet_t create_sys_pubpacket(uint8_t, uint8_t, uint8_t, char *, char *, uint8_t);
protocol_packet_t *create_sys_pubpacket_p(uint8_t, uint8_t, uint8_t, char *, char *, uint8_t);
packed_t pack_sys_pubpacket(uint8_t, uint8_t, uint8_t, char *, char *, uint8_t);
packed_t pack_data_packet(uint8_t, char *);


#endif

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include <stdint.h>
#include "ringbuf.h"

/* Packet type definition */
#define CLIENT_PACKET 0x80
#define SYSTEM_PACKET 0x81

/* Operation codes */
#define HANDSHAKE           0x00
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


/* Subscription packet from client, qos and offset are optional and fallback
   respectively to 0 and -1 */
struct sub_packet {
    uint8_t qos;
    int64_t offset;
    uint8_t *channel_name;
};

/* Publish packet from system, qos and redelivered are optional and fallback
   respectively to 0 and 0, ID is an atomically auto-incremented long long int,
   data represents the message to be published */
struct sys_pubpacket {
    uint8_t qos;
    uint8_t redelivered;
    uint64_t id;
    uint8_t *data;
};

/* Publish packet from client, qos and redelivered are optional and fallback
   respectively to 0 and 0, data represents the message to be published */
struct cli_pubpacket {
    uint8_t qos;
    uint8_t redelivered;
    uint8_t *data;
};

/* Protocol defined packet, based on type and opcode fields, payload union have
   to be populated with the right structure packet according to his provenience
   and payload */
typedef struct {
    uint8_t type;
    uint8_t opcode;
    union {
        struct sub_packet *sub_packet;
        struct sys_pubpacket *sys_pubpacket;
        struct cli_pubpacket *cli_pubpacket;
        uint8_t *data;
    } payload;
} protocol_packet_t;

/* Contains the byte array version of the protocol_packet_t and the size contained */
typedef struct {
    ssize_t size;
    uint8_t *data;
} packed_t;


/* Packs protocol_packet_t fields into a byte array according to the endianess
   of the system, returning a packed_t packet. Data field and size are finally
   used to send it through the wire using sokets */
packed_t *pack(protocol_packet_t*);

/* Opposite of pack, return an exit code and populate the protocol_packet_t
   structure passed in as argument, allowing the caller to decide to allocate
   it or use a stack defined pointer */
int8_t unpack(uint8_t *, protocol_packet_t *);

/* Create a generic packet, return a protocol_packet_t with fields type, opcode
   and generic bytes as payload union field data */
protocol_packet_t *create_data_packet(uint8_t, uint8_t *);

/* Create a SYSTEM_PACKET type subcription packet, like data_packet but with
   payload set to sys_subpacket */
protocol_packet_t *create_sys_subpacket(uint8_t, uint8_t, int64_t, char *);

/* Create a SYSTEM_PACKET type publish packet, again like previous packets but
   with payload union set and filled to sys_pubpacket */
protocol_packet_t *create_sys_pubpacket(uint8_t, uint8_t, uint8_t, char *, char *, uint8_t);

/* Create a CLIENT_PACKET type publish packet, like previous packets but
   with payload union set and filled to cli_pubpacket, resulting in a slightly
   lighter version of the sys_pubpacket */
protocol_packet_t *create_cli_pubpacket(uint8_t, uint8_t, uint8_t, char *, char *);

/* Mix create_sys_pubpacket with pack to return a packed_t structure ready to
   be sent */
packed_t *pack_sys_pubpacket(uint8_t, uint8_t, uint8_t, char *, char *, uint8_t);

/* Same as pack_sys_pubpacket but creating a generic data packet */
packed_t *pack_data_packet(uint8_t, uint8_t *);


#endif

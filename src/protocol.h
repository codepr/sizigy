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
#define JOIN                0x0b
#define JOIN_ACK            0x0c
#define REPLICA             0x0d

/* Deliverance guarantee */
#define AT_MOST_ONCE  0x00
#define AT_LEAST_ONCE 0x01


/* Handshake packet, needed to set an ID to a client and making it capable of
   reconnecting with existing sessions */
struct handshake_packet {
    uint8_t clean_session;
    uint8_t *id;
};

/* Subscription packet from client, qos and offset are optional and fallback
   respectively to 0 and -1 */
struct sub_packet {
    uint8_t qos;
    int64_t offset;
    uint8_t *channel_name;
};

/* Publish packet from system, qos and redelivered are optional and fallback
   respectively to 0 and 0, ID is an atomically auto-incremented long long int,
   data represents the message to be published.

   Publish packet from client, qos and redelivered are optional and fallback
   respectively to 0 and 0, data represents the message to be published */
struct pub_packet {
    uint8_t qos;
    uint8_t redelivered;
    union {
        struct {
            uint64_t id;
            uint8_t *payload;
        };
        uint8_t *data;
    };
};

/* Protocol defined packet, based on type and opcode fields, payload union have
   to be populated with the right structure packet according to his provenience
   and payload */
typedef struct {
    uint8_t type;
    uint8_t opcode;
    union {
        struct sub_packet *sub_packet;
        struct pub_packet *pub_packet;
        /* struct sys_pubpacket *sys_pubpacket; */
        /* struct cli_pubpacket *cli_pubpacket; */
        struct handshake_packet *handshake_packet;
        uint8_t *data;
    };
} protocol_packet_t;

/* Contains the byte array version of the protocol_packet_t and the size contained */
typedef struct {
    ssize_t size;
    uint8_t *data;
} packed_t;


typedef packed_t request_t;
typedef packed_t response_t;

/* Packs protocol_packet_t fields into a byte array according to the endianess
   of the system, returning a packed_t packet. Data field and size are finally
   used to send it through the wire using sokets */
packed_t *pack(protocol_packet_t*);

/* Opposite of pack, return an exit code and populate the protocol_packet_t
   structure passed in as argument, allowing the caller to decide to allocate
   it or use a stack defined pointer */
int8_t unpack(uint8_t *, protocol_packet_t *);

#define build_request_subscribe(c, o) (build_packet(CLIENT_PACKET, SUBSCRIBE_CHANNEL, 0, 0, (c), NULL, (o), 0))
#define build_request_publish(q, r, c, m) (build_packet(CLIENT_PACKET, PUBLISH_MESSAGE, (q), (r), (c), (m), 0, 0))
#define build_response_publish(q, r, c, m, i) (build_packet(SYSTEM_PACKET, PUBLISH_MESSAGE, (q), (r), (c), (m), 0, (i)))
#define build_request_ack(o, m) (build_packet(CLIENT_PACKET, (o), 0, 0, NULL, (m), 0, 0))
#define build_response_ack(o, m) (build_packet(SYSTEM_PACKET, (o), 0, 0, NULL, (m), 0, 0))

protocol_packet_t *build_packet(uint8_t, uint8_t, uint8_t, uint8_t, char *, char *, int64_t, uint8_t);


#endif

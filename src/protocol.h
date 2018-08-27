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
#define SUBSCRIBE           0x01
#define UNSUBSCRIBE         0x02
#define PUBLISH             0x03
#define QUIT                0x04
#define ACK                 0x05
#define NACK                0x06
#define DATA                0x07
#define PING                0x08
#define CLUSTER_JOIN        0x09
#define CLUSTER_JOIN_ACK    0x0a
#define REPLICA             0x0b

/* Deliverance guarantee */
#define AT_MOST_ONCE  0x00
#define AT_LEAST_ONCE 0x01

// request:
//
//  - header { type: int, opcode: int }
//    - handshake   { id: str, clean_session: int }
//    - subscribe   { channel: str, offset: int, qos: int }
//    - unsubscribe { channel: str }
//    - publish     { channel: str, message: str, qos: int }
//    - ack         { id: int, data: str }
//
// response:
//
//  - header { type: int, opcode: int }
//    - ack/nack    { data: str }
//    - publish     { id: int, channel: str, message: str, qos: int, deliver: int }


typedef struct {
    uint8_t magic;
    uint8_t type;
    uint8_t opcode;
    uint32_t data_len;
    union {
        /* Handshake request */
        struct {
            uint16_t sub_id_len;
            uint8_t *sub_id;
            uint8_t clean_session;
        };
        /* Subscribe/publish request */
        struct {
            uint16_t channel_len;
            uint32_t message_len;
            uint8_t qos;
            int64_t offset;
            uint8_t *channel;
            uint8_t *message;
        };
        /* Ack request */
        struct {
            uint16_t ack_len;
            uint64_t id;
            uint8_t *ack_data;
        };
        /* Unsubscribe etc. */
        uint8_t *data;
    };
} request_t;


typedef struct {
    uint8_t magic;
    uint8_t type;
    uint8_t opcode;
    uint32_t data_len;
    union {
        /* Publish response */
        struct {
            uint16_t channel_len;
            uint32_t message_len;
            uint8_t qos;
            uint8_t sent_count;
            uint64_t id;
            uint8_t *channel;
            uint8_t *message;
        };
        /* Ack/Nack */
        uint8_t *data;
    };
} response_t;


/* Contains the byte array version of the protocol_packet_t and the size contained */
typedef struct {
    ssize_t size;
    uint8_t *data;
} packed_t;


/* Opposite of pack, return an exit code and populate the protocol_packet_t
   structure passed in as argument, allowing the caller to decide to allocate
   it or use a stack defined pointer */
packed_t *pack_request(request_t *);
int8_t unpack_request(uint8_t *, request_t *);
packed_t *pack_response(response_t *);
int8_t unpack_response(uint8_t *, response_t*);


#define build_ack_req(o, m) (build_ack_request(CLIENT_PACKET, (o), 0, (m)))
#define build_ack_res(o, m) (build_ack_response(SYSTEM_PACKET, (o), (m)))
#define build_rep_req(q, c, m) (build_subscribe_request(SYSTEM_PACKET, REPLICA, (q), (c), (m), 0))
#define build_pub_req(q, c, m) (build_subscribe_request(CLIENT_PACKET, PUBLISH, (q), (c), (m), 0))
#define build_pub_res(q, c, m, i) (build_publish_response(SYSTEM_PACKET, PUBLISH, (q), (c), (m), (i)))

request_t *build_ack_request(uint8_t, uint8_t, uint64_t, char *);
request_t *build_handshake_request(uint8_t, uint8_t, uint8_t, char *);
request_t *build_unsubscribe_request(uint8_t, uint8_t, char *);
request_t *build_subscribe_request(uint8_t, uint8_t, uint8_t, char *, char *, int64_t);
response_t *build_publish_response(uint8_t, uint8_t, uint8_t, char *, char *, uint8_t);
response_t *build_ack_response(uint8_t, uint8_t, uint8_t *);


#endif

/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2018, Andrea Giacomo Baldan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include <stdint.h>

/* Packet type definition */
#define REQUEST   0xfc
#define RESPONSE  0xfe

/* Operation codes */
#define CONNECT          0x00
#define SUBSCRIBE        0x01
#define UNSUBSCRIBE      0x02
#define PUBLISH          0x03
#define QUIT             0x04
#define DATA             0x07
#define CLUSTER_JOIN     0x09
#define CLUSTER_JOIN_ACK 0x0a
#define REPLICA          0x0b
#define CONNACK          0x0c
#define SUBACK           0x0d
#define PUBACK           0x0f

// Placeholders for QoS 2 *future implementation*
#define PUBREC           0x10
#define PUBREL           0x11
#define PUBCOMP          0x12
#define PINGREQ          0x13
#define PINGRESP         0x14

/* Deliverance guarantee */
#define AT_MOST_ONCE  0x00
#define AT_LEAST_ONCE 0x01
#define EXACTLY_ONCE  0x02

// request:
//
//  - header { type: int, opcode: int }
//    - connect   { id: str, clean_session: int }
//    - subscribe   { channel: str, qos: int }
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
    uint8_t type;
    uint8_t opcode;
    uint32_t data_len;
} Header;


typedef struct {
    Header *header;
    union {
        /* Connect request */
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
            uint8_t retain;
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
} Request;


typedef struct {
    Header *header;
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
        uint8_t rc;
    };
} Response;


/* Contains the byte array version of the protocol_packet_t and the size contained */
typedef struct {
    ssize_t size;
    uint8_t *data;
} Buffer;


/* Opposite of pack, return an exit code and populate the protocol_packet_t
   structure passed in as argument, allowing the caller to decide to allocate
   it or use a stack defined pointer */
Buffer *pack_request(Request *);
int8_t unpack_request(uint8_t *, Request *);
Buffer *pack_response(Response *);
int8_t unpack_response(uint8_t *, Response*);
void free_buffer(Buffer *);

#define build_ack_req(o, m) (build_ack_request(REQUEST, (o), 0, (m)))
#define build_ack_res(o, m) (build_ack_response(RESPONSE, (o), (m)))
#define build_rep_req(q, c, m) (build_subscribe_request(REQUEST, REPLICA, (q), (c), (m)))
#define build_pub_req(q, c, m) (build_subscribe_request(REQUEST, PUBLISH, (q), (c), (m), 0))
#define build_pub_res(q, c, m, i) (build_publish_response(RESPONSE, PUBLISH, (q), (c), (m), (i)))

Request *build_ack_request(uint8_t, uint8_t, uint64_t, char *);
Request *build_connect_request(uint8_t, uint8_t, uint8_t, char *);
Request *build_unsubscribe_request(uint8_t, uint8_t, char *);
Request *build_subscribe_request(uint8_t, uint8_t, uint8_t, char *, char *);
Response *build_publish_response(uint8_t, uint8_t, uint8_t, char *, char *, uint8_t);
Response *build_ack_response(uint8_t, uint8_t, uint8_t);


#endif

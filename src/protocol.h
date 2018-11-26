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

/* Error codes */
#define OK              0x00
#define EOOM            0x01

/* Operation codes */
#define CONNECT         0x10
#define CONNACK         0x20
#define PUBLISH         0x30
#define PUBACK          0x40
#define PUBREC          0x50
#define PUBREL          0x60
#define PUBCOMP         0x70
#define SUBSCRIBE       0x80
#define SUBACK          0x90
#define UNSUBSCRIBE     0xA0
#define UNSUBACK        0xB0
#define PINGREQ         0xC0
#define PINGRESP        0xD0
#define DISCONNECT      0xF0

/* Deliverance guarantee */
#define AT_MOST_ONCE    0x00
#define AT_LEAST_ONCE   0x01
#define EXACTLY_ONCE    0x02

// request:
//
//  - header { type: int, opcode: int }
//    - connect   { id: str, clean_session: int }
//    - subscribe   { topic: str, qos: int }
//    - unsubscribe { topic: str }
//    - publish     { topic: str, message: str, qos: int }
//    - ack         { id: int, data: str }
//
// response:
//
//  - header { type: int, opcode: int }
//    - ack/nack    { data: str }
//    - publish     { id: int, topic: str, message: str, qos: int, deliver: int }


#define HEADERLEN sizeof(uint8_t) + sizeof(uint32_t)


typedef struct {
    uint8_t opcode;
    uint32_t size;
} Header;


typedef struct {
    Header *header;
    uint16_t idlen;
    uint8_t *id;
    uint16_t keepalive;
    uint8_t clean_session;
} Connect;


typedef struct {
    Header *header;
    uint16_t topic_len;
    uint8_t qos;
    uint8_t *topic;
} Subscribe;


typedef struct {
    Header *header;
    uint16_t topic_len;
    uint8_t *topic;
} Unsubscribe;


typedef struct {
    Header *header;
    uint16_t topic_len;
    uint32_t message_len;
    uint8_t qos;
    uint8_t retain;
    uint8_t dup;
    uint8_t *topic;
    uint8_t *message;
} Publish;


typedef struct {
    Header *header;
    uint16_t id;
    uint8_t rc;
} Ack;


/* Contains the byte array version of the protocol_packet_t and the size
   contained */
typedef struct {
    size_t size;
    size_t pos;
    uint8_t *data;
} Buffer;


/* Opposite of pack, return an exit code and populate the protocol_packet_t
   structure passed in as argument, allowing the caller to decide to allocate
   it or use a stack defined pointer */

Buffer *buffer_init(const size_t);
void buffer_destroy(Buffer *);


// Reading data
uint8_t read_uint8(Buffer *);
uint16_t read_uint16(Buffer *);
uint32_t read_uint32(Buffer *);
uint64_t read_uint64(Buffer *);
uint8_t *read_string(Buffer *, size_t);


// Write data
void write_uint8(Buffer *, uint8_t);
void write_uint16(Buffer *, uint16_t);
void write_uint32(Buffer *, uint32_t);
void write_uint64(Buffer *, uint64_t);
void write_string(Buffer *, uint8_t *);


int8_t unpack_connect(Buffer *, Connect *);
int8_t unpack_subscribe(Buffer *, Subscribe *);
int8_t unpack_publish(Buffer *, Publish *);
int8_t unpack_ack(Buffer *, Ack *);


Ack *ack_pkt(const uint8_t, const uint16_t, const uint8_t);
Connect *connect_pkt(const uint8_t *, const uint8_t, const uint8_t);
Subscribe *subscribe_pkt(const uint8_t *, const uint8_t);
Unsubscribe *unsubscribe_pkt(const uint8_t *);
Publish *publish_pkt(const uint8_t *,
        const uint8_t *, const uint8_t, const uint8_t, const uint8_t);

void free_ack(Ack **);
void free_connect(Connect **);
void free_publish(Publish **);
void free_subscribe(Subscribe **);
void free_unsubscribe(Unsubscribe **);

void pack_connect(Buffer *, Connect *);
void pack_subscribe(Buffer *, Subscribe *);
void pack_publish(Buffer *, Publish *);
void pack_ack(Buffer *, Ack *);

#endif

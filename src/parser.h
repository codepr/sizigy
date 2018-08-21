#ifndef PARSER_H
#define PARSER_H

#include <stdint.h>
#include "protocol.h"


#define ERR_UNKNOWN   0x64
#define ERR_MISS_CHAN 0x65
#define ERR_MISS_MEX  0x66
#define ERR_MISS_ID   0x67


struct handshake {
    uint8_t clean_session;
    char *id;
};


struct build {
    int64_t offset;
    char *channel_name;
};


struct action {
    uint8_t redelivered;
    char *channel_name;
    char *message;
};


typedef struct {
    uint8_t opcode;
    uint8_t qos;
    union {
        struct build *b;
        struct action *a;
        struct handshake *h;
    } cmd;
} command_t;


command_t *parse_command(protocol_packet_t *);


#endif

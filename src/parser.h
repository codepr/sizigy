#ifndef PARSER_H
#define PARSER_H

#include <stdint.h>
#include "protocol.h"

#define STR_EQ(s1, s2) strcasecmp(s1, s2) == 0

#define ERR_UNKNOWN   0x64
#define ERR_MISS_CHAN 0x65
#define ERR_MISS_MEX  0x66


struct build {
    uint64_t offset;
    char *channel_name;
};


struct action {
    uint8_t redelivered;
    char *channel_name;
    char *message;
};


struct command {
    uint8_t opcode;
    uint8_t qos;
    union {
        struct build b;
        struct action a;
    } cmd;
};


struct command parse_command(char *);
struct command parse_protocol_command(struct protocol_packet);


#endif

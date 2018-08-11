#ifndef PARSER_H
#define PARSER_H

#include <stdint.h>
#include <string.h>

#define STR_EQ(s1, s2) strcasecmp(s1, s2) == 0


enum commands { CREATE, PUBLISH, SUBSCRIBE, UNSUBSCRIBE, DELETE, QUIT, ERR_UNKNOWN, ERR_MISS_CHAN, ERR_MISS_MEX };


struct build {
    char *channel_name;
};

struct action {
    char *channel_name;
    char *message;
};

struct command {
    uint16_t type;
    union {
        struct build b;
        struct action a;
    } cmd;
};


char *append_string(const char *, const char *);
struct command parse_command(char *);


#endif

#ifndef CHANNEL_H
#define CHANNEL_H

#include <stdint.h>
#include "list.h"
#include "queue.h"


#define ADD_DELAY       0
#define PUB_DELAY       500
#define MAX_PUB_RETRY   10
#define PUB_RETRY_DELAY 300


typedef struct {
    char *name;
    list_t *subscribers;
    queue_t *messages;
} channel_t;


typedef struct {
    uint64_t id;
    uint8_t qos;
    uint8_t redelivered;
    time_t creation_time;
    char *channel;
    char *payload;
} message_t;


struct subscriber {
    int fd;
    uint8_t qos;
    int64_t offset;
    char *name;
};


channel_t *create_channel(char *);
void add_subscriber(channel_t *, struct subscriber *);
void del_subscriber(channel_t *, struct subscriber *);
int publish_message(channel_t *, uint8_t, void *, int);
void store_message(channel_t *, const uint64_t, uint8_t, uint8_t, const char *, int);
void destroy_channel(channel_t *);

#endif

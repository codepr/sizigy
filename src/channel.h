#ifndef CHANNEL_H
#define CHANNEL_H

#include <stdint.h>
#include "list.h"
#include "queue.h"


typedef struct {
    char *name;
    list_t *subscribers;
    queue_t *messages;
} channel_t;


struct subscriber {
    int fd;
    uint8_t qos;
    char *name;
};


channel_t *create_channel(char *);
void add_subscriber(channel_t *, struct subscriber *);
void del_subscriber(channel_t *, struct subscriber *);
int publish_message(channel_t *, uint8_t, void *);
void destroy_channel(channel_t *);

#endif

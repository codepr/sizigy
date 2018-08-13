#ifndef CHANNEL_H
#define CHANNEL_H

#include <stdint.h>
#include "list.h"
#include "queue.h"


struct channel {
    char *name;
    list *subscribers;
    queue *messages;
};


struct subscriber {
    int fd;
    uint8_t qos;
    char *name;
};


struct channel *create_channel(char *);
void add_subscriber(struct channel *, struct subscriber *);
void del_subscriber(struct channel *, struct subscriber *);
int publish_message(struct channel *, uint16_t, void *);
void destroy_channel(struct channel *);

#endif

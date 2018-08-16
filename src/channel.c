#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include "map.h"
#include "util.h"
#include "server.h"
#include "parser.h"
#include "channel.h"


static int sub_compare(void *arg1, void *arg2) {
    list_node *node1 = (list_node *) arg1;
    list_node *node2 = (list_node *) arg2;
    struct subscriber *sub1 = (struct subscriber *) node1->data;
    struct subscriber *sub2 = (struct subscriber *) node2->data;
    // FIXME should be && in place of ||
    if (sub1->fd == sub2->fd)
        return 0;
    return 1;
}


struct channel *create_channel(char *name) {
   struct channel *channel = malloc(sizeof(struct channel));
   channel->name = name;
   channel->subscribers = list_create();
   channel->messages = create_queue();
   pthread_mutex_init(&(channel->lock), NULL);
   return channel;
}


void add_subscriber(struct channel *chan, struct subscriber *subscriber) {
    chan->subscribers = list_head_insert(chan->subscribers, subscriber);
}


void del_subscriber(struct channel *chan, struct subscriber *subscriber) {
    list_node node = { subscriber, NULL };
    chan->subscribers->head = list_remove_node(chan->subscribers->head, &node, sub_compare);
}


int publish_message(struct channel *chan, uint8_t qos, void *message) {
    /* pthread_mutex_lock(&chan->lock); */
    char *channel = append_string(chan->name, " ");
    uint8_t duplicate = 0;
    struct protocol_packet pp = create_sys_pubpacket(PUBLISH_MESSAGE, qos, duplicate, channel, message, 1);
    uint64_t id = pp.payload.sys_pubpacket.id;
    struct packed p = pack(pp);

    printf("*** [%p] PUBLISH (id=%ld qos=%d redelivered=%d message=%s) on channel %s (%ld bytes)\n",
            (void *) pthread_self(), id, qos, duplicate, (char *) message, chan->name, p.size);

    struct message *mex = malloc(sizeof(struct message));
    mex->qos = qos;
    mex->id = id;
    mex->channel = channel;
    mex->payload = message;
    /* Add message to the queue associated to the channel */
    enqueue(chan->messages, mex);

    /* Iterate through all the subscribers to send them the message */
    list_node *cursor = chan->subscribers->head;
    while (cursor) {
        struct subscriber *sub = (struct subscriber *) cursor->data;
        if (sendall(sub->fd, p.data, &p.size) < 0) {
            return -1;
        }
        if (qos == AT_LEAST_ONCE) {
            /* Add message to the waiting ACK map */
            map_put(global.ack_waiting, &id, &pp);
        }
        cursor = cursor->next;
    }
    free(p.data);
    /* pthread_mutex_unlock(&chan->lock); */
    return 0;
}


void destroy_channel(struct channel *chan) {
    if (chan->name)
        free(chan->name);
    if (chan->subscribers)
        list_release(chan->subscribers);
    if (chan->messages)
        release_queue(chan->messages);
    free(chan);
}

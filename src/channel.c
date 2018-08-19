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


channel_t *create_channel(char *name) {
   channel_t *channel = malloc(sizeof(channel_t));

   if (!channel) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }

   channel->name = strdup(name);
   channel->subscribers = list_create();
   channel->messages = create_queue();
   return channel;
}


void add_subscriber(channel_t *chan, struct subscriber *subscriber) {
    chan->subscribers = list_head_insert(chan->subscribers, subscriber);
}


void del_subscriber(channel_t *chan, struct subscriber *subscriber) {
    list_node node = { subscriber, NULL };
    chan->subscribers->head = list_remove_node(chan->subscribers->head, &node, sub_compare);
}


int publish_message(channel_t *chan, uint8_t qos, void *message) {
    int ret = 0;
    char *channel = append_string(chan->name, " ");
    uint8_t duplicate = 0;
    protocol_packet_t *pp = create_sys_pubpacket(PUBLISH_MESSAGE, qos, duplicate, channel, message, 1);
    uint64_t id = pp->payload.sys_pubpacket->id;
    packed_t *p = pack(pp);
    /* Prepare packet for AT_LEAST_ONCE subscribers */
    pp->payload.sys_pubpacket->qos = AT_LEAST_ONCE;
    packed_t *p_ack = pack(pp);

    DEBUG("*** PUBLISH (id=%ld qos=%d redelivered=%d message=%s) on channel %s (%ld bytes)\n",
            id, qos, duplicate, (char *) message, chan->name, p->size);

    /* Add message to the queue_t associated to the channel */
    enqueue(chan->messages, pp);

    /* Iterate through all the subscribers to send them the message */
    list_node *cursor = chan->subscribers->head;
    while (cursor) {
        struct subscriber *sub = (struct subscriber *) cursor->data;
        /* Check if subscriber has a qos != qos param */
        if (sub->qos > qos) {
            if (sendall(sub->fd, p_ack->data, &p_ack->size) < 0) {
                ret = -1;
                DEBUG("Can't send data to AT_LEAST_ONCE subscriber\n");
                goto cleanup;
            }
        }
        else {
            if (sendall(sub->fd, p->data, &p->size) < 0) {
                ret = -1;
                DEBUG("Can't send data to AT_MOST_ONCE subscriber\n");
                goto cleanup;
            }
        }
        cursor = cursor->next;
    }
cleanup:
    /* if (qos == AT_LEAST_ONCE || sub->qos == AT_LEAST_ONCE) { */
    /*     #<{(| Add message to the waiting ACK map_t |)}># */
    /*     map_put(global.ack_waiting, &id, pp); */
    /* } */
    free(p->data);
    free(p_ack->data);
    free(p);
    free(p_ack);
    /* free(pp); */
    free(message);
    return ret;
}


void destroy_channel(channel_t *chan) {
    if (chan->name)
        free(chan->name);
    if (chan->subscribers)
        list_release(chan->subscribers);
    if (chan->messages)
        release_queue(chan->messages);
    free(chan);
}

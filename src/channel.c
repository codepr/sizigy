#include <string.h>
#include <sys/socket.h>
#include "parser.h"
#include "channel.h"


static int sub_compare(void *arg1, void *arg2) {
    list_node *node1 = (list_node *) arg1;
    list_node *node2 = (list_node *) arg2;
    struct subscriber *sub1 = (struct subscriber *) node1->data;
    struct subscriber *sub2 = (struct subscriber *) node2->data;
    // FIXME should be && in place of ||
    if ((STR_EQ(sub1->name, sub2->name)) || sub1->fd == sub2->fd)
        return 0;
    return 1;
}


struct channel *create_channel(char *name) {
   struct channel *channel = malloc(sizeof(struct channel));
   channel->name = name;
   channel->subscribers = list_create();
   channel->messages = create_queue();
   return channel;
}


void add_subscriber(struct channel *chan, struct subscriber *subscriber) {
    chan->subscribers = list_head_insert(chan->subscribers, subscriber);
}


void del_subscriber(struct channel *chan, struct subscriber *subscriber) {
    list_node node = { subscriber, NULL };
    chan->subscribers->head = list_remove_node(chan->subscribers->head, &node, sub_compare);
}


int publish_message(struct channel *chan, void *message) {
    /* Add message to the queue associated to the channel */
    enqueue(chan->messages, message);
    struct protocol_packet pp = create_single_packet(DATA, message);
    struct packed p = pack(pp);
    /* Iterate through all the subscribers to send them the message */
    list_node *cursor = chan->subscribers->head;
    while (cursor) {
        struct subscriber *sub = (struct subscriber *) cursor->data;
        if (send(sub->fd, p.data, p.size, MSG_NOSIGNAL) < 0) {
            return -1;
        }
        cursor = cursor->next;
    }
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

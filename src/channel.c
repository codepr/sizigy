#include <time.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include "map.h"
#include "util.h"
#include "server.h"
#include "parser.h"
#include "network.h"
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


int publish_message(channel_t *chan, uint8_t qos, void *message, int incr) {
    int total_bytes_sent = 0;
    uint8_t qos_mod = 0;
    int increment = incr < 0 ? 0 : 1;
    char *channel = append_string(chan->name, " ");
    uint8_t duplicate = 0;
    protocol_packet_t *pp = create_sys_pubpacket(PUBLISH_MESSAGE, qos, duplicate, channel, message, increment);
    uint64_t id = pp->payload.sys_pubpacket->id;
    packed_t *p = pack(pp);
    int send_rc = 0;
    int pubdelay = 0;
    int base_delay = 500;
    uint64_t bps = get_value(global.throughput);
    double start_time = throttler_t_get_start(global.throttler);

    /* Naive throttler, try to maintain the throughput under a fixed threshold */
    if (bps > 20 * 1024 * 1024 && start_time ==  0.0) {
        throttler_set_us(global.throttler, 500);
        pubdelay = base_delay;
    } else if (bps > 20 * 1024 * 1024 && ((clock() - start_time) / CLOCKS_PER_SEC) < 20) {
        throttler_set_us(global.throttler, base_delay * 2);
        pubdelay = base_delay * 1.5;
    }

    /* Prepare packet for AT_LEAST_ONCE subscribers */
    if (pp->payload.sys_pubpacket->qos == AT_MOST_ONCE) {
        pp->payload.sys_pubpacket->qos = AT_LEAST_ONCE;
        qos_mod = 1;
    }
    packed_t *p_ack = pack(pp);

    /* Restore original qos */
    if (qos_mod)
        pp->payload.sys_pubpacket->qos = AT_MOST_ONCE;

    DEBUG("PUBLISH bytes=%ld channel=%s id=%ld qos=%d redelivered=%d message=%s",
            p->size, chan->name, id, qos, duplicate, (char *) message);

    /* Add message to the queue_t associated to the channel */
    enqueue(chan->messages, pp);

    /* Sent bytes sentinel */
    ssize_t sent = 0;

    /* Iterate through all the subscribers to send them the message */
    list_node *cursor = chan->subscribers->head;
    while (cursor) {
        struct subscriber *sub = (struct subscriber *) cursor->data;
        int retry = MAX_PUB_RETRY;
        int delay = PUB_RETRY_DELAY;
        /* Check if subscriber has a qos != qos param */
        if (sub->qos > qos) {
            do {
                send_rc = sendall(sub->fd, p_ack->data, p_ack->size, &sent);
                if (send_rc < 0) {
                    perror("Can't publish");
                    /* DEBUG("Can't send data to AT_LEAST_ONCE subscriber: fd %d qos %d", sub->fd, sub->qos); */
                    delay = retry < MAX_PUB_RETRY / 2 ? delay : (delay + 50) + (50 * (retry - (MAX_PUB_RETRY / 2)));
                    printf("RETRY %d DELAY %d\n", retry, delay);
                    usleep(delay);
                    --retry;
                }
                total_bytes_sent += sent;
            } while (send_rc < 0 && retry > 0);
        }
        else {
            do {
                send_rc = sendall(sub->fd, p->data, p->size, &sent);
                if (send_rc < 0) {
                    perror("Can't publish");
                    /* DEBUG("Can't send data to AT_MOST_ONCE subscriber: size %ld", p->size); */
                    delay = retry < MAX_PUB_RETRY / 2 ? delay : (delay + 50) + (50 * (retry - (MAX_PUB_RETRY / 2)));
                    printf("RETRY %d DELAY %d\n", retry, delay);
                    usleep(delay);
                    --retry;
                }
                total_bytes_sent += sent;
            } while (send_rc < 0 && retry > 0);
        }
        DEBUG("Publishing to %s", sub->name);
        if (pubdelay > 0) {
            DEBUG("Applying delay");
            usleep(pubdelay);
        }
        cursor = cursor->next;
    }
    bps = get_value(global.throughput);
    DEBUG("THROUGHPUT %ld Mb/s", bps/(1024*1024));
    free(p->data);
    free(p_ack->data);
    free(p);
    free(p_ack);
    free(channel);
    free(message);
    return total_bytes_sent;
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

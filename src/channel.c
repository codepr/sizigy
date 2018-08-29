#include <time.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include "map.h"
#include "util.h"
#include "server.h"
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
    if (!channel) oom("creating channel");

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


void store_message(channel_t *channel, const uint64_t id,
        uint8_t qos, uint8_t redelivered, const char *payload, int check_peers) {

    message_t *m = malloc(sizeof(message_t));
    if (!m) oom("creating message to be stored");

    m->creation_time = time(NULL);
    m->id = id;
    m->qos = qos;
    m->redelivered = redelivered;
    m->payload = strdup(payload);
    m->channel = channel->name;
    enqueue(channel->messages, m);

    // Check if cluster has members and spread the replicas
    if (check_peers == 1 && global.peers->len > 0) {
        char *m = append_string(channel->name, " ");
        request_t *replica_r = build_rep_req(qos, m, (char *) payload);
        replica_r->offset = id;
        packed_t *p = pack_request(replica_r);
        list_node *cur = global.peers->head;

        while (cur) {
            client_t *c = (client_t *) cur->data;
            sendall(c->fd, p->data, p->size, &(ssize_t) { 0 });
            cur = cur->next;
        }

        free(m);
        /* free(replica_r->channel); */
        free(replica_r);
        free_packed(p);
    }
}


int publish_message(channel_t *chan, uint8_t qos, void *message, int incr) {
    int total_bytes_sent = 0;
    uint8_t qos_mod = 0;
    int increment = incr < 0 ? 0 : 1;
    char *channel = append_string(chan->name, " ");
    uint8_t duplicate = 0;
    response_t *response = build_pub_res(qos, channel, message, increment);
    uint64_t id = response->id;
    packed_t *p = pack_response(response);
    int send_rc = 0;
    int pubdelay = 0;

    if (ADD_DELAY) {
        int base_delay = PUB_DELAY;
        uint64_t bps = read_atomic(global.throughput);
        double start_time = throttler_t_get_start(global.throttler);

        /* Naive throttler, try to maintain the throughput under a fixed threshold */
        if (bps > 20 * 1024 * 1024 && start_time ==  0.0) {
            throttler_set_us(global.throttler, 500);
            pubdelay = base_delay;
        } else if (bps > 20 * 1024 * 1024 && ((clock() - start_time) / CLOCKS_PER_SEC) < 20) {
            throttler_set_us(global.throttler, base_delay * 2);
            pubdelay = base_delay * 1.5;
        }
    }

    /* Prepare packet for AT_LEAST_ONCE subscribers */
    if (response->qos == AT_MOST_ONCE) {
        response->qos = AT_LEAST_ONCE;
        qos_mod = 1;
    }
    packed_t *p_ack = pack_response(response);

    /* Restore original qos */
    if (qos_mod)
        response->qos = AT_MOST_ONCE;

    DEBUG("PUBLISH bytes=%ld channel=%s id=%ld qos=%d redelivered=%d message=%s",
            p->size, chan->name, id, qos, duplicate, (char *) message);

    /* Add message to the queue_t associated to the channel */
    store_message(chan, response->id, response->qos, response->sent_count, message, 1);

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
    free_packed(p);
    free_packed(p_ack);
    free(response);
    free(channel);
    free(message);
    return total_bytes_sent;
}


void destroy_channel(channel_t *chan) {
    if (chan->name)
        free(chan->name);
    if (chan->subscribers)
        list_release(chan->subscribers, 1);
    if (chan->messages)
        release_queue(chan->messages);
    free(chan);
}

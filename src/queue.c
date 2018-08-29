#include <stdio.h>
#include "queue.h"
#include "channel.h"


queue_t *create_queue(void) {
    queue_t *q = calloc(1, sizeof(queue_t));
    if (!q) return NULL;
    q->len = 0;
    q->front = q->rear = NULL;
    /* Initialize mutex */
    pthread_mutex_init(&(q->lock), NULL);
    pthread_cond_init(&(q->cond), NULL);

    return q;
}


void release_queue(queue_t *q) {
    if (q != NULL) {
        /* pthread_mutex_lock(&(q->lock)); */
        while(q->len > 0) {
            dequeue(q);
        }
        /* pthread_mutex_unlock(&(q->lock)); */
        /* pthread_mutex_destroy(&(q->lock)); */
        /* pthread_cond_destroy(&(q->cond)); */
        free(q);
    }
}


/* insert data on the rear item */
void enqueue(queue_t *q, void *data) {

    pthread_mutex_lock(&(q->lock));
    queue_item *new_item = malloc(sizeof(queue_item));

    if (!new_item) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }

    new_item->next = NULL;
    new_item->data = data;
    q->len++;
    if (q->front == NULL && q->rear == NULL) {
        q->front = new_item;
        q->rear = new_item;
    }
    else {
        q->rear->next = new_item;
        q->rear = new_item;
    }

    pthread_cond_signal(&(q->cond));
    pthread_mutex_unlock(&(q->lock));
}


/* remove data from the front item and deallocate it */
void *dequeue(queue_t * q) {

    pthread_mutex_lock(&(q->lock));

    while (q->len == 0)
        pthread_cond_wait(&(q->cond), &(q->lock));

    void *item = NULL;
    queue_item *del_item;
    del_item = q->front;
    q->front = q->front->next;
    if (!q->front)
        q->rear = NULL;
    item = del_item->data;
    if (del_item)
        free(del_item);
    q->len--;

    pthread_mutex_unlock(&(q->lock));
    return item;
}


int send_queue(queue_t *q, void *ptr, sendfunc f) {
    /* Dirty hack that works as a MVP solution */
    struct subscriber *sub = (struct subscriber *) ptr;
    int ret = 0;
    uint64_t n = q->len;
    queue_item *item = q->front;
    while (item) {
        if (n <= sub->offset || sub->offset > q->len || sub->offset == 0)
            ret = f(item, ptr);
        item = item->next;
        n--;
    }
    return ret;
}

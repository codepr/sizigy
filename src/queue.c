#include <stdio.h>
#include "queue.h"


queue *create_queue(void) {
    queue *q = calloc(1, sizeof(queue));
    if (!q) return NULL;
    q->len = 0;
    q->front = q->rear = NULL;
    /* Initialize mutex */
    pthread_mutex_init(&(q->lock), NULL);
    pthread_cond_init(&(q->cond), NULL);

    return q;
}


void release_queue(queue *q) {
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
void enqueue(queue *q, void *data) {

    pthread_mutex_lock(&(q->lock));
    queue_item *new_item = malloc(sizeof(queue_item));

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
void *dequeue(queue* q) {

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


int send_queue(queue *q, int fd, sendfunc f) {
    int ret = 0;
    queue_item *item = q->front;
    while (item) {
        ret = f(item, fd);
        item = item->next;
    }
    return ret;
}

#ifndef QUEUE_H
#define QUEUE_H


#include <stdlib.h>
#include <pthread.h>


typedef int (*sendfunc)(void *, int);


typedef struct queue_item {
    void *data;
    struct queue_item *next;
} queue_item;


typedef struct queue_t {
    unsigned long len;
    queue_item *front;
    queue_item *rear;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} queue_t;


queue_t *create_queue(void);
void release_queue(queue_t *);
void enqueue(queue_t *, void *);
void *dequeue(queue_t *);
int send_queue(queue_t *, int, sendfunc);


#endif

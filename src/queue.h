#ifndef QUEUE_H
#define QUEUE_H


#include <stdlib.h>
#include <pthread.h>


typedef int (*sendfunc)(void *, int);


typedef struct queue_item {
    void *data;
    struct queue_item *next;
} queue_item;


typedef struct queue {
    unsigned long len;
    queue_item *front;
    queue_item *rear;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} queue;


queue *create_queue(void);
void release_queue(queue *);
void enqueue(queue *, void *);
void *dequeue(queue *);
int send_queue(queue *, int, sendfunc);


#endif

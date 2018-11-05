/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2018, Andrea Giacomo Baldan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include "queue.h"
#include "channel.h"


Queue *create_queue(void) {
    Queue *q = calloc(1, sizeof(Queue));
    if (!q) return NULL;
    q->len = 0;
    q->front = q->rear = NULL;
    /* Initialize mutex */
    pthread_mutex_init(&(q->lock), NULL);
    pthread_cond_init(&(q->cond), NULL);

    return q;
}


void release_queue(Queue *q) {
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
void enqueue(Queue *q, void *data) {

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
void *dequeue(Queue * q) {

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


int send_queue(Queue *q, void *ptr, sendfunc f) {
    /* Dirty hack that works as a MVP solution */
    int ret = 0;
    uint64_t n = q->len;
    queue_item *item = q->front;
    while (item) {
        ret = f(item, ptr);
        item = item->next;
        n--;
    }
    return ret;
}

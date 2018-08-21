#include <assert.h>
#include <stdlib.h>
#include "ringbuf.h"


struct ringbuf {
    uint8_t *buffer;
    size_t head;
    size_t tail;
    size_t max;
    uint8_t full : 1;
};


ringbuf_t *ringbuf_init(uint8_t *buffer, size_t size) {
    assert(buffer && size);

    ringbuf_t *rbuf = malloc(sizeof(ringbuf_t));
    assert(rbuf);

    rbuf->buffer = buffer;
    rbuf->max = size;
    ringbuf_reset(rbuf);

    assert(ringbuf_empty(rbuf));

    return rbuf;
}


void ringbuf_reset(ringbuf_t *rbuf) {
    assert(rbuf);

    rbuf->head = 0;
    rbuf->tail = 0;
    rbuf->full = 0;
}


void ringbuf_free(ringbuf_t *rbuf) {
    assert(rbuf);
    free(rbuf);
}


uint8_t ringbuf_full(ringbuf_t *rbuf) {
    assert(rbuf);
    return rbuf->full;
}


uint8_t ringbuf_empty(ringbuf_t *rbuf) {
    assert(rbuf);
    return (!rbuf->full && (rbuf->head == rbuf->tail));
}


size_t ringbuf_capacity(ringbuf_t *rbuf) {
    assert(rbuf);
    return rbuf->max;
}


size_t ringbuf_size(ringbuf_t *rbuf) {

    assert(rbuf);

    size_t size = rbuf->max;

    if (!rbuf->full) {
        if (rbuf->head >= rbuf->tail) {
            size = (rbuf->head - rbuf->tail);
        } else {
            size = (rbuf->max + rbuf->head - rbuf->tail);
        }
    }

    return size;
}


static void advance_pointer(ringbuf_t *rbuf) {
    assert(rbuf);

    if (rbuf->full) {
        rbuf->tail = (rbuf->tail + 1) % rbuf->max;
    }

    rbuf->head = (rbuf->head + 1) % rbuf->max;
    rbuf->full = (rbuf->head == rbuf->tail);
}


static void retreat_pointer(ringbuf_t *rbuf) {
    assert(rbuf);

    rbuf->full = 0;
    rbuf->tail = (rbuf->tail + 1) % rbuf->max;
}


int8_t ringbuf_push(ringbuf_t *rbuf, uint8_t data) {
    int8_t r = -1;

    assert(rbuf && rbuf->buffer);

    if (!ringbuf_full(rbuf)) {
        rbuf->buffer[rbuf->head] = data;
        advance_pointer(rbuf);
        r = 0;
    }

    return r;
}


int8_t ringbuf_bulk_push(ringbuf_t *rbuf, uint8_t *data, size_t size) {
    int8_t r = 0;
    for (uint8_t i = 0; i < size; ++i) {
        r = ringbuf_push(rbuf, data[i]);
        if (r == -1)
            break;
    }
    return r;
}


int8_t ringbuf_pop(ringbuf_t *rbuf, uint8_t *data) {
    assert(rbuf && data && rbuf->buffer);

    int8_t r = -1;

    if (!ringbuf_empty(rbuf)) {
        *data = rbuf->buffer[rbuf->tail];
        retreat_pointer(rbuf);

        r = 0;
    }

    return r;
}

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


Ringbuffer *ringbuf_init(uint8_t *buffer, size_t size) {
    assert(buffer && size);

    Ringbuffer *rbuf = malloc(sizeof(Ringbuffer));
    assert(rbuf);

    rbuf->buffer = buffer;
    rbuf->max = size;
    ringbuf_reset(rbuf);

    assert(ringbuf_empty(rbuf));

    return rbuf;
}


void ringbuf_reset(Ringbuffer *rbuf) {
    assert(rbuf);

    rbuf->head = 0;
    rbuf->tail = 0;
    rbuf->full = 0;
}


void ringbuf_free(Ringbuffer *rbuf) {
    assert(rbuf);
    free(rbuf);
}


uint8_t ringbuf_full(Ringbuffer *rbuf) {
    assert(rbuf);
    return rbuf->full;
}


uint8_t ringbuf_empty(Ringbuffer *rbuf) {
    assert(rbuf);
    return (!rbuf->full && (rbuf->head == rbuf->tail));
}


size_t ringbuf_capacity(Ringbuffer *rbuf) {
    assert(rbuf);
    return rbuf->max;
}


size_t ringbuf_size(Ringbuffer *rbuf) {

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


static void advance_pointer(Ringbuffer *rbuf) {
    assert(rbuf);

    if (rbuf->full) {
        rbuf->tail = (rbuf->tail + 1) % rbuf->max;
    }

    rbuf->head = (rbuf->head + 1) % rbuf->max;
    rbuf->full = (rbuf->head == rbuf->tail);
}


static void retreat_pointer(Ringbuffer *rbuf) {
    assert(rbuf);

    rbuf->full = 0;
    rbuf->tail = (rbuf->tail + 1) % rbuf->max;
}


int8_t ringbuf_push(Ringbuffer *rbuf, uint8_t data) {
    int8_t r = -1;

    assert(rbuf && rbuf->buffer);

    if (!ringbuf_full(rbuf)) {
        rbuf->buffer[rbuf->head] = data;
        advance_pointer(rbuf);
        r = 0;
    }

    return r;
}


int8_t ringbuf_bulk_push(Ringbuffer *rbuf, uint8_t *data, size_t size) {
    int8_t r = 0;
    for (size_t i = 0; i < size; ++i) {
        r = ringbuf_push(rbuf, data[i]);
        if (r == -1)
            break;
    }
    return r;
}


int8_t ringbuf_pop(Ringbuffer *rbuf, uint8_t *data) {
    assert(rbuf && data && rbuf->buffer);

    int8_t r = -1;

    if (!ringbuf_empty(rbuf)) {
        *data = rbuf->buffer[rbuf->tail];
        retreat_pointer(rbuf);

        r = 0;
    }

    return r;
}

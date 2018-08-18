#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdio.h>
#include <stdint.h>


typedef struct ringbuf ringbuf_t;


ringbuf_t *ringbuf_init(uint8_t *, size_t);
void ringbuf_free(ringbuf_t *);
void ringbuf_reset(ringbuf_t *);
int8_t ringbuf_put(ringbuf_t *, uint8_t);
int8_t ringbuf_bulk_put(ringbuf_t *, uint8_t *, size_t);
int8_t ringbuf_get(ringbuf_t *, uint8_t *);
uint8_t ringbuf_empty(ringbuf_t *);
uint8_t ringbuf_full(ringbuf_t *);
size_t ringbuf_capacity(ringbuf_t *);
size_t ringbuf_size(ringbuf_t *);


#endif

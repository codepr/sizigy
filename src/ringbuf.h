#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdio.h>
#include <stdint.h>


typedef struct ringbuf ringbuf_t;

/* Initialize the structure by associating a byte buffer, alloc on the heap so
   it has to be freed with ringbuf_free */
ringbuf_t *ringbuf_init(uint8_t *, size_t);

/* Free the circular buffer */
void ringbuf_free(ringbuf_t *);

/* Make tail = head and full to false (an empty ringbuf) */
void ringbuf_reset(ringbuf_t *);

/* Push a single byte into the buffer and move forward the interator pointer */
int8_t ringbuf_push(ringbuf_t *, uint8_t);

/* Push each element of a bytearray into the buffer */
int8_t ringbuf_bulk_push(ringbuf_t *, uint8_t *, size_t);

/* Pop out the front of the buffer */
int8_t ringbuf_pop(ringbuf_t *, uint8_t *);

/* Check if the buffer is empty, returning 0 or 1 according to the result */
uint8_t ringbuf_empty(ringbuf_t *);

/* Check if the buffer is full, returning 0 or 1 according to the result */
uint8_t ringbuf_full(ringbuf_t *);

/* Return the max size of the buffer, e.g. the bytearray size used to init the
   buffer */
size_t ringbuf_capacity(ringbuf_t *);

/* Return the current size of the buffer, e.g. the nr of bytes currently stored
   inside */
size_t ringbuf_size(ringbuf_t *);


#endif

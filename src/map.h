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

#ifndef MAP_H
#define MAP_H


#define MAP_OK   0
#define MAP_ERR  -1
#define MAP_FULL -2
#define CRC32(c, x) crc32(c, x)


typedef int (*func)(void *, void *);
typedef int (*func3)(void *, void *, void *);


/* We need to keep keys and values */
typedef struct {
    void *key;
    void *val;
    unsigned int in_use : 1;
} map_entry;


/*
 * An hashMap has some maximum size and current size, as well as the data to
 * hold.
 */
typedef struct {
    unsigned long table_size;
    unsigned long size;
    map_entry *entries;
} Map;


/* Map API */
Map *map_create(void);
void map_release(Map *);
int map_put(Map *, void *, void *);
void *map_get(Map *, void *);
map_entry *map_get_entry(Map *, void *);
int map_del(Map *, void *);
int map_iterate2(Map *, func, void *);
int map_iterate3(Map *, func3, void *, void *);

unsigned long crc32(const unsigned char *, unsigned int);

#endif

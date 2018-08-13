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
 * An hashmap has some maximum size and current size, as well as the data to
 * hold.
 */
typedef struct {
    unsigned long table_size;
    unsigned long size;
    map_entry *entries;
} map;


/* Map API */
map *map_create(void);
void map_release(map *);
int map_put(map *, void *, void *);
void *map_get(map *, void *);
map_entry *map_get_entry(map *, void *);
int map_del(map *, void *);
int map_iterate2(map *, func, void *);
int map_iterate3(map *, func3, void *, void *);

unsigned long crc32(const unsigned char *, unsigned int);

#endif

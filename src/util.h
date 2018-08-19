#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>


enum { INFO, ERROR, DEBUG };


typedef struct counter counter_t;

counter_t *init_counter(void);
void increment_by(counter_t *, uint64_t);
void increment(counter_t *);
uint64_t incr_read(counter_t *);
uint64_t read_counter(counter_t *);
void remove_newline(char *);
char *append_string(const char *, const char *);

/* logging */

void s_log(uint8_t, const char *, ...);

#define LOG(...) s_log( __VA_ARGS__ )
#define DEBUG(...) LOG(DEBUG, __VA_ARGS__)
#define ERROR(...) LOG(ERR, __VA_ARGS__)
#define INFO(...) LOG(INFO, __VA_ARGS__)

#endif

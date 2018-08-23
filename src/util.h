#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdint.h>


#define MAX_LOG_SIZE 128


enum { INFO, ERROR, DEBUG };


typedef struct counter counter_t;

typedef struct atomic atomic_t;

typedef struct throttler throttler_t;


throttler_t *init_throttler(void);
void throttler_set_us(throttler_t *, const uint64_t);
uint64_t throttler_get_us(throttler_t *);
double throttler_t_get_start(throttler_t *);
atomic_t *init_atomic(void);
void set_value(atomic_t *, const uint64_t);
uint64_t get_value(atomic_t *);
counter_t *init_counter(void);
void increment_by(counter_t *, const uint64_t);
void increment(counter_t *);
uint64_t incr_read(counter_t *);
uint64_t read_counter(counter_t *);
void free_counter(counter_t *);
void remove_newline(char *);
char *append_string(const char *, const char *);
const char *random_name(const size_t);

/* logging */

void s_log(const uint8_t level, const char *, ...);

#define LOG(...) s_log( __VA_ARGS__ )
#define DEBUG(...) LOG(DEBUG, __VA_ARGS__)
#define ERROR(...) LOG(ERR, __VA_ARGS__)
#define INFO(...) LOG(INFO, __VA_ARGS__)

#endif

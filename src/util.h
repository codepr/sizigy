#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdint.h>


#define MAX_LOG_SIZE 128


enum { INFO, ERROR, DEBUG };


typedef struct atomic atomic_t;

typedef struct throttler throttler_t;


throttler_t *init_throttler(void);
void throttler_set_us(throttler_t *, const uint64_t);
uint64_t throttler_get_us(throttler_t *);
double throttler_t_get_start(throttler_t *);
atomic_t *init_atomic(void);
void write_atomic(atomic_t *, const uint64_t);
void increment_by(atomic_t *, const uint64_t);
void increment(atomic_t *);
uint64_t incr_read_atomic(atomic_t *);
uint64_t read_atomic(atomic_t *);
void free_atomic(atomic_t *);
void remove_newline(char *);
char *append_string(const char *, const char *);
const char *random_name(const size_t);
int parse_int(char *);
void oom(const char *);

/* logging */

void s_log(const uint8_t level, const char *, ...);

#define LOG(...) s_log( __VA_ARGS__ )
#define DEBUG(...) LOG(DEBUG, __VA_ARGS__)
#define ERROR(...) LOG(ERR, __VA_ARGS__)
#define INFO(...) LOG(INFO, __VA_ARGS__)

#endif

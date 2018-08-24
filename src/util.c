#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "util.h"
#include "server.h"


struct counter {
    uint64_t value;
    pthread_mutex_t lock;
};


struct atomic {
    uint64_t value;
    pthread_mutex_t lock;
};


struct throttler {
    uint64_t us;
    double start;
    pthread_mutex_t lock;
};


void remove_newline(char *str) {
    str[strcspn(str, "\n")] = 0;
}


char *append_string(const char *s1, const char *s2) {
    const size_t len1 = strlen(s1);
    const size_t len2 = strlen(s2);
    char *result = malloc(len1 + len2 + 1); // +1 for the null-terminator
    if (!result) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    // in real code you would check for errors in malloc here
    memcpy(result, s1, len1);
    memcpy(result + len1, s2, len2 + 1); // +1 to copy the null-terminator
    return result;
}


throttler_t *init_throttler(void) {
    throttler_t *t = malloc(sizeof(throttler_t));
    t->us = 0;
    t->start = 0.0;
    pthread_mutex_init(&(t->lock), NULL);
    return t;
}


void throttler_set_us(throttler_t *t, const uint64_t us) {
    pthread_mutex_lock(&(t->lock));
    t->us = us;
    t->start = clock();
    pthread_mutex_unlock(&(t->lock));
}


uint64_t throttler_get_us(throttler_t *t) {
    pthread_mutex_lock(&(t->lock));
    uint64_t us = t->us;
    pthread_mutex_unlock(&(t->lock));
    return us;
}


double throttler_t_get_start(throttler_t *t) {
    pthread_mutex_lock(&(t->lock));
    double start = t->start;
    pthread_mutex_unlock(&(t->lock));
    return start;
}


atomic_t *init_atomic(void) {
    atomic_t *a = malloc(sizeof(atomic_t));
    a->value = 0;
    pthread_mutex_init(&a->lock, NULL);
    return a;
}


void set_value(atomic_t *a, const uint64_t value) {
    pthread_mutex_lock(&(a->lock));
    a->value = value;
    pthread_mutex_unlock(&(a->lock));
}


uint64_t get_value(atomic_t *a) {
    pthread_mutex_lock(&(a->lock));
    uint64_t v = a->value;
    pthread_mutex_unlock(&(a->lock));
    return v;
}


counter_t *init_counter(void) {
    counter_t *c = malloc(sizeof(counter_t));
    c->value = 0;
    pthread_mutex_init(&c->lock, NULL);
    return c;
}


void increment_by(counter_t *c, const uint64_t by) {
    pthread_mutex_lock(&c->lock);
    c->value += by;
    pthread_mutex_unlock(&c->lock);
}


void increment(counter_t *c) {
    increment_by(c, 1);
}


uint64_t incr_read(counter_t *c) {
    pthread_mutex_lock(&c->lock);
    c->value += 1;
    uint64_t rc = c->value;
    pthread_mutex_unlock(&c->lock);
    return rc;
}


uint64_t read_counter(counter_t *c) {
    pthread_mutex_lock(&(c->lock));
    uint64_t rc = c->value;
    pthread_mutex_unlock(&(c->lock));
    return rc;
}


void free_counter(counter_t *c) {
    free(c);
}


const char *random_name(const size_t len) {
    size_t length = len;
    char *pool = "abcdefghijklmnopqrstwxyz0123456789";
    /* Length of the string */
    int i = 0;

    char *node_name = malloc(length + 1);
    node_name[length] = '\0';

    /* build name using random positions in the poll */
    while(length--) {
        node_name[i++] = pool[(rand() % strlen(pool))];
    }

    return node_name;
}


void s_log(const uint8_t level, const char *fmt, ...) {
    va_list ap;
    char msg[MAX_LOG_SIZE];

    if (level > global.loglevel) return;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    // Just for standard output for now
    FILE *fp = stdout;
    if (!fp) return;
    // Distinguish message level prefix
    const char *mark = "I!#";
    char buf[64];
    time_t now = time(0);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(fp, "%s %c %s\n", buf, mark[level], msg);
    fflush(fp);
}


int parse_int(char *str) {
    int n = 0;
    char *s = str;
    while (*s != '\0') {
        n = (n * 10) + (*s - '0');
        s++;
    }
    return n;
}

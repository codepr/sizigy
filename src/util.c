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

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "util.h"
#include "server.h"


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
    if (!result) oom("appending string");

    // in real code you would check for errors in malloc here
    memcpy(result, s1, len1);
    memcpy(result + len1, s2, len2 + 1); // +1 to copy the null-terminator
    return result;
}


Throttler *init_throttler(void) {
    Throttler *t = malloc(sizeof(Throttler));
    t->us = 0;
    t->start = 0.0;
    pthread_mutex_init(&(t->lock), NULL);
    return t;
}


void throttler_set_us(Throttler *t, const uint64_t us) {
    pthread_mutex_lock(&(t->lock));
    t->us = us;
    t->start = clock();
    pthread_mutex_unlock(&(t->lock));
}


uint64_t throttler_get_us(Throttler *t) {
    pthread_mutex_lock(&(t->lock));
    uint64_t us = t->us;
    pthread_mutex_unlock(&(t->lock));
    return us;
}


double Throttler_get_start(Throttler *t) {
    pthread_mutex_lock(&(t->lock));
    double start = t->start;
    pthread_mutex_unlock(&(t->lock));
    return start;
}


Atomic *init_atomic(void) {
    Atomic *a = malloc(sizeof(Atomic));
    a->value = 0;
    pthread_mutex_init(&(a->lock), NULL);
    return a;
}


void write_atomic(Atomic *a, const uint64_t value) {
    pthread_mutex_lock(&(a->lock));
    a->value = value;
    pthread_mutex_unlock(&(a->lock));
}


void increment_by(Atomic *c, const uint64_t by) {
    pthread_mutex_lock(&c->lock);
    c->value += by;
    pthread_mutex_unlock(&c->lock);
}


void increment(Atomic *c) {
    increment_by(c, 1);
}


uint64_t incr_read_atomic(Atomic *c) {
    pthread_mutex_lock(&c->lock);
    c->value += 1;
    uint64_t rc = c->value;
    pthread_mutex_unlock(&c->lock);
    return rc;
}


uint64_t read_atomic(Atomic *c) {
    pthread_mutex_lock(&(c->lock));
    uint64_t rc = c->value;
    pthread_mutex_unlock(&(c->lock));
    return rc;
}


void free_atomic(Atomic *c) {
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
    char msg[MAX_LOG_SIZE + 4];

    if (level > global.loglevel) return;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    /* Truncate message too long */
    memcpy(msg+MAX_LOG_SIZE, "...", 3);
    msg[MAX_LOG_SIZE + 3] = '\0';

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


void oom(const char *msg) {
    fprintf(stderr, "malloc(3) failed: %s %s\n", strerror(errno), msg);
    fflush(stderr);
    exit(EXIT_FAILURE);
}

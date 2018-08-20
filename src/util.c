#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "util.h"
#include "server.h"


struct counter {
    uint64_t value;
    pthread_mutex_t lock;
};


void remove_newline(char *str) {
    str[strcspn(str, "\n")] = 0;
}


char* append_string(const char *s1, const char *s2) {
    const size_t len1 = strlen(s1);
    const size_t len2 = strlen(s2);
    char *result = malloc(len1 + len2 + 1); // +1 for the null-terminator
    // in real code you would check for errors in malloc here
    memcpy(result, s1, len1);
    memcpy(result + len1, s2, len2 + 1); // +1 to copy the null-terminator
    return result;
}

/* char *append_string(const char *str, const char *token) { */
/*     size_t len = strlen(str) + strlen(token); */
/*     char *ret = malloc(len * sizeof(char) + 1); */
/*     if (!ret) { */
/*         perror("malloc(3) failed"); */
/*         exit(EXIT_FAILURE); */
/*     } */
/*     *ret = '\0'; */
/*     return strcat(strcat(ret, str), token); */
/* } */


counter_t *init_counter(void) {
    counter_t *c = malloc(sizeof(counter_t));
    c->value = 0;
    pthread_mutex_init(&c->lock, NULL);
    return c;
}


void increment_by(counter_t *c, uint64_t by) {
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
    pthread_mutex_lock(&c->lock);
    uint64_t rc = c->value;
    pthread_mutex_unlock(&c->lock);
    return rc;
}


void s_log(uint8_t level, const char *info, ...) {
    /* Print log only if level is the same of the instance loglevel */
    if (level <= global.loglevel) {
        va_list argptr;
        va_start(argptr, info);
        char time_buff[50];
        char prefix[50];
        time_t now = time(0);
        strftime(prefix, 50, "%Y-%m-%d %H:%M:%S", localtime(&now));
        sprintf(time_buff, " ");
        int totlen = strlen(prefix) + strlen(info) + strlen(time_buff) + 3;
        char content[totlen];
        memset(content, 0x00, sizeof(content));
        strncpy(content, prefix, totlen - 1);
        content[totlen - 1] = '\0';
        strncat(content, time_buff, totlen - strlen(content) - 1);
        strncat(content, info, totlen - strlen(content) - 1);
        vfprintf(stdout, content, argptr);
        va_end(argptr);
    }
}


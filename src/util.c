#include <string.h>
#include <stdlib.h>
#include "util.h"


void remove_newline(char *str) {
    str[strcspn(str, "\n")] = 0;
}


char *append_string(const char *str, const char *token) {
    size_t len = strlen(str) + strlen(token);
    char *ret = malloc(len * sizeof(char) + 1);
    *ret = '\0';
    return strcat(strcat(ret, str), token);
}


void init_counter(counter_t *c) {
  c->value = 1;
  pthread_mutex_init(&c->lock, NULL);
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

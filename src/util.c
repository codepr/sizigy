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



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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include "network.h"
#include "protocol.h"


char *readline(char *prompt) {
    char buffer[BUFSIZE];
    fputs(prompt, stdout);
    fgets(buffer, BUFSIZE, stdin);
    char *cpy = malloc(strlen(buffer) + 1);
    strncpy(cpy, buffer, strlen(cpy) - 1);
    cpy[strlen(cpy)] = '\0';
    return cpy;
}


int main(int argc, char **argv) {

    int connfd = make_connection("127.0.0.1", 9090);
    ssize_t n = 0;
    char buffer[BUFSIZE];
    protocol_packet_t *quit = build_response_ack(QUIT, "");
    Buffer *quitp = pack(quit);

    while (1) {
        char *input = readline("> ");

        if (strncasecmp(input, "QUIT", 4) == 0) {
            if ((n = sendall(connfd, quitp->data, quitp->size, &(ssize_t) { 0 })) < 0)
                printf("Error packing\n");
            free(input);
            break;
        }

        protocol_packet_t *pub = build_request_publish(0, 0, "test01 ", input);
        Buffer *p_pub = pack(pub);

        if ((n = sendall(connfd, p_pub->data, p_pub->size, &(ssize_t) { 0 })) < 0)
            printf("Error packing\n");

        if ((n = recv(connfd, buffer, BUFSIZE, 0)) < 0)
            printf("Error receiving\n");

        free(p_pub);
        free(input);
    }

    free(quit);
    free(quitp);

    return 0;
}

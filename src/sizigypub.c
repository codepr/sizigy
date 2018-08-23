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
    protocol_packet_t *quit = create_data_packet(QUIT, (uint8_t *) "");
    packed_t *quitp = pack(quit);

    while (1) {
        char *input = readline("> ");

        if (strncasecmp(input, "QUIT", 4) == 0) {
            if ((n = sendall(connfd, quitp->data, quitp->size, &(ssize_t) { 0 })) < 0)
                printf("Error packing\n");
            free(input);
            break;
        }

        protocol_packet_t *pub = create_cli_pubpacket(PUBLISH_MESSAGE, 0, 0, "test01 ", input);
        packed_t *p_pub = pack(pub);

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

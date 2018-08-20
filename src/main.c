#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/eventfd.h>
#include "server.h"
#include "protocol.h"
#include "parser.h"
#include "list.h"


void sigint_handler(int signum) {
    eventfd_write(global.run, 1);
}


int main(int argc, char **argv) {
    signal(SIGINT, sigint_handler);
    start_server();
    /* char p[390]; */
    /* memset(p, 'h', 390); */
    /* p[389] = 0; */
    /* uint8_t *array = (uint8_t *) p; */
    /* memcpy(array, p, 390); */
    /* array[389] = 0; */
    /* protocol_packet_t *pp = create_cli_pubpacket(PUBLISH_MESSAGE, 0, 0, "hello", p); */
    /* packed_t *ppp = pack_clipubpacket(pp->payload.cli_pubpacket); */
    /* printf("%s\n", pp->payload.cli_pubpacket->data); */
    /* printf("%ld\n", ppp->size); */
    /* struct cli_pubpacket *cl = unpack_cli_pubpacket(ppp->data); */
    /* printf("%s\n", cl->data); */
    return 0;
}

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include "server.h"
#include "protocol.h"
#include "parser.h"
#include "list.h"

/* Catch Signal Handler functio */
void signal_callback_handler(int signum){

        printf("Caught signal SIGPIPE %d\n",signum);
}

/* static int compare(void *arg1, void *arg2) { */
/*     list_node *node1 = (list_node *) arg1; */
/*     list_node *node2 = (list_node *) arg2; */
/*     if (STR_EQ(node1->data, node2->data)) { */
/*         printf("%s == %s\n", node1->data, node2->data); */
/*         return 0; */
/*     } */
/*     return 1; */
/* } */


int main(int argc, char **argv) {
    /* struct protocol_packet packet = create_packet(); */
    /* printf("%d %d %s %ld\n", packet.type, packet.opcode, packet.data, sizeof(struct protocol_packet)); */
    /* struct packed pkd = pack(packet); */
    /* printf("%s %ld\n", pkd.data, sizeof(pkd)); */
    /* struct protocol_packet pkt = unpack(pkd.data, pkd.size); */
    /* printf("%d %d %s %ld\n", pkt.type, pkt.opcode,pkt.data, sizeof(struct protocol_packet)); */
    /* Catch Signal Handler SIGPIPE */
    /* signal(SIGPIPE, signal_callback_handler); */
    start_server();
    /* list *l = list_create(); */
    /* char *n = "hello"; */
    /* char *m = "world"; */
    /* char *s = "see"; */
    /* list_head_insert(l, n); */
    /* list_head_insert(l, m); */
    /* list_head_insert(l, s); */
    /* list_node *cursor = l->head; */
    /* while (cursor) { */
    /*     printf(" %s\n", cursor->data); */
    /*     cursor = cursor->next; */
    /* } */
    /* printf("-------\n"); */
    /* list_node node = { "world", NULL }; */
    /* l->head = list_remove_node(l->head, &node, compare); */
    /* list_node *c = l->head; */
    /* while (c) { */
    /*     printf(" %s\n", c->data); */
    /*     c = c->next; */
    /* } */
    return 0;
}

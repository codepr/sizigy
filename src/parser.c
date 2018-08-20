#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "protocol.h"
#include "util.h"


static int MAX_COMMAND_SIZE = 2048;


command_t *parse_command(protocol_packet_t *packet) {

    char tmp[MAX_COMMAND_SIZE];
    command_t *comm = malloc(sizeof(command_t));
    if (!comm) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    comm->opcode = packet->opcode;
    comm->qos = 0;

    switch (packet->opcode) {
        case ACK:
        case PING:
        case DATA:
        case CREATE_CHANNEL:
        case DELETE_CHANNEL:
        case UNSUBSCRIBE_CHANNEL:
            if (!packet->payload.data)
                comm->opcode = ERR_MISS_CHAN;
            else {
                struct build *b = malloc(sizeof(struct build));
                b->offset = 0;
                b->channel_name = strdup((char *) packet->payload.data);
                comm->cmd.b = b;
                free(packet->payload.data);
            }
            break;
        case SUBSCRIBE_CHANNEL:
            if (!packet->payload.sub_packet->channel_name)
                comm->opcode = ERR_MISS_CHAN;
            else {
                comm->qos = packet->payload.sub_packet->qos;
                struct build *b = malloc(sizeof(struct build));
                b->offset = packet->payload.sub_packet->offset;
                b->channel_name = strdup((char *) packet->payload.sub_packet->channel_name);
                comm->cmd.b = b;
                free(packet->payload.sub_packet->channel_name);
                free(packet->payload.sub_packet);
            }
            break;
        case PUBLISH_MESSAGE:
            if (packet->type == SYSTEM_PACKET) {
                comm->qos = packet->payload.sys_pubpacket->qos;
                size_t pub_len = strlen((char *) packet->payload.sys_pubpacket->data + 1);
                memcpy(tmp, packet->payload.sys_pubpacket->data, pub_len);
                tmp[pub_len] = '\0';
            }
            else {
                // XXX should check strictly for the only two options available
                comm->qos = packet->payload.cli_pubpacket->qos;
                size_t pub_len = strlen((char *) packet->payload.cli_pubpacket->data);
                memcpy(tmp, packet->payload.cli_pubpacket->data, pub_len);
                tmp[pub_len] = '\0';
            }
            remove_newline(tmp);
            char *channel = strtok(tmp, " ");
            if (!channel)
                comm->opcode = ERR_MISS_CHAN;
            else {
                /* printf("paylad=%s payload_len=%ld size=%ld len=%ld\n", channel_name, payload_len, sizeof(channel_name), strlen(channel_name)); */

                char *message_str = strtok(NULL, "\0");
                if (!message_str)
                    comm->opcode = ERR_MISS_MEX;
                else {
                    struct action *a = malloc(sizeof(struct action));
                    if (!a) {
                        perror("malloc(3) failed");
                        exit(EXIT_FAILURE);
                    }
                    a->channel_name = strdup(channel);
                    a->message = strdup(message_str);
                    if (packet->type == SYSTEM_PACKET)
                        a->redelivered = packet->payload.sys_pubpacket->redelivered;
                    else
                        // XXX should check strictly for the only two options available
                        a->redelivered = packet->payload.cli_pubpacket->redelivered;
                    comm->cmd.a = a;
                }
            }
            break;
        case QUIT:
            break;
        default:
            comm->opcode = ERR_UNKNOWN;
            break;
    }
    return comm;
}

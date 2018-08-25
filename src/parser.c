#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "protocol.h"
#include "util.h"


command_t *parse_command(protocol_packet_t *packet) {
    char *tmp = NULL;
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
        case JOIN:
        case JOIN_ACK:
        case DATA:
        case CREATE_CHANNEL:
        case DELETE_CHANNEL:
        case UNSUBSCRIBE_CHANNEL:
            if (!packet->data)
                comm->opcode = ERR_MISS_CHAN;
            else {
                struct build *b = malloc(sizeof(struct build));
                b->offset = 0;
                b->channel_name = strdup((char *) packet->data);
                comm->b = b;
                free(packet->data);
            }
            break;
        case HANDSHAKE:
            if (packet->handshake_packet->clean_session == 0 && !packet->handshake_packet->id)
                comm->opcode = ERR_MISS_ID;
            else {
                struct handshake *hp = malloc(sizeof(struct handshake));
                hp->clean_session = packet->handshake_packet->clean_session;
                if (!packet->handshake_packet->id)
                    hp->id = (char *) random_name(16);
                else
                    hp->id = (char *) strdup((char *) packet->handshake_packet->id);
                comm->h = hp;
                free(packet->handshake_packet->id);
                free(packet->handshake_packet);
            }
            break;
        case SUBSCRIBE_CHANNEL:
            if (!packet->sub_packet->channel_name)
                comm->opcode = ERR_MISS_CHAN;
            else {
                comm->qos = packet->sub_packet->qos;
                struct build *b = malloc(sizeof(struct build));
                b->offset = packet->sub_packet->offset;
                b->channel_name = strdup((char *) packet->sub_packet->channel_name);
                comm->b = b;
                free(packet->sub_packet->channel_name);
                free(packet->sub_packet);
            }
            break;
        case REPLICA:
        case PUBLISH_MESSAGE:
            if (packet->type == SYSTEM_PACKET) {
                comm->qos = packet->pub_packet->qos;
                size_t pub_len = strlen((char *) packet->pub_packet->payload + 1);
                tmp = malloc(pub_len + 1);
                memcpy(tmp, packet->pub_packet->payload, pub_len);
                tmp[pub_len] = '\0';
            }
            else {
                // XXX should check strictly for the only two options available
                comm->qos = packet->pub_packet->qos;
                size_t pub_len = strlen((char *) packet->pub_packet->data);
                tmp = malloc(pub_len + 1);
                memcpy(tmp, packet->pub_packet->data, pub_len);
                tmp[pub_len] = '\0';
            }
            remove_newline(tmp);
            char *channel = strtok(tmp, " ");
            if (!channel)
                comm->opcode = ERR_MISS_CHAN;
            else {
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
                        a->redelivered = packet->pub_packet->redelivered;
                    else
                        // XXX should check strictly for the only two options available
                        a->redelivered = packet->pub_packet->redelivered;
                    comm->a = a;
                }
            }
            break;
        case QUIT:
            break;
        default:
            comm->opcode = ERR_UNKNOWN;
            break;
    }
    free(tmp);
    return comm;
}

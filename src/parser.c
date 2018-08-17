#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "protocol.h"
#include "util.h"


static int MAX_COMMAND_SIZE = 2048;


command_t parse_protocol_command(protocol_packet_t packet) {

    char tmp[MAX_COMMAND_SIZE];
    command_t comm;
    char *channel_name = NULL;
    comm.opcode = packet.opcode;
    comm.qos = 0;

    switch (packet.opcode) {
        case ACK:
        case PING:
        case CREATE_CHANNEL:
        case DELETE_CHANNEL:
        case UNSUBSCRIBE_CHANNEL:
            if (!packet.payload.data)
                comm.opcode = ERR_MISS_CHAN;
            else {
                size_t payload_len = strlen(packet.payload.data);
                channel_name = malloc(payload_len);

                if (!channel_name) {
                    perror("malloc(3) failed");
                    exit(EXIT_FAILURE);
                }

                memcpy(channel_name, packet.payload.data, payload_len);
                /* strcpy(channel_name, packet.payload.data); */
                channel_name[payload_len] = '\0';
                struct build b = { 0, channel_name };
                comm.cmd.b = b;
            }
            break;
        case SUBSCRIBE_CHANNEL:
            if (!packet.payload.sub_packet.channel_name)
                comm.opcode = ERR_MISS_CHAN;
            else {
                size_t payload_len = strlen(packet.payload.sub_packet.channel_name);
                channel_name = malloc(payload_len);

                if (!channel_name) {
                    perror("malloc(3) failed");
                    exit(EXIT_FAILURE);
                }

                /* strcpy(channel_name, packet.payload.sub_packet.channel_name); */
                memcpy(channel_name, packet.payload.sub_packet.channel_name, payload_len);
                channel_name[payload_len] = '\0';
                comm.qos = packet.payload.sub_packet.qos;
                struct build b = { packet.payload.sub_packet.offset, channel_name };
                comm.cmd.b = b;
            }
            break;
        case PUBLISH_MESSAGE:
            if (packet.type == SYSTEM_PACKET) {
                comm.qos = packet.payload.sys_pubpacket.qos;
                size_t pub_len = strlen(packet.payload.sys_pubpacket.data);
                memcpy(tmp, packet.payload.sys_pubpacket.data, pub_len);
                tmp[pub_len] = '\0';
                /* strcpy(tmp, packet.payload.sys_pubpacket.data); */
            }
            else {
                // XXX should check strictly for the only two options available
                comm.qos = packet.payload.cli_pubpacket.qos;
                size_t pub_len = strlen(packet.payload.cli_pubpacket.data);
                memcpy(tmp, packet.payload.cli_pubpacket.data, pub_len);
                tmp[pub_len] = '\0';
                /* strcpy(tmp, packet.payload.cli_pubpacket.data); */
            }
            remove_newline(tmp);
            char *channel = strtok(tmp, " ");
            if (!channel)
                comm.opcode = ERR_MISS_CHAN;
            else {
                size_t payload_len = strlen(channel);
                channel_name = malloc(payload_len);

                if (!channel_name) {
                    perror("malloc(3) failed");
                    exit(EXIT_FAILURE);
                }

                /* strcpy(channel_name, channel); */
                memcpy(channel_name, channel, payload_len);
                channel_name[payload_len] = '\0';

                /* printf("paylad=%s payload_len=%ld size=%ld len=%ld\n", channel_name, payload_len, sizeof(channel_name), strlen(channel_name)); */

                char *message_str = strtok(NULL, "\0");
                if (!message_str)
                    comm.opcode = ERR_MISS_MEX;
                else {
                    size_t message_len = strlen(message_str);
                    char *message = malloc(message_len);

                    if (!message) {
                        perror("malloc(3) failed");
                        exit(EXIT_FAILURE);
                    }

                    /* strcpy(message, message_str); */
                    memcpy(message, message_str, message_len);
                    message[message_len] = '\0';
                    struct action a;
                    a.channel_name = channel_name;
                    a.message = message;
                    if (packet.type == SYSTEM_PACKET)
                        a.redelivered = packet.payload.sys_pubpacket.redelivered;
                    else
                        // XXX should check strictly for the only two options available
                        a.redelivered = packet.payload.cli_pubpacket.redelivered;
                    comm.cmd.a = a;
                }
            }
            break;
        case QUIT:
            break;
        default:
            comm.opcode = ERR_UNKNOWN;
            break;
    }
    return comm;
}

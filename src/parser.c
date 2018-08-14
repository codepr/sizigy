#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "protocol.h"
#include "util.h"


static int MAX_COMMAND_SIZE = 2048;


struct command parse_protocol_command(struct protocol_packet packet) {

    char tmp[MAX_COMMAND_SIZE];
    struct command comm;
    char *channel_name = NULL;
    comm.opcode = packet.opcode;
    comm.qos = 0;

    switch (packet.opcode) {
        case ACK:
        case CREATE_CHANNEL:
        case DELETE_CHANNEL:
        case UNSUBSCRIBE_CHANNEL:
            if (!packet.payload.data)
                comm.opcode = ERR_MISS_CHAN;
            else {
                channel_name = malloc(sizeof(*packet.payload.data));
                strcpy(channel_name, packet.payload.data);
                struct build b = { 0, channel_name };
                comm.cmd.b = b;
            }
            break;
        case SUBSCRIBE_CHANNEL:
            if (!packet.payload.sub_packet.channel_name)
                comm.opcode = ERR_MISS_CHAN;
            else {
                channel_name = malloc(sizeof(*packet.payload.sub_packet.channel_name));
                strcpy(channel_name, packet.payload.sub_packet.channel_name);
                comm.qos = packet.payload.sub_packet.qos;
                struct build b = { packet.payload.sub_packet.offset, channel_name };
                comm.cmd.b = b;
            }
            break;
        case PUBLISH_MESSAGE:
            if (packet.type == SYSTEM_PACKET) {
                comm.qos = packet.payload.sys_pubpacket.qos;
                strcpy(tmp, packet.payload.sys_pubpacket.data);
            }
            else {
                // XXX should check strictly for the only two options available
                comm.qos = packet.payload.cli_pubpacket.qos;
                strcpy(tmp, packet.payload.cli_pubpacket.data);
            }
            remove_newline(tmp);
            char *channel = strtok(tmp, " ");
            if (!channel)
                comm.opcode = ERR_MISS_CHAN;
            else {
                channel_name = malloc(sizeof(*channel));
                strcpy(channel_name, channel);
                char *message_str = strtok(NULL, "\0");
                if (!message_str)
                    comm.opcode = ERR_MISS_MEX;
                else {
                    char *message = malloc(sizeof(*message_str));
                    strcpy(message, message_str);
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

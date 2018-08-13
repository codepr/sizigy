#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "protocol.h"
#include "util.h"


static int MAX_COMMAND_SIZE = 1024;


struct command parse_command(char *buf) {
    char tmp[MAX_COMMAND_SIZE];
    strcpy(tmp, buf);
    remove_newline(tmp);
    char *cmd = strtok(tmp, " ");
    char *channel_name;
    struct command comm;

    if (!cmd) {
        comm.type = ERR_UNKNOWN;
        return comm;
    }

    if (STR_EQ(cmd, "QUIT")) {
        comm.type = QUIT;
    } else if (STR_EQ(cmd, "CREATE") || STR_EQ(cmd, "DELETE")
            || STR_EQ(cmd, "SUBSCRIBE") || STR_EQ(cmd, "UNSUBSCRIBE")) {

        if (STR_EQ(cmd, "CREATE"))
            comm.type = CREATE_CHANNEL;
        else if (STR_EQ(cmd, "DELETE"))
            comm.type = DELETE_CHANNEL;
        else if (STR_EQ(cmd, "SUBSCRIBE"))
            comm.type = SUBSCRIBE_CHANNEL;
        else
            comm.type = UNSUBSCRIBE_CHANNEL;
        char *channel = strtok(NULL, "\n");
        if (!channel)
            comm.type = ERR_MISS_CHAN;
        else {
            channel_name = malloc(sizeof(*channel));
            strcpy(channel_name, channel);
            struct build b = { channel_name };
            comm.cmd.b = b;
        }
    } else if (STR_EQ(cmd, "PUBLISH")) {
        char *channel = strtok(NULL, " ");
        if (!channel)
            comm.type = ERR_MISS_CHAN;
        else {
            channel_name = malloc(sizeof(*channel));
            strcpy(channel_name, channel);
            char *message_str = strtok(NULL, "\0");
            if (!message_str)
                comm.type = ERR_MISS_MEX;
            else {
                char *message = malloc(sizeof(*message_str));
                strcpy(message, message_str);
                struct action a = { channel_name, message };
                comm.type = PUBLISH_MESSAGE;
                comm.cmd.a = a;
            }
        }
    } else {
        comm.type = ERR_UNKNOWN;
    }
    return comm;
}


struct command parse_protocol_command(struct protocol_packet packet) {

    char tmp[MAX_COMMAND_SIZE];
    struct command comm;
    char *channel_name = NULL;
    comm.type = packet.opcode;
    comm.qos = packet.deliver_level;
    comm.redelivered = packet.redelivered;

    switch (packet.opcode) {
        case ACK:
        case CREATE_CHANNEL:
        case DELETE_CHANNEL:
        case SUBSCRIBE_CHANNEL:
        case UNSUBSCRIBE_CHANNEL:
            if (!packet.data)
                comm.type = ERR_MISS_CHAN;
            else {
                channel_name = malloc(sizeof(*packet.data));
                strcpy(channel_name, packet.data);
                struct build b = { channel_name };
                comm.cmd.b = b;
            }
            break;
        case PUBLISH_MESSAGE:
            strcpy(tmp, packet.data);
            remove_newline(tmp);
            char *channel = strtok(tmp, " ");
            if (!channel)
                comm.type = ERR_MISS_CHAN;
            else {
                channel_name = malloc(sizeof(*channel));
                strcpy(channel_name, channel);
                char *message_str = strtok(NULL, "\0");
                if (!message_str)
                    comm.type = ERR_MISS_MEX;
                else {
                    char *message = malloc(sizeof(*message_str));
                    strcpy(message, message_str);
                    struct action a = { channel_name, message };
                    comm.cmd.a = a;
                }
            }
            break;
        case QUIT:
            break;
        default:
            comm.type = ERR_UNKNOWN;
            break;
    }

    return comm;
}

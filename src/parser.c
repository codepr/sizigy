#include <stdio.h>
#include <stdlib.h>
#include "parser.h"


static int MAX_COMMAND_SIZE = 1024;


static void remove_newline(char *str) {
    str[strcspn(str, "\n")] = 0;
}


char *append_string(const char *str, const char *token) {
    size_t len = strlen(str) + strlen(token);
    char *ret = malloc(len * sizeof(char) + 1);
    *ret = '\0';
    return strcat(strcat(ret, str), token);
}


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
            comm.type = CREATE;
        else if (STR_EQ(cmd, "DELETE"))
            comm.type = DELETE;
        else if (STR_EQ(cmd, "SUBSCRIBE"))
            comm.type = SUBSCRIBE;
        else
            comm.type = UNSUBSCRIBE;
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
                comm.type = PUBLISH;
                comm.cmd.a = a;
            }
        }
    } else {
        comm.type = ERR_UNKNOWN;
    }
    return comm;
}

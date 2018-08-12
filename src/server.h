#ifndef NETWORKING_H
#define NETWORKING_H

#define EPOLL_WORKERS 4
#define MAX_EVENTS	  64
#define BUFSIZE		  2048

#define OK "OK\n"
#define E_UNKNOWN "ERR: Unknown command\n"
#define E_MISS_CHAN "ERR: Missing channel name\n"
#define E_MISS_MEX "ERR: Missing message to publish\n"


enum REPLY_TYPE { ACK_REPLY, NACK_REPLY, DATA_REPLY };


struct socks {
    int epollfd;
    int serversock;
};

struct reply {
    int type;
    int fd;
    char *data;
    char *channel;
};


int start_server();

#endif

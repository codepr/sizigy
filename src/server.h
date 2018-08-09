#ifndef NETWORKING_H
#define NETWORKING_H


#include "list.h"
#include "queue.h"


#define EPOLL_WORKERS 4
#define MAX_EVENTS	  64
#define BUFSIZE		  2048

#define OK "OK\n"
#define E_UNKNOWN "ERR: Unknown command\n"
#define E_MISS_CHAN "ERR: Missing channel name\n"
#define E_MISS_MEX "ERR: Missing message to publish\n"


enum REPLY_TYPE { OK_REPLY, BULK_REPLY };


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

/* struct channel { */
/*     char *name; */
/*     list *subscribers; */
/*     queue *messages; */
/* }; */
/*  */
/* struct subscriber { */
/*     int fd; */
/*     char *name; */
/* }; */


int start_server();

#endif

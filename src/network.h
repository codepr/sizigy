#ifndef NETWORK_H
#define NETWORK_H

#include "ringbuf.h"


#define BUFSIZE 2048

#define ONEMB   1048576


/* Initiate a connection to a remote host defined by a pair host:port */
int make_connection(const char *, int);

/* Set non-blocking socket */
int set_nonblocking(int );

/* Auxiliary function for creating epoll server */
int create_and_bind(const char *, const char *);

/*
 * Create a non-blocking socket and make it listen on the specfied address and
 * port
 */
int make_listen(const char *, const char *);

/* Accept a connection and add it to the right epollfd */
int accept_connection(int, int);

/* Epoll management functions */
void add_epoll(int, int, void *);
void mod_epoll(int, int, int, void *);

/* I/O management functions */
int sendall(int, uint8_t *, ssize_t *);
int recvall(int, ringbuf_t *, ssize_t);

#endif

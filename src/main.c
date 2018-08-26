#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/eventfd.h>
#include "server.h"
#include "network.h"


void sigint_handler(int signum) {
    for (int i = 0; i < EPOLL_WORKERS + 1; i++) {
        eventfd_write(global.run, 1);
        usleep(1500);
    }
}


int main(int argc, char **argv) {
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
    char *addr = "127.0.0.1";
    char *port = "9090";
    int debug = 1;
    int workers = 4;
    int fd = -1;
    int opt;

    while ((opt = getopt(argc, argv, "a:p:vn:")) != -1) {
        switch (opt) {
            case 'a':
                addr = optarg;
                break;
            case 'p':
                port = optarg;
                break;
            case 'v':
                debug = 1;
                break;
            case 'n':
                workers = atoi(optarg);
            default:
                fprintf(stderr, "Usage: %s [-a addr] [-p port] [-v]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (optind < argc) {
        if (strncasecmp(argv[optind], "join", strlen(argv[optind])) == 0) {
            char *target = argv[optind + 1];
            int tport = atoi(argv[optind + 2]);

            fd = make_connection(target, tport);
            set_nonblocking(fd);
        }
    }

    if (debug == 1) global.loglevel = DEBUG;
    else global.loglevel = INFO;

    global.workers = workers;

    start_server(addr, port, fd);
    return 0;
}

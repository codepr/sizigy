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
    int fd = -1;

    if (argc == 3) {
        addr = argv[1];
        port = argv[2];
    } else if (argc > 4) {
        addr = argv[1];
        port = argv[2];

        char *target = argv[4];
        char *tport = argv[5];

        int target_port = parse_int(tport);
        fd = make_connection(target, target_port);
        set_nonblocking(fd);
    }

    start_server(addr, port, fd);
    return 0;
}

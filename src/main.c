#include <signal.h>
#include <sys/eventfd.h>
#include "server.h"


void sigint_handler(int signum) {
    eventfd_write(global.run, 1);
}


int main(int argc, char **argv) {
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
    start_server();
    return 0;
}

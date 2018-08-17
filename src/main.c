#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/eventfd.h>
#include "server.h"
#include "protocol.h"
#include "parser.h"
#include "list.h"


void sigint_handler(int signum) {
    eventfd_write(global.run, 1);
}


int main(int argc, char **argv) {
    signal(SIGINT, sigint_handler);
    start_server();
    return 0;
}

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include "server.h"
#include "protocol.h"
#include "parser.h"
#include "list.h"

/* Catch Signal Handler functio */
void signal_callback_handler(int signum){

        printf("Caught signal SIGPIPE %d\n",signum);
}


int main(int argc, char **argv) {
    start_server();
    return 0;
}

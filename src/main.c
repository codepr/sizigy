/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2018, Andrea Giacomo Baldan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/eventfd.h>
#include "server.h"
#include "network.h"


void sigint_handler(int signum) {
    for (int i = 0; i < EPOLL_WORKERS + 1; i++) {
        eventfd_write(config.run, 1);
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
            int tport = atoi(argv[optind + 2]) + 10000;

            fd = make_connection(target, tport);
            set_nonblocking(fd);
        }
    }

    if (debug == 1) config.loglevel = DEBUG;
    else config.loglevel = INFO;

    config.workers = workers;

    start_server(addr, port, fd);
    return 0;
}

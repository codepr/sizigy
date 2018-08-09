CC=gcc
CFLAGS=-std=gnu99 -Wall -lrt -lpthread -O3 -pedantic
BIN=./bin
SRC=src/map.c 			\
	src/list.c			\
	src/queue.c			\
	src/parser.c 		\
	src/server.c		\
	src/channel.c

toyker: $(SRC)
	mkdir -p $(BIN) && $(CC) $(CFLAGS) $(SRC) src/main.c -o $(BIN)/toyker

clean:
	rm -f $(BIN)/toyker

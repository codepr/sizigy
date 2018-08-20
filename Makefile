CC=gcc
CFLAGS=-std=gnu99 -Wall -lrt -lpthread -O3 -pedantic
BIN=./bin
SRC=src/map.c 		\
	src/util.c		\
	src/list.c		\
	src/queue.c		\
	src/parser.c 	\
	src/server.c	\
	src/channel.c   \
	src/ringbuf.c	\
	src/network.c	\
	src/protocol.c

SUBSRC=src/protocol.c src/network.c src/ringbuf.c

PUBSRC=src/protocol.c src/network.c

sizigy: $(SRC)
	mkdir -p $(BIN) && $(CC) $(CFLAGS) $(SRC) src/main.c -o $(BIN)/sizigy

sizigysub: $(SUBSRC)
	mkdir -p $(BIN) && $(CC) $(CFLAGS) $(SRC) src/sizigysub.c -o $(BIN)/sizigysub

sizigypub: $(SUBSRC)
	mkdir -p $(BIN) && $(CC) $(CFLAGS) $(SRC) src/sizigypub.c -o $(BIN)/sizigypub

clean:
	rm -f $(BIN)/sizigy

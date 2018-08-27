CC=gcc
CFLAGS=-std=gnu11 -Wall -lrt -lpthread -O3 -pedantic
DEBUGFLAGS=-ggdb -fsanitize=address -fno-omit-frame-pointer
ADDITIONAL=-fsanitize=undefined
BIN=./bin
SRC=src/map.c 		\
	src/util.c		\
	src/list.c		\
	src/queue.c		\
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

debug:
	mkdir -p $(BIN) && $(CC) $(CFLAGS) $(DEBUGFLAGS) $(SRC) src/main.c -o $(BIN)/sizigy

clean:
	rm -f $(BIN)/sizigy

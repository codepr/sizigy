CC=gcc
CFLAGS=-std=c11 -Wall -lrt -lpthread -O3 -pedantic -D_DEFAULT_SOURCE
DEBUGFLAGS=-ggdb -fsanitize=address -fno-omit-frame-pointer -pg
ADDITIONAL=-fsanitize=undefined
BIN=./bin
SRC=src/hashmap.c 		\
	src/util.c		\
	src/list.c		\
	src/queue.c		\
	src/server.c	\
	src/ringbuf.c	\
	src/network.c	\
	src/protocol.c  \
	src/sizigy.c

sizigy: $(SRC)
	mkdir -p $(BIN) && $(CC) $(CFLAGS) $(SRC) src/main.c -o $(BIN)/sizigy \
		&& $(CC) $(CFLAGS) $(SRC) src/sizigysub.c -o $(BIN)/sizigysub \
		&& $(CC) $(CFLAGS) $(SRC) src/sizigypub.c -o $(BIN)/sizigypub

debug:
	mkdir -p $(BIN) && $(CC) $(CFLAGS) $(DEBUGFLAGS) $(SRC) src/main.c -o $(BIN)/sizigy \
		&& $(CC) $(CFLAGS) $(DEBUGFLAGS) $(SRC) src/sizigypub.c -o $(BIN)/sizigypub \
		&& $(CC) $(CFLAGS) $(DEBUGFLAGS) $(SRC) src/sizigysub.c -o $(BIN)/sizigysub

single:
	mkdir -p $(BIN) && $(CC) $(CFLAGS) $(DEBUGFLAGS) $(SRC) src/main.c -o $(BIN)/sizigy

s:
	mkdir -p $(BIN) && $(CC) $(CFLAGS) $(SRC) src/main.c -o $(BIN)/sizigy

clean:
	rm -f $(BIN)/*

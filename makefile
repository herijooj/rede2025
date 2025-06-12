CC=gcc
CFLAGS=-Wall -g

all: server client

server: server.c sockets.c sockets.h
	$(CC) $(CFLAGS) -o server server.c sockets.c

client: client.c sockets.c sockets.h
	$(CC) $(CFLAGS) -o client client.c sockets.c

clean:
	rm -f server client *.o

test: all
	./test_script.sh

.PHONY: all clean test

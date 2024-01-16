CC = gcc
CFLAGS = -Wextra -Wall -ggdb -std=c99

OBJECTS = server.o

all: rserver

rserver: server.o
	$(CC) $(CFLAGS) -o rserver server.o

server.o: server.c
	$(CC) $(CFLAGS) -c server.c

clean:
	rm -f $(OBJECTS) rserver
# Makefile for Text Conferencing Lab
# Adjust or add flags/libraries as necessary.

CC = gcc
CFLAGS = -Wall -pthread

all: server client

server: server.c
	$(CC) $(CFLAGS) server.c -o server

client: client.c
	$(CC) $(CFLAGS) client.c -o client

clean:
	rm -f server client *.o

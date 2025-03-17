CC = gcc
CFLAGS = -Wall

all: deliver server

deliver: deliver.c
	$(CC) $(CFLAGS) -o deliver deliver.c

server: server.c
	$(CC) $(CFLAGS) -o server server.c

clean:
	rm -f deliver server

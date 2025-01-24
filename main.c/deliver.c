/*
** talker.c -- a datagram "client" demo

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT "3490" // the port the listener is expecting to receive on

int main(int argc, char *argv[])
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;

    if (argc != 3) {
        fprintf(stderr,"usage: talker hostname message\n");
        exit(1);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;       // AF_INET or AF_INET6
    hints.ai_socktype = SOCK_DGRAM;    // UDP

    // Resolve the server hostname and port
    if ((rv = getaddrinfo(argv[1], PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(rv));
        return 1;
    }

    // Loop through all results; create a socket from the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("talker: socket");
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "talker: failed to create socket\n");
        return 2;
    }

    // Send the message to the listener
    """
    if ((numbytes = sendto(sockfd, argv[2], strlen(argv[2]), 0,
             p->ai_addr, p->ai_addrlen)) == -1) {
        perror("talker: sendto");
        exit(1);
    }
    """
    // 2) GET USER INPUT
    // ----------------------
    char userInput[256];
    printf("Enter command as ftp <filename>: ");
    if (!fgets(userInput, sizeof(userInput), stdin)) {
        perror("fgets");
        exit(1);
    }

    // Extract filename (everything after "ftp ")
    char *filename = userInput + 4;

    // Check if file exists by trying to open it
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        exit(1);
    } 
    else {
        // We can successfully open the file
        fclose(fp);
    }

    //Send message to server.c
    const char *msgToServer = userInput; 
    if ((numbytes = sendto(sockfd, msgToServer, strlen(msgToServer), 0,
                           p->ai_addr, p->ai_addrlen)) == -1) {
        perror("talker: sendto");
        exit(1);
    }

    //Wait for the server to respond
    addr_len = sizeof their_addr;
    if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN - 1, 0,
                             (struct sockaddr *)&their_addr, &addr_len)) == -1) {
        perror("talker: recvfrom");
        exit(1);
    }

    // Null-terminate the received data so we can treat it as a string
    buf[numbytes] = '\0';

    //Check the server's response
    if (strcmp(buf, "yes") == 0) {
        printf("A file transfer can start.\n");
    } else {
        exit(1);
    }


    freeaddrinfo(servinfo); // free up the linked list

    printf("talker: sent %d bytes to %s\n", numbytes, argv[1]);

    close(sockfd);

    return 0;
}
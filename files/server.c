/*
** listener.c -- a datagram sockets "server" demo
**
** From Beej’s Guide to Network Programming
** https://beej.us/guide/bgnet
**
** usage: listener <port>
**        e.g., listener 4950
*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#define MAXBUFLEN 100

int main(int argc, char *argv[])
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    struct sockaddr_storage their_addr;
    char buf[MAXBUFLEN];
    socklen_t addr_len;
    char s[INET6_ADDRSTRLEN];

    if (argc != 2) {
        fprintf(stderr,"usage: listener port\n");
        exit(1);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;       // set to AF_INET or AF_INET6 to force one or the other
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;       // use my IP

    if ((rv = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("listener: socket");
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("listener: bind");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "listener: failed to bind socket\n");
        return 2;
    }

    freeaddrinfo(servinfo);

    printf("listener: waiting to recvfrom...\n");

    addr_len = sizeof their_addr;
    if ((rv = recvfrom(sockfd, buf, MAXBUFLEN-1, 0,
        (struct sockaddr *)&their_addr, &addr_len)) == -1) {
        perror("recvfrom");
        exit(1);
    }

    printf("listener: got packet from %s\n",
        inet_ntop(their_addr.ss_family,
            (their_addr.ss_family == AF_INET ?
             (void *)&((struct sockaddr_in*)&their_addr)->sin_addr :
             (void *)&((struct sockaddr_in6*)&their_addr)->sin6_addr),
            s, sizeof s));
    printf("listener: packet is %d bytes long\n", rv);
    buf[rv] = '\0';
    printf("listener: packet contains \"%s\"\n", buf);


    //check if ftp exists
    if (strncmp(buf, "ftp ", 4) != 0) {
        fprintf(stderr, "Invalid format. Usage: ftp <filename>\n");
        return 1;
    }

    const char *confirmMsg = "yes";
    if (sendto(sockfd, confirmMsg, strlen(confirmMsg), 0,
               (struct sockaddr *)&their_addr, addr_len) == -1) {
        perror("listener: sendto (confirmMsg)");
    }


    close(sockfd);

    return 0;
}
/*
** talker.c -- a datagram "client" demo

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT "4090" // the port the listener is expecting to receive on
#define DATA_SIZE 1000
#define HEADER_SIZE 512
#define MAXBUFLEN 100

struct packet {
    unsigned int total_frag;
    unsigned int frag_no;
    unsigned int size;
    char* filename;
    char filedata[1000];
};

int main(int argc, char *argv[])
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;

    // This structure holds the address of whoever we receive from
    struct sockaddr_storage their_addr;

    // This holds the length of that address
    socklen_t addr_len;

    // This buffer holds incoming data
    char buf[MAXBUFLEN];

    // For example, if you need to track bytes read
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
    // 2) GET USER INPUT
    // ----------------------
    char userInput [512] = {'\0'};
    printf("Enter command as ftp <filename>: ");
    if (!fgets(userInput, sizeof(userInput), stdin)) {
        perror("fgets");
        exit(1);
    }

    size_t len = strlen(userInput);
    if (len > 0 && userInput[len - 1] == '\n') {
        userInput[len - 1] = '\0';
    }

    if (strncmp(userInput, "ftp", 3) != 0) {
        perror("Failed to receive command ftp");
        exit(1);
    }


    memmove(userInput, userInput + 4, strlen(userInput) - 3);

    if (access(userInput, F_OK) != 0) {
        perror("File check (access)");
        exit(1);
    }

    char filename[256];
    strncpy(filename, userInput, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';    

    // Open the file and compute total num of fragments 
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("fopen");
        exit(1);
    }
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned int total_frag = file_size / DATA_SIZE;
    if (file_size % DATA_SIZE != 0) total_frag++; 

    // Coqqqqqqqqqqqqqq
    for (unsigned int frag_no = 1; frag_no <= total_frag; frag_no++) {
         unsigned int data_size = DATA_SIZE;
         if (frag_no == total_frag && (file_size % DATA_SIZE) != 0) {
              data_size = file_size % DATA_SIZE;
         }

         // Read up to 1000 bytes from the file
         char file_buffer[DATA_SIZE];
         size_t bytes_read = fread(file_buffer, 1, data_size, fp);
         if (bytes_read != data_size) {
              perror("fread");
              exit(1);
         }

         // Build the header string: "total_frag:frag_no:size:filename:"
         // Note: The header ends with a colon. File data is appended immediately after.
         char header[512];
         int header_len = snprintf(header, sizeof(header), "%u:%u:%u:%s:",
                                   total_frag, frag_no, data_size, filename);
         if (header_len < 0) {
              perror("snprintf");
              exit(1);
         }

         // Allocate a buffer for the complete packet: header + file data.
         int packet_len = header_len + data_size;
         char *packet = malloc(packet_len);
         if (!packet) {
              perror("malloc");
              exit(1);
         }
         memcpy(packet, header, header_len);
         memcpy(packet + header_len, file_buffer, data_size);

         // Send the packet to the server.
         int sent = sendto(sockfd, packet, packet_len, 0,
                           p->ai_addr, p->ai_addrlen);
         if (sent != packet_len) {
              perror("sendto packet");
              free(packet);
              exit(1);
         }
         free(packet);

         // Wait for an ACK from the server before proceeding.
         char ack[100];
         if ((numbytes = recvfrom(sockfd, ack, sizeof(ack) - 1, 0, NULL, NULL)) == -1) {
              perror("recvfrom ack");
              exit(1);
         }
         ack[numbytes] = '\0';
         if (strcmp(ack, "ACK") != 0) {
              fprintf(stderr, "Did not receive proper ACK for fragment %u\n", frag_no);
              // For simplicity we assume correct ACK reception; in a robust design you would retry.
         }
         printf("Sent fragment %u/%u, size %u bytes\n", frag_no, total_frag, data_size);
    }


    //Send message to server.c
    const char *msgToServer = "ftp"; 
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
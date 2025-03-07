#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>

#define MAXBUFLEN 2000    // Must be large enough to hold header + up to 1000 bytes of file data
#define HEADER_SIZE 512   // Maximum header size (sufficient for "total_frag:frag_no:size:filename:")

int main(int argc, char *argv[]) {
    clock_t start, end;
    double measuredRTT;
    start = clock();
    
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <UDP listen port>\n", argv[0]);
        exit(1);
    }

    // Initialize random number generator for packet drop simulation
    srand(time(NULL));

    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;      // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_DGRAM;   // UDP
    hints.ai_flags = AI_PASSIVE;      // Use my IP

    if ((rv = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // Loop through all results and bind to the first we can.
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind socket\n");
        return 2;
    }

    freeaddrinfo(servinfo);

    printf("server: waiting for connections on port %s...\n", argv[1]);

    struct sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buf[MAXBUFLEN];
    int numbytes;

    // Initial Handshake: The client first sends a handshake message "ftp" and server sends back "yes"
    numbytes = recvfrom(sockfd, buf, MAXBUFLEN - 1, 0,
                        (struct sockaddr *)&client_addr, &addr_len);
    if (numbytes == -1) {
        perror("server: recvfrom");
        exit(1);
    }
    buf[numbytes] = '\0';
    printf("server: received initial message from %s\n",
           inet_ntop(client_addr.ss_family,
                     (client_addr.ss_family == AF_INET ?
                      (void *)&((struct sockaddr_in *)&client_addr)->sin_addr :
                      (void *)&((struct sockaddr_in6 *)&client_addr)->sin6_addr),
                     s, sizeof s));
    printf("server: initial message is \"%s\"\n", buf);

    if (strcmp(buf, "ftp") == 0) {
        const char *confirmMsg = "yes";
        if (sendto(sockfd, confirmMsg, strlen(confirmMsg), 0,
                   (struct sockaddr *)&client_addr, addr_len) == -1) {
            perror("server: sendto (confirmMsg)");
            exit(1);
        }
        printf("server: handshake complete, file transfer will begin...\n");
        end = clock();
        measuredRTT = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("Initial handshake RTT: %f seconds\n", measuredRTT);
    } else {
        fprintf(stderr, "server: unexpected initial message: %s\n", buf);
        exit(1);
    }

    // File transfer
    FILE *fp = NULL;          // File pointer for writing received file data
    int done = 0;
    while (!done) {
        numbytes = recvfrom(sockfd, buf, MAXBUFLEN, 0,
                            (struct sockaddr *)&client_addr, &addr_len);
        if (numbytes == -1) {
            perror("server: recvfrom");
            exit(1);
        }
        // Extract header using four-colon format
        int colon_count = 0, i;
        for (i = 0; i < numbytes; i++) {
            if (buf[i] == ':') {
                colon_count++;
                if (colon_count == 4)
                    break;
            }
        }
        if (colon_count < 4) {
            fprintf(stderr, "server: incomplete header received\n");
            continue;  
        }
        int header_len = i + 1;  // Include the fourth colon

        // Copy header to a temporary null-terminated string
        char header_str[HEADER_SIZE];
        if (header_len >= HEADER_SIZE) {
            fprintf(stderr, "server: header too long\n");
            continue;
        }
        memcpy(header_str, buf, header_len);
        header_str[header_len] = '\0';

        // Parse the header
        unsigned int total_frag, frag_no, data_size;
        char filename[256];
        int fields = sscanf(header_str, "%u:%u:%u:%255[^:]:", 
                            &total_frag, &frag_no, &data_size, filename);
        if (fields != 4) {
            fprintf(stderr, "server: error parsing header: %s\n", header_str);
            continue;
        }
        printf("server: received fragment %u of %u, data size: %u, file: %s\n",
               frag_no, total_frag, data_size, filename);

        // Simulate packet drop (drop 1% of packets)
        double r = (double)rand() / RAND_MAX;
        if (r < 0.01) {
            printf("server: simulating drop of fragment %u\n", frag_no);
            // Do not process or ACK this packet; let the client retransmit.
            continue;
        }

        // If this is the first fragment, open the file for writing
        if (frag_no == 1) {
            char filepath[512] = "./saved/";
            strcat(filepath, filename);
            fp = fopen(filepath, "wb");
            if (fp == NULL) {
                perror("server: fopen");
                exit(1);
            }
            printf("server: created file \"%s\" for writing\n", filepath);
        }

        // Write the file data (note: binary data, so use data_size from header)
        if (fp) {
            size_t written = fwrite(buf + header_len, 1, data_size, fp);
            if (written != data_size) {
                perror("server: fwrite");
                exit(1);
            }
        }

        // Send an acknowledgement ("ACK") back to the client
        const char *ack = "ACK";
        if (sendto(sockfd, ack, strlen(ack), 0,
                   (struct sockaddr *)&client_addr, addr_len) == -1) {
            perror("server: sendto (ACK)");
            exit(1);
        }
        printf("server: sent ACK for fragment %u\n", frag_no);

        // If this is the last fragment, close the file and finish
        if (frag_no == total_frag) {
            printf("server: last fragment received. File transfer complete.\n");
            fclose(fp);
            fp = NULL;
            done = 1; 
        }
    }
    close(sockfd);
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT "4090"     // Port on which the server is listening
#define DATA_SIZE 1000  // Maximum file fragment size
#define HEADER_SIZE 512 // Maximum header size
#define MAXBUFLEN 100   // Buffer size for incoming messages

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr,"Usage: %s <server address> <server port>\n", argv[0]);
        exit(1);
    }
    
    int sockfd, rv, numbytes;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage server_addr;
    socklen_t addr_len;
    char buf[MAXBUFLEN];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;    // AF_INET or AF_INET6
    hints.ai_socktype = SOCK_DGRAM; // UDP

    if ((rv = getaddrinfo(argv[1], argv[2], &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(rv));
        exit(1);
    }
    
    // Create a socket using the first result
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
            perror("socket");
            continue;
        }
        break;
    }
    if (p == NULL) {
        fprintf(stderr, "Failed to create socket\n");
        exit(1);
    }
    
    // Get user input: "ftp <filename>"
    char userInput[512] = {'\0'};
    printf("Enter command as ftp <filename>: ");
    if (!fgets(userInput, sizeof(userInput), stdin)) {
        perror("fgets");
        exit(1);
    }
    size_t len = strlen(userInput);
    if (len > 0 && userInput[len - 1] == '\n')
        userInput[len - 1] = '\0';
    
    if (strncmp(userInput, "ftp ", 4) != 0) {
        fprintf(stderr, "Invalid command. Command must start with 'ftp '\n");
        exit(1);
    }
    
    // Extract the filename from userInput
    char filename[256];
    strncpy(filename, userInput + 4, sizeof(filename)-1);
    filename[sizeof(filename)-1] = '\0';
    
    // Check if the file exists
    if (access(filename, F_OK) != 0) {
        perror("File check (access)");
        exit(1);
    }
    
    // Send the initial "ftp" message to the server
    const char *init_msg = "ftp";
    if ((numbytes = sendto(sockfd, init_msg, strlen(init_msg), 0,
                           p->ai_addr, p->ai_addrlen)) == -1) {
        perror("sendto (initial ftp)");
        exit(1);
    }
    
    // Wait for the server's response ("yes")
    addr_len = sizeof(server_addr);
    if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN - 1, 0,
                             (struct sockaddr *)&server_addr, &addr_len)) == -1) {
        perror("recvfrom");
        exit(1);
    }
    buf[numbytes] = '\0';
    if (strcmp(buf, "yes") != 0) {
        fprintf(stderr, "Server did not accept file transfer.\n");
        exit(1);
    }
    printf("Server accepted file transfer.\n");
    
    // Open the file and compute the total number of fragments
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("fopen");
        exit(1);
    }
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned int total_frag = file_size / DATA_SIZE;
    if (file_size % DATA_SIZE != 0)
        total_frag++; 
    
    // Send file fragments
    for (unsigned int frag_no = 1; frag_no <= total_frag; frag_no++) {
         unsigned int data_size = DATA_SIZE;
         if (frag_no == total_frag && (file_size % DATA_SIZE) != 0) {
              data_size = file_size % DATA_SIZE;
         }
         
         // Read file data
         char file_buffer[DATA_SIZE];
         size_t bytes_read = fread(file_buffer, 1, data_size, fp);
         if (bytes_read != data_size) {
              perror("fread");
              exit(1);
         }
         
         // Build header: "total_frag:frag_no:size:filename:"
         char header[HEADER_SIZE];
         int header_len = snprintf(header, sizeof(header), "%u:%u:%u:%s:",
                                   total_frag, frag_no, data_size, filename);
         if (header_len < 0) {
              perror("snprintf");
              exit(1);
         }
         
         // Allocate and build the complete packet: header + file data.
         int packet_len = header_len + data_size;
         char *packet = malloc(packet_len);
         if (!packet) {
              perror("malloc");
              exit(1);
         }
         memcpy(packet, header, header_len);
         memcpy(packet + header_len, file_buffer, data_size);
         
         // Send the packet
         int sent = sendto(sockfd, packet, packet_len, 0,
                           p->ai_addr, p->ai_addrlen);
         if (sent != packet_len) {
              perror("sendto (packet)");
              free(packet);
              exit(1);
         }
         free(packet);
         
         // Wait for ACK from the server
         char ack[100];
         if ((numbytes = recvfrom(sockfd, ack, sizeof(ack) - 1, 0, NULL, NULL)) == -1) {
              perror("recvfrom (ACK)");
              exit(1);
         }
         ack[numbytes] = '\0';
         if (strcmp(ack, "ACK") != 0) {
              fprintf(stderr, "Did not receive proper ACK for fragment %u\n", frag_no);
              // In a robust implementation, you might want to retry here.
         }
         printf("Sent fragment %u/%u, size %u bytes\n", frag_no, total_frag, data_size);
    }
    
    fclose(fp);
    freeaddrinfo(servinfo);
    close(sockfd);
    printf("File transfer complete.\n");
    return 0;
}
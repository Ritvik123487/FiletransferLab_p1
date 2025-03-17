/*
 * client.c - Text Conferencing Client Program
 *
 * Usage: ./client
 *
 * Commands:
 *   /login <clientID> <password> <server-IP> <server-port>
 *   /logout
 *   /joinsession <sessionID>
 *   /leavesession
 *   /createsession <sessionID>
 *   /list
 *   /quit
 *   <text>   (sends a message to the current session)
 *
 * This client uses a separate thread to receive messages from the server.
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 #include <pthread.h>
 #include <errno.h>
 #include <arpa/inet.h>
 #include <sys/socket.h>
 
 #define MAX_NAME  50
 #define MAX_DATA  1024
 
 // --------------------- Packet Types ---------------------
 #define LOGIN       1
 #define LO_ACK      2
 #define LO_NAK      3
 #define EXIT        4
 #define JOIN        5
 #define JN_ACK      6
 #define JN_NAK      7
 #define LEAVE_SESS  8
 #define NEW_SESS    9
 #define NS_ACK      10
 #define MESSAGE     11
 #define QUERY       12
 #define QU_ACK      13
 
 // --------------------- Message Structure ---------------------
 struct message {
     unsigned int type;
     unsigned int size;
     unsigned char source[MAX_NAME];
     unsigned char data[MAX_DATA];
 };
 
 // Global variables
 int sockfd = -1;            // Socket descriptor for server
 int loggedIn = 0;           // 1 if logged in, 0 otherwise
 pthread_t recv_thread;      // Thread to receive server messages
 char clientID[MAX_NAME] = {0};
 
 // --------------------- Utility Functions ---------------------
 
 /**
  * Send a struct message to the server.
  */
 int send_message(struct message *msg) {
     int total = sizeof(struct message);
     int sent = 0;
     int n;
     while (sent < total) {
         n = write(sockfd, ((char *)msg) + sent, total - sent);
         if (n <= 0) {
             perror("write");
             return -1;
         }
         sent += n;
     }
     return 0;
 }
 
 /**
  * Receive a struct message from the server.
  */
 int recv_message(struct message *msg) {
     int total = sizeof(struct message);
     int recvd = 0;
     int n;
     while (recvd < total) {
         n = read(sockfd, ((char *)msg) + recvd, total - recvd);
         if (n <= 0) {
             return -1;
         }
         recvd += n;
     }
     return 0;
 }
 
 /**
  * Thread function to continuously receive messages from the server.
  */
 void *receive_handler(void *arg) {
     struct message msg;
     while (1) {
         if (recv_message(&msg) < 0) {
             // Connection lost or server closed
             printf("\nDisconnected from server.\n");
             close(sockfd);
             sockfd = -1;
             loggedIn = 0;
             pthread_exit(NULL);
         }
         // Handle received messages
         switch (msg.type) {
             case MESSAGE:
                 // Print text message
                 printf("[%s]: %s\n", msg.source, msg.data);
                 break;
             case LO_ACK:
                 printf("Login successful.\n");
                 break;
             case LO_NAK:
                 printf("Login failed: %s\n", msg.data);
                 // If we fail login, we can close the socket here or remain open
                 break;
             case JN_ACK:
                 printf("Joined session: %s\n", msg.data);
                 break;
             case JN_NAK:
                 printf("Failed to join session: %s\n", msg.data);
                 break;
             case NS_ACK:
                 printf("Created and joined new session: %s\n", msg.data);
                 break;
             case QU_ACK:
                 printf("List of users and sessions:\n%s\n", msg.data);
                 break;
             default:
                 printf("Received unknown message type: %d\n", msg.type);
                 break;
         }
     }
     return NULL;
 }
 
 /**
  * Helper function to clear a message struct.
  */
 void clear_message(struct message *msg) {
     memset(msg, 0, sizeof(struct message));
 }
 
 // --------------------- Main ---------------------
 int main() {
     char input[MAX_DATA];
     char command[50];
 
     printf("Text Conferencing Client\n");
     printf("Commands:\n");
     printf("  /login <clientID> <password> <server-IP> <server-port>\n");
     printf("  /logout\n");
     printf("  /joinsession <sessionID>\n");
     printf("  /leavesession\n");
     printf("  /createsession <sessionID>\n");
     printf("  /list\n");
     printf("  /quit\n");
     printf("  <text>\n\n");
 
     while (1) {
         printf("> ");
         fflush(stdout);
         if (fgets(input, sizeof(input), stdin) == NULL) {
             continue;
         }
         // Remove trailing newline
         input[strcspn(input, "\n")] = '\0';
 
         // Parse first token as command
         if (sscanf(input, "%s", command) != 1) {
             continue;
         }
 
         // ----------------------------------------------------
         // /login <clientID> <password> <server-IP> <server-port>
         // ----------------------------------------------------
         if (strcmp(command, "/login") == 0) {
             if (loggedIn) {
                 printf("Already logged in.\n");
                 continue;
             }
 
             char password[50], serverIP[50];
             int serverPort;
             // Expect four parameters after /login
             if (sscanf(input, "/login %s %s %s %d", clientID, password, serverIP, &serverPort) != 4) {
                 printf("Usage: /login <clientID> <password> <server-IP> <server-port>\n");
                 continue;
             }
 
             // Create socket
             sockfd = socket(AF_INET, SOCK_STREAM, 0);
             if (sockfd < 0) {
                 perror("socket");
                 continue;
             }
 
             // Setup server address
             struct sockaddr_in serv_addr;
             memset(&serv_addr, 0, sizeof(serv_addr));
             serv_addr.sin_family = AF_INET;
             serv_addr.sin_port = htons(serverPort);
             if (inet_pton(AF_INET, serverIP, &serv_addr.sin_addr) <= 0) {
                 perror("inet_pton");
                 close(sockfd);
                 sockfd = -1;
                 continue;
             }
 
             // Connect to server
             if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
                 perror("connect");
                 close(sockfd);
                 sockfd = -1;
                 continue;
             }
 
             // Build LOGIN message
             struct message msg;
             clear_message(&msg);
             msg.type = LOGIN;
             // Put clientID in source
             strncpy((char *)msg.source, clientID, MAX_NAME - 1);
             // Put password in data
             strncpy((char *)msg.data, password, MAX_DATA - 1);
             msg.size = strlen((char *)msg.data);
 
             // Send LOGIN
             if (send_message(&msg) < 0) {
                 printf("Failed to send login message.\n");
                 close(sockfd);
                 sockfd = -1;
                 continue;
             }
 
             // Start a receiver thread
             if (pthread_create(&recv_thread, NULL, receive_handler, NULL) != 0) {
                 perror("pthread_create");
                 close(sockfd);
                 sockfd = -1;
                 continue;
             }
 
             loggedIn = 1;
         }
         // -------------
         // /logout
         // -------------
         else if (strcmp(command, "/logout") == 0) {
             if (!loggedIn || sockfd < 0) {
                 printf("Not logged in.\n");
                 continue;
             }
             struct message msg;
             clear_message(&msg);
             msg.type = EXIT;
             // Send exit message to server
             send_message(&msg);
 
             // Cancel the receiver thread and close socket
             pthread_cancel(recv_thread);
             pthread_join(recv_thread, NULL);
             close(sockfd);
             sockfd = -1;
             loggedIn = 0;
             printf("Logged out.\n");
         }
         // ----------------
         // /joinsession <sessionID>
         // ----------------
         else if (strcmp(command, "/joinsession") == 0) {
             if (!loggedIn || sockfd < 0) {
                 printf("You must be logged in first.\n");
                 continue;
             }
             char sessionID[50];
             if (sscanf(input, "/joinsession %s", sessionID) != 1) {
                 printf("Usage: /joinsession <sessionID>\n");
                 continue;
             }
             struct message msg;
             clear_message(&msg);
             msg.type = JOIN;
             strncpy((char *)msg.data, sessionID, MAX_DATA - 1);
             msg.size = strlen((char *)msg.data);
             send_message(&msg);
         }
         // -------------
         // /leavesession
         // -------------
         else if (strcmp(command, "/leavesession") == 0) {
             if (!loggedIn || sockfd < 0) {
                 printf("You must be logged in first.\n");
                 continue;
             }
             struct message msg;
             clear_message(&msg);
             msg.type = LEAVE_SESS;
             send_message(&msg);
         }
         // ----------------
         // /createsession <sessionID>
         // ----------------
         else if (strcmp(command, "/createsession") == 0) {
             if (!loggedIn || sockfd < 0) {
                 printf("You must be logged in first.\n");
                 continue;
             }
             char sessionID[50];
             if (sscanf(input, "/createsession %s", sessionID) != 1) {
                 printf("Usage: /createsession <sessionID>\n");
                 continue;
             }
             struct message msg;
             clear_message(&msg);
             msg.type = NEW_SESS;
             strncpy((char *)msg.data, sessionID, MAX_DATA - 1);
             msg.size = strlen((char *)msg.data);
             send_message(&msg);
         }
         // -------------
         // /list
         // -------------
         else if (strcmp(command, "/list") == 0) {
             if (!loggedIn || sockfd < 0) {
                 printf("You must be logged in first.\n");
                 continue;
             }
             struct message msg;
             clear_message(&msg);
             msg.type = QUERY;
             send_message(&msg);
         }
         // -------------
         // /quit
         // -------------
         else if (strcmp(command, "/quit") == 0) {
             if (loggedIn && sockfd >= 0) {
                 // Send EXIT to server
                 struct message msg;
                 clear_message(&msg);
                 msg.type = EXIT;
                 send_message(&msg);
                 // Cleanup
                 pthread_cancel(recv_thread);
                 pthread_join(recv_thread, NULL);
                 close(sockfd);
             }
             printf("Exiting client.\n");
             exit(0);
         }
         // -------------
         // Send message to session
         // -------------
         else {
             if (!loggedIn || sockfd < 0) {
                 printf("You must be logged in to send messages.\n");
                 continue;
             }
             // Treat this as a message to the current session
             struct message msg;
             clear_message(&msg);
             msg.type = MESSAGE;
             strncpy((char *)msg.data, input, MAX_DATA - 1);
             msg.size = strlen((char *)msg.data);
             // Fill source if desired (server might overwrite anyway)
             strncpy((char *)msg.source, clientID, MAX_NAME - 1);
             send_message(&msg);
         }
     }
     return 0;
 }
 
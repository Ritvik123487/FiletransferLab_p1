/*
 * server.c - Text Conferencing Server Program with Multiple Sessions
 *            and Inactivity Timer
 *
 * Usage: ./server <port>
 *
 * The server listens on <port>, accepts multiple client connections,
 * routes text messages for conferencing, and disconnects clients that
 * have been inactive for a long time.
 *
 * IMPORTANT:
 *  - This code is a skeleton to demonstrate the overall approach.
 *  - You must adjust data structures, concurrency mechanisms,
 *    and error handling as needed.
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 #include <pthread.h>
 #include <errno.h>
 #include <arpa/inet.h>
 #include <sys/socket.h>
 #include <sys/types.h>
 #include <netinet/in.h>
 #include <time.h>   // for time functions
 
 // --------------------- DEFINITIONS ---------------------
 #define MAX_CLIENTS  100   // Max number of concurrently connected clients
 #define MAX_SESSIONS 100   // Max number of conference sessions
 #define MAX_NAME     50
 #define MAX_DATA     1024
 #define INACTIVITY_THRESHOLD 60  // Inactivity threshold in seconds
 
 // Packet type definitions (must match client)
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
 
 // --------------------- DATA STRUCTURES ---------------------
 
 // Updated message structure with a session field.
 struct message {
     unsigned int type;
     unsigned int size;
     unsigned char source[MAX_NAME];
     unsigned char session[MAX_NAME];  // holds session id for this message
     unsigned char data[MAX_DATA];
 };
 
 // Information about a single client.
 // Updated to support multiple sessions and track last activity time.
 typedef struct {
     int  sockfd;                      // Socket descriptor
     char clientID[MAX_NAME];          // Unique client name (ID)
     char sessions[MAX_SESSIONS][MAX_NAME]; // List of sessions this client has joined
     int  session_count;               // Number of sessions the client is in
     struct sockaddr_in clientAddr;    // Client address
     int  active;                      // 1 if logged in, 0 otherwise
     time_t last_active;               // Timestamp of last activity
 } client_t;
 
 // Information about a single conference session
 typedef struct {
     char sessionID[MAX_NAME];
     int  num_members;
     char members[MAX_CLIENTS][MAX_NAME];  // List of client IDs
 } session_t;
 
 // --------------------- GLOBALS ---------------------
 static client_t   clients[MAX_CLIENTS];     // Connected clients
 static session_t  sessions[MAX_SESSIONS];     // Active sessions
 static int        num_sessions = 0;           // Number of currently active sessions
 
 // A simple, hard-coded user database
 typedef struct {
     char username[MAX_NAME];
     char password[MAX_NAME];
 } user_db_entry;
 
 static user_db_entry user_db[] = {
     {"jill", "eW94dsol"},
     {"jack", "432wlFd"},
     {"alice", "12345"},
     {"bob",   "qwerty"},
     { "", "" } // sentinel
 };
 
 // For thread-safety, we use a mutex to guard shared data structures
 pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
 
 // --------------------- UTILITY FUNCTIONS ---------------------
 
 /**
  * Send a struct message to the given socket.
  */
 int send_message(int sockfd, struct message *msg) {
     int total = sizeof(struct message);
     int sent = 0, n;
     while (sent < total) {
         n = write(sockfd, ((char*)msg) + sent, total - sent);
         if (n <= 0) {
             perror("send_message write");
             return -1;
         }
         sent += n;
     }
     return 0;
 }
 
 /**
  * Receive a struct message from the given socket.
  */
 int recv_message(int sockfd, struct message *msg) {
     int total = sizeof(struct message);
     int recvd = 0, n;
     while (recvd < total) {
         n = read(sockfd, ((char*)msg) + recvd, total - recvd);
         if (n <= 0) {
             return -1;
         }
         recvd += n;
     }
     return 0;
 }
 
 /**
  * Check if username/password is in our user_db.
  * Return 1 if valid, 0 if invalid.
  */
 int authenticate_user(const char *username, const char *password) {
     for (int i = 0; user_db[i].username[0] != '\0'; i++) {
         if (strcmp(user_db[i].username, username) == 0 &&
             strcmp(user_db[i].password, password) == 0) {
             return 1;
         }
     }
     return 0;
 }
 
 /**
  * Find an available client slot (or -1 if none).
  */
 int find_free_client_slot() {
     for (int i = 0; i < MAX_CLIENTS; i++) {
         if (!clients[i].active) {
             return i;
         }
     }
     return -1;
 }
 
 /**
  * Find a client slot by client ID (return index or -1).
  */
 int find_client_by_id(const char *clientID) {
     for (int i = 0; i < MAX_CLIENTS; i++) {
         if (clients[i].active &&
             strcmp(clients[i].clientID, clientID) == 0) {
             return i;
         }
     }
     return -1;
 }
 
 /**
  * Find a session by sessionID (return index or -1 if not found).
  */
 int find_session(const char *sessionID) {
     for (int i = 0; i < num_sessions; i++) {
         if (strcmp(sessions[i].sessionID, sessionID) == 0) {
             return i;
         }
     }
     return -1;
 }
 
 /**
  * Create a new session (return index or -1 on error).
  */
 int create_session(const char *sessionID) {
     if (num_sessions >= MAX_SESSIONS) {
         return -1;
     }
     if (find_session(sessionID) != -1) {
         return -1;  // already exists
     }
     strncpy(sessions[num_sessions].sessionID, sessionID, MAX_NAME - 1);
     sessions[num_sessions].num_members = 0;
     num_sessions++;
     return (num_sessions - 1);
 }
 
 /**
  * Add a client to a session (returns 0 on success, -1 on error).
  */
 int add_client_to_session(const char *clientID, const char *sessionID) {
     int sidx = find_session(sessionID);
     if (sidx < 0) {
         return -1; // session not found
     }
     session_t *sess = &sessions[sidx];
     for (int i = 0; i < sess->num_members; i++) {
         if (strcmp(sess->members[i], clientID) == 0) {
             return 0; // already a member, do nothing
         }
     }
     if (sess->num_members < MAX_CLIENTS) {
         strncpy(sess->members[sess->num_members], clientID, MAX_NAME - 1);
         sess->num_members++;
         return 0;
     }
     return -1;
 }
 
 /**
  * Remove a client from a session.
  */
 void remove_client_from_session(const char *clientID, const char *sessionID) {
     int sidx = find_session(sessionID);
     if (sidx < 0) {
         return;
     }
     session_t *sess = &sessions[sidx];
     int found_index = -1;
     for (int i = 0; i < sess->num_members; i++) {
         if (strcmp(sess->members[i], clientID) == 0) {
             found_index = i;
             break;
         }
     }
     if (found_index != -1) {
         for (int i = found_index; i < sess->num_members - 1; i++) {
             strcpy(sess->members[i], sess->members[i + 1]);
         }
         sess->num_members--;
     }
     if (sess->num_members == 0) {
         for (int i = sidx; i < num_sessions - 1; i++) {
             sessions[i] = sessions[i + 1];
         }
         num_sessions--;
     }
 }
 
 /**
  * Broadcast a message to all clients in the given session.
  */
 void broadcast_message(const char *sessionID, struct message *msg) {
     int sidx = find_session(sessionID);
     if (sidx < 0) {
         return;
     }
     session_t *sess = &sessions[sidx];
     for (int i = 0; i < sess->num_members; i++) {
         int cidx = find_client_by_id(sess->members[i]);
         if (cidx >= 0) {
             send_message(clients[cidx].sockfd, msg);
         }
     }
 }
 
 /**
  * Build a list of all connected clients and sessions for QU_ACK.
  */
 void build_list(char *outbuf, int outbuf_size) {
     char tmp[1024];
     memset(tmp, 0, sizeof(tmp));
 
     strcat(tmp, "Users:\n");
     for (int i = 0; i < MAX_CLIENTS; i++) {
         if (clients[i].active) {
             strcat(tmp, "  ");
             strcat(tmp, clients[i].clientID);
             strcat(tmp, "\n");
         }
     }
 
     strcat(tmp, "\nSessions:\n");
     for (int i = 0; i < num_sessions; i++) {
         char line[256];
         snprintf(line, sizeof(line), "  %s (%d members)\n",
                  sessions[i].sessionID, sessions[i].num_members);
         strcat(tmp, line);
     }
 
     strncpy(outbuf, tmp, outbuf_size - 1);
 }
 
 // --------------------- INACTIVITY MONITOR THREAD ---------------------
 
 void *inactivity_monitor(void *arg) {
     while (1) {
         sleep(5);  // Check every 5 seconds
         pthread_mutex_lock(&mutex);
         time_t now = time(NULL);
         for (int i = 0; i < MAX_CLIENTS; i++) {
             if (clients[i].active) {
                 if (difftime(now, clients[i].last_active) > INACTIVITY_THRESHOLD) {
                     printf("Disconnecting client '%s' due to inactivity.\n", clients[i].clientID);
                     for (int j = 0; j < clients[i].session_count; j++) {
                         remove_client_from_session(clients[i].clientID, clients[i].sessions[j]);
                     }
                     clients[i].session_count = 0;
                     close(clients[i].sockfd);
                     clients[i].active = 0;
                 }
             }
         }
         pthread_mutex_unlock(&mutex);
     }
     return NULL;
 }
 
 // --------------------- PER-CLIENT THREAD ---------------------
 
 void *client_thread(void *arg) {
     int my_index = *(int*)arg;
     free(arg);  // allocated in main accept loop
     int sockfd = clients[my_index].sockfd;
     char clientID[MAX_NAME];
     strcpy(clientID, clients[my_index].clientID);
 
     struct message msg;
 
     while (1) {
         if (recv_message(sockfd, &msg) < 0) {
             break;
         }
         pthread_mutex_lock(&mutex);
         // Update last activity time upon receiving any message.
         clients[my_index].last_active = time(NULL);
 
         if (msg.type == EXIT) {
             for (int i = 0; i < clients[my_index].session_count; i++) {
                 remove_client_from_session(clientID, clients[my_index].sessions[i]);
             }
             clients[my_index].session_count = 0;
             clients[my_index].active = 0;
             close(sockfd);
             pthread_mutex_unlock(&mutex);
             printf("Client '%s' logged out.\n", clientID);
             return NULL;
         }
         else if (msg.type == JOIN) {
             char sessionID[MAX_NAME];
             strncpy(sessionID, (char*)msg.data, MAX_NAME - 1);
             int sidx = find_session(sessionID);
             if (sidx < 0) {
                 struct message nak;
                 memset(&nak, 0, sizeof(nak));
                 nak.type = JN_NAK;
                 snprintf((char*)nak.data, MAX_DATA, "%s: session not found", sessionID);
                 send_message(sockfd, &nak);
             } else {
                 int alreadyIn = 0;
                 for (int i = 0; i < clients[my_index].session_count; i++) {
                     if (strcmp(clients[my_index].sessions[i], sessionID) == 0) {
                         alreadyIn = 1;
                         break;
                     }
                 }
                 if (alreadyIn) {
                     struct message ack;
                     memset(&ack, 0, sizeof(ack));
                     ack.type = JN_ACK;
                     strncpy((char*)ack.data, sessionID, MAX_DATA - 1);
                     send_message(sockfd, &ack);
                 } else {
                     if (add_client_to_session(clientID, sessionID) == 0) {
                         if (clients[my_index].session_count < MAX_SESSIONS) {
                             strncpy(clients[my_index].sessions[clients[my_index].session_count],
                                     sessionID, MAX_NAME - 1);
                             clients[my_index].session_count++;
                         }
                         struct message ack;
                         memset(&ack, 0, sizeof(ack));
                         ack.type = JN_ACK;
                         strncpy((char*)ack.data, sessionID, MAX_DATA - 1);
                         send_message(sockfd, &ack);
                         printf("Client '%s' joined session '%s'.\n", clientID, sessionID);
                     } else {
                         struct message nak;
                         memset(&nak, 0, sizeof(nak));
                         nak.type = JN_NAK;
                         strcpy((char*)nak.data, "Session is full or error adding");
                         send_message(sockfd, &nak);
                     }
                 }
             }
         }
         else if (msg.type == LEAVE_SESS) {
             char sessionID[MAX_NAME];
             strncpy(sessionID, (char*)msg.session, MAX_NAME - 1);
             int found = -1;
             for (int i = 0; i < clients[my_index].session_count; i++) {
                 if (strcmp(clients[my_index].sessions[i], sessionID) == 0) {
                     found = i;
                     break;
                 }
             }
             if (found != -1) {
                 remove_client_from_session(clientID, sessionID);
                 for (int i = found; i < clients[my_index].session_count - 1; i++) {
                     strcpy(clients[my_index].sessions[i], clients[my_index].sessions[i+1]);
                 }
                 clients[my_index].session_count--;
                 printf("Client '%s' left session '%s'.\n", clientID, sessionID);
             }
         }
         else if (msg.type == NEW_SESS) {
             char newSessionID[MAX_NAME];
             strncpy(newSessionID, (char*)msg.data, MAX_NAME - 1);
             int sidx = create_session(newSessionID);
             if (sidx < 0) {
                 struct message nak;
                 memset(&nak, 0, sizeof(nak));
                 nak.type = JN_NAK;
                 snprintf((char*)nak.data, MAX_DATA, "Failed to create session %s", newSessionID);
                 send_message(sockfd, &nak);
             } else {
                 add_client_to_session(clientID, newSessionID);
                 if (clients[my_index].session_count < MAX_SESSIONS) {
                     strncpy(clients[my_index].sessions[clients[my_index].session_count],
                             newSessionID, MAX_NAME - 1);
                     clients[my_index].session_count++;
                 }
                 struct message ack;
                 memset(&ack, 0, sizeof(ack));
                 ack.type = NS_ACK;
                 strncpy((char*)ack.data, newSessionID, MAX_DATA - 1);
                 send_message(sockfd, &ack);
                 printf("Client '%s' created session '%s'.\n", clientID, newSessionID);
             }
         }
         else if (msg.type == MESSAGE) {
             strncpy((char*)msg.source, clientID, MAX_NAME - 1);
             broadcast_message((char*)msg.session, &msg);
         }
         else if (msg.type == QUERY) {
             char buf[1024];
             memset(buf, 0, sizeof(buf));
             build_list(buf, sizeof(buf));
             struct message ack;
             memset(&ack, 0, sizeof(ack));
             ack.type = QU_ACK;
             strncpy((char*)ack.data, buf, MAX_DATA - 1);
             ack.size = strlen((char*)ack.data);
             send_message(sockfd, &ack);
         }
         else {
             fprintf(stderr, "Unknown message type %d from client %s\n",
                     msg.type, clientID);
         }
         pthread_mutex_unlock(&mutex);
     }
 
     // Handle abrupt disconnection.
     pthread_mutex_lock(&mutex);
     if (clients[my_index].active) {
         for (int i = 0; i < clients[my_index].session_count; i++) {
             remove_client_from_session(clientID, clients[my_index].sessions[i]);
         }
         clients[my_index].session_count = 0;
         clients[my_index].active = 0;
         close(sockfd);
         printf("Client '%s' disconnected.\n", clientID);
     }
     pthread_mutex_unlock(&mutex);
 
     return NULL;
 }
 
 // --------------------- MAIN FUNCTION ---------------------
 
 int main(int argc, char *argv[]) {
     if (argc != 2) {
         fprintf(stderr, "Usage: %s <port>\n", argv[0]);
         exit(EXIT_FAILURE);
     }
     int port = atoi(argv[1]);
 
     memset(clients, 0, sizeof(clients));
     memset(sessions, 0, sizeof(sessions));
 
     int server_sock = socket(AF_INET, SOCK_STREAM, 0);
     if (server_sock < 0) {
         perror("socket");
         exit(EXIT_FAILURE);
     }
 
     int optval = 1;
     setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
 
     struct sockaddr_in serv_addr;
     memset(&serv_addr, 0, sizeof(serv_addr));
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(port);
 
     if (bind(server_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
         perror("bind");
         close(server_sock);
         exit(EXIT_FAILURE);
     }
 
     if (listen(server_sock, 10) < 0) {
         perror("listen");
         close(server_sock);
         exit(EXIT_FAILURE);
     }
 
     printf("Server listening on port %d...\n", port);
 
     // Start the inactivity monitor thread.
     pthread_t monitor_tid;
     if (pthread_create(&monitor_tid, NULL, inactivity_monitor, NULL) != 0) {
         perror("pthread_create for inactivity monitor");
         exit(EXIT_FAILURE);
     }
 
     while (1) {
         struct sockaddr_in client_addr;
         socklen_t addr_len = sizeof(client_addr);
         int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
         if (client_sock < 0) {
             perror("accept");
             continue;
         }
 
         struct message msg;
         if (recv_message(client_sock, &msg) < 0) {
             close(client_sock);
             continue;
         }
 
         if (msg.type != LOGIN) {
             close(client_sock);
             continue;
         }
 
         char clientID[MAX_NAME];
         char password[MAX_DATA];
         strncpy(clientID, (char*)msg.source, MAX_NAME - 1);
         strncpy(password, (char*)msg.data, MAX_DATA - 1);
 
         pthread_mutex_lock(&mutex);
 
         if (find_client_by_id(clientID) != -1) {
             struct message nak;
             memset(&nak, 0, sizeof(nak));
             nak.type = LO_NAK;
             strcpy((char*)nak.data, "Client ID already in use");
             send_message(client_sock, &nak);
             close(client_sock);
             pthread_mutex_unlock(&mutex);
             continue;
         }
 
         if (!authenticate_user(clientID, password)) {
             struct message nak;
             memset(&nak, 0, sizeof(nak));
             nak.type = LO_NAK;
             strcpy((char*)nak.data, "Invalid username/password");
             send_message(client_sock, &nak);
             close(client_sock);
             pthread_mutex_unlock(&mutex);
             continue;
         }
 
         int idx = find_free_client_slot();
         if (idx < 0) {
             struct message nak;
             memset(&nak, 0, sizeof(nak));
             nak.type = LO_NAK;
             strcpy((char*)nak.data, "Server full");
             send_message(client_sock, &nak);
             close(client_sock);
             pthread_mutex_unlock(&mutex);
             continue;
         }
 
         clients[idx].sockfd = client_sock;
         strncpy(clients[idx].clientID, clientID, MAX_NAME - 1);
         clients[idx].clientAddr = client_addr;
         clients[idx].active = 1;
         clients[idx].session_count = 0;
         clients[idx].last_active = time(NULL);  // Set initial activity time
 
         struct message ack;
         memset(&ack, 0, sizeof(ack));
         ack.type = LO_ACK;
         strcpy((char*)ack.data, "Login successful");
         send_message(client_sock, &ack);
 
         printf("Client '%s' logged in.\n", clientID);
 
         pthread_t tid;
         int *arg = malloc(sizeof(int));
         *arg = idx;
         if (pthread_create(&tid, NULL, client_thread, arg) != 0) {
             perror("pthread_create");
             clients[idx].active = 0;
             close(client_sock);
         }
 
         pthread_mutex_unlock(&mutex);
     }
 
     close(server_sock);
     return 0;
 }
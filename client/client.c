// UDP Peer and TCP File Peer

/* DEFINITIONS */
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <arpa/inet.h>

#define DEFAULT_NAME_SIZE 20
#define STANDARD_BUF_SIZE 99
#define CONTENT_BUF_SIZE 1280


/* STRUCTS */
struct __attribute__((__packed__)) pdu {
    // Struct for standard datagram 
    char type; 
    char data[STANDARD_BUF_SIZE]; 
};
struct __attribute__((__packed__)) cpdu {
    // Struct for standard datagram 
    char type; 
    char data[CONTENT_BUF_SIZE];
};
struct __attribute__((__packed__)) spdu {
    // Struct for requesting TCP information
    char type;
    char peer_name[DEFAULT_NAME_SIZE];
    char content_name[DEFAULT_NAME_SIZE];
};
struct __attribute__((__packed__)) rpdu {
    // Struct for registering new files
    char type;
    char peer_name[DEFAULT_NAME_SIZE];
    char content_name[DEFAULT_NAME_SIZE];
    char address[30];
};
struct __attribute__((__packed__)) File {
    // Struct for tracking which files we have active
    int s;
    struct rpdu file_descriptor;
    struct File *next;
};
struct File* head = NULL;

// Global client name to be passed as a command line argument to identify this user with and debug flag for showing for print messages
int debug = 0;
char client_name[DEFAULT_NAME_SIZE];

/* UTILITY FUNCTIONS */

// MISC
void processFileDownload(int sockfd, char *content_name)
{ // This file processes the TCP upload of the content_server. We open the file, write it to a buffer byte by byte until there is nothing left to read, 
  // and then we tell the content_client that there is nothing left to receive before exiting
    FILE *fp = fopen(content_name, "r");

    struct cpdu file_content_packet;
    bzero(&file_content_packet, sizeof(file_content_packet));
    file_content_packet.type = 'C';
    while (fgets(file_content_packet.data, CONTENT_BUF_SIZE, fp) != NULL)
    {
        if (send(sockfd, &file_content_packet, sizeof(file_content_packet), 0) == -1)
        {
            perror("Error while sending file contents...\n");
            return;
        }
        bzero(file_content_packet.data, CONTENT_BUF_SIZE);
    }

    bzero(&file_content_packet, sizeof(file_content_packet));
    file_content_packet.type = 'E';
    if (send(sockfd, &file_content_packet, sizeof(file_content_packet), 0) == -1)
    {
        perror("Error while sending file contents...\n");
        return;
    }
}
int acceptNewClient(int socket)
{ // Accept incoming TCP connection found by select multiplexing
    int client_socket;
    struct sockaddr_in client_addr;
    int addr_size = sizeof(client_addr);

    client_socket = accept(socket, (struct sockaddr *)&client_addr, &addr_size);
    if (client_socket < 0)
    {
        printf("Failed to accept new client connection...\n");
    }
    return client_socket;
}
void handleDownload(int sockfd)
{ // INCOMING TCP connection being handled...
    int new_sd = acceptNewClient(sockfd);
    if (new_sd < 0)
    {
        printf("Cannot handle incoming TCP request...\n");
        return;
    }
    struct pdu content_name;
    bzero(&content_name, sizeof(content_name));

    if (recv(new_sd, &content_name, sizeof(content_name), 0) < 0)
    {
        printf("No filename received from content peer...\n");
        return;
    }
    processFileDownload(new_sd, content_name.data);
}

// R
void addToHostedFiles(int newsockfd, struct rpdu h_file)
{ // Track files which index_server is tracking as available at this content server
    if (head == NULL)
    {
        head = (struct File*)malloc(sizeof(struct File));
        head->s = newsockfd; // available
        head->file_descriptor = h_file;
        head->next = NULL;
    } else
    {
        struct File* new_head = (struct File*)malloc(sizeof(struct File));
        new_head->s = newsockfd;
        new_head->file_descriptor = h_file;
        new_head->next = head;
        head = new_head;
    }
}
int waitRegisteredAcknowledgement(int sockfd, int *s, struct sockaddr_in socket_addr, int socket_addr_size)
{ // Process server status response from corresponding file registration request
    struct pdu server_response;
    bzero(&server_response, sizeof(server_response));
    if (recvfrom(sockfd, &server_response, sizeof(server_response), 0, (struct sockaddr*)&socket_addr, &socket_addr_size) < 0)
    { // TO-DO: Critical errors are when the client and server lose sync. This will require a restart and will later be handled more robustly
        printf("CRITICAL ERROR... Please try again later.\n");
        close(*s);
        return 0;
    }
    if (server_response.type == 'A')
    { // Accept or reject file based on if the server successfully registered it or not. Return the corresponding boolean
        printf("File accepted\n");
        return 1;
    } else if (server_response.type == 'E')
    {
        printf("Something went wrong... %s\n", server_response.data);
        close(*s);
        return 0;
    }
    return 0;
}
void makePassiveSocket(int sockfd, struct sockaddr_in socket_addr, int socket_addr_size)
{ // Create TCP socket to host file with and create associated rpdu struct for the file.
    struct rpdu this;
    bzero(&this, sizeof(this));
    this.type = 'R';
    strcpy(this.peer_name, client_name);

    printf("Which file would you like to register? \n");
    scanf("%s", this.content_name);

    /* Harasees Singh Gill's heuristic to get Ubuntu 20.04 private IP address
        Essentially we write the output of "hostname -I" to a file, tokenize and split it, and then read the corresponding IP address for the machine*/
    FILE *ls_cmd = popen("hostname -I", "r");
    if (ls_cmd == NULL) {
        fprintf(stderr, "popen(3) error");
        exit(EXIT_FAILURE);
    }

    static char buff[1024];
    size_t n;

    while ((n = fread(buff, 1, sizeof(buff)-1, ls_cmd)) > 0) {
        buff[n] = '\0';
    }

    char THIS_IP[INET_ADDRSTRLEN];
    strcpy(THIS_IP, strtok(buff, " "));

    // Create new socket with some available port and the machine IP we found above
    struct sockaddr_in reg_addr;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    bzero(&reg_addr, sizeof(reg_addr));
    reg_addr.sin_family = AF_INET;
    reg_addr.sin_port = htons(0);
    inet_pton(AF_INET, THIS_IP, &(reg_addr.sin_addr));

    bind(s, (struct sockaddr *)&reg_addr, sizeof(reg_addr));
    listen(s, 1);

    socklen_t alen = sizeof (struct sockaddr_in);  
    getsockname(s, (struct sockaddr *) &reg_addr, &alen);     
    bzero(&THIS_IP, INET_ADDRSTRLEN);
    if (debug) // debug variable for testing. Globally initialized and available
        printf("%s\n", inet_ntop(AF_INET, &(reg_addr.sin_addr), THIS_IP, INET_ADDRSTRLEN));
    if (pclose(ls_cmd) < 0)
        perror("pclose(3) error");

    strcat(THIS_IP, ":");

    char port[DEFAULT_NAME_SIZE];
    sprintf(port, "%u", ntohs(reg_addr.sin_port));
    strcpy(this.address, strcat(THIS_IP, port));
    
    // Send the file to register to the server and then depending on the result of the registration, add the file to a list of hosted files. 
    sendto(sockfd, &this, sizeof(this), 0, (struct sockaddr*)&socket_addr, socket_addr_size);
    int flag = waitRegisteredAcknowledgement(sockfd, &s, socket_addr, socket_addr_size);
    if (flag)
        addToHostedFiles(s, this);
    else
        close(s);
}

// S
void downloadFile(int sockfd, char *content_name)
{ // Downloading file from TCP socket as client_peer
    int downloading = 1;
    struct cpdu packet;
    bzero(&packet, sizeof(packet));

    FILE *fp = fopen(content_name, "w");
    if (fp == NULL)
    {
        printf("Error creating file...\n");
        return;
    }
    while (downloading)
    { // downloading until E packet is received
        if (recv(sockfd, &packet, sizeof(packet), 0) < 0)
        {
            printf("Error receiving packet from server...\n");
            return;
        }
        if (packet.type == 'E')
        {
            downloading = 0;
            if (strlen(packet.data) != 0)
            {
                printf("%s\n", packet.data);
            } else
            {
                printf("File successfully downloaded...\n");
            }
            return;
        }
        printf("Downloading...\n");
        fprintf(fp, "%s", packet.data);
        bzero(&packet, sizeof(packet));
    }
    if (debug)
        printf("Finished downloading...\n");
}
void establishConnection(char *my_name, char *content_name, char *ip, char *port)
{ // connect to TCP socket of client_server as client_peer after receiving IP and port from index server
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    if (sockfd == -1)
    {
        printf("Failed to create TCP socket...\n");
        return;
    } else if (debug)
        printf("TCP Socket created...\n");

    bzero(&serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(port));
    inet_pton(AF_INET, ip, &(serv_addr.sin_addr));
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0)
    {
        printf("Connection to server failed...\n");
        return;
    }
    // Send a D type PDU to tell the client a file is ready to download. 
    struct pdu signal_packet = {'D'};
    bzero(signal_packet.data, STANDARD_BUF_SIZE);
    strcpy(signal_packet.data, content_name);

    if (send(sockfd, &signal_packet, sizeof(signal_packet), 0) < 0)
    {
        printf("Error establishing connection with the content server...\n");
        return;
    }

    downloadFile(sockfd, content_name);
    close(sockfd);
}
void requestFileFromServer(int sockfd, struct sockaddr_in socket_addr, int socket_addr_size, char *peer)
{ // Get IP and port of content_server from index server with an SPDU
    struct spdu request_packet = {'S'};
    bzero(request_packet.peer_name, DEFAULT_NAME_SIZE);
    strcpy(request_packet.peer_name, peer);
    bzero(request_packet.content_name, DEFAULT_NAME_SIZE);

    printf("Which file would you like to request for download from the server? \n");
    scanf("%s", request_packet.content_name);
    char content_name[DEFAULT_NAME_SIZE];
    strcpy(content_name, request_packet.content_name);

    sendto(sockfd, &request_packet, sizeof(request_packet), 0, (struct sockaddr *)&socket_addr, socket_addr_size);

    struct pdu receive_address;
    bzero(&receive_address, sizeof(receive_address));

    // If a content_server does not exist, print the index_server's provided error message. Otherwise, tokenize and get the parameters and download the file
    recvfrom(sockfd, &receive_address, sizeof(receive_address), 0, (struct sockaddr *)&socket_addr, &socket_addr_size);
    if (receive_address.type == 'E')
    {
        printf("%s", receive_address.data);
        return;
    }

    char *token = strtok(receive_address.data, ":");
    char content_server_ip[30], content_server_port[DEFAULT_NAME_SIZE];
    strcpy(content_server_ip, token);
    strcpy(content_server_port, strtok(NULL, ":"));
    establishConnection(peer, content_name, content_server_ip, content_server_port);

    char num = 'R';
    sendto(sockfd, &num, sizeof(num), 0, (struct sockaddr *)&socket_addr, socket_addr_size);

    struct rpdu this;
    bzero(&this, sizeof(this));
    this.type = 'R';
    strcpy(this.peer_name, client_name);
    strcpy(this.content_name, content_name);
    printf("%s", this.content_name);

    /* Harasees Singh Gill's heuristic to get Ubuntu 20.04 private IP address
        Same thing...*/
    FILE *ls_cmd = popen("hostname -I", "r");
    if (ls_cmd == NULL) {
        fprintf(stderr, "popen(3) error");
        exit(EXIT_FAILURE);
    }

    static char buff[1024];
    size_t n;

    while ((n = fread(buff, 1, sizeof(buff)-1, ls_cmd)) > 0) {
        buff[n] = '\0';
    }

    char THIS_IP[INET_ADDRSTRLEN];
    strcpy(THIS_IP, strtok(buff, " "));

    struct sockaddr_in reg_addr;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    bzero(&reg_addr, sizeof(reg_addr));
    reg_addr.sin_family = AF_INET;
    reg_addr.sin_port = htons(0);
    inet_pton(AF_INET, THIS_IP, &(reg_addr.sin_addr));

    bind(s, (struct sockaddr *)&reg_addr, sizeof(reg_addr));
    listen(s, 1);

    socklen_t alen = sizeof (struct sockaddr_in);  
    getsockname(s, (struct sockaddr *) &reg_addr, &alen);     
    bzero(&THIS_IP, INET_ADDRSTRLEN);
    printf("%s\n", inet_ntop(AF_INET, &(reg_addr.sin_addr), THIS_IP, INET_ADDRSTRLEN));
    if (pclose(ls_cmd) < 0)
        perror("pclose(3) error");

    strcat(THIS_IP, ":");

    char port[DEFAULT_NAME_SIZE];
    sprintf(port, "%u", ntohs(reg_addr.sin_port));
    strcpy(this.address, strcat(THIS_IP, port));
    
    sendto(sockfd, &this, sizeof(this), 0, (struct sockaddr*)&socket_addr, socket_addr_size);
    int flag = waitRegisteredAcknowledgement(sockfd, &s, socket_addr, socket_addr_size);
    if (flag)
        addToHostedFiles(s, this);
}

// T
void removeFromHostedFiles(char *file_name)
{ // Terminate a file by removing it from the linked list of files hosted by this content_server
    struct File *temp = head, *prev;
    if (temp != NULL && 
        strcmp(temp->file_descriptor.content_name, file_name) == 0)
    { // If it is the first item in the list
        head = temp->next;
        free(temp);
        printf("%s was removed from the server and removed from local list of hosted files...\n", file_name);
        return;
    }

    while (temp != NULL && 
        strcmp(temp->file_descriptor.content_name, file_name) != 0)
    { // while it is not the item by name and the linked list has not reached its end
        prev = temp;
        temp = temp->next;
    }

    if (temp == NULL) // If the item was not found in the list of hosted files
        printf("%s was not found in the list of hosted files...\n", file_name);
        return;

    // Item was found and was not the first item in the list
    prev->next = temp->next;
    free(temp);
    printf("%s was removed from the server and removed from local list of hosted files...\n", file_name);
}
int waitDeletionAcknowledgement(int sockfd, struct sockaddr_in socket_addr, int socket_addr_size, struct pdu packet)
{ // Same for waiting for acknowledgement of registration. This time we wait to hear that our file was removed from the server and we can close the socket. 
    if (sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&socket_addr, socket_addr_size) < 0)
    {
        printf("ERROR: Could not request file deletion from server...\n");
        return 0;
    }

    bzero(&packet, sizeof(packet));
    if (recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&socket_addr, &socket_addr_size) < 0)
    { 
        printf("CRITICAL ERROR... Please try again later.\n");
        return 0;
    }
    if (packet.type == 'A')
    {
        printf("File deleted\n");
        return 1;
    } else if (packet.type == 'E')
    {
        printf("Something went wrong... %s\n", packet.data);
        return 0;
    }
    return 0;
}
void destroyExistingSocket(int sockfd, struct sockaddr_in socket_addr, int socket_addr_size)
{ // Main T function. Send request and wait for server response with status of request. 
    struct pdu packet = {'T'};
    char file_to_delete[DEFAULT_NAME_SIZE];
    bzero(packet.data, STANDARD_BUF_SIZE);

    printf("Please type the file you would like to delete...\n");
    scanf("%s", file_to_delete);

    strcpy(packet.data, file_to_delete);
    strcat(packet.data, ":");
    strcat(packet.data, client_name);

    int x = waitDeletionAcknowledgement(sockfd, socket_addr, socket_addr_size, packet);
    if (x)
        removeFromHostedFiles(file_to_delete);
}
// O
void printHostedFiles(int sockfd, struct sockaddr_in socket_addr, int socket_addr_size)
{ // Request list of files and then receive the incoming 'O' pdu until an 'E' pdu is witnessed. Then stop printing and return.
    struct pdu available_file = {'O'};
    bzero(available_file.data, STANDARD_BUF_SIZE);
    if (sendto(sockfd, &available_file, sizeof(available_file), 0, (struct sockaddr*)&socket_addr, socket_addr_size) < 0)
    {
        printf("Failed to request files. Please try again later...\n");
        return;
    }

    while (1)
    { // Get PDUs from index server until an E packet is contained. 
        bzero(&available_file, sizeof(available_file));
        if (recvfrom(sockfd, &available_file, sizeof(available_file), 0, (struct sockaddr*)&socket_addr, &socket_addr_size) < 0)
        {
            printf("Failed to receive file from server. Please try again later...\n");
            return;
        }
        if (available_file.type == 'E')
        {
            if (strlen(available_file.data) > 0)
            {
                printf("%s\n", available_file.data);
            }
            printf("\n");
            return;
        }

        // For every PDU obtained, tokenize, split, obtain the character arrays, and print them as a formatted string on the terminal. 
        char *token = strtok(available_file.data, ":");
        char peer_name[DEFAULT_NAME_SIZE], content_name[DEFAULT_NAME_SIZE];
        int i = 0;
        while(token != NULL)
        {
            if (i == 0)
            {
                strcpy(peer_name, token);
            } else if (i == 1)
            {
                strcpy(content_name, token);
            }
            i++;
            token = strtok(NULL, ":");
        }

        printf("PEER: %s    CONTENT: %s\n", peer_name, content_name);
    }
}


/* MAIN FUNCTION */
int main(int argc, char *argv[])
{ 
    if (argc != 4)
    {
        printf("Incorrect usage: ./client SERVER_IP_ADDR SERVER_PORT CLIENT_NAME");
        exit(1);
    }
    char* SERVER_IP_ADDR = argv[1];
    int SERVER_PORT = atoi(argv[2]);
    strcpy(client_name, argv[3]);
    // Quick prototyping
    // char* SERVER_IP_ADDR = "127.0.0.1";
    // strcpy(client_name, "Jeff");
    // int SERVER_PORT = 8008;

    int sockfd = 0;
    ssize_t recv_size;

    struct sockaddr_in socket_addr;
    int from_length;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) 
    {
        printf("\nCould not create socket \n");
        return 1;
    }

    bzero(&socket_addr, sizeof(socket_addr));

    // setup connection to index server
    socket_addr.sin_family = AF_INET;
    socket_addr.sin_port = htons(SERVER_PORT);
    socket_addr.sin_addr.s_addr = inet_addr(SERVER_IP_ADDR);
    from_length = sizeof(socket_addr);

    int choice = 'R';
    struct File *n;
    printf("(%s) Enter:\nR: Register content\nS: Make download request\nT: Deregister content\nO: Request list of registered content\nL: Leave\n", client_name);
    while (choice != 'L')
    { // We begin the main loop. We wait for a socket in ready sockets to fire. 0 represents terminal input. We process terminal or socket... whichever is first
        int continue_flag = 0;
        n = head;

        fd_set ready_sockets;
        FD_ZERO(&ready_sockets);
        FD_SET(0, &ready_sockets);

        while (n != NULL)
        { // If there are sockets to monitor in the linked list, store them in ready sockets now. Select is destructive***
            if (debug)
                printf("Socket belongs to: %s. The file being served is %s...\n", n->file_descriptor.peer_name, n->file_descriptor.content_name);

            FD_SET(n->s, &ready_sockets);
            n = n->next;
        }
        if (select(FD_SETSIZE, &ready_sockets, NULL, NULL, NULL) < 0)
        {
            perror("error during select...\n");
            exit(-1);
        }

        if (FD_ISSET(0, &ready_sockets))
        { // If the input came from a terminal...
            scanf("%ls", &choice);
            printf("\n");

            char num = choice;
            sendto(sockfd, &num, sizeof(num), 0, (struct sockaddr *)&socket_addr, sizeof(struct sockaddr));

            switch(choice)
            {
                case 'R':
                    makePassiveSocket(sockfd, socket_addr, from_length);
                    break;
                case 'S':
                    requestFileFromServer(sockfd, socket_addr, from_length, client_name);
                    break;
                case 'T':
                    destroyExistingSocket(sockfd, socket_addr, from_length);
                    break;
                case 'O':
                    printHostedFiles(sockfd, socket_addr, from_length);
                    break;
                case 'L':
                    if (sendto(sockfd, &client_name, DEFAULT_NAME_SIZE, 0, (struct sockaddr *)&socket_addr, from_length) < 0)
                    {
                        printf("CRITICAL ERROR: Server was not informed of the peer leaving\n\n");
                    };
                    printf("Exiting from server...\n");
                    close(sockfd);
                    exit(0);
            }
        } else
        { // INCOMING TCP REQUEST
            printf("Serving new client...\n");
            int clientfd;
            n = head;
            while (n != NULL)
            {
                if (FD_ISSET(n->s, &ready_sockets))
                {
                    clientfd = n->s;
                    break;
                }
                n = n -> next;
            }
            handleDownload(clientfd);
        }
        // We reprint our options at the end of every loop
        printf("\n(%s) Enter:\nR: Register content\nS: Make download request\nT: Deregister content\nO: Request list of registered content\nL: Leave\n", client_name);
    }
}
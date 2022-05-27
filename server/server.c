// UDP Peer and TCP Index Server


/* DEFINITIONS */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#define DEFAULT_NAME_SIZE 20
#define STANDARD_BUF_SIZE 99
#define CONTENT_BUF_SIZE 1280


/* STRUCTS */
struct __attribute__((__packed__)) rpdu {
    // Struct for registering new files
    char type;
    char peer_name[DEFAULT_NAME_SIZE];
    char content_name[DEFAULT_NAME_SIZE];
    char address[30];
};
struct __attribute__((__packed__)) hosted_file {
    // Linked list struct for maintaining files. Note: status will be 'A'(ctive) or 'B'(usy) and only 'A' peers will be recommended as content servers
    char status;
    struct rpdu file_description;
    struct hosted_file* next;
};
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

// Making linked list globally available and setting a debug flag to determine whether print statements are shown
int debug = 0;
struct hosted_file* head = NULL;


/* UTILITY FUNCTIONS */
// MISC
void localFilePrint(struct hosted_file * n)
{ // print the remaining files in the linked list after removing orphans
    if (n == NULL)
    {
        printf("PEER:     CONTENT:     ADDRESS: \n");
        return;
    }
    while (n != NULL)
    {
        printf("PEER: %s    CONTENT: %s    ADDRESS: %s\n", n->file_description.peer_name, n->file_description.content_name, n->file_description.address);
        n = n->next;
    }
}

// L
void removeOrphanFiles(char disconnecting_peer[DEFAULT_NAME_SIZE])
{ // Remove orphan files that are leftover when a peer disconnecs
    while (1)
    { // Basically runs until the linked list has removed every file belonging to the user with the given name
      // If the first element is a hit, we shift the list. If not, we look for a hit and shift that part of that list. Otherwise, we finish
        struct hosted_file *temp = head, *prev;
        if (temp != NULL && strcmp(temp->file_description.peer_name, disconnecting_peer) == 0)
        {
            head = temp->next;
            free(temp);
            continue;
        }

        while (temp != NULL && strcmp(temp->file_description.peer_name, disconnecting_peer) != 0)
        {
            prev = temp;
            temp = temp->next;
        }
        if (temp == NULL)
        {
            printf("Orphans have been murdered...\n");
            break;
        }
        prev->next = temp->next;
        free(temp);
    }
    if (!debug)
        localFilePrint(head);
}

// O
void printHostedFiles(int sockfd, struct hosted_file * n, struct sockaddr_in socket_addr, int socket_addr_size)
{ // For every node in the linked list, send the information in a 'O' type pdu. The client stops receiving when it gets an 'E' type pdu
    struct pdu packet;
    bzero(&packet, sizeof(packet));
    if (recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&socket_addr, &socket_addr_size) < 0)
    {
        printf("Failed to receive packet...\n");
        struct pdu error_packet = {'E'};
        bzero(error_packet.data, STANDARD_BUF_SIZE);
        strcpy(error_packet.data, "Error. Please try again later...\n");
        sendto(sockfd, &error_packet, sizeof(error_packet), 0, (struct sockaddr *)&socket_addr, socket_addr_size);
        return;
    }

    while (n != NULL)
    { // Some string operations happen to append all the information in the buffer separated with a ":"
        bzero(&packet, sizeof(packet));
        packet.type = 'O';
        strcat(strcpy(packet.data, n->file_description.peer_name), ":");
        strcat(packet.data, n->file_description.content_name);
        if (debug)
            printf("%s\n", packet.data);

        sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&socket_addr, socket_addr_size);
        n = n->next;
    }
    bzero(&packet, sizeof(packet));
    packet.type = 'E';
    sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&socket_addr, socket_addr_size);
}

// T
int removeItemFromList(struct rpdu *file_to_remove)
{ // Similar logic for removal but this time we know there can only be one matching rpdu so we specifically look for it
    struct hosted_file *temp = head, *prev;
    if (temp != NULL && 
        strcmp(temp->file_description.content_name, file_to_remove->content_name) == 0 && 
        strcmp(temp->file_description.peer_name, file_to_remove->peer_name) == 0)
    {
        head = temp->next;
        free(temp);
        return 1;
    }

    while (temp != NULL && 
        (strcmp(temp->file_description.content_name, file_to_remove->content_name) != 0 || 
        strcmp(temp->file_description.peer_name, file_to_remove->peer_name) != 0))
    {
        prev = temp;
        temp = temp->next;
    }

    if (temp == NULL)
        return 0;

    prev->next = temp->next;
    free(temp);
    return 1;
}
int itemInList(struct hosted_file * n, char content[99]) // PEER_NAME:CONTENT_NAME
{ // Checks if the item already exists in the linked list before deleting and returns 1 if the item is in the linked list
    char *token = strtok(content, ":");
    char content_name[DEFAULT_NAME_SIZE], peer_name[DEFAULT_NAME_SIZE];
    int i = 0;
    while(token != NULL)
    {
        if (i++ == 0)
        {
            strcpy(content_name, token);
        } else
        {
            strcpy(peer_name, token);
        }
        token = strtok(NULL, ":");
    }

    while (n != NULL)
    {
        if (strcmp(n->file_description.content_name, content_name) == 0 && strcmp(n->file_description.peer_name, peer_name) == 0)
        {
            return removeItemFromList(&n->file_description);
        }
        n = n->next;
    }
    return 0;
}
void deRegisterContent(int sockfd, struct sockaddr_in client_addr, int *client_addr_size)
{ // Base function for T. Received peer name and file to delete in the pdu buffer as "%s:%s" and then respond to client with status of request
    struct pdu file_to_delete;
    bzero(&file_to_delete, sizeof(file_to_delete));
    if (recvfrom(sockfd, &file_to_delete, sizeof(file_to_delete), 0, (struct sockaddr *)&client_addr, client_addr_size) < 0)
    {
        printf("Error receiving file name from client. Please try again later...\n");
        return;
    }
    int flag = head != NULL ? itemInList(head, file_to_delete.data) : 0;
    bzero(&file_to_delete, sizeof(file_to_delete));

    if (debug)
        printf("%d\n", flag);

    if (flag)
    {
        file_to_delete.type = 'A';
        sendto(sockfd, &file_to_delete, sizeof(file_to_delete), 0, (struct sockaddr*)&client_addr, *client_addr_size);
        if (debug)
            printf("ITEM SUCCESSFULLY DELETED\n");
    } else
    {
        file_to_delete.type = 'E';
        strcpy(file_to_delete.data, "ERROR: Node was not found in list");
        sendto(sockfd, &file_to_delete, sizeof(file_to_delete), 0, (struct sockaddr*)&client_addr, *client_addr_size);
    }
}


// S
void sendFileInfo(int sockfd, struct sockaddr_in client_addr, int *client_addr_size, struct hosted_file file, char *peer_name)
{ // Send content_server TCP IP and port for requested file in a string formatted as %s:%s to the requesting peer 
    struct pdu file_info_packet;
    bzero(&file_info_packet, sizeof(file_info_packet));
    file_info_packet.type = 'S';
    strcpy(file_info_packet.data, file.file_description.address);
    if (sendto(sockfd, &file_info_packet, sizeof(file_info_packet), 0, (struct sockaddr*)&client_addr, *client_addr_size) < 0)
    { // If the request could not be sent, send an E PDU instead to note the error on both ends
        struct pdu error_packet = {'E'};
        bzero(error_packet.data, STANDARD_BUF_SIZE);
        strcpy(error_packet.data, "Could not receive address...\n");
        sendto(sockfd, &error_packet, sizeof(error_packet), 0, (struct sockaddr*)&client_addr, *client_addr_size);
    }
    // Set the file.status to busy after it has been sent to a requesting peer
    file.status = 'B';
}
struct hosted_file* getHostedFile(struct hosted_file *n, struct spdu packet)
{ // Return specified file
    while (n != NULL)
    { // While the file is in the linked list 
        if (strcmp(n->file_description.content_name, packet.content_name) == 0)
        {
            if (n->status == 'A')
            { // and is available for download
                return n;                
            }
        }
        n = n->next;
    }
    return NULL;
}
void processDownloadRequest(int sockfd, struct sockaddr_in client_addr, int *client_addr_size)
{ // This is the main function for S type requests from a peer. It checks to see if the file exists, and sends the information if it does.
  // If the information does not exist, it sends an E type error PDU instead to note the error at both ends
    if (head == NULL)
    {
        struct pdu error_packet = {'E'};
        bzero(error_packet.data, STANDARD_BUF_SIZE);
        strcpy(error_packet.data, "There are no content servers serving this file...\n");
        sendto(sockfd, &error_packet, sizeof(error_packet), 0, (struct sockaddr*)&client_addr, *client_addr_size);
        return;
    }

    struct spdu request_packet;
    bzero(&request_packet, sizeof(request_packet));
    recvfrom(sockfd, &request_packet, sizeof(request_packet), 0, (struct sockaddr *)&client_addr, client_addr_size);
    char *peer;
    strcpy(peer, request_packet.peer_name);
    struct hosted_file *requested_file = getHostedFile(head, request_packet);

    if (requested_file == NULL)
    {
        struct pdu error_packet = {'E'};
        bzero(error_packet.data, STANDARD_BUF_SIZE);
        strcpy(error_packet.data, "There are no content servers serving this file...\n");
        sendto(sockfd, &error_packet, sizeof(error_packet), 0, (struct sockaddr*)&client_addr, *client_addr_size);
        return;
    } else
    {
        sendFileInfo(sockfd, client_addr, client_addr_size, *requested_file, peer);
    }
}


// R
int findMatchingContent(struct hosted_file* n, struct rpdu curr_file)
{ // Find if this content already exists under this peer in the node. Return 0 if it does exist. 
    while (n != NULL)
    {
        if (strcmp(n->file_description.content_name, curr_file.content_name) == 0)
        {
            if (strcmp(n->file_description.peer_name, curr_file.peer_name) == 0)
            {
                return 0;
            }
        }
        n = n->next;
    }
    return 1;
}
void rejectClient(int sockfd, char *msg, struct sockaddr_in* client_addr, int *client_addr_size)
{ // Reject the client from registering the files. If the msg is err, something went wrong during the acknowledgement. In this case we remove the faulty node we just added
    struct pdu packet;
    packet.type = 'E'; 
    if (msg == "pname")
    {
        strcpy(packet.data, "Select a different name...");
    } else if (msg == "err")
    {
        struct hosted_file* temp = head;
        head = temp->next;
        free(temp);
        strcpy(packet.data, "Critical error... Exiting.");
    }
    sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)client_addr, *client_addr_size);
}
void acknowledgeClient(int sockfd, struct sockaddr_in* client_addr, int *client_addr_size)
{ // Simple acknowledgement that the request was received, is registered with the server, and the given port should be ready to take requests
    struct pdu packet = { 'A' };
    
    if (sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)client_addr, *client_addr_size) < 0)
    {
        printf("CRITICAL ERROR. COULD NOT ACKNOWLEDGE CLIENT...\n");
        rejectClient(sockfd, "err", client_addr, client_addr_size);
    }
}
void registerContent(int sockfd, struct sockaddr_in client_addr, int *client_addr_size)
{ // Main R function. Receives registration request and adds it to the linked list. Informs the client of the status of their request upon completion
    struct rpdu curr_content;
    bzero(&curr_content, sizeof(curr_content));
    if (recvfrom(sockfd, &curr_content, sizeof(curr_content), 0, (struct sockaddr *)&client_addr, client_addr_size) < 0)
    {
        fprintf(stderr, "\n%s\n", strerror(errno));
        return;
    } else
    {
        if (debug)
        {
            printf("Testing: %c\n", curr_content.type);
            printf("Testing: %s\n", curr_content.peer_name);
            printf("Testing: %s\n", curr_content.content_name);
            printf("Testing: %s\n\n", curr_content.address);
        }
    }
    if (head == NULL)
    {
        head = (struct hosted_file*)malloc(sizeof(struct hosted_file));
        head->status = 'A'; // available
        head->file_description = curr_content;
        head->next = NULL;
        acknowledgeClient(sockfd, &client_addr, client_addr_size);
    } else
    {
        int flag = findMatchingContent(head, curr_content);
        if (flag)
        { // no matching content
            struct hosted_file* new_head = (struct hosted_file*)malloc(sizeof(struct hosted_file));
            new_head->status = 'A';
            new_head->file_description = curr_content;
            new_head->next = head;
            head = new_head;

            acknowledgeClient(sockfd, &client_addr, client_addr_size);
        } else
        {
            rejectClient(sockfd, "pname", &client_addr, client_addr_size);
        }
    }
}

/* MAIN */
int main(int argc, char *argv[])
{ // We comment out the arguments so that we can use "./server" and "./client_{i}" and such to run the programs. UDP runs on 127.0.0.1:8080 for debugging
    if (argc != 2)
    {
        printf("You have passed in an invalid input. Please run in the format: ./server portNumber.\n");
        exit(1);
    }
    int port = atoi(argv[1]);
    // int port = 8008;
    int sockfd, num, binding;
    long file_size;
    char requested_file_path[STANDARD_BUF_SIZE];

    struct sockaddr_in server_addr, client_addr;
    ssize_t recv_size;
    socklen_t from_length;

    bzero(&server_addr, sizeof(server_addr));

    // Create UDP socket listener on specified port on available IP in the network.
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);
    from_length = sizeof(server_addr);

    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0)
    {
        printf("Error creating socket...\n");
        return 1;
    }

    printf("Server is starting...\n");
    if ((binding = bind(sockfd, (struct sockaddr*)&server_addr, from_length)) < 0)
    {
        printf("\n Error binding socket...\n");
        close(sockfd);
        return 1;
    }

    printf("Server has binded... Server is now running.\n");
    int len = sizeof(client_addr);

    while(1)
    {
        char num;
        recvfrom(sockfd, &num, sizeof(num), 0, (struct sockaddr *)&client_addr, &len);
        printf("Request: %c\n\n", num);

        int i = 0;
        switch(num)
        {
            case 'R':
                registerContent(sockfd, client_addr, &len);
                break;
            case 'S':
                processDownloadRequest(sockfd, client_addr, &len);
                break;
            case 'T':
            {
                deRegisterContent(sockfd, client_addr, &len);
                break;
            }
            case 'O':
                printHostedFiles(sockfd, head, client_addr, len);
                break;
            case 'L':
            {
                char leaving_peer[DEFAULT_NAME_SIZE];
                if (recvfrom(sockfd, &leaving_peer, DEFAULT_NAME_SIZE, 0, (struct sockaddr *)&client_addr, &len) < 0)
                { // CRITICAL errors occur when the peer and server lose sink (because of UDP unreliability most of the time). Requires a restart...
                    printf("CRITICAL ERROR: Peer left without notice...\n\n");
                }
                // When a client leaves, note it and remove their orphan files from the linked list. 
                printf("Client has left the peer group...\n");
                removeOrphanFiles(leaving_peer);
                break;
            }
        }
    }
}

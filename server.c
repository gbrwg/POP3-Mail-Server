/* server.c
 * Handles the creation of a server socket and data sending.
 * Author  : Jonatan Schroeder
 * Modified: Nov 6, 2021
 *
 * Modified by: Norm Hutchinson
 * Modified: Mar 5, 2022
 *
 * Notes: You will find useful examples in Beej's Guide to Network
 * Programming (http://beej.us/guide/bgnet/).
 */

#include "server.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <stdarg.h>
#include <signal.h>
#include <pthread.h>

#define BACKLOG 10 



// function to get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/* TODO: Fill in the server code. You are required to listen on all interfaces for connections. For each connection,
 * invoke the handler on a new thread. */
void run_server(const char *port, void (*handler)(void *)) {
    int sockfd, new_fd; // Server socket file descriptor
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage client_addr;
    socklen_t sin_size;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int rv;

    // Set up the address struct for the server
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; 
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    // Get address information for the server
    if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1); 
    }

    // Loop through all the results and bind to the first available address
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }
        
        // Allow the socket to be reused
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        // Bind to the socket
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break; // If we get here, we must have connected successfully
    }

    freeaddrinfo(servinfo); // All done with this structure

    // if p is NULL, we failed to bind
    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    // Listen on the socket
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    
    dlog("server: waiting for connections...\n");

    while (1) {  // main accept() loop
        sin_size = sizeof client_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&client_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr *)&client_addr), s, sizeof s);
        dlog("server: got connection from %s\n", s);

        // Create a new thread to handle the client connection
        pthread_t thread_id;
        int *client_fd = malloc(sizeof(int));
        *client_fd = new_fd;

        if (client_fd == NULL) {
            perror("memory allocation failed");
            close(new_fd);
            continue;
        }

        *client_fd = new_fd;

        if (pthread_create(&thread_id, NULL, (void *)handler, (void *)client_fd) != 0) {
            perror("pthread_create");
            free(client_fd);
            close(new_fd);
        }

    }

    close(sockfd);
}

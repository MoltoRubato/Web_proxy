/**
 * Socket utility functions
 */

#include "htproxy.h"

/* 
 * This function is adapted from practical 8 server.c
 */
int create_listening_socket(char *port) {
    int sockfd, s;
    struct addrinfo hints, *res;
    
    // Create address we're going to listen on
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6;      // Allow IPv6 (which also supports IPv4)
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE;     // For bind, listen, accept
    
    s = getaddrinfo(NULL, port, &hints, &res);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        exit(EXIT_FAILURE);
    }
    
    // Create socket
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    // Proj2 Specified code block, reuse port if possible
    int enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    
    // Bind address to the socket
    if (bind(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    
    freeaddrinfo(res);
    return sockfd;
}

/* 
 * This function is adapted from practical 8 client.c
 */
int connect_to_origin_server(char *host) {
    int sockfd, s;
    struct addrinfo hints, *servinfo, *p;
    
    // Check if host is enclosed in square brackets
    char *real_host = host;
    char *stripped_host = NULL;
    
    // strip the brackets for getaddrinfo
    size_t host_len = strlen(host);
    if (host_len > 2 && host[0] == '[' && host[host_len - 1] == ']') {
        stripped_host = malloc(host_len - 1);
        if (!stripped_host) {
            perror("malloc");
            return -1;
        }
        
        strncpy(stripped_host, host + 1, host_len - 2);
        stripped_host[host_len - 2] = '\0';
        
        real_host = stripped_host;
    }
    
    // Create address
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;     // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP
    
    // Get server address info
    s = getaddrinfo(real_host, "80", &hints, &servinfo);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        if (stripped_host) {
            free(stripped_host);
        }
        return -1;
    }
    
    // Connect to the first valid result
    for (p = servinfo; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            continue;
        }
        
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) != -1) {
            break; // Success
        }
        
        close(sockfd);
    }
    
    if (p == NULL) {
        fprintf(stderr, "Failed to connect to origin server\n");
        freeaddrinfo(servinfo);
        if (stripped_host) {
            free(stripped_host);
        }
        return -1;
    }
    
    freeaddrinfo(servinfo);
    if (stripped_host) {
        free(stripped_host);
    }
    return sockfd;
}
#ifndef PROXY_H
#define PROXY_H

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <ctype.h>
#include <errno.h>
#include <sys/socket.h>
#include <signal.h>

#define BUFFER_SIZE 65536      // 64KB buffer size
#define MAX_REQUEST_SIZE 65536 // 64KB request size
#define BACKLOG 10            // required in project spec

// Function declarations
int create_listening_socket(char *port);
char *extract_host_header(char *request, int request_len);
char *extract_request_uri(char *request);
int connect_to_origin_server(char *host);
void handle_client_request(int client_fd);
void cleanup_and_exit(int signum);

#endif
/**
 * utilities for extracting from HTTP requests
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L

#include "htproxy.h"

/* 
* Function to extract the Host header from the request
*/
char *extract_host_header(char *request, int request_len) {
    // Find Host header
    char *host_header = strcasestr(request, "\nHost:");
    if (!host_header) {
        // Try finding it at the beginning of the request
        host_header = strcasestr(request, "Host:");
        if (!host_header) {
            return NULL;
        }
    } else {
        host_header++; // Skip the \n
    }
    
    host_header += 5; // Skip "Host:"
    while (*host_header == ' ') host_header++; // Skip spaces
    
    // Find end of the line
    char *end = strstr(host_header, "\r\n");
    if (!end) {
        return NULL;
    }
    
    // Allocate memory for the host value
    int host_len = end - host_header;
    char *host = malloc(host_len + 1);
    if (!host) {
        perror("malloc");
        return NULL;
    }
    
    // Copy the host value
    strncpy(host, host_header, host_len);
    host[host_len] = '\0';
    
    return host;
}



/* 
* Function to extract the URI from the request
*/
char *extract_request_uri(char *request) {
    // Find the first line (request line)
    char *line_end = strstr(request, "\r\n");
    if (!line_end) {
        return NULL;
    }
    
    // Find first space (after GET)
    char *first_space = strchr(request, ' ');
    if (!first_space || first_space >= line_end) {
        return NULL;
    }
    
    // Skip the space
    char *uri_start = first_space + 1;
    
    // Find second space (before "HTTP/1.1")
    char *second_space = strchr(uri_start, ' ');
    if (!second_space || second_space >= line_end) {
        return NULL;
    }
    
    // Allocate memory for the URI
    int uri_len = second_space - uri_start;
    char *uri = malloc(uri_len + 1);
    if (!uri) {
        perror("malloc");
        return NULL;
    }
    
    // Copy the URI
    strncpy(uri, uri_start, uri_len);
    uri[uri_len] = '\0';
    
    return uri;
}
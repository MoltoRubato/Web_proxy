#include "htproxy.h"
#include "cache.h"

cache_t cache;
int caching_enabled = 0;

int main(int argc, char **argv) {
    int opt, listen_port_provided = 0;
    char *listen_port = NULL;
    
    // Get command line arguments
    while ((opt = getopt(argc, argv, "p:c")) != -1) {
        switch (opt) {
            case 'p':
                listen_port = optarg;
                listen_port_provided = 1;
                break;
            case 'c':
                caching_enabled = 1;
                break;
            default:
                fprintf(stderr, "Usage: %s -p listen-port [-c]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    
    // Check if required arguments are provided
    if (!listen_port_provided) {
        fprintf(stderr, "Usage: %s -p listen-port [-c]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    if (caching_enabled) {
        cache_init(&cache);
        
        // cleanup cache on exit
        signal(SIGINT, cleanup_and_exit);
        signal(SIGTERM, cleanup_and_exit);
    }

    
    
    // Create listening socket
    int sockfd = create_listening_socket(listen_port);
    
    // Listen on the socket
    if (listen(sockfd, BACKLOG) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    // Wait for new connection
    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t client_addr_size = sizeof(client_addr);
        
        // Accept a connection
        int client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_size);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }
        
        printf("Accepted\n");
        fflush(stdout);
        
        // Handle the request
        handle_client_request(client_fd);
        
        // Close client socket after handling the request
        close(client_fd);
    }
    
    // Cleanup cache
    if (caching_enabled) {
        cache_cleanup(&cache);
    }
    
    close(sockfd);
    return 0;
}

void handle_client_request(int client_fd) {
    char request[MAX_REQUEST_SIZE];
    int request_len = 0;
    int end_of_headers = 0;
    
    // Read the request
    while (!end_of_headers && request_len < MAX_REQUEST_SIZE - 1) {
        int bytes_read = recv(client_fd, request + request_len, 
                             MAX_REQUEST_SIZE - 1 - request_len, 0);
        
        if (bytes_read <= 0) {
            if (bytes_read < 0) {
                perror("recv");
            }
            return;
        }
        
        request_len += bytes_read;
        request[request_len] = '\0';
        
        // Check if we're at the end and have the complete header
        if (strstr(request, "\r\n\r\n") != NULL) {
            end_of_headers = 1;
        }
    }
    
    if (!end_of_headers) {
        fprintf(stderr, "Incomplete request header\n");
        return;
    }
    
    char *header_end = strstr(request, "\r\n\r\n");
    if (!header_end) {
        fprintf(stderr, "Invalid request format\n");
        return;
    }
    
    // Log the last line of the header before the blank line
    char *last_line_start = header_end;
    // Now work backwards to find the (start of) the last line
    while (last_line_start > request && 
           !(last_line_start >= request + 2 && 
             last_line_start[-2] == '\r' && last_line_start[-1] == '\n')) {
        last_line_start--;
    }
    
    // Extract the last line for logging
    int last_line_len = header_end - last_line_start;
    char last_line[last_line_len + 1];
    strncpy(last_line, last_line_start, last_line_len);
    last_line[last_line_len] = '\0';
    
    printf("Request tail %s\n", last_line);
    fflush(stdout);
    
    // Extract host from Host header
    char *host = extract_host_header(request, request_len);
    if (!host) {
        fprintf(stderr, "No Host header found in request\n");
        return;
    }

    // Extract URI
    char *request_uri = extract_request_uri(request);
    if (!request_uri) {
        fprintf(stderr, "Invalid request format\n");
        free(host);
        return;
    }
    
    int total_request_len = (header_end - request) + 4; // for \r\n\r\n
    int stale_entry_index = -1;

    // Check cache for this request (if caching is enabled)
    if (caching_enabled && request_len <= MAX_REQUEST_SIZE_TO_CACHE) {
        int cache_index = cache_find(&cache, request, total_request_len);
        
        if (cache_index != -1) {
            // Found in cache and it's not stale
            printf("Serving %s %s from cache\n", host, request_uri);
            fflush(stdout);
            
            // Send the cached response to the client
            int cached_response_len = cache.entries[cache_index].response_len;
            int bytes_sent = 0;
            
            while (bytes_sent < cached_response_len) {
                int sent = send(client_fd, 
                               cache.entries[cache_index].response + bytes_sent, 
                               cached_response_len - bytes_sent, 0);
                if (sent <= 0) {
                    perror("send to client from cache");
                    free(host);
                    free(request_uri);
                    return;
                }
                bytes_sent += sent;
            }
            
            free(host);
            free(request_uri);
            return;
        }
        
        // Check if we have a stale entry for this request
        for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
            if (cache.entries[i].valid && 
                cache.entries[i].request_len == total_request_len &&
                memcmp(cache.entries[i].request, request, total_request_len) == 0 &&
                is_cache_entry_stale(&cache, i)) {
                
                // Found a stale entry, but don't evict it yet
                stale_entry_index = i;
                break;
            }
        }
        
        // Only prepare eviction if we don't have a stale entry to replace
        if (stale_entry_index == -1) {
            cache_prepare_eviction_if_needed(&cache, total_request_len);
        }
    }

    // Either caching is disabled or we had a cache miss
    printf("GETting %s %s\n", host, request_uri);
    fflush(stdout);
    
    // Connect to origin server using the extracted host
    int server_fd = connect_to_origin_server(host);
    if (server_fd < 0) {
        fprintf(stderr, "Failed to connect to origin server: %s\n", host); 
        free(host);
        free(request_uri);
        return;
    }
    
    // Send the request to the origin server
    int bytes_sent = 0;
    
    while (bytes_sent < total_request_len) {
        int sent = send(server_fd, request + bytes_sent, 
                       total_request_len - bytes_sent, 0);
        if (sent <= 0) {
            perror("write to server");
            close(server_fd);
            free(host);
            free(request_uri);
            return;
        }
        bytes_sent += sent;
    }
    
    // Read the response from the origin server and forward it to client
    char response_buffer[BUFFER_SIZE];
    int total_bytes_forwarded = 0;
    int response_header_complete = 0;
    char header_accumulator[MAX_REQUEST_SIZE];
    int header_bytes_accumulated = 0;
    long content_length = -1;
    int header_bytes_forwarded = 0;
    
    // Prepare buffer for complete response if caching is enabled
    char *complete_response = NULL;
    int complete_response_size = 0;
    int complete_response_capacity = 0;
    
    if (caching_enabled && total_request_len <= MAX_REQUEST_SIZE_TO_CACHE) {
        complete_response_capacity = BUFFER_SIZE;
        complete_response = malloc(complete_response_capacity);
        if (!complete_response) {
            perror("malloc for response cache");
        }
    }
    
    while (1) {
        int bytes_read = recv(server_fd, response_buffer, BUFFER_SIZE, 0);
        
        if (bytes_read <= 0) {
            // Connection closed or some error
            break;
        }
        
        // If we're caching, add this to the complete response
        if (complete_response && bytes_read > 0) {
            // Check if we need to grow our buffer
            if (complete_response_size + bytes_read > complete_response_capacity) {
                // Double the capacity
                int new_capacity = complete_response_capacity * 2;
                char *new_buffer = realloc(complete_response, new_capacity);
                if (new_buffer) {
                    complete_response = new_buffer;
                    complete_response_capacity = new_capacity;
                } else {
                    // If realloc fails, don't caching for this request
                    perror("realloc for response cache");
                    free(complete_response);
                    complete_response = NULL;
                }
            }
            
            // Add to complete response if we still have a buffer
            if (complete_response) {
                memcpy(complete_response + complete_response_size, response_buffer, bytes_read);
                complete_response_size += bytes_read;
            }
        }
        
        // If we haven't found the complete header yet, accumulate it
        if (!response_header_complete) {
            // Copy data to header accumulator
            int bytes_to_copy = bytes_read;
            if (header_bytes_accumulated + bytes_to_copy >= MAX_REQUEST_SIZE) {
                bytes_to_copy = MAX_REQUEST_SIZE - header_bytes_accumulated - 1;
            }
            
            if (bytes_to_copy > 0) {
                memcpy(header_accumulator + header_bytes_accumulated, response_buffer, bytes_to_copy);
                header_bytes_accumulated += bytes_to_copy;
                header_accumulator[header_bytes_accumulated] = '\0';
            }
            
            // Check for end of header
            char *header_end_pos = strstr(header_accumulator, "\r\n\r\n");
            if (header_end_pos) {
                response_header_complete = 1;
                header_bytes_forwarded = (header_end_pos - header_accumulator) + 4;
                
                // Extract Content-Length from accumulated header
                char *content_len_start = strcasestr(header_accumulator, "Content-Length:");
                if (content_len_start) {
                    content_len_start += 15; // Skip "Content-Length:"
                    while (*content_len_start == ' ') content_len_start++; // Skip spaces
                    content_length = strtol(content_len_start, NULL, 10);
                    
                    printf("Response body length %ld\n", content_length);
                    fflush(stdout);
                }
            }
        }
        
        // Forward all received bytes to client
        int client_bytes_sent = 0;
        while (client_bytes_sent < bytes_read) {
            int sent = send(client_fd, response_buffer + client_bytes_sent, 
                           bytes_read - client_bytes_sent, 0);
            if (sent <= 0) {
                perror("send to client");
                close(server_fd);
                free(host);
                free(request_uri);
                if (complete_response) free(complete_response);
                return;
            }
            client_bytes_sent += sent;
        }
        
        total_bytes_forwarded += bytes_read;
        
        // If we know the content length and have forwarded header + content, we're done
        if (response_header_complete && content_length >= 0) {
            if (total_bytes_forwarded >= header_bytes_forwarded + content_length) {
                break;
            }
        }
    }
    
    // Handle caching after we have the complete response
    if (caching_enabled && complete_response && total_request_len <= MAX_REQUEST_SIZE_TO_CACHE) {
        if (complete_response_size <= MAX_CACHE_ENTRY_SIZE) {
            // Check if response is cacheable, task3
            if (is_cacheable_response(header_accumulator)) {
                // Extract max-age for task4
                uint32_t max_age = extract_max_age(header_accumulator);
                
                // If we had a stale entry, replace it directly
                if (stale_entry_index != -1) {
                    // Free the old stale entry
                    free(cache.entries[stale_entry_index].request);
                    free(cache.entries[stale_entry_index].response);
                    free(cache.entries[stale_entry_index].host);
                    free(cache.entries[stale_entry_index].uri);
                    
                    // Replace with new data
                    cache.entries[stale_entry_index].request = malloc(total_request_len);
                    cache.entries[stale_entry_index].response = malloc(complete_response_size);
                    
                    if (cache.entries[stale_entry_index].request && cache.entries[stale_entry_index].response) {
                        memcpy(cache.entries[stale_entry_index].request, request, total_request_len);
                        cache.entries[stale_entry_index].request_len = total_request_len;
                        memcpy(cache.entries[stale_entry_index].response, complete_response, complete_response_size);
                        cache.entries[stale_entry_index].response_len = complete_response_size;
                        cache.entries[stale_entry_index].host = strdup(host);
                        cache.entries[stale_entry_index].uri = strdup(request_uri);
                        cache.entries[stale_entry_index].cached_at = get_monotonic_time_ms();
                        cache.entries[stale_entry_index].max_age = max_age;
                        cache_update_lru(&cache, stale_entry_index);
                    } else {
                        // If allocation failed, mark entry as invalid
                        cache.entries[stale_entry_index].valid = 0;
                        cache.size--;
                    }
                } else {
                    // No stale entry, use normal cache_add
                    cache_add(&cache, request, total_request_len, 
                            complete_response, complete_response_size,
                            host, request_uri, max_age);
                }
            } else {
                // Not cacheable - if we had a stale entry, evict it now
                if (stale_entry_index != -1) {
                    printf("Evicting %s %s from cache\n", 
                          cache.entries[stale_entry_index].host, 
                          cache.entries[stale_entry_index].uri);
                    fflush(stdout);
                    
                    free(cache.entries[stale_entry_index].request);
                    free(cache.entries[stale_entry_index].response);
                    free(cache.entries[stale_entry_index].host);
                    free(cache.entries[stale_entry_index].uri);
                    cache.entries[stale_entry_index].valid = 0;
                    cache.size--;
                }
                
                printf("Not caching %s %s\n", host, request_uri);
                fflush(stdout);
            }
        } else {
            // Response too large, if we had a stale entry, evict it
            if (stale_entry_index != -1) {
                printf("Evicting %s %s from cache\n", 
                      cache.entries[stale_entry_index].host, 
                      cache.entries[stale_entry_index].uri);
                fflush(stdout);
                
                free(cache.entries[stale_entry_index].request);
                free(cache.entries[stale_entry_index].response);
                free(cache.entries[stale_entry_index].host);
                free(cache.entries[stale_entry_index].uri);
                cache.entries[stale_entry_index].valid = 0;
                cache.size--;
            }
        }
        
        free(complete_response);
    }
   
    close(server_fd);
    free(host);
    free(request_uri);
}

// Free cache on exit
void cleanup_and_exit(int signum) {
    if (caching_enabled) {
        cache_cleanup(&cache);
    }
    exit(0);
}


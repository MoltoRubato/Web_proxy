#ifndef CACHE_H
#define CACHE_H

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/time.h>

#define MAX_CACHE_ENTRIES 10
#define MAX_CACHE_ENTRY_SIZE (100 * 1024)  // 100 KiB
#define MAX_REQUEST_SIZE_TO_CACHE 2000     // 2000 bytes

typedef struct {
    char *request;              // key
    int request_len;            
    char *response;             // value
    int response_len;           
    uint64_t last_accessed;     // Time for LRU
    char *host;                 
    char *uri;                  
    int valid;                  // if the entry is valid
    uint64_t cached_at;         // When this entry was cached
    uint32_t max_age;           // max-age (0 = no expiration) 
} cache_entry_t;

typedef struct {
    cache_entry_t entries[MAX_CACHE_ENTRIES];
    int size;
    uint64_t access_sequence;
    uint64_t start_time;        // Reference time when cache was initialized                   
} cache_t;

// Function declarations
void cache_init(cache_t *cache);
void cache_cleanup(cache_t *cache);
int cache_find(cache_t *cache, const char *request, int request_len);
int cache_add(cache_t *cache, const char *request, int request_len, 
             const char *response, int response_len,
             const char *host, const char *uri, uint32_t max_age);
void cache_update_lru(cache_t *cache, int index);
int cache_find_lru(cache_t *cache);
int cache_prepare_eviction_if_needed(cache_t *cache, int request_len);
int is_cacheable_response(const char *response_header);
uint32_t extract_max_age(const char *response_header);
uint64_t get_monotonic_time_ms(void);
int is_cache_entry_stale(cache_t *cache, int index);

#endif
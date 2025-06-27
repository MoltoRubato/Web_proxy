#include "cache.h"

uint64_t get_monotonic_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

void cache_init(cache_t *cache) {
    memset(cache, 0, sizeof(cache_t));
    cache->access_sequence = 0;
    cache->start_time = get_monotonic_time_ms();
}

void cache_cleanup(cache_t *cache) {
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (cache->entries[i].valid) {
            free(cache->entries[i].request);
            free(cache->entries[i].response);
            free(cache->entries[i].host);
            free(cache->entries[i].uri);
            cache->entries[i].valid = 0;
        }
    }
    cache->size = 0;
}

int is_cache_entry_stale(cache_t *cache, int index) {
    if (!cache->entries[index].valid || cache->entries[index].max_age == 0) {
        return 0; // Not valid or no expiration
    }
    
    uint64_t current_time = get_monotonic_time_ms();
    uint64_t age_ms = current_time - cache->entries[index].cached_at;
    uint64_t max_age_ms = (uint64_t)cache->entries[index].max_age * 1000;
    
    return age_ms > max_age_ms;
}

int cache_find(cache_t *cache, const char *request, int request_len) {
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (cache->entries[i].valid &&
            cache->entries[i].request_len == request_len &&
            memcmp(cache->entries[i].request, request, request_len) == 0) {
            
            // Check if entry is stale
            if (is_cache_entry_stale(cache, i)) {
                printf("Stale entry for %s %s\n", 
                      cache->entries[i].host, 
                      cache->entries[i].uri);
                fflush(stdout);
                
                return -1;
            }
            
            // Update LRU time for valid, non-stale entry
            cache_update_lru(cache, i);
            return i;
        }
    }
    return -1;  // Not found
}

void cache_update_lru(cache_t *cache, int index) {
    if (index >= 0 && index < MAX_CACHE_ENTRIES && cache->entries[index].valid) {
        cache->access_sequence++;
        cache->entries[index].last_accessed = cache->access_sequence;
    }
}

int cache_find_lru(cache_t *cache) {
    int lru_index = -1;
    uint64_t oldest_time = UINT64_MAX;
    
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (cache->entries[i].valid && cache->entries[i].last_accessed < oldest_time) {
            oldest_time = cache->entries[i].last_accessed;
            lru_index = i;
        }
    }
    
    // If we didn't find a valid entry, find the first invalid one
    if (lru_index == -1) {
        for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
            if (!cache->entries[i].valid) {
                return i;
            }
        }
    }
    
    return lru_index;
}

int cache_add(cache_t *cache, const char *request, int request_len, 
             const char *response, int response_len,
             const char *host, const char *uri, uint32_t max_age) {
    
    // Check if request is too large to cache
    if (request_len > MAX_REQUEST_SIZE_TO_CACHE) {
        return 0;
    }
    
    // Check if response is too large to cache
    if (response_len > MAX_CACHE_ENTRY_SIZE) {
        return 0;
    }
    
    int index;
    
    // If cache is not full, find an empty slot
    if (cache->size < MAX_CACHE_ENTRIES) {
        for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
            if (!cache->entries[i].valid) {
                index = i;
                cache->size++;
                break;
            }
        }
    } else {
        // Cache is full, find LRU entry to evict
        index = cache_find_lru(cache);
        
        // Log eviction
        if (cache->entries[index].valid) {
            printf("Evicting %s %s from cache\n", 
                  cache->entries[index].host, 
                  cache->entries[index].uri);
            fflush(stdout);
            
            // Free old entry
            free(cache->entries[index].request);
            free(cache->entries[index].response);
            free(cache->entries[index].host);
            free(cache->entries[index].uri);
        }
    }
    
    // Copy request
    cache->entries[index].request = malloc(request_len);
    if (!cache->entries[index].request) {
        perror("malloc");
        return 0;
    }
    memcpy(cache->entries[index].request, request, request_len);
    cache->entries[index].request_len = request_len;
    
    // Copy response
    cache->entries[index].response = malloc(response_len);
    if (!cache->entries[index].response) {
        free(cache->entries[index].request);
        perror("malloc");
        return 0;
    }
    memcpy(cache->entries[index].response, response, response_len);
    cache->entries[index].response_len = response_len;
    
    // Store host and URI for logging
    cache->entries[index].host = strdup(host);
    cache->entries[index].uri = strdup(uri);
    
    // Set time and validity
    cache->access_sequence++;
    cache->entries[index].last_accessed = cache->access_sequence;
    cache->entries[index].cached_at = get_monotonic_time_ms();
    cache->entries[index].max_age = max_age;
    cache->entries[index].valid = 1;
    
    return 1;
}

int cache_prepare_eviction_if_needed(cache_t *cache, int request_len) {
    // Check if request is too large to cache
    if (request_len > MAX_REQUEST_SIZE_TO_CACHE) {
        return 0;
    }
    
    // If cache is full, we need to evict regardless
    if (cache->size >= MAX_CACHE_ENTRIES) {
        int index = cache_find_lru(cache);
        
        // Log evict message
        if (index >= 0 && cache->entries[index].valid) {
            printf("Evicting %s %s from cache\n", 
                  cache->entries[index].host, 
                  cache->entries[index].uri);
            fflush(stdout);
            
            // Free old entry
            free(cache->entries[index].request);
            free(cache->entries[index].response);
            free(cache->entries[index].host);
            free(cache->entries[index].uri);
            cache->entries[index].valid = 0;
            cache->size--;
        }
        return 1;
    }
    
    return 0;
}

uint32_t extract_max_age(const char *response_header) {
    // Find Cache-Control header
    char *cache_control_start = strcasestr(response_header, "Cache-Control:");
    if (!cache_control_start) {
        return 0;
    }
    
    cache_control_start += 14; // Skip "Cache-Control:"
    while (*cache_control_start == ' ' || *cache_control_start == '\t') {
        cache_control_start++;
    }
    
    char *line_end = strstr(cache_control_start, "\r\n");
    if (!line_end) {
        return 0;
    }
    
    // Extract Cache-Control tag
    int value_len = line_end - cache_control_start;
    char cache_control_value[value_len + 1];
    strncpy(cache_control_value, cache_control_start, value_len);
    cache_control_value[value_len] = '\0';
    
    // Look for max-age directive
    char *max_age_pos = strcasestr(cache_control_value, "max-age");
    if (!max_age_pos) {
        return 0; // No max-age directive
    }
    
    // Find the equals sign
    char *equals_pos = strchr(max_age_pos, '=');
    if (!equals_pos) {
        return 0; // Invalid max-age directive
    }
    
    equals_pos++; // Skip the '='
    while (*equals_pos == ' ' || *equals_pos == '\t') {
        equals_pos++; // Skip whitespace after =
    }
    
    // Parse the number
    char *end_ptr;
    long max_age_value = strtol(equals_pos, &end_ptr, 10);
    
    // Check we got a valid number
    if (end_ptr == equals_pos || max_age_value < 0) {
        return 0;
    }
    
    // Ensure it fits in uint32_t
    if (max_age_value > UINT32_MAX) {
        return UINT32_MAX;
    }
    
    return (uint32_t)max_age_value;
}

// Stage 3: Check if response is cacheable
int is_cacheable_response(const char *response_header) {
    // Check "Cache-Control" header
    char *cache_control_start = strcasestr(response_header, "Cache-Control:");
    if (!cache_control_start) {
        // No "Cache-Control", respect response
        return 1;
    }

    cache_control_start += 14;
    while (*cache_control_start == ' ' || *cache_control_start == '\t') {
        cache_control_start++;
    }
    
    char *line_end = strstr(cache_control_start, "\r\n");
    if (!line_end) {
        return 1; // Treat it as cacheable just in case
    }
    
    // Extract the value of Cache-Control
    int value_len = line_end - cache_control_start;
    char cache_control_value[value_len + 1];
    strncpy(cache_control_value, cache_control_start, value_len);
    cache_control_value[value_len] = '\0';
    
    // Convert to lowercase
    for (int i = 0; i < value_len; i++) {
        cache_control_value[i] = tolower(cache_control_value[i]);
    }
    
    // Check for directives
    char *check_directive = cache_control_value;
    while (check_directive && *check_directive) {
        // Skip whitespace and commas
        while (*check_directive == ' ' || *check_directive == '\t' || *check_directive == ',') {
            check_directive++;
        }
        
        if (*check_directive == '\0') break;
        
        // Checking directives starting now
        if (strncmp(check_directive, "private", 7) == 0) {
            char next_char = check_directive[7];
            if (next_char == '\0' || next_char == ' ' || next_char == '\t' || 
                next_char == ',' || next_char == '=') {
                return 0; // private directive
            }
        }
        
        if (strncmp(check_directive, "no-store", 8) == 0) {
            char next_char = check_directive[8];
            if (next_char == '\0' || next_char == ' ' || next_char == '\t' || 
                next_char == ',' || next_char == '=') {
                return 0; // no-store directive
            }
        }
        
        if (strncmp(check_directive, "no-cache", 8) == 0) {
            char next_char = check_directive[8];
            if (next_char == '\0' || next_char == ' ' || next_char == '\t' || 
                next_char == ',' || next_char == '=') {
                return 0; // no-cache directive
            }
        }
        
        if (strncmp(check_directive, "max-age=0", 9) == 0) {
            char next_char = check_directive[9];
            if (next_char == '\0' || next_char == ' ' || next_char == '\t' || next_char == ',') {
                return 0; // max-age=0 directive
            }
        }
        
        if (strncmp(check_directive, "must-revalidate", 15) == 0) {
            char next_char = check_directive[15];
            if (next_char == '\0' || next_char == ' ' || next_char == '\t' || 
                next_char == ',' || next_char == '=') {
                return 0; // must-revalidate directive
            }
        }
        
        if (strncmp(check_directive, "proxy-revalidate", 16) == 0) {
            char next_char = check_directive[16];
            if (next_char == '\0' || next_char == ' ' || next_char == '\t' || 
                next_char == ',' || next_char == '=') {
                return 0; // proxy-revalidate directive
            }
        }
        
        // Move to next directive
        while (*check_directive && *check_directive != ',' && 
               *check_directive != ' ' && *check_directive != '\t') {
            check_directive++;
        }
    }
    
    return 1; // No problematic directives found, meaning its cacheable
}
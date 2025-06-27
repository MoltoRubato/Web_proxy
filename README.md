# htproxy - HTTP/1.1 Web Proxy

A simple caching web proxy for HTTP/1.1 requests written in C, implementing intelligent caching strategies and HTTP compliance.

## Overview

This project implements a multi-stage web proxy that can:
- Forward HTTP GET requests to origin servers
- Cache responses with intelligent eviction policies
- Respect HTTP Cache-Control directives
- Handle cache expiration with max-age support
- Perform conditional requests using If-Modified-Since headers

## Features

### Stage 1: Basic Proxying
- Accepts incoming HTTP requests on a specified port
- Forwards requests to origin servers on port 80
- Returns complete responses to clients
- Supports both IPv4 and IPv6 connections
- Handles responses of any size (with optional 100KB truncation)

### Stage 2: Naive Caching
- LRU (Least Recently Used) cache with 10 entries
- Each cache entry supports up to 100KB responses
- Caches requests under 2KB in size
- Automatic eviction when cache is full

### Stage 3: HTTP-Compliant Caching
- Respects Cache-Control headers
- Refuses to cache responses with:
  - `private`
  - `no-store`
  - `no-cache`
  - `max-age=0`
  - `must-revalidate`
  - `proxy-revalidate`
- Proper parsing of complex Cache-Control directives

### Stage 4: Cache Expiration
- Implements `max-age` directive support
- Automatically expires stale cache entries
- Fetches fresh content when cached data expires
- Maintains separate expiration times per cache entry

## Build Instructions

### Prerequisites
- GCC compiler with C99 support
- Make utility
- POSIX-compliant system (Linux/Unix)

### Compilation
```bash
make clean && make
```

This will produce an executable named `htproxy` in the project root directory.

## Usage

```bash
./htproxy -p <listen-port> [-c]
```

### Arguments
- `-p <listen-port>`: TCP port number to listen on
- `-c`: Enable caching (optional, required for stages 2-4)

### Examples
```bash
# Basic proxy without caching
./htproxy -p 8080

# Proxy with caching enabled
./htproxy -p 8080 -c
```

## Testing

The proxy can be tested with various HTTP clients:

### Using curl
```bash
# Set proxy and test
export http_proxy=http://localhost:8080
curl http://example.com
```

### Using telnet
```bash
telnet localhost 8080
# Then manually type HTTP request:
GET / HTTP/1.1
Host: example.com

```

### Using web browsers
Configure your browser's HTTP proxy settings to point to `localhost:<port>`.

## Logging Output

The proxy provides detailed logging to stdout:

```
Accepted                                    # New connection accepted
Request tail: <last-header-line>           # Last line of request headers
GETting <host> <request-URI>               # Forwarding request to origin
Response body length <content-length>      # Size of response body
Serving <host> <request-URI> from cache    # Cache hit
Evicting <host> <request-URI> from cache   # Cache eviction
Not caching <host> <request-URI>           # Uncacheable response
Stale entry for <host> <request-URI>       # Expired cache entry
Entry for <host> <request-URI> unmodified  # 304 response handling
```

## Contributor
- Kerui Huang

This project was developed as part of COMP30023 Computer Systems at the University of Melbourne.

## License
This project is submitted for academic evaluation and is not intended for commercial use.

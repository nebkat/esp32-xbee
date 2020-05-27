/*
 * This file is part of the ESP32-XBee distribution (https://github.com/nebkat/esp32-xbee).
 * Copyright (c) 2019 Nebojsa Cvetkovic.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <mbedtls/base64.h>
#include <sys/socket.h>
#include <lwip/netdb.h>

#include "util.h"

void destroy_socket(int *socket) {
    if (*socket < 0) return;
    shutdown(*socket, SHUT_RDWR);
    close(*socket);
    *socket = -1;
}

// Include space for port
static char addr_str[INET6_ADDRSTRLEN + 2 + 6 + 1];

char *sockaddrtostr(struct sockaddr *a) {
    struct sockaddr_in *a4 = (struct sockaddr_in *) a;
    struct sockaddr_in6 *a6 = (struct sockaddr_in6 *) a;

    sa_family_t family = a->sa_family;
    int port = 0;
    ip4_addr_t *addr4 = NULL;
    ip6_addr_t *addr6 = NULL;
    if (family == PF_INET) {
        addr4 = (ip4_addr_t *) &a4->sin_addr.s_addr;
        port = a4->sin_port;
    } else if (family == PF_INET6) {
        addr6 = (ip6_addr_t *) &a6->sin6_addr;

        if (ip6_addr_isipv4mappedipv6(addr6)) {
            family = PF_INET;
            addr4 = (ip4_addr_t *) &addr6->addr[3];
        }

        port = a6->sin6_port;
    }

    // Get address string
    if (family == PF_INET) {
        inet_ntop(AF_INET, addr4, addr_str, INET_ADDRSTRLEN);
    } else if (family == PF_INET6) {
        addr_str[0] = '[';
        inet_ntop(AF_INET6, addr6, addr_str + 1, INET6_ADDRSTRLEN);
        int ip_len = strlen(addr_str);
        addr_str[ip_len] = ']';
        addr_str[ip_len + 1] = '\0';
    } else {
        return "UNKNOWN";
    }

    // Append port number
    sprintf(addr_str + strlen(addr_str), ":%d", ntohs(port));

    return addr_str;
}

char *extract_http_header(const char *buffer, const char *key) {
    // Need space for key, at least 1 character, and newline
    if (strlen(key) + 2 > strlen(buffer)) return NULL;

    // Cheap search ignores potential problems where searched key is suffix of another longer key
    char *start = strcasestr(buffer, key);
    if (!start) return NULL;
    start += strlen(key);

    char *end = strstr(start, "\r\n");
    if (!end) return NULL;

    // Trim whitespace at start and end
    while (isspace((unsigned char) *start) && start < end) start++;
    while (isspace((unsigned char) *(end - 1)) && start < end) end--;

    int len = (int) (end - start);
    if (len == 0) return NULL;

    char *header_value = malloc(len + 1);
    if (header_value == NULL) return NULL;

    memcpy(header_value, start, len);
    header_value[len] = '\0';
    return header_value;
}

int connect_socket(char *host, int port, int socktype) {
    int err;
    struct addrinfo addr_hints;
    struct addrinfo *addr_results;

    // Obtain address(es) matching host/port
    memset(&addr_hints, 0, sizeof(struct addrinfo));
    addr_hints.ai_family = AF_UNSPEC;
    addr_hints.ai_socktype = socktype;
    addr_hints.ai_flags = AI_NUMERICSERV;
    addr_hints.ai_protocol = 0;

    char port_string[6];
    sprintf(port_string, "%u", port);
    err = getaddrinfo(host, port_string, &addr_hints, &addr_results);
    if (err < 0) return CONNECT_SOCKET_ERROR_RESOLVE;

    int sock = -1;

    // Try all resolved hosts
    for (struct addrinfo *addr_result = addr_results; addr_result != NULL; addr_result = addr_result->ai_next) {
        sock = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
        if (sock < 0) continue;

        if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) == 0) break;

        close(sock);

        sock = -1;
    }

    freeaddrinfo(addr_results);

    if (sock < 0) return CONNECT_SOCKET_ERROR_CONNECT;

    // Read/write timeouts
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    err = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    if (err != 0) goto _opts_error;
    err = setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
    if (err != 0) goto _opts_error;

    // Reuse address
    int reuse = 1;
    err = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (err != 0) goto _opts_error;

    return sock;

    _opts_error:
    close(sock);
    return CONNECT_SOCKET_ERROR_OPTS;
}

char *http_auth_basic_header(const char *username, const char *password) {
    int out;
    char *user_info = NULL;
    char *digest = NULL;
    size_t n = 0;
    asprintf(&user_info, "%s:%s", username, password);
    mbedtls_base64_encode(NULL, 0, &n, (const unsigned char *)user_info, strlen(user_info));
    digest = calloc(1, 6 + n + 1);
    strcpy(digest, "Basic ");
    mbedtls_base64_encode((unsigned char *)digest + 6, n, (size_t *)&out, (const unsigned char *)user_info, strlen(user_info));
    free(user_info);
    return digest;
}

esp_err_t write_all(int fd, char *buf, size_t buf_len) {
    int ret;
    while (buf_len > 0) {
        ret = write(fd, buf, buf_len);
        if (ret < 0) return ESP_FAIL;

        buf += ret;
        buf_len -= ret;
    }
    return ESP_OK;
}
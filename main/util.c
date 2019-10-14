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
#include <esp_log.h>

#include "util.h"

void destroy_socket(int *socket) {
    if (*socket < 0) return;
    shutdown(*socket, SHUT_RDWR);
    close(*socket);
    *socket = -1;
}

int connect_socket(char *host, int port, int socktype) {
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
    if (getaddrinfo(host, port_string, &addr_hints, &addr_results) < 0) {
        return CONNECT_SOCKET_ERROR_RESOLVE;
    }

    int sock = CONNECT_SOCKET_ERROR_CONNECT;

    // Try all resolved hosts
    for (struct addrinfo *addr_result = addr_results; addr_result != NULL; addr_result = addr_result->ai_next) {
        sock = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
        if (sock < 0) {
            continue;
        }

        if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) == 0) {
            break;
        }

        close(sock);
    }

    freeaddrinfo(addr_results);

    return sock;
}

char *http_auth_basic(const char *username, const char *password) {
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
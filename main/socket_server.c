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

#include <string.h>
#include <sys/param.h>
#include "math.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "tcpip_adapter.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include <wifi.h>
#include <status_led.h>

#include "socket_server.h"
#include "config.h"
#include "uart.h"
#include "util.h"

static const char *TAG = "SOCKET_SERVER";

#define BUFFER_SIZE 1024

#ifndef CONFIG_EXAMPLE_IPV6
#define SOCKET_ADDR_FAMILY AF_INET
#define SOCKET_IP_PROTOCOL IPPROTO_IP
#elif
#define SOCKET_ADDR_FAMILY AF_INET6
#define SOCKET_IP_PROTOCOL IPPROTO_IP6
#endif

int sock_tcp, sock_udp;
char *buffer;

static status_led_handle_t status_led = NULL;

typedef struct socket_client_t {
    int socket;
    struct sockaddr_in6 addr;
    int type;
    SLIST_ENTRY(socket_client_t) next;
} socket_client_t;

SLIST_HEAD(socket_client_list_t, socket_client_t) socket_client_list;

static bool socket_address_equal(struct sockaddr_in6 *a, struct sockaddr_in6 *b) {
    if (a->sin6_family != b->sin6_family) return false;

    if (a->sin6_family == PF_INET) {
        struct sockaddr_in *a4 = (struct sockaddr_in *) a;
        struct sockaddr_in *b4 = (struct sockaddr_in *) b;

        return a4->sin_addr.s_addr == b4->sin_addr.s_addr && a4->sin_port == b4->sin_port;
    } else if (a->sin6_family == PF_INET6) {
        return memcmp(&a->sin6_addr, &b->sin6_addr, sizeof(a->sin6_addr)) == 0 && a->sin6_port == b->sin6_port;
    } else {
        return false;
    }
}

static socket_client_t * socket_client_add(int sock, struct sockaddr_in6 addr, int socktype) {
    socket_client_t *client = malloc(sizeof(socket_client_t));
    client->socket = sock;
    client->addr = addr;
    client->type = socktype;

    SLIST_INSERT_HEAD(&socket_client_list, client, next);

    char *addr_str = sockaddrtostr((struct sockaddr *) &addr);
    ESP_LOGI(TAG, "Accepted %s client %s", SOCKTYPE_NAME(socktype), addr_str);
    uart_nmea("$PESP,SOCK,SRV,%s,CONNECTED,%s", SOCKTYPE_NAME(socktype), addr_str);

    if (status_led != NULL) status_led->flashing_mode = STATUS_LED_FADE;

    return client;
}

static void socket_client_remove(socket_client_t *socket_client) {
    char *addr_str = sockaddrtostr((struct sockaddr *) &socket_client->addr);
    ESP_LOGI(TAG, "Disconnected %s client %s", SOCKTYPE_NAME(socket_client->type), addr_str);
    uart_nmea("$PESP,SOCK,SRV,%s,DISCONNECTED,%s", SOCKTYPE_NAME(socket_client->type), addr_str);

    destroy_socket(&socket_client->socket);

    SLIST_REMOVE(&socket_client_list, socket_client, socket_client_t, next);
    free(socket_client);

    if (status_led != NULL && SLIST_EMPTY(&socket_client_list)) status_led->flashing_mode = STATUS_LED_STATIC;
}

static void socket_server_uart_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data) {
    uart_data_t *data = event_data;

    socket_client_t *client, *client_tmp;
    SLIST_FOREACH_SAFE(client, &socket_client_list, next, client_tmp) {
        int err = write(client->socket, data->buffer, data->len);
        if (err < 0) socket_client_remove(client);
    }
}

static int socket_init(int socktype, int port, int *sock) {
    #ifndef CONFIG_EXAMPLE_IPV6
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(port);
    #else // IPV6
        struct sockaddr_in6 dest_addr;
        bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons(port);
    #endif

    *sock = socket(SOCKET_ADDR_FAMILY, socktype, SOCKET_IP_PROTOCOL);
    ERROR_ACTION(TAG, *sock < 0, return *sock, "Could not create %s socket: %d %s", SOCKTYPE_NAME(socktype), errno, strerror(errno));

    int reuse = 1;
    int err = setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    ERROR_ACTION(TAG, err != 0, destroy_socket(sock); return err, "Could not set %s socket options: %d %s", SOCKTYPE_NAME(socktype), errno, strerror(errno));

    err = bind(*sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    ERROR_ACTION(TAG, err != 0, destroy_socket(sock); return err, "Could not bind %s socket: %d %s", SOCKTYPE_NAME(socktype), errno, strerror(errno));

    ESP_LOGI(TAG, "%s socket listening on port %d", SOCKTYPE_NAME(socktype), port);
    uart_nmea("$PESP,SOCK,SRV,%s,BIND,%d", SOCKTYPE_NAME(socktype), port);

    return 0;
}

static int socket_tcp_init() {
    int port = config_get_u16(CONF_ITEM(KEY_CONFIG_SOCKET_SERVER_TCP_PORT));

    int err = socket_init(SOCK_STREAM, port, &sock_tcp);
    if (err < 0) return err;

    err = listen(sock_tcp, 1);
    ERROR_ACTION(TAG, err != 0, destroy_socket(&sock_tcp); return err, "Could not listen to TCP socket: %d %s", errno, strerror(errno));

    return 0;
}

static esp_err_t socket_tcp_accept() {
    struct sockaddr_in6 source_addr;
    uint addr_len = sizeof(source_addr);
    int sock = accept(sock_tcp, (struct sockaddr *)&source_addr, &addr_len);
    ERROR_ACTION(TAG, sock < 0, return ESP_FAIL, "Could not accept new TCP connection: %d %s", errno, strerror(errno));

    socket_client_add(sock, source_addr, SOCK_STREAM);
    return ESP_OK;
}

static int socket_udp_init() {
    int port = config_get_u16(CONF_ITEM(KEY_CONFIG_SOCKET_SERVER_UDP_PORT));

    return socket_init(SOCK_DGRAM, port, &sock_udp);
}

static bool socket_udp_has_client(struct sockaddr_in6 *source_addr) {
    socket_client_t *client;
    SLIST_FOREACH(client, &socket_client_list, next) {
        if (client->type != SOCK_DGRAM) continue;

        struct sockaddr_in6 *client_addr = ((struct sockaddr_in6 *) &client->addr);

        if (socket_address_equal(source_addr, client_addr)) return true;
    }

    return false;
}

static esp_err_t socket_udp_client_accept(struct sockaddr_in6 source_addr) {
    if (socket_udp_has_client(&source_addr)) return ESP_OK;

    int sock = socket(SOCKET_ADDR_FAMILY, SOCK_DGRAM, SOCKET_IP_PROTOCOL);
    ERROR_ACTION(TAG, sock < 0, return sock, "Could not create client UDP socket: %d %s", errno, strerror(errno));

    int reuse = 1;
    int err = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    ERROR_ACTION(TAG, sock < 0, destroy_socket(&sock); return err, "Could not set client UDP socket options: %d %s", errno, strerror(errno));

    struct sockaddr_in6 server_addr;
    socklen_t socklen = sizeof(server_addr);
    getsockname(sock_udp, (struct sockaddr *)&server_addr, &socklen);
    err = bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    ERROR_ACTION(TAG, err != 0, destroy_socket(&sock); return err, "Could not bind client UDP socket: %d %s", errno, strerror(errno));

    err = connect(sock, (struct sockaddr *)&source_addr, sizeof(source_addr));
    ERROR_ACTION(TAG, err != 0, destroy_socket(&sock); return err, "Could not connect client UDP socket: %d %s", errno, strerror(errno));

    socket_client_add(sock, source_addr, SOCK_DGRAM);
    return ESP_OK;
}

static esp_err_t socket_udp_accept() {
    struct sockaddr_in6 source_addr;
    socklen_t socklen = sizeof(source_addr);

    // Receive until nothing left to receive
    int len;
    while ((len = recvfrom(sock_udp, buffer, sizeof(buffer), MSG_DONTWAIT, (struct sockaddr *)&source_addr, &socklen)) > 0) {
        // Multiple connections could have been made at once, so accept for every receive just in case
        socket_udp_client_accept(source_addr);

        uart_write(buffer, len);
    }

    // Error occurred during receiving
    if (len < 0 && errno != EWOULDBLOCK) {
        ESP_LOGE(TAG, "Unable to receive UDP connection: %d %s", errno, strerror(errno));
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void socket_clients_receive(fd_set *socket_set) {
    socket_client_t *client, *client_tmp;
    SLIST_FOREACH_SAFE(client, &socket_client_list, next, client_tmp) {
        if (!FD_ISSET(client->socket, socket_set)) continue;

        // Receive until nothing left to receive
        int len;
        while ((len = recv(client->socket, buffer, sizeof(buffer), MSG_DONTWAIT)) > 0) {
            uart_write(buffer, len);
        }

        // Remove on error
        if (len < 0 && errno != EWOULDBLOCK) {
            socket_client_remove(client);
        }
    }
}

static void socket_server_task(void *ctx) {
    uart_register_handler(socket_server_uart_handler);

    config_color_t status_led_color = config_get_color(CONF_ITEM(KEY_CONFIG_SOCKET_SERVER_COLOR));
    if (status_led_color.rgba != 0) status_led = status_led_add(status_led_color.rgba, STATUS_LED_STATIC, 500, 2000, 0);

    while (true) {
        SLIST_INIT(&socket_client_list);

        socket_tcp_init();
        socket_udp_init();

        // Accept/receive loop
        buffer = malloc(BUFFER_SIZE);
        fd_set socket_set;
        while (true) {
            // Reset all selected
            FD_ZERO(&socket_set);

            // New TCP/UDP connections
            FD_SET(sock_tcp, &socket_set);
            FD_SET(sock_udp, &socket_set);

            int maxfd = MAX(sock_tcp, sock_udp);

            // Existing connections
            socket_client_t *client;
            SLIST_FOREACH(client, &socket_client_list, next) {
                FD_SET(client->socket, &socket_set);
                maxfd = MAX(maxfd, client->socket);
            }

            // Wait for activity on one of selected
            int err = select(maxfd + 1, &socket_set, NULL, NULL, NULL);
            ERROR_ACTION(TAG, err < 0, goto _error, "Could not select socket to receive from: %d %s", errno, strerror(errno))

            // Accept new connections
            if (FD_ISSET(sock_tcp, &socket_set)) socket_tcp_accept();
            if (FD_ISSET(sock_udp, &socket_set)) socket_udp_accept();

            // Receive from existing connections
            socket_clients_receive(&socket_set);
        }

        _error:
        destroy_socket(&sock_tcp);
        destroy_socket(&sock_udp);
        socket_client_t *client, *client_tmp;
        SLIST_FOREACH_SAFE(client, &socket_client_list, next, client_tmp) {
            destroy_socket(&client->socket);
            SLIST_REMOVE(&socket_client_list, client, socket_client_t, next);
            free(client);
        }

        free(buffer);
    }
    vTaskDelete(NULL);
}

void socket_server_init() {
    if (!config_get_bool1(CONF_ITEM(KEY_CONFIG_SOCKET_SERVER_ACTIVE))) return;

    xTaskCreate(socket_server_task, "socket_server_task", 4096, NULL, 2, NULL);
}
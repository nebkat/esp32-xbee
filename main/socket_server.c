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
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include <wifi.h>

#include "socket_server.h"
#include "config.h"
#include "uart.h"
#include "util.h"

static const char *TAG = "SOCKET";

#define SOCKET_TCP_PORT_DEFAULT 23
#define SOCKET_UDP_PORT_DEFAULT 23

#ifndef CONFIG_EXAMPLE_IPV6
#define SOCKET_ADDR_FAMILY AF_INET
#define SOCKET_IP_PROTOCOL IPPROTO_IP
#elif
#define SOCKET_ADDR_FAMILY AF_INET6
#define SOCKET_IP_PROTOCOL IPPROTO_IP6
#endif

typedef struct socket_client_t {
    int socket;
    struct sockaddr_in6 addr;
    int type;
    struct socket_client_t *next;
} socket_client_t;

socket_client_t *clients = NULL;

int socket_tcp, socket_udp;

char rx_buffer[128];

// Include port
char addr_str[INET6_ADDRSTRLEN + 6 + 1];

static void socket_address_get_string(struct sockaddr_in6 *a) {
    int port = 0;
    // Get address string
    if (a->sin6_family == PF_INET) {
        struct sockaddr_in *a4 = (struct sockaddr_in *) a;

        inet_ntop(AF_INET, &a4->sin_addr, addr_str, INET_ADDRSTRLEN);
        port = a4->sin_port;
    } else if (a->sin6_family == PF_INET6) {
        inet_ntop(AF_INET6, &a->sin6_addr, addr_str, INET6_ADDRSTRLEN);
        port = a->sin6_port;
    } else {
        strcpy(addr_str, "UNKNOWN");
    }

    // Append port number
    sprintf(addr_str + strlen(addr_str), ":%d", port);
}

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

static socket_client_t * socket_client_add(int sock, struct sockaddr_in6 addr, int type) {
    socket_client_t *client = malloc(sizeof(socket_client_t));
    client->socket = sock;
    client->addr = addr;
    client->type = type;
    client->next = clients;
    clients = client;

    socket_address_get_string(&addr);
    ESP_LOGI(TAG, "Accepted %s client %s", type == SOCK_STREAM ? "TCP" : "UDP", addr_str);

    return client;
}

static void socket_client_remove(socket_client_t **client_tcp_ptr) {
    socket_address_get_string(&(*client_tcp_ptr)->addr);
    ESP_LOGI(TAG, "Disconnected %s client %s", (*client_tcp_ptr)->type == SOCK_STREAM ? "TCP" : "UDP", addr_str);

    destroy_socket(&(*client_tcp_ptr)->socket);

    socket_client_t *remove_client = *client_tcp_ptr;
    *client_tcp_ptr = remove_client->next;

    free(remove_client);
}

static void socket_server_uart_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data) {
    uart_data_t *data = event_data;

    // TODO: for (socket_client_t *client = clients)

    socket_client_t **client_ptr = &clients;
    socket_client_t *client = clients;
    while (client != NULL) {
        int err = send(client->socket, data->buffer, data->len, 0);
        if (err < 0) {
            socket_client_remove(client_ptr);
        } else {
            client_ptr = &client->next;
        }

        client = *client_ptr;
    }
}

static int socket_tcp_init() {
    int port = config_get_u16(CONF_ITEM(KEY_CONFIG_SOCKET_SERVER_TCP_PORT));

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

    socket_tcp = socket(SOCKET_ADDR_FAMILY, SOCK_STREAM, SOCKET_IP_PROTOCOL);
    if (socket_tcp < 0) {
        ESP_LOGE(TAG, "Unable to TCP create socket: errno %d", errno);
        return socket_tcp;
    }
    ESP_LOGI(TAG, "TCP socket created");

    int reuse = 1;
    int err = setsockopt(socket_tcp, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (err != 0) {
        ESP_LOGE(TAG, "Unable to set TCP socket options: errno %d", errno);
        perror(NULL);
    }

    err = bind(socket_tcp, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "TCP socket unable to bind: errno %d", errno);
        return err;
    }
    ESP_LOGI(TAG, "TCP socket bound, port %d", port);

    err = listen(socket_tcp, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        return err;
    }
    ESP_LOGI(TAG, "TCP socket listening");

    return 0;
}

static int socket_udp_init() {
    int port = config_get_u16(CONF_ITEM(KEY_CONFIG_SOCKET_SERVER_UDP_PORT));

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

    socket_udp = socket(SOCKET_ADDR_FAMILY, SOCK_DGRAM, SOCKET_IP_PROTOCOL);
    if (socket_udp < 0) {
        ESP_LOGE(TAG, "Unable to create UDP socket: errno %d", errno);
        return socket_udp;
    }
    ESP_LOGI(TAG, "UDP socket created");

    int err = bind(socket_udp, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "UDP socket unable to bind: errno %d", errno);
        return err;
    }
    ESP_LOGI(TAG, "UDP socket bound, port %d", port);

    return 0;
}

static esp_err_t socket_tcp_accept() {
    struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
    uint addr_len = sizeof(source_addr);
    int sock = accept(socket_tcp, (struct sockaddr *)&source_addr, &addr_len);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to accept TCP connection: errno %d", errno);
        return ESP_FAIL;
    }

    socket_client_add(sock, source_addr, SOCK_STREAM);
    return ESP_OK;
}

static void socket_tcp_clients_receive(fd_set *socket_set) {
    socket_client_t **client_ptr = &clients;
    socket_client_t *client = clients;
    while (client != NULL) {
        if (FD_ISSET(client->socket, socket_set)) {
            int len = recv(client->socket, rx_buffer, sizeof(rx_buffer), 0);
            if (len <= 0) {
                socket_client_remove(client_ptr);
            } else {
                client_ptr = &client->next;
            }

            uart_write(rx_buffer, len);
        } else {
            client_ptr = &client->next;
        }

        client = *client_ptr;
    }
}

static bool socket_udp_has_client(struct sockaddr_in6 *source_addr) {
    socket_client_t *client_udp = clients;
    while (client_udp != NULL) {
        if (client_udp->type != SOCK_DGRAM) continue;

        struct sockaddr_in6 *client_addr = ((struct sockaddr_in6 *) &client_udp->addr);

        if (socket_address_equal(source_addr, client_addr)) return true;

        client_udp = client_udp->next;
    }

    return false;
}

static esp_err_t socket_udp_client_accept(struct sockaddr_in6 source_addr) {
    if (socket_udp_has_client(&source_addr)) return ESP_OK;

    int sock = socket(SOCKET_ADDR_FAMILY, SOCK_DGRAM, SOCKET_IP_PROTOCOL);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create UDP client socket: errno %d", errno);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "UDP client socket created");

    int err = connect(sock, (struct sockaddr *)&source_addr, sizeof(source_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "UDP client socket unable to connect: errno %d", errno);
        return err;
    }
    ESP_LOGI(TAG, "UDP client socket connected");

    socket_client_add(sock, source_addr, SOCK_DGRAM);
    return ESP_OK;
}

static esp_err_t socket_udp_receive() {
    struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
    socklen_t socklen = sizeof(source_addr);
    int len = recvfrom(socket_udp, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&source_addr, &socklen);
    // Error occurred during receiving
    if (len < 0) {
        ESP_LOGE(TAG, "Unable to receive UDP connection: errno %d", errno);
        return ESP_FAIL;
    }

    uart_write(rx_buffer, len);

    return socket_udp_client_accept(source_addr);
}

static void socket_server_task(void *ctx) {
    while (true) {
        wait_for_ip();

        socket_tcp_init();
        socket_udp_init();

        // Accept/receive loop
        fd_set socket_set;
        while (true) {
            FD_ZERO(&socket_set);

            FD_SET(socket_tcp, &socket_set);
            FD_SET(socket_udp, &socket_set);

            int maxfd = MAX(socket_tcp, socket_udp);

            // TCP clients
            for (socket_client_t *client_ptr = clients; client_ptr != NULL; client_ptr = client_ptr->next) {
                if (client_ptr->type != SOCK_STREAM) continue;

                FD_SET(client_ptr->socket, &socket_set);
                maxfd = MAX(maxfd, client_ptr->socket);
            }

            if (select(maxfd + 1, &socket_set, NULL, NULL, NULL) < 0) {
                ESP_LOGE(TAG, "Select failed: errno %d", errno);
                break;
            }

            if (FD_ISSET(socket_tcp, &socket_set)) socket_tcp_accept();
            if (FD_ISSET(socket_udp, &socket_set)) socket_udp_receive();

            // TCP clients
            socket_tcp_clients_receive(&socket_set);
        }

        destroy_socket(&socket_tcp);
        destroy_socket(&socket_udp);
    }
    vTaskDelete(NULL);
}

void socket_server_init() {
    if (!config_get_boola(CONF_ITEM(KEY_CONFIG_SOCKET_SERVER_ACTIVE))) return;

    xTaskCreate(socket_server_task, "socket_server_task", 32768, NULL, 2, NULL);

    // Receive UART messages
    uart_register_handler(socket_server_uart_handler);
}
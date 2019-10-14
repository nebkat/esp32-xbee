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

#include <limits.h>
#include <stdbool.h>
#include <esp_log.h>
#include <esp_event_base.h>
#include <sys/socket.h>
#include <wifi.h>
#include <mdns.h>
#include <tasks.h>
#include "ntrip.h"
#include "config.h"
#include "util.h"
#include "uart.h"

static const char *TAG = "NTRIP_CASTER";

#define NTRIP_CASTER_NAME "ESP32XBeeNtripCaster"

#define BUFFER_SIZE 8192

static int socket_caster = -1;

typedef struct ntrip_caster_client_t {
    int socket;
    struct ntrip_caster_client_t *next;
} ntrip_caster_client_t;

ntrip_caster_client_t *caster_clients;

static void ntrip_caster_client_remove(ntrip_caster_client_t **caster_client_ptr) {
    destroy_socket(&(*caster_client_ptr)->socket);

    ntrip_caster_client_t *remove_client = *caster_client_ptr;
    *caster_client_ptr = remove_client->next;

    free(remove_client);
}

static void ntrip_caster_uart_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data) {
    uart_data_t *data = event_data;

    ntrip_caster_client_t **caster_client_ptr = &caster_clients;
    ntrip_caster_client_t *caster_client = caster_clients;
    while (caster_client != NULL) {
        int err = send(caster_client->socket, data->buffer, data->len, 0);
        if (err < 0) {
            ESP_LOGI(TAG, "Client disconnected");
            ntrip_caster_client_remove(caster_client_ptr);
        } else {
            caster_client_ptr = &caster_client->next;
        }

        caster_client = *caster_client_ptr;
    }
}

static int ntrip_caster_socket_init() {
    int port = config_get_u16(CONF_ITEM(KEY_CONFIG_NTRIP_CASTER_PORT));

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

    socket_caster = socket(SOCKET_ADDR_FAMILY, SOCK_STREAM, SOCKET_IP_PROTOCOL);
    if (socket_caster < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return socket_caster;
    }
    ESP_LOGI(TAG, "TCP socket created");

    int reuse = 1;
    int err = setsockopt(socket_caster, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (err != 0) {
        ESP_LOGE(TAG, "Unable to set socket options: errno %d", errno);
        perror(NULL);
    }

    err = bind(socket_caster, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        return err;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", port);

    err = listen(socket_caster, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        return err;
    }
    ESP_LOGI(TAG, "Socket listening");

    return 0;
}

void ntrip_caster_task(void *ctx) {
    uart_register_handler(ntrip_caster_uart_handler);

    while (true) {
        wait_for_ip();

        destroy_socket(&socket_caster);

        ntrip_caster_socket_init();

        char mountpoint[64];
        char username[64];
        char password[64];
        size_t length;

        length = 64;
        config_get_str_blob(CONF_ITEM(KEY_CONFIG_NTRIP_CASTER_USERNAME), (char *) username, &length);
        length = 64;
        config_get_str_blob(CONF_ITEM(KEY_CONFIG_NTRIP_CASTER_PASSWORD), (char *) password, &length);
        length = 64;
        config_get_str_blob(CONF_ITEM(KEY_CONFIG_NTRIP_CASTER_MOUNTPOINT), (char *) mountpoint, &length);

        int socket_caster_client = -1;
        char *request = malloc(BUFFER_SIZE);
        while (true) {
            destroy_socket(&socket_caster_client);

            struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
            uint addr_len = sizeof(source_addr);
            socket_caster_client = accept(socket_caster, (struct sockaddr *)&source_addr, &addr_len);
            if (socket_caster_client < 0) {
                ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
                break;
            }

            ESP_LOGI(TAG, "Accepted request");

            int len = recv(socket_caster_client, request, BUFFER_SIZE - 1, 0);
            if (len <= 0) {
                ESP_LOGE(TAG, "Could not receive from client");
                continue;
            }
            request[len] = '\0';

            char *token = strtok(request, NEWLINE);
            char path[64];
            if (sscanf(token, "GET /%64[^\r]", path) != 1) {
                // Unsupported method, no action required in NTRIP
                continue;
            }

            char *basic_authentication = strlen(username) == 0 ? NULL : http_auth_basic(username, password);

            bool valid = true;
            bool authenticated = basic_authentication == NULL ? true : false;
            bool ntrip_agent = false;
            while ((token = strtok(NULL, NEWLINE)) != NULL) {
                // End of header
                if (strlen(token) == 0) break;

                char key[64];
                char value[256];
                if (sscanf(token, "%63[^:]:%*[ ]%255[^\n]", key, value) != 2) {
                    valid = false;
                    ESP_LOGE(TAG, "Detected invalid header line: %s", token);
                    break;
                }

                if (strcasecmp(key, "User-Agent") == 0) {
                    ntrip_agent = strcasestr(value, "NTRIP") != NULL;
                } else if (!authenticated && strcasecmp(key, "Authorization") == 0) {
                    authenticated = strcasestr(value, "Basic ") == value && strcasecmp(value + 6, basic_authentication + 6) == 0;
                }
            }

            free(basic_authentication);

            // Skip invalid requests
            if (!valid) continue;

            // Mountpoint requested
            if (strstr(path, mountpoint) == path) {
                if (!authenticated) {
                    char *message = "Authorization Required";

                    char *response = NULL;
                    asprintf(&response, "HTTP/1.0 401 Unauthorized" NEWLINE \
                            "Server: %s/1.0" NEWLINE \
                            "WWW-Authenticate: Basic realm=\"/%s\"" NEWLINE
                            "Content-Type: text/plain" NEWLINE \
                            "Content-Length: %d" NEWLINE \
                            "Connection: close" NEWLINE \
                            NEWLINE \
                            "%s",
                            NTRIP_CASTER_NAME, mountpoint, strlen(message), message);

                    int err = send(socket_caster_client, response, strlen(response), 0);
                    free(response);
                    if (err < 0) {
                        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    }

                    continue;
                }

                char response[] = "ICY 200 OK" NEWLINE;
                int err = send(socket_caster_client, response, sizeof(response), 0);
                if (err < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    continue;
                }

                ntrip_caster_client_t *client = malloc(sizeof(ntrip_caster_client_t));
                client->socket = socket_caster_client;
                client->next = caster_clients;
                caster_clients = client;

                // Socket will now be dealt with by ntrip_caster_uart_handler
                socket_caster_client = -1;

                continue;
            }

            // Unknown mountpoint or sourcetable requested
            char *stream = NULL;
            asprintf(&stream, "STR;%s;;;;;;;;0.00;0.00;0;0;;none;%c;N;0;" NEWLINE "ENDSOURCETABLE",
                    mountpoint, strlen(username) == 0 ? 'N' : 'B');

            char *response = NULL;
            asprintf(&response, "%s 200 OK" NEWLINE \
                "Server: %s/1.0" NEWLINE \
                "Content-Type: text/plain" NEWLINE \
                "Content-Length: %d" NEWLINE \
                "Connection: close" NEWLINE \
                NEWLINE \
                "%s",
                ntrip_agent ? "SOURCETABLE" : "HTTP/1.0", NTRIP_CASTER_NAME, strlen(stream), stream);

            int err = send(socket_caster_client, response, strlen(response), 0);
            free(stream);
            free(response);
            if (err < 0) {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
            }
        }

        free(request);
    }

    vTaskDelete(NULL);
}

void ntrip_caster_init() {
    if (!config_get_bool1(CONF_ITEM(KEY_CONFIG_NTRIP_CASTER_ACTIVE))) return;

    xTaskCreate(ntrip_caster_task, "ntrip_caster_task", 16384, NULL, TASK_PRIORITY_NTRIP, NULL);
}
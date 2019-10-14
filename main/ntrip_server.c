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
#include <tasks.h>
#include "ntrip.h"
#include "config.h"
#include "util.h"
#include "uart.h"

static const char *TAG = "NTRIP_SERVER";

#define NTRIP_SERVER_NAME "ESP32XBeeNtripServer"

static int socket_server = -1;
static int server_keep_alive;

static void ntrip_server_uart_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data) {
    if (socket_server == -1) return;

    uart_data_t *data = event_data;
    int sent = send(socket_server, data->buffer, data->len, 0);
    if (sent < 0) destroy_socket(&socket_server);

    server_keep_alive = 0;
}

static bool ntrip_response_ok(void *response) {
    return strstr(response, "OK") == response || strstr(response, "ICY 200 OK") == response;
}

void ntrip_server_task(void *ctx) {
    uart_register_handler(ntrip_server_uart_handler);

    while (true) {
        wait_for_ip();

        destroy_socket(&socket_server);

        char host[128];
        uint16_t port;
        char mountpoint[64];
        char password[64];

        size_t length;

        config_get_primitive(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_PORT), &port);
        length = 128;
        config_get_str_blob(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_HOST), (char *) host, &length);
        length = 64;
        config_get_str_blob(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_PASSWORD), (char *) password, &length);
        length = 64;
        config_get_str_blob(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_MOUNTPOINT), (char *) mountpoint, &length);

        ESP_LOGI(TAG, "Connecting to %s:%d/%s", host, port, mountpoint);
        socket_server = connect_socket(host, port, SOCK_STREAM);
        if (socket_server == CONNECT_SOCKET_ERROR_RESOLVE) {
            ESP_LOGE(TAG, "Could not resolve host");
            continue;
        } else if (socket_server == CONNECT_SOCKET_ERROR_CONNECT) {
            ESP_LOGE(TAG, "Could not connect to host: errno %d", errno);
            continue;
        }

        char *request = NULL;
        asprintf(&request, "SOURCE %s /%s HTTP/1.1" NEWLINE \
                "Source-Agent: NTRIP %s/1.0" NEWLINE \
                NEWLINE, password, mountpoint, NTRIP_SERVER_NAME);
        int err = send(socket_server, request, strlen(request), 0);
        free(request);
        if (err < 0) {
            ESP_LOGE(TAG, "Error occured during sending: errno %d", errno);
            continue;
        }

        char *response = malloc(128);
        int len = recv(socket_server, response, 128, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "recv failed: errno %d", errno);
            free(response);
            continue;
        }

        bool response_ok = ntrip_response_ok(response);
        free(response);
        if (!response_ok) {
            ESP_LOGE(TAG, "Error connecting: %s", response);
            continue;
        }

        ESP_LOGI(TAG, "Successfully connected to %s:%d/%s", host, port, mountpoint);

        // Keep alive
        server_keep_alive = NTRIP_KEEP_ALIVE_THRESHOLD;
        while (true) {
            if (server_keep_alive >= NTRIP_KEEP_ALIVE_THRESHOLD) {
                int sent = send(socket_server, NEWLINE, NEWLINE_LENGTH, 0);
                if (sent < 0) {
                    break;
                }

                server_keep_alive = 0;
            }

            server_keep_alive += NTRIP_KEEP_ALIVE_THRESHOLD / 10;
            vTaskDelay(pdMS_TO_TICKS(NTRIP_KEEP_ALIVE_THRESHOLD / 10));
        }

        ESP_LOGW(TAG, "Disconnected from %s:%d/%s", host, port, mountpoint);
    }

    vTaskDelete(NULL);
}

void ntrip_server_init() {
    if (!config_get_bool1(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_ACTIVE))) return;

    xTaskCreate(ntrip_server_task, "ntrip_server_task", 16384, NULL, TASK_PRIORITY_NTRIP, NULL);
}
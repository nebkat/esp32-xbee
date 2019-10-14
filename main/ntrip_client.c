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
#include <lwip/netdb.h>
#include <coap_config.h>
#include <status_led.h>
#include "ntrip.h"
#include "config.h"
#include "util.h"
#include "uart.h"

static const char *TAG = "NTRIP_CLIENT";

#define NTRIP_CLIENT_NAME "ESP32XBeeNtripClient"

static int socket_client = -1;

static status_led_handle_t status_led = NULL;

static void ntrip_client_uart_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data) {
    if (socket_client == -1) return;

    uart_data_t *data = event_data;
    int sent = send(socket_client, data->buffer, data->len, 0);
    if (sent < 0) destroy_socket(&socket_client);
}

static bool ntrip_response_ok(void *response) {
    return strstr(response, "OK") == response || strstr(response, "ICY 200 OK") == response;
}

static void ntrip_client_task(void *ctx) {
    uart_register_handler(ntrip_client_uart_handler);

    while (true) {
        status_led_remove(status_led);

        wait_for_ip();

        destroy_socket(&socket_client);

        char host[128];
        uint16_t port;
        char mountpoint[64];
        char username[64];
        char password[64];

        size_t length;

        config_get_primitive(CONF_ITEM(KEY_CONFIG_NTRIP_CLIENT_PORT), &port);
        length = 128;
        config_get_str_blob(CONF_ITEM(KEY_CONFIG_NTRIP_CLIENT_HOST), (char *) host, &length);
        length = 64;
        config_get_str_blob(CONF_ITEM(KEY_CONFIG_NTRIP_CLIENT_USERNAME), (char *) username, &length);
        length = 64;
        config_get_str_blob(CONF_ITEM(KEY_CONFIG_NTRIP_CLIENT_PASSWORD), (char *) password, &length);
        length = 64;
        config_get_str_blob(CONF_ITEM(KEY_CONFIG_NTRIP_CLIENT_MOUNTPOINT), (char *) mountpoint, &length);

        ESP_LOGI(TAG, "Connecting to %s:%d/%s", host, port, mountpoint);
        socket_client = connect_socket(host, port, SOCK_STREAM);
        if (socket_client == CONNECT_SOCKET_ERROR_RESOLVE) {
            ESP_LOGE(TAG, "Could not resolve host");
            continue;
        } else if (socket_client == CONNECT_SOCKET_ERROR_CONNECT) {
            ESP_LOGE(TAG, "Could not connect to host: errno %d", errno);
            continue;
        }

        char *authorization = http_auth_basic(username, password);
        char *request = NULL;
        asprintf(&request, "GET /%s HTTP/1.1" NEWLINE \
                "User-Agent: NTRIP %s/1.0" NEWLINE \
                "Authorization: %s" NEWLINE
                NEWLINE
                , mountpoint, NTRIP_CLIENT_NAME, authorization);
        int err = send(socket_client, request, strlen(request), 0);
        free(authorization);
        free(request);
        if (err < 0) {
            ESP_LOGE(TAG, "Error occured during sending: errno %d", errno);
            continue;
        }

        char *response = malloc(128);
        int len = recv(socket_client, response, 128, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "recv failed: errno %d", errno);
            free(response);
            continue;
        }

        if (!ntrip_response_ok(response)) {
            ESP_LOGE(TAG, "Error connecting: %s", response);
            free(response);
            continue;
        }

        ESP_LOGI(TAG, "Successfully connected to %s:%d/%s", host, port, mountpoint);

        status_led = status_led_add(0x00FF0055, STATUS_LED_FADE, 500, 5000, 0);

        while ((len = recv(socket_client, response, 128, 0)) >= 0) {
            uart_write(response, len);
        }
        free(response);

        ESP_LOGW(TAG, "Disconnected from %s:%d/%s", host, port, mountpoint);
    }

    vTaskDelete(NULL);
}

void ntrip_client_init() {
    if (!config_get_bool1(CONF_ITEM(KEY_CONFIG_NTRIP_CLIENT_ACTIVE))) return;

    xTaskCreate(ntrip_client_task, "ntrip_client_task", 16384, NULL, TASK_PRIORITY_NTRIP, NULL);
}
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

#include <stdbool.h>
#include <lwip/inet.h>
#include <sys/socket.h>
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_event_base.h>
#include "ntrip2.h"
#include "config.h"
#include "uart.h"
#include "util.h"

#define HTTP_CONTENT_BUFFER_SIZE 1024
#define NTRIP_PORT_DEFAULT 2101

static const char *TAG = "NTRIP";

esp_http_client_handle_t server_client = NULL;

void ntrip_uart_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data) {
    uart_data_t *data = event_data;

    if (server_client != NULL) {
        esp_http_client_write(server_client, (char *) data->buffer, data->len);
    }
}

void ntrip2_server_task2(void *ctx) {
    while (true) {
        server_client = NULL;

        uint16_t port;
        char host[128];
        char path[256];
        char mountpoint[128];
        char username[128];
        char password[128];

        config_get_u16p(KEY_CONFIG_NTRIP_SERVER_PORT, (uint16_t *) &port, NTRIP_PORT_DEFAULT);
        config_get_str(KEY_CONFIG_NTRIP_SERVER_HOST, (char *) &host, NULL, "88.99.242.79");

        config_get_str(KEY_CONFIG_NTRIP_SERVER_USERNAME, (char *) &username, NULL, "nebkat");
        config_get_str(KEY_CONFIG_NTRIP_SERVER_PASSWORD, (char *) &password, NULL, "disco");
        config_get_str(KEY_CONFIG_NTRIP_SERVER_MOUNTPOINT, (char *) &mountpoint, NULL, "/llewellyn");

        // Configure host URL
        esp_http_client_config_t config = {
                .host = host,
                .port = config_get_u16(KEY_CONFIG_NTRIP_SERVER_PORT, NTRIP_PORT_DEFAULT),
                .method = HTTP_METHOD_POST,
                .path = path,
                .auth_type = HTTP_AUTH_TYPE_BASIC,
                .username = username,
                .password = password
        };

        /*"POST /ExampleMountpoint HTTP/1.1<CR><LF>\n"
        "Host: ntrip.example.com<CR><LF>\n"
        "Ntrip-Version: Ntrip/2.0<CR><LF>\n"
        "Authorization: Basic bnRyaXA6c2VjcmV0<CR><LF>\n"
        "User-Agent: NTRIP ExampleServer/2.0<CR><LF>\n"
        "Connection: close<CR><LF>\n"
        "<CR><LF> "*/

        DEBUG(ESP_LOG_ERROR, TAG);

        // Initialize client
        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_http_client_set_header(client, "Connection", "close");
        esp_http_client_set_header(client, "Ntrip-Version", "Ntrip/2.0");
        esp_http_client_set_header(client, "User-Agent", "NTRIP ESP32XBeeNtripClient/2.0");

        DEBUG(ESP_LOG_ERROR, TAG);

        esp_err_t err;
        if ((err = esp_http_client_open(client, 0)) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            break;
        }

        char buffer[256];

        vTaskDelay(pdMS_TO_TICKS(1000));

        //int read = esp_http_client_read(client, buffer, sizeof(buffer));
        //ESP_LOGE(TAG, "Read %d %.*s", read, read, buffer);

        int content_length = esp_http_client_fetch_headers(client);
        if (esp_http_client_get_status_code(client) != 200) {
            ESP_LOGE(TAG, "Could not connect mountpoint: status %d", esp_http_client_get_status_code(client));
            break;
        }

        server_client = client;

        for (int i = 0; i < 10000000; i++) {
            vTaskDelay(pdMS_TO_TICKS(500));

            char *message = "abcdef";
            esp_http_client_write(client, message, (int) strlen(message));
        }

        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }

    vTaskDelete(NULL);
}

void ntrip2_server_task(void *ctx) {
    while (true) {
        server_client = NULL;

        DEBUG(ESP_LOG_ERROR, TAG);

        char host[128];
        char path[256];
        char mountpoint[128];
        char password[128];

        config_get_str(KEY_CONFIG_NTRIP_SERVER_HOST, (char *) &host, NULL, "88.99.242.79");
        config_get_str(KEY_CONFIG_NTRIP_SERVER_PASSWORD, (char *) &password, NULL, "disco");
        config_get_str(KEY_CONFIG_NTRIP_SERVER_MOUNTPOINT, (char *) &mountpoint, NULL, "/llewellyn");

        sprintf(path, "%s %s", password, mountpoint);

        // Configure host URL
        esp_http_client_config_t config = {
                .host = host,
                .port = config_get_u16(KEY_CONFIG_NTRIP_SERVER_PORT, NTRIP_PORT_DEFAULT),
                .method = HTTP_METHOD_MAX,//HTTP_METHOD_SOURCE,
                .path = path
        };

        // Initialize client
        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_http_client_set_header(client, "Source-Agent", "NTRIP ESP32XBeeNtripClient/2.0");


        esp_err_t err;
        if ((err = esp_http_client_open(client, 0)) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            break;
        }

        char buffer[256];
        err = esp_http_client_get_headers(client);
        if (err != 0) {
            ESP_LOGE(TAG, "Error marking headers as raw!");
        }

        ESP_LOGI(TAG, "Ready to read");

        vTaskDelay(pdMS_TO_TICKS(100));

        int read = esp_http_client_read(client, buffer, sizeof(buffer));
        ESP_LOGE(TAG, "Read %d %.*s", read, read, buffer);


        if (esp_http_client_get_status_code(client) != 200) {
            ESP_LOGE(TAG, "Could not connect mountpoint: status %d", esp_http_client_get_status_code(client));
            break;
        }

        server_client = client;

        for (int i = 0; i < 10000000; i++) {
            vTaskDelay(pdMS_TO_TICKS(500));

            char *message = "abcdef";
            esp_http_client_write(client, message, (int) strlen(message));
        }

        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }

    vTaskDelete(NULL);
}

static void ntrip_client_task(void) {
    while (true) {
        // Configure host URL
        esp_http_client_config_t config = {
                .method = HTTP_METHOD_GET,
                .auth_type = HTTP_AUTH_TYPE_BASIC
        };

        DEBUG(ESP_LOG_ERROR, TAG);

        config_get_u16p(KEY_CONFIG_NTRIP_CLIENT_PORT, (uint16_t *) &config.port, NTRIP_PORT_DEFAULT);
        config_get_str(KEY_CONFIG_NTRIP_CLIENT_HOST, (char *) &config.host, NULL, "");
        config_get_str(KEY_CONFIG_NTRIP_CLIENT_USERNAME, (char *) &config.username, NULL, "");
        config_get_str(KEY_CONFIG_NTRIP_CLIENT_PASSWORD, (char *) &config.password, NULL, "");
        config_get_str(KEY_CONFIG_NTRIP_CLIENT_MOUNTPOINT, (char *) &config.path, NULL, "");

        DEBUG(ESP_LOG_ERROR, TAG);

        // Initialize client
        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_http_client_set_method(client, HTTP_METHOD_GET);
        esp_http_client_set_header(client, "Connection", "close");
        esp_http_client_set_header(client, "Ntrip-Version", "Ntrip/2.0");
        esp_http_client_set_header(client, "User-Agent", "NTRIP ESP32XBeeNtripClient/2.0");

        DEBUG(ESP_LOG_ERROR, TAG);

        esp_err_t err;
        if ((err = esp_http_client_open(client, 0)) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            break;
        }

        DEBUG(ESP_LOG_ERROR, TAG);

        int content_length = esp_http_client_fetch_headers(client);
        if (esp_http_client_get_status_code(client) != 200) {
            ESP_LOGE(TAG, "Could not connect mountpoint: status %d", esp_http_client_get_status_code(client));
            break;
        }

        if (!esp_http_client_is_chunked_response(client)) {
            ESP_LOGE(TAG, "Expected chunked response from NTRIP");
            break;
        }


        char *buffer = malloc(HTTP_CONTENT_BUFFER_SIZE + 1);
        int read;
        while ((read = esp_http_client_read(client, buffer, content_length)) >= 0) {
            ESP_LOGW(TAG, "Read %d bytes %.*s", read, read, buffer);
            uart_write(buffer, read);
        }

        free(buffer);

        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
}
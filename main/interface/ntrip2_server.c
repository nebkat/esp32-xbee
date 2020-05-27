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
#include <esp_http_client.h>
#include "interface/ntrip.h"
#include "config.h"
#include "util.h"
#include "uart.h"

static const char *TAG = "NTRIP_SERVER";

#define BUFFER_SIZE 512

esp_http_client_handle_t http = NULL;
static int server_keep_alive;

static void ntrip2_server_uart_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data) {
    if (http == NULL) return;

    uart_data_t *data = event_data;
    int sent = esp_http_client_write(http, (char *) data->buffer, data->len);
    if (sent < 0) {
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
        http = NULL;
    }

    server_keep_alive = 0;
}

static void ntrip2_server_task(void *ctx) {
    uart_register_handler(ntrip2_server_uart_handler);

    while (true) {
        wait_for_ip();

        char *host, *mountpoint, *username, *password;
        uint16_t port = config_get_u16(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_PORT));
        config_get_str_blob_alloc(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_HOST), (void **) &host);
        config_get_str_blob_alloc(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_USERNAME), (void **) &username);
        config_get_str_blob_alloc(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_PASSWORD), (void **) &password);

        // Prepend '/' to mountpoint path
        size_t length;
        config_get_str_blob(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_MOUNTPOINT), NULL, &length);
        mountpoint = malloc(length + 1);
        mountpoint[0] = '/';
        config_get_str_blob(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_MOUNTPOINT), mountpoint + 1, &length);

        // Configure host URL
        esp_http_client_config_t config = {
                .host = host,
                .port = port,
                .method = HTTP_METHOD_POST,
                .path = mountpoint,
                .auth_type = HTTP_AUTH_TYPE_BASIC,
                .username = username,
                .password = password
        };

        // Initialize server
        http = esp_http_client_init(&config);
        esp_http_client_set_header(http, "Ntrip-Version", "Ntrip/2.0");
        esp_http_client_set_header(http, "User-Agent", "NTRIP " NTRIP_SERVER_NAME "/2.0");
        esp_http_client_set_header(http, "Connection", "close");

        esp_err_t err = esp_http_client_open(http, 0);
        ERROR_ACTION(TAG, err != ESP_OK, goto _error, "Could not open HTTP connection: %d %s", err, esp_err_to_name(err));

        err = esp_http_client_fetch_headers(http);
        ERROR_ACTION(TAG, err < 0, goto _error, "Could not connect to caster: %d %s", errno, strerror(errno));
        int status_code = esp_http_client_get_status_code(http);
        ERROR_ACTION(TAG, status_code != 200, goto _error, "Could not access mountpoint: %d", status_code);

        ESP_LOGI(TAG, "Successfully connected to %s:%d/%s", host, port, mountpoint);
        uart_nmea("$PESP,NTRIP,SRV,CONNECTED,%s:%d,%s", host, port, mountpoint);

        // Keep alive
        server_keep_alive = NTRIP_KEEP_ALIVE_THRESHOLD;
        while (true) {
            if (server_keep_alive >= NTRIP_KEEP_ALIVE_THRESHOLD) {
                int sent = esp_http_client_write(http, NEWLINE, NEWLINE_LENGTH);
                if (sent < 0) break;

                server_keep_alive = 0;
            }

            server_keep_alive += NTRIP_KEEP_ALIVE_THRESHOLD / 10;
            vTaskDelay(pdMS_TO_TICKS(NTRIP_KEEP_ALIVE_THRESHOLD / 10));
        }

        ESP_LOGW(TAG, "Disconnected from %s:%d/%s", host, port, mountpoint);
        uart_nmea("$PESP,NTRIP,SRV,DISCONNECTED,%s:%d,%s", host, port, mountpoint);

        _error:
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
        http = NULL;

        free(host);
        free(mountpoint);
        free(username);
        free(password);
    }

    vTaskDelete(NULL);
}

void ntrip2_server_init() {
    if (!config_get_bool1(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_ACTIVE))) return;

    xTaskCreate(ntrip2_server_task, "ntrip2_server_task", 16384, NULL, TASK_PRIORITY_NTRIP, NULL);
}
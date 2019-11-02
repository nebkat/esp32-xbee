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
#include <esp_log.h>
#include <esp_event_base.h>
#include <sys/socket.h>
#include <wifi.h>
#include <tasks.h>
#include <status_led.h>
#include <retry.h>
#include "ntrip.h"
#include "config.h"
#include "util.h"
#include "uart.h"

static const char *TAG = "NTRIP_CLIENT";

#define BUFFER_SIZE 512

static int sock = -1;

static status_led_handle_t status_led = NULL;

static void ntrip_client_uart_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data) {
    if (sock == -1) return;

    uart_data_t *data = event_data;
    int sent = send(sock, data->buffer, data->len, 0);
    if (sent < 0) destroy_socket(&sock);
}

static void ntrip_client_task(void *ctx) {
    uart_register_handler(ntrip_client_uart_handler);

    config_color_t status_led_color = config_get_color(CONF_ITEM(KEY_CONFIG_NTRIP_CLIENT_COLOR));
    if (status_led_color.rgba != 0) status_led = status_led_add(status_led_color.rgba, STATUS_LED_FADE, 500, 2000, 0);
    if (status_led != NULL) status_led->active = false;

    retry_delay_handle_t delay_handle = retry_init(true, 5, 2000);

    while (true) {
        retry_delay(delay_handle);

        wait_for_ip();

        char *buffer = NULL;

        char *host, *mountpoint, *username, *password;
        uint16_t port = config_get_u16(CONF_ITEM(KEY_CONFIG_NTRIP_CLIENT_PORT));
        config_get_str_blob_alloc(CONF_ITEM(KEY_CONFIG_NTRIP_CLIENT_HOST), (void **) &host);
        config_get_str_blob_alloc(CONF_ITEM(KEY_CONFIG_NTRIP_CLIENT_USERNAME), (void **) &username);
        config_get_str_blob_alloc(CONF_ITEM(KEY_CONFIG_NTRIP_CLIENT_PASSWORD), (void **) &password);
        config_get_str_blob_alloc(CONF_ITEM(KEY_CONFIG_NTRIP_CLIENT_MOUNTPOINT), (void **) &mountpoint);

        ESP_LOGI(TAG, "Connecting to %s:%d/%s", host, port, mountpoint);
        uart_nmea("$PESP,NTRIP,CLI,CONNECTING,%s:%d,%s", host, port, mountpoint);
        sock = connect_socket(host, port, SOCK_STREAM);
        ERROR_ACTION(TAG, sock == CONNECT_SOCKET_ERROR_RESOLVE, goto _error, "Could not resolve host");
        ERROR_ACTION(TAG, sock == CONNECT_SOCKET_ERROR_CONNECT, goto _error, "Could not connect to host");

        buffer = malloc(BUFFER_SIZE);

        char *authorization = http_auth_basic_header(username, password);
        snprintf(buffer, BUFFER_SIZE, "GET /%s HTTP/1.1" NEWLINE \
                "User-Agent: NTRIP %s/1.0" NEWLINE \
                "Authorization: %s" NEWLINE
                NEWLINE
                , mountpoint, NTRIP_CLIENT_NAME, authorization);
        free(authorization);

        int err = write(sock, buffer, strlen(buffer));
        ERROR_ACTION(TAG, err < 0, goto _error, "Could not send request to caster: %d %s", errno, strerror(errno));

        int len = read(sock, buffer, BUFFER_SIZE - 1);
        ERROR_ACTION(TAG, len <= 0, goto _error, "Could not receive response from caster: %d %s", errno, strerror(errno));
        buffer[len] = '\0';

        char *status = extract_http_header(buffer, "");
        ERROR_ACTION(TAG, status == NULL || !ntrip_response_ok(status), free(status); goto _error, "Could not connect to mountpoint: %s", status == NULL ? "HTTP response malformed" : status);
        free(status);

        ESP_LOGI(TAG, "Successfully connected to %s:%d/%s", host, port, mountpoint);
        uart_nmea("$PESP,NTRIP,CLI,CONNECTED,%s:%d,%s", host, port, mountpoint);

        retry_reset(delay_handle);

        if (status_led != NULL) status_led->active = true;

        while ((len = read(sock, buffer, BUFFER_SIZE)) >= 0) {
            uart_write(buffer, len);
        }

        if (status_led != NULL) status_led->active = false;

        ESP_LOGW(TAG, "Disconnected from %s:%d/%s", host, port, mountpoint);
        uart_nmea("$PESP,NTRIP,CLI,DISCONNECTED,%s:%d,%s", host, port, mountpoint);

        _error:
        destroy_socket(&sock);

        free(buffer);
        free(host);
        free(mountpoint);
        free(username);
        free(password);
    }

    vTaskDelete(NULL);
}

void ntrip_client_init() {
    if (!config_get_bool1(CONF_ITEM(KEY_CONFIG_NTRIP_CLIENT_ACTIVE))) return;

    xTaskCreate(ntrip_client_task, "ntrip_client_task", 4096, NULL, TASK_PRIORITY_NTRIP, NULL);
}
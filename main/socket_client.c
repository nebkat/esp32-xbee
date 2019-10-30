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

#include <sys/param.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <uart.h>
#include <util.h>
#include <status_led.h>
#include <wifi.h>
#include <esp_log.h>
#include <sys/socket.h>
#include "socket_client.h"

#include <config.h>

static const char *TAG = "SOCKET_CLIENT";

#define BUFFER_SIZE 1024

static int sock = -1;
static status_led_handle_t status_led = NULL;

static void socket_client_uart_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data) {
    if (sock == -1) return;
    uart_data_t *data = event_data;

    int err = write(sock, data->buffer, data->len);
    if (err < 0) destroy_socket(&sock);
}

static void socket_client_task(void *ctx) {
    uart_register_handler(socket_client_uart_handler);

    status_led = status_led_add(0x00FF0055, STATUS_LED_FADE, 500, 5000, 0);
    status_led->active = false;

    while (true) {
        wait_for_ip();

        char *host, *connect_message;
        uint16_t port = config_get_u16(CONF_ITEM(KEY_CONFIG_SOCKET_CLIENT_PORT));
        config_get_str_blob_alloc(CONF_ITEM(KEY_CONFIG_SOCKET_CLIENT_HOST), (void **) &host);
        config_get_str_blob_alloc(CONF_ITEM(KEY_CONFIG_SOCKET_CLIENT_CONNECT_MESSAGE), (void **) &connect_message);
        int socktype = config_get_bool1(CONF_ITEM(KEY_CONFIG_SOCKET_CLIENT_TYPE_TCP_UDP)) ? SOCK_STREAM : SOCK_DGRAM;

        ESP_LOGI(TAG, "Connecting to %s host %s:%d", SOCKTYPE_NAME(socktype), host, port);
        sock = connect_socket(host, port, socktype);
        ERROR_ACTION(TAG, sock == CONNECT_SOCKET_ERROR_RESOLVE, goto _error, "Could not resolve host");
        ERROR_ACTION(TAG, sock == CONNECT_SOCKET_ERROR_CONNECT, goto _error, "Could not connect to host");

        int err = write(sock, connect_message, strlen(connect_message));
        free(connect_message);
        ERROR_ACTION(TAG, err < 0, goto _error, "Could not send connection message: %d %s", errno, strerror(errno));

        status_led->active = true;

        ESP_LOGI(TAG, "Successfully connected to %s:%d", host, port);
        uart_nmea("$PESP,SOCK,CLI,CONNECTED,%s:%d", host, port);

        char *buffer = malloc(BUFFER_SIZE);

        int len;
        while ((len = read(sock, buffer, BUFFER_SIZE)) >= 0) {
            uart_write(buffer, len);
        }

        free(buffer);

        status_led->active = false;

        ESP_LOGW(TAG, "Disconnected from %s:%d: %d %s", host, port, errno, strerror(errno));
        uart_nmea("$PESP,SOCK,CLI,DISCONNECTED,%s:%d", host, port);

        _error:
        destroy_socket(&sock);

        free(host);
        free(connect_message);
    }

    vTaskDelete(NULL);
}

void socket_client_init() {
    if (!config_get_bool1(CONF_ITEM(KEY_CONFIG_SOCKET_CLIENT_ACTIVE))) return;

    xTaskCreate(socket_client_task, "socket_client_task", 32768, NULL, 2, NULL);
}
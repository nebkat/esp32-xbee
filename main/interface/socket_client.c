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
#include "interface/socket_client.h"

#include <config.h>
#include <retry.h>
#include <stream_stats.h>
#include <tasks.h>

static const char *TAG = "SOCKET_CLIENT";

#define BUFFER_SIZE 1024

static int sock = -1;

static status_led_handle_t status_led = NULL;
static stream_stats_handle_t stream_stats = NULL;

static void socket_client_uart_handler(void* handler_args, esp_event_base_t base, int32_t length, void* buffer) {
    if (sock == -1) return;

    stream_stats_increment(stream_stats, 0, length);

    int err = write(sock, buffer, length);
    if (err < 0) destroy_socket(&sock);
}

static void socket_client_task(void *ctx) {
    uart_register_read_handler(socket_client_uart_handler);

    config_color_t status_led_color = config_get_color(CONF_ITEM(KEY_CONFIG_SOCKET_CLIENT_COLOR));
    if (status_led_color.rgba != 0) status_led = status_led_add(status_led_color.rgba, STATUS_LED_FADE, 500, 2000, 0);
    if (status_led != NULL) status_led->active = false;

    stream_stats = stream_stats_new("socket_client");

    retry_delay_handle_t delay_handle = retry_init(true, 5, 2000, 0);

    while (true) {
        retry_delay(delay_handle);

        wait_for_ip();

        char *host, *connect_message;
        uint16_t port = config_get_u16(CONF_ITEM(KEY_CONFIG_SOCKET_CLIENT_PORT));
        config_get_str_blob_alloc(CONF_ITEM(KEY_CONFIG_SOCKET_CLIENT_HOST), (void **) &host);
        config_get_str_blob_alloc(CONF_ITEM(KEY_CONFIG_SOCKET_CLIENT_CONNECT_MESSAGE), (void **) &connect_message);
        int socktype = config_get_bool1(CONF_ITEM(KEY_CONFIG_SOCKET_CLIENT_TYPE_TCP_UDP)) ? SOCK_STREAM : SOCK_DGRAM;

        ESP_LOGI(TAG, "Connecting to %s host %s:%d", SOCKTYPE_NAME(socktype), host, port);
        uart_nmea("$PESP,SOCK,CLI,%s,CONNECTING,%s:%d", SOCKTYPE_NAME(socktype), host, port);
        sock = connect_socket(host, port, socktype);
        ERROR_ACTION(TAG, sock == CONNECT_SOCKET_ERROR_RESOLVE, goto _error, "Could not resolve host");
        ERROR_ACTION(TAG, sock == CONNECT_SOCKET_ERROR_CONNECT, goto _error, "Could not connect to host");

        int err = write(sock, connect_message, strlen(connect_message));
        free(connect_message);
        ERROR_ACTION(TAG, err < 0, goto _error, "Could not send connection message: %d %s", errno, strerror(errno));

        ESP_LOGI(TAG, "Successfully connected to %s:%d", host, port);
        uart_nmea("$PESP,SOCK,CLI,%s,CONNECTED,%s:%d", SOCKTYPE_NAME(socktype), host, port);

        retry_reset(delay_handle);

        if (status_led != NULL) status_led->active = true;

        char *buffer = malloc(BUFFER_SIZE);

        int len;
        while ((len = read(sock, buffer, BUFFER_SIZE)) >= 0) {
            uart_write(buffer, len);

            stream_stats_increment(stream_stats, len, 0);
        }

        free(buffer);

        if (status_led != NULL) status_led->active = false;

        ESP_LOGW(TAG, "Disconnected from %s:%d: %d %s", host, port, errno, strerror(errno));
        uart_nmea("$PESP,SOCK,CLI,%s,DISCONNECTED,%s:%d", SOCKTYPE_NAME(socktype), host, port);

        _error:
        destroy_socket(&sock);

        free(host);
        free(connect_message);
    }

    vTaskDelete(NULL);
}

void socket_client_init() {
    if (!config_get_bool1(CONF_ITEM(KEY_CONFIG_SOCKET_CLIENT_ACTIVE))) return;

    xTaskCreate(socket_client_task, "socket_client_task", 4096, NULL, TASK_PRIORITY_INTERFACE, NULL);
}
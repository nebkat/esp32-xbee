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
#include <stream_stats.h>
#include <freertos/event_groups.h>
#include <esp_ota_ops.h>
#include "interface/ntrip.h"
#include "config.h"
#include "util.h"
#include "uart.h"

static const char *TAG = "NTRIP_SERVER";

#define BUFFER_SIZE 512

static const int CASTER_READY_BIT = BIT0;
static const int DATA_READY_BIT = BIT1;
static const int DATA_SENT_BIT = BIT2;

static int sock = -1;

static int data_keep_alive;
static EventGroupHandle_t server_event_group;

static status_led_handle_t status_led = NULL;
static stream_stats_handle_t stream_stats = NULL;

static TaskHandle_t server_task = NULL;
static TaskHandle_t sleep_task = NULL;

static void ntrip_server_uart_handler(void* handler_args, esp_event_base_t base, int32_t length, void* buffer) {
    EventBits_t event_bits = xEventGroupGetBits(server_event_group);

    // Reset data availability bit
    if ((event_bits & DATA_READY_BIT) == 0) {
        xEventGroupSetBits(server_event_group, DATA_READY_BIT);

        if (event_bits & DATA_SENT_BIT)
            ESP_LOGI(TAG, "Data received by UART, will now reconnect to caster if disconnected");
    }
    data_keep_alive = 0;

    // Ignore if caster is not connected and ready for data
    if ((event_bits & CASTER_READY_BIT) == 0) return;

    // Caster is connected and some data will be sent
    if ((event_bits & DATA_SENT_BIT) == 0) xEventGroupSetBits(server_event_group, DATA_SENT_BIT);

    int sent = write(sock, buffer, length);
    if (sent < 0) {
        destroy_socket(&sock);
        vTaskResume(server_task);
    } else {
        stream_stats_increment(stream_stats, 0, sent);
    }
}

static void ntrip_server_sleep_task(void *ctx) {
    vTaskSuspend(NULL);

    while (true) {
        // If wait time exceeded, clear data ready bit
        if (data_keep_alive == NTRIP_KEEP_ALIVE_THRESHOLD) {
            xEventGroupClearBits(server_event_group, DATA_READY_BIT);
            ESP_LOGW(TAG, "No data received by UART in %d seconds, will not reconnect to caster if disconnected", NTRIP_KEEP_ALIVE_THRESHOLD / 1000);
        }
        data_keep_alive += NTRIP_KEEP_ALIVE_THRESHOLD / 10;
        vTaskDelay(pdMS_TO_TICKS(NTRIP_KEEP_ALIVE_THRESHOLD / 10));
    }
}

static void ntrip_server_task(void *ctx) {
    server_event_group = xEventGroupCreate();
    uart_register_read_handler(ntrip_server_uart_handler);
    xTaskCreate(ntrip_server_sleep_task, "ntrip_server_sleep_task", 2048, NULL, TASK_PRIORITY_INTERFACE, &sleep_task);

    config_color_t status_led_color = config_get_color(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_COLOR));
    if (status_led_color.rgba != 0) status_led = status_led_add(status_led_color.rgba, STATUS_LED_FADE, 500, 2000, 0);
    if (status_led != NULL) status_led->active = false;

    stream_stats = stream_stats_new("ntrip_server");

    retry_delay_handle_t delay_handle = retry_init(true, 5, 2000, 0);

    while (true) {
        retry_delay(delay_handle);

        // Wait for data to be available
        if ((xEventGroupGetBits(server_event_group) & DATA_READY_BIT) == 0) {
            ESP_LOGI(TAG, "Waiting for UART input to connect to caster");
            uart_nmea("$PESP,NTRIP,SRV,WAITING");
            xEventGroupWaitBits(server_event_group, DATA_READY_BIT, true, false, portMAX_DELAY);
        }

        vTaskResume(sleep_task);

        wait_for_ip();

        char *buffer = NULL;

        char *host, *mountpoint, *password;
        uint16_t port = config_get_u16(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_PORT));
        config_get_primitive(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_PORT), &port);
        config_get_str_blob_alloc(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_HOST), (void **) &host);
        config_get_str_blob_alloc(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_PASSWORD), (void **) &password);
        config_get_str_blob_alloc(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_MOUNTPOINT), (void **) &mountpoint);

        ESP_LOGI(TAG, "Connecting to %s:%d/%s", host, port, mountpoint);
        uart_nmea("$PESP,NTRIP,SRV,CONNECTING,%s:%d,%s", host, port, mountpoint);
        sock = connect_socket(host, port, SOCK_STREAM);
        ERROR_ACTION(TAG, sock == CONNECT_SOCKET_ERROR_RESOLVE, goto _error, "Could not resolve host");
        ERROR_ACTION(TAG, sock == CONNECT_SOCKET_ERROR_CONNECT, goto _error, "Could not connect to host");

        buffer = malloc(BUFFER_SIZE);

        snprintf(buffer, BUFFER_SIZE, "SOURCE %s /%s" NEWLINE \
                "Source-Agent: NTRIP %s/%s" NEWLINE \
                NEWLINE, password, mountpoint, NTRIP_SERVER_NAME, &esp_ota_get_app_description()->version[1]);

        int err = write(sock, buffer, strlen(buffer));
        ERROR_ACTION(TAG, err < 0, goto _error, "Could not send request to caster: %d %s", errno, strerror(errno));

        int len = read(sock, buffer, BUFFER_SIZE - 1);
        ERROR_ACTION(TAG, len <= 0, goto _error, "Could not receive response from caster: %d %s", errno, strerror(errno));
        buffer[len] = '\0';

        char *status = extract_http_header(buffer, "");
        ERROR_ACTION(TAG, status == NULL || !ntrip_response_ok(status), free(status); goto _error,
                "Could not connect to mountpoint: %s", status == NULL ? "HTTP response malformed" : status);
        free(status);

        ESP_LOGI(TAG, "Successfully connected to %s:%d/%s", host, port, mountpoint);
        uart_nmea("$PESP,NTRIP,SRV,CONNECTED,%s:%d,%s", host, port, mountpoint);

        retry_reset(delay_handle);

        if (status_led != NULL) status_led->active = true;

        // Connected
        xEventGroupSetBits(server_event_group, CASTER_READY_BIT);

        // Await disconnect from UART handler
        vTaskSuspend(NULL);

        // Disconnected
        xEventGroupClearBits(server_event_group, CASTER_READY_BIT | DATA_SENT_BIT);

        if (status_led != NULL) status_led->active = false;

        ESP_LOGW(TAG, "Disconnected from %s:%d/%s", host, port, mountpoint);
        uart_nmea("$PESP,NTRIP,SRV,DISCONNECTED,%s:%d,%s", host, port, mountpoint);

        _error:
        vTaskSuspend(sleep_task);

        destroy_socket(&sock);

        free(buffer);
        free(host);
        free(mountpoint);
        free(password);
    }
}

void ntrip_server_init() {
    if (!config_get_bool1(CONF_ITEM(KEY_CONFIG_NTRIP_SERVER_ACTIVE))) return;

    xTaskCreate(ntrip_server_task, "ntrip_server_task", 4096, NULL, TASK_PRIORITY_INTERFACE, &server_task);
}

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

#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <esp_log.h>
#include <string.h>
#include <protocol/nmea.h>
#include <stream_stats.h>

#include "uart.h"
#include "config.h"
#include "interface/socket_server.h"
#include "tasks.h"

static const char *TAG = "UART";

ESP_EVENT_DEFINE_BASE(UART_EVENT_READ);
ESP_EVENT_DEFINE_BASE(UART_EVENT_WRITE);

void uart_register_read_handler(esp_event_handler_t event_handler) {
    ESP_ERROR_CHECK(esp_event_handler_register(UART_EVENT_READ, ESP_EVENT_ANY_ID, event_handler, NULL));
}

void uart_unregister_read_handler(esp_event_handler_t event_handler) {
    ESP_ERROR_CHECK(esp_event_handler_unregister(UART_EVENT_READ, ESP_EVENT_ANY_ID, event_handler));
}

void uart_register_write_handler(esp_event_handler_t event_handler) {
    ESP_ERROR_CHECK(esp_event_handler_register(UART_EVENT_WRITE, ESP_EVENT_ANY_ID, event_handler, NULL));
}

void uart_unregister_write_handler(esp_event_handler_t event_handler) {
    ESP_ERROR_CHECK(esp_event_handler_unregister(UART_EVENT_WRITE, ESP_EVENT_ANY_ID, event_handler));
}

static int uart_port = -1;
static bool uart_log_forward = false;

static stream_stats_handle_t stream_stats;

static void uart_task(void *ctx);

void uart_init() {
    uart_log_forward = config_get_bool1(CONF_ITEM(KEY_CONFIG_UART_LOG_FORWARD));

    uart_port = config_get_u8(CONF_ITEM(KEY_CONFIG_UART_NUM));

    uart_hw_flowcontrol_t flow_ctrl;
    bool flow_ctrl_rts = config_get_bool1(CONF_ITEM(KEY_CONFIG_UART_FLOW_CTRL_RTS));
    bool flow_ctrl_cts = config_get_bool1(CONF_ITEM(KEY_CONFIG_UART_FLOW_CTRL_CTS));
    if (flow_ctrl_rts && flow_ctrl_cts) {
        flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS;
    } else if (flow_ctrl_rts) {
        flow_ctrl = UART_HW_FLOWCTRL_RTS;
    } else if (flow_ctrl_cts) {
        flow_ctrl = UART_HW_FLOWCTRL_CTS;
    } else {
        flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    }

    uart_config_t uart_config = {
            .baud_rate = config_get_u32(CONF_ITEM(KEY_CONFIG_UART_BAUD_RATE)),
            .data_bits = config_get_u8(CONF_ITEM(KEY_CONFIG_UART_DATA_BITS)),
            .parity = config_get_u8(CONF_ITEM(KEY_CONFIG_UART_PARITY)),
            .stop_bits = config_get_u8(CONF_ITEM(KEY_CONFIG_UART_STOP_BITS)),
            .flow_ctrl = flow_ctrl
    };
    ESP_ERROR_CHECK(uart_param_config(uart_port, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(
            uart_port,
            config_get_i8(CONF_ITEM(KEY_CONFIG_UART_TX_PIN)),
            config_get_i8(CONF_ITEM(KEY_CONFIG_UART_RX_PIN)),
            config_get_i8(CONF_ITEM(KEY_CONFIG_UART_RTS_PIN)),
            config_get_i8(CONF_ITEM(KEY_CONFIG_UART_CTS_PIN))
    ));
    ESP_ERROR_CHECK(uart_driver_install(uart_port, UART_BUFFER_SIZE, UART_BUFFER_SIZE, 0, NULL, 0));

    stream_stats = stream_stats_new("uart");

    xTaskCreate(uart_task, "uart_task", 8192, NULL, TASK_PRIORITY_UART, NULL);
}

static void uart_task(void *ctx) {
    uint8_t buffer[UART_BUFFER_SIZE];

    while (true) {
        int32_t len = uart_read_bytes(uart_port, buffer, sizeof(buffer), pdMS_TO_TICKS(50));
        if (len < 0) {
            ESP_LOGE(TAG, "Error reading from UART");
        } else if (len == 0) {
            continue;
        }

        stream_stats_increment(stream_stats, len, 0);

        esp_event_post(UART_EVENT_READ, len, &buffer, len, portMAX_DELAY);
    }
}

void uart_inject(void *buf, size_t len) {
    esp_event_post(UART_EVENT_READ, len,  buf, len, portMAX_DELAY);
}

int uart_log(char *buf, size_t len) {
    if (!uart_log_forward) return 0;
    return uart_write(buf, len);
}

int uart_nmea(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    char *nmea;
    nmea_vasprintf(&nmea, fmt, args);
    int l = uart_write(nmea, strlen(nmea));
    free(nmea);

    va_end(args);

    return l;
}

int uart_write(char *buf, size_t len) {
    if (uart_port < 0) return 0;
    if (len == 0) return 0;

    int written = uart_write_bytes(uart_port, buf, len);
    if (written < 0) return written;

    stream_stats_increment(stream_stats, 0, len);

    esp_event_post(UART_EVENT_WRITE, len, buf, len, portMAX_DELAY);

    return written;
}